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
#include "mode.h"
#include "spi.h"
#include "store.h"
#include "ui.h"
#include "wire.h"
#include "switch_hid.h"

#define UART_ID     uart0
#define BAUD_RATE   115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define HEARTBEAT_MS 1000
#define UART_POLL_MS 10

static uint8_t hid_service_buffer[700];
static uint8_t pnp_service_buffer[200];

static btstack_packet_callback_registration_t hci_events;
static btstack_timer_source_t heartbeat;
static btstack_timer_source_t uart_poll;
static btstack_timer_source_t empty_timer;
static btstack_timer_source_t reconnect_timer;

static void uart_poll_handler(btstack_timer_source_t *ts)
{
    probe_uart_task();
    /* TinyUSB の駆動。有線では報告送信、無線では CDC の面倒を見る。 */
    wire_task();
    mode_poll(to_ms_since_boot(get_absolute_time()));
    btstack_run_loop_set_timer(ts, UART_POLL_MS);
    btstack_run_loop_add_timer(ts);
}

static void empty_timer_handler(btstack_timer_source_t *ts)
{
    /* Classic HID の送出要求。有線では BT へ送らない。 */
    if (mode_is_wired()) {
        return;
    }
    if (probe_hid_cid != 0u) {
        hid_device_request_can_send_now_event(probe_hid_cid);
    }
    btstack_run_loop_set_timer(ts, probe_send_interval_ms());
    btstack_run_loop_add_timer(ts);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size)
{
    char msg[96];
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
    if (ev == 0x31u || ev == 0x32u || ev == 0x33u || ev == 0x36u) {
        if (probe_ssp_count < 255u) {
            probe_ssp_count++;
        }
    }
    {
        bool want = probe_hci_verbose;
        if (!want && probe_empty_sent == 0u && probe_state_sent == 0u) {
            want = true;
        }
        if (!want && (ev == 0x05u || ev == 0x18u)) {
            want = true;
        }
        if (!want && ev >= 0x31u && ev <= 0x36u) {
            want = true;
        }
        /* Read BD ADDR 応答は常時出す。MAC 偽装の成否はここで見る。 */
        if (ev == 0x0eu && size >= 12u && packet[3] == 0x01u &&
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

    switch (ev) {
        case BTSTACK_EVENT_STATE: {
            bd_addr_t addr;
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
                break;
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
            store_color_load();
            /* Classic の自動再接続は無線だけ。有線では USB を見に行く。 */
            if (!mode_is_wired() && store_host_load()) {
                snprintf(msg, sizeof(msg), "reconnect to %s",
                         bd_addr_to_str(probe_host_addr));
                probe_line(msg);
                btstack_run_loop_set_timer(&reconnect_timer, 2000);
                btstack_run_loop_add_timer(&reconnect_timer);
            } else if (!mode_is_wired()) {
                probe_line("no host. open Change-Grip screen on Switch");
            } else {
                probe_line("wired mode. connect USB to Switch");
            }
            if (store_cap_load()) {
                probe_line("cap saved. B to wake");
            } else {
                probe_line("cap none. C to capture Joy-Con wake");
            }
            break;
        }
        case HCI_EVENT_CONNECTION_REQUEST:
            probe_line("conn request (Switch found us)");
            break;
        case HCI_EVENT_CONNECTION_COMPLETE: {
            uint8_t cst = hci_event_connection_complete_get_status(packet);
            snprintf(msg, sizeof(msg), "conn status=0x%02x%s", cst,
                     (cst == ERROR_CODE_SUCCESS) ? " ok" : " FAIL");
            probe_line(msg);
            if (cst == 0x04u) {
                probe_line("0x04=Page Timeout (peer silent)");
            }
            if (cst != ERROR_CODE_SUCCESS) {
                break;
            }
            probe_connected_at_ms = to_ms_since_boot(get_absolute_time());
            probe_ssp_count = 0u;
            break;
        }
        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            uint8_t reason =
                hci_event_disconnection_complete_get_reason(packet);
            {
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
            break;
        }
        case HCI_EVENT_HID_META: {
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
            break;
        }
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

    if (cyw43_arch_init() != 0) {
        probe_line("NG: cyw43_arch_init");
        while (1) {
            tight_loop_contents();
        }
    }
    link_init();
    mode_boot();
    /* USB CDC の読み書きは自前ドライバ（wire.c）が行う。 */
    wire_stdio_init();

    if (!mode_is_wired()) {
        gap_discoverable_control(1);
        gap_connectable_control(1);
        gap_set_class_of_device(SWITCH_CLASS_OF_DEVICE);
        gap_set_local_name(SWITCH_GAP_NAME);
        gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                             LM_LINK_POLICY_ENABLE_SNIFF_MODE);
        gap_set_allow_role_switch(true);
        gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
        gap_ssp_set_auto_accept(true);
    } else {
        /* 有線では Classic に名乗らない。BLE（取込・再生）は使う。 */
        gap_discoverable_control(0);
        gap_connectable_control(0);
    }

    l2cap_init();
    sdp_init();

    if (!mode_is_wired()) {
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
    }

    hci_events.callback = &packet_handler;
    hci_add_event_handler(&hci_events);

    hci_power_control(HCI_POWER_ON);
    /* transport が SDK 既定 MAC を入れるため後で上書きする。 */
    hci_set_bd_addr(probe_addr);

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

    probe_line(mode_is_wired()
                   ? "ready. wired USB HID / C capture / B wake / S input"
                   : "ready. C capture / B wake / S input / ? status");
    btstack_run_loop_execute();

    while (1) {
        probe_uart_task();
        sleep_ms(1);
    }
    return 0;
}
