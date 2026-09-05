/* pico-wakecon: Switch2 wake (BLE) + Switch1 Pro Controller (Classic)。
 * 運用: C 取込 → B 再生で Switch2 を起こし、Classic でプロコンとして使う。 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "btstack.h"
#include "cap.h"
#include "hid.h"
#include "link.h"
#include "spi.h"
#include "store.h"
#include "ui.h"
#include "switch_hid.h"
#include "usb_wired.h"

#define UART_ID     uart0
#define BAUD_RATE   115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define HEARTBEAT_MS 1000
#define UART_POLL_MS 10
#define USB_POLL_MS 1

/* HCI event code names (BTstack/BT spec values, renamed for readability). */
#define HCI_EV_SSP_BEGIN 0x31u
#define HCI_EV_SSP_END 0x36u
#define HCI_EV_CONN_REQUEST 0x05u
#define HCI_EV_CONN_COMPLETE 0x03u
#define HCI_EV_BDADDR_RSP 0x0eu

static uint8_t hid_service_buffer[700];
static uint8_t pnp_service_buffer[200];

static btstack_packet_callback_registration_t hci_events;
static btstack_timer_source_t heartbeat;
static btstack_timer_source_t uart_poll;
static btstack_timer_source_t empty_timer;
static btstack_timer_source_t reconnect_timer;
static btstack_timer_source_t usb_poll;

static void uart_poll_handler(btstack_timer_source_t *ts)
{
    probe_uart_task();
    /* 有線 USB のポンプ (10ms)。新規タイマを足さず既存周期に相乗りする。 */
    usb_wired_task(to_ms_since_boot(get_absolute_time()));
    btstack_run_loop_set_timer(ts, UART_POLL_MS);
    btstack_run_loop_add_timer(ts);
}

/* 有線 USB の定常ポンプ (1ms)。2wiCC のタイトループ相当。
 * 10ms 周期の uart_poll 相乗りでは厳格なホストの列挙に応答しきれない。 */
static void usb_poll_handler(btstack_timer_source_t *ts)
{
    usb_wired_pump();
    btstack_run_loop_set_timer(ts, USB_POLL_MS);
    btstack_run_loop_add_timer(ts);
}

static void empty_timer_handler(btstack_timer_source_t *ts)
{
    if (probe_hid_cid != 0u) {
        hid_device_request_can_send_now_event(probe_hid_cid);
    }
    btstack_run_loop_set_timer(ts, probe_send_interval_ms());
    btstack_run_loop_add_timer(ts);
}

/* HCI verbose log + BDADDR応答表示。書式は従来通り。 */
static void log_hci_packet(uint8_t ev, uint8_t *packet, uint16_t size)
{
    char msg[96];
    bool want = probe_hci_verbose;
    if (!want && probe_empty_sent == 0u && probe_state_sent == 0u) {
        want = true;
    }
    if (!want && (ev == HCI_EV_CONN_REQUEST || ev == 0x18u)) {
        want = true;
    }
    if (!want && ev >= HCI_EV_SSP_BEGIN && ev <= HCI_EV_SSP_END) {
        want = true;
    }
    /* Read BD ADDR 応答は常時出す。MAC 偽装の成否はここで見る。 */
    if (ev == HCI_EV_BDADDR_RSP && size >= 12u && packet[3] == 0x01u &&
        packet[4] == 0x09u && packet[5] == 0x10u) {
        snprintf(msg, sizeof(msg),
                 "BDADDR=%02x%02x%02x%02x%02x%02x",
                 packet[11], packet[10], packet[9],
                 packet[8], packet[7], packet[6]);
        probe_line(msg);
    }
    if (want) {
        snprintf(msg, sizeof(msg),
                 "[HCI] ev=0x%02x len=%u data=%02x %02x %02x %02x",
                 ev, packet[1], packet[2], packet[3], packet[4],
                 packet[5]);
        probe_line(msg);
    }
}

static void handle_bt_ready(uint8_t *packet)
{
    char msg[96];
    bd_addr_t addr;
    (void)packet;
    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
        return;
    }
    gap_local_bd_addr(addr);
    snprintf(msg, sizeof(msg), "BT READY %s", bd_addr_to_str(addr));
    probe_line(msg);
    if (addr[0] == SWITCH_OUI_0 && addr[1] == SWITCH_OUI_1 &&
        addr[2] == SWITCH_OUI_2) {
        probe_line("addr ok (7C:BB:8A)");
    } else {
        probe_line("addr NG (SDK default remains)");
    }
    snprintf(msg, sizeof(msg), "link keys=%d",
             link_key_count());
    probe_line(msg);
    /* 保存の読込自体は main 側で済ませてある (有線起動では BT READY が
     * 来ないため)。ここでは読込済みの状態だけを見る。 */
    if (probe_host_known) {
        snprintf(msg, sizeof(msg), "reconnect to %s",
                 bd_addr_to_str(probe_host_addr));
        probe_line(msg);
        btstack_run_loop_set_timer(&reconnect_timer, 2000);
        btstack_run_loop_add_timer(&reconnect_timer);
    } else {
        probe_line("no host. open Change-Grip screen on Switch");
    }
    if (probe_cap_valid) {
        probe_line("cap saved. B to wake");
    } else {
        probe_line("cap none. C to capture Joy-Con wake");
    }
}

static void handle_conn_complete(uint8_t *packet)
{
    char msg[96];
    uint8_t cst = hci_event_connection_complete_get_status(packet);
    snprintf(msg, sizeof(msg), "conn status=0x%02x%s", cst,
             (cst == ERROR_CODE_SUCCESS) ? " ok" : " FAIL");
    probe_line(msg);
    if (cst == 0x04u) {
        probe_line("0x04=Page Timeout (peer silent)");
    }
    if (cst != ERROR_CODE_SUCCESS) {
        return;
    }
    probe_connected_at_ms = to_ms_since_boot(get_absolute_time());
    probe_ssp_count = 0u;
}

static void handle_disc(uint8_t *packet)
{
    char msg[96];
    uint8_t reason =
        hci_event_disconnection_complete_get_reason(packet);
    uint32_t held_ms = to_ms_since_boot(get_absolute_time()) -
        probe_connected_at_ms;
    snprintf(msg, sizeof(msg), "disc reason=0x%02x held=%lums",
             reason, (unsigned long)held_ms);
    probe_line(msg);
    if (reason == 0x05u && probe_ssp_count == 0u &&
        held_ms < 200u) {
        probe_line("console stall (no SSP). power OFF Switch, or K + re-pair");
    } else if (probe_ssp_count > 0u) {
        snprintf(msg, sizeof(msg), "ssp=%u ok. check our code",
                 probe_ssp_count);
        probe_line(msg);
    }
    probe_hid_cid = 0u;
    link_note_disconnected();
    probe_line("reconnect armed");
}

static void handle_hid_meta(uint8_t *packet)
{
    char msg[96];
    uint8_t sub = hci_event_hid_meta_get_subevent_code(packet);
    switch (sub) {
        case HID_SUBEVENT_CONNECTION_OPENED: {
            uint8_t st =
                hid_subevent_connection_opened_get_status(packet);
            if (st != ERROR_CODE_SUCCESS) {
                snprintf(msg, sizeof(msg),
                         "hid open FAIL status=0x%02x", st);
                probe_line(msg);
                if (st == 0x66u) {
                    probe_line("0x66=refused security. K + re-pair both");
                }
                probe_hid_cid = 0u;
                link_note_disconnected();
                break;
            }
            probe_hid_cid =
                hid_subevent_connection_opened_get_hid_cid(packet);
            probe_hid_reset();
            {
                bd_addr_t a;
                hid_subevent_connection_opened_get_bd_addr(packet, a);
                store_host(a);
                snprintf(msg, sizeof(msg), "hid open. host %s saved",
                         bd_addr_to_str(a));
                probe_line(msg);
            }
            link_mark_connected();
            break;
        }
        case HID_SUBEVENT_CONNECTION_CLOSED:
            probe_hid_cid = 0u;
            link_note_disconnected();
            probe_line("hid closed");
            break;
        case HID_SUBEVENT_CAN_SEND_NOW:
            probe_can_send_now();
            break;
        default:
            snprintf(msg, sizeof(msg), "hid sub=0x%02x", sub);
            probe_line(msg);
            break;
    }
}
static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size)
{
    uint8_t ev;
    (void)channel;
    (void)size;
    ev = hci_event_packet_get_type(packet);
    /* LE 切断は Classic 側の処理へ落とさない。 */
    if (ev == HCI_EVENT_DISCONNECTION_COMPLETE &&
        link_is_le(hci_event_disconnection_complete_get_connection_handle(
            packet))) {
        link_le_packet(packet_type, packet, size);
        probe_line("le disc");
        return;
    }
    link_le_packet(packet_type, packet, size);
    if (ev == HCI_EV_SSP_BEGIN || ev == 0x32u || ev == 0x33u || ev == HCI_EV_SSP_END) {
        if (probe_ssp_count < 255u) {
            probe_ssp_count++;
        }
    }
    log_hci_packet(ev, packet, size);

    switch (ev) {
        case BTSTACK_EVENT_STATE:
            handle_bt_ready(packet);
            break;
        case HCI_EVENT_CONNECTION_REQUEST:
            probe_line("conn request (Switch found us)");
            break;
        case HCI_EVENT_CONNECTION_COMPLETE:
            handle_conn_complete(packet);
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            handle_disc(packet);
            break;
        case HCI_EVENT_HID_META:
            handle_hid_meta(packet);
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            if (packet_type == HCI_EVENT_PACKET) {
                link_cap_report(packet);
            }
            break;
        default:
            break;
    }
}

int main(void)
{
    hid_sdp_record_t hid_params = {
        SWITCH_CLASS_OF_DEVICE,
        33, 1, 1, 1, 0, 0, 0xFFFF, 0xFFFF, 3200,
        switch_bt_report_descriptor,
        sizeof(switch_bt_report_descriptor),
        SWITCH_HID_NAME,
    };
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    while (uart_is_readable(UART_ID)) {
        (void)uart_getc(UART_ID);
    }
    probe_line("");
    probe_line("=== wakecon ===");

    /* 有線 USB を BT より先に初期化する。列挙前の消費電流制限
     * (未設定時 100mA) にかからないよう、CYW43/BT が起動する前に
     * pull-up を上げて列挙させる。BT 内部の初期化順は変えない。 */
    usb_wired_init();

    if (cyw43_arch_init() != 0) {
        probe_line("NG: cyw43_arch_init");
        while (1) {
            tight_loop_contents();
        }
    }
    /* 起動中のつなぎポンプ。run loop 開始前の約2〜3秒に SETUP を落とさない。 */
    usb_wired_pump();
    link_init();
    /* 保存の読込は BT の生死によらず行う。有線起動では BT READY が
     * 来ないため、ここで読んでおかないと ? 表示が既定値になる。 */
    store_color_load();
    store_host_load();
    store_cap_load();

    gap_discoverable_control(1);
    gap_connectable_control(1);
    gap_set_class_of_device(SWITCH_CLASS_OF_DEVICE);
    gap_set_local_name(SWITCH_GAP_NAME);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(true);

    usb_wired_pump();
    l2cap_init();
    sdp_init();

    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    hid_create_sdp_record(hid_service_buffer,
                          sdp_create_service_record_handle(), &hid_params);
    btstack_assert(de_get_len(hid_service_buffer) <= sizeof(hid_service_buffer));
    sdp_register_service(hid_service_buffer);

    memset(pnp_service_buffer, 0, sizeof(pnp_service_buffer));
    device_id_create_sdp_record(pnp_service_buffer,
                                sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_USB,
                                SWITCH_VENDOR_ID, SWITCH_PRODUCT_ID,
                                SWITCH_PRODUCT_VERSION);
    btstack_assert(de_get_len(pnp_service_buffer) <= sizeof(pnp_service_buffer));
    sdp_register_service(pnp_service_buffer);

    hid_device_init(1, sizeof(switch_bt_report_descriptor),
                    switch_bt_report_descriptor);
    hid_device_accept_truncated_hid_reports(true);
    hid_device_register_report_data_callback(&probe_report_handler);
    hid_device_register_packet_handler(&packet_handler);

    hci_events.callback = &packet_handler;
    hci_add_event_handler(&hci_events);

    /* 保存された W を復元する。有線起動は電波を上げず USB 先行で列挙させる
     * (無線チップ動作中は Switch が列挙しない実測のため)。電源操作は
     * link_radio_update に一元化する。BT 内部の初期化順は変えない。 */
    usb_wired_set_enabled(store_wired_load());
    link_apply_wired_mode(usb_wired_is_enabled());
    /* transport が SDK 既定 MAC を入れるため後で上書きする。 */
    hci_set_bd_addr(probe_addr);
    usb_wired_pump();

    btstack_run_loop_set_timer_handler(&heartbeat,
                                       &probe_heartbeat_handler);
    btstack_run_loop_set_timer(&heartbeat, HEARTBEAT_MS);
    btstack_run_loop_add_timer(&heartbeat);

    btstack_run_loop_set_timer_handler(&uart_poll, &uart_poll_handler);
    btstack_run_loop_set_timer(&uart_poll, UART_POLL_MS);
    btstack_run_loop_add_timer(&uart_poll);

    btstack_run_loop_set_timer_handler(&empty_timer, &empty_timer_handler);
    btstack_run_loop_set_timer(&empty_timer, 100);
    btstack_run_loop_add_timer(&empty_timer);

    btstack_run_loop_set_timer_handler(&reconnect_timer,
                                       &link_reconnect_handler);

    btstack_run_loop_set_timer_handler(&usb_poll, &usb_poll_handler);
    btstack_run_loop_set_timer(&usb_poll, USB_POLL_MS);
    btstack_run_loop_add_timer(&usb_poll);

    probe_line("ready. C capture / B wake / S input / ? status");
    /* 有線起動のときだけ USB を密にポンプする。ホストが列挙するまで
     * (最大10秒)。厳格なホストの SETUP を落とさないため。列挙済みなら
     * 即進む。無線起動では待たない (USB は給電専用のため)。 */
    if (usb_wired_is_enabled()) {
        uint32_t pump_until =
            to_ms_since_boot(get_absolute_time()) + 10000u;
        while (!usb_wired_is_configured() &&
               (int32_t)(to_ms_since_boot(get_absolute_time()) -
                         pump_until) < 0) {
            usb_wired_pump();
            sleep_us(100);
        }
    }
    btstack_run_loop_execute();

    while (1) {
        probe_uart_task();
        sleep_ms(1);
    }
    return 0;
}
