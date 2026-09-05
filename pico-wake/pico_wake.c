#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
#include "hardware/uart.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "switch2_wake.h"
#include "switch2_wake_adv.h"
#include "switch2_wake_input.h"
#include "switch2_wake_store.h"
#include "switch2_wake_att_db.h"
#include "switch2_wake_cmd.h"
#include "switch2_wake_capture.h"
#include "app_state.h"
#include "app_config.h"
#include "app_common.h"
#include "app_ports_adv.h"
#include "app_ports_store.h"
#include "app_ports_cmd.h"
#include "app_ports_att.h"
#include "app_att.h"
#include "app_ui.h"

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUD 115200
#define POLL_MS 1
#define BUTTON_POLL_MS 10
#define FORGET_CONFIRM_MS 5000u

switch2_wake_adv_t adv;
switch2_wake_uart_parser_t uart_parser;
switch2_wake_store_t store;
bd_addr_t local_identity;
switch2_wake_att_report_t att_report;
hci_con_handle_t le_connection = HCI_CON_HANDLE_INVALID;
/* W5 観測用のカウンタ。Switch 2 が何を触ったかを数える。 */
uint32_t att_read_count;
uint32_t att_write_count;
uint32_t att_desc_read_count;
uint16_t att_last_read_handle;
uint16_t att_last_write_handle;
uint16_t att_cccd_response;
uint16_t att_cccd_input;        /* ★2026/09/03 追加 0x000f 入力購読 */
uint32_t att_desc_write_count;  /* ★2026/09/03 追加 Descriptor への write */
uint32_t att_cmd_write_count;   /* ★2026/09/03 追加 0x0014/0016/0018 */
uint32_t le_conn_ms;            /* ★2026/09/03 追加 接続からの経過を測る */
uint32_t att_event_count;       /* ★2026/09/03 追加 ATT イベントの総数 */
uint16_t att_mtu;               /* ★2026/09/03 追加 交換された MTU */
bd_addr_t last_peer_addr;       /* ★2026/09/03 追加 接続してきた本体のアドレス */
uint8_t last_peer_type;         /* ★同 アドレス種別（0=public 1=random） */
bool last_peer_valid;           /* ★同 一度でも接続があったか */
#ifdef WAKE_BUTTON_GPIO
switch2_wake_button_t button;
#endif
btstack_timer_source_t poll_timer;
btstack_packet_callback_registration_t hci_events;
bd_addr_t null_addr;
bool stack_ready;
#ifdef WAKE_BUTTON_GPIO
uint32_t button_poll_at_ms;
#endif
bool forget_armed;
uint32_t forget_deadline_ms;
bool hci_verbose;

/* Bill-git1 流キャプチャ＋リプレイ用。 */
switch2_wake_capture_table_t cap_table;
bool cap_scanning;
uint32_t cap_deadline_ms;
switch2_wake_capture_saved_t cap_saved;
bool cap_saved_valid;
bool beacon_active;
uint32_t beacon_deadline_ms;

/* Switch 2 独自コマンド・非同期応答。秘密値はログへ出さない。 */
switch2_wake_cmd_t cmd_state;
btstack_crypto_aes128_t cmd_aes_request;
switch2_wake_cmd_aes_done_t cmd_aes_done;
void *cmd_aes_done_ctx;
uint8_t cmd_aes_output[16];
uint8_t cmd_aes_key[16];       /* 非同期完了まで入力を保持する */
uint8_t cmd_aes_plain[16];     /* 呼出し元のスタックを参照しない */
uint8_t cmd_pending_response[WAKE_CMD_RESPONSE_MAX];
uint16_t cmd_pending_response_len;
bool cmd_response_pending;
uint32_t cmd_bond_ok;
uint32_t cmd_registered_ok;

static void poll_uart(void) {
    switch2_wake_input_command_t command;
    while (uart_is_readable(UART_ID)) {
        char ch = (char)uart_getc(UART_ID);
        if (switch2_wake_uart_feed(&uart_parser, ch, &command)) {
            handle_command(&command);
        }
    }
}

#ifdef WAKE_BUTTON_GPIO
static void handle_button(switch2_wake_button_event_t event) {
    uint32_t actions = 0u;
    wake_result_t result;
    if (event == WAKE_BUTTON_WAKE) {
        result = switch2_wake_request(WAKE_SOURCE_BUTTON, now_ms(), &actions);
        apply_actions("BUTTON-W", result, actions);
    } else if (event == WAKE_BUTTON_PAIR) {
        result = switch2_wake_pair_request(WAKE_SOURCE_BUTTON, now_ms(), &actions);
        apply_actions("BUTTON-P", result, actions);
    } else if (event == WAKE_BUTTON_FORGET) {
        switch2_wake_adv_clear_peer(&adv);
        (void)switch2_wake_store_forget(&store);
        cmd_clear_pending_response();
        switch2_wake_cmd_clear_pairing(&cmd_state);
        result = switch2_wake_forget(WAKE_SOURCE_BUTTON, &actions);
        apply_actions("BUTTON-K", result, actions);
    }
}
#endif

static void poll_handler(btstack_timer_source_t *timer) {
    uint32_t actions = 0u;
    wake_result_t result;
    poll_uart();
#ifdef WAKE_BUTTON_GPIO
    if ((int32_t)(now_ms() - button_poll_at_ms) >= 0) {
        button_poll_at_ms = now_ms() + BUTTON_POLL_MS;
        handle_button(switch2_wake_button_sample(
        &button, gpio_get(WAKE_BUTTON_GPIO) == 0, now_ms()));
    }
#endif
    if (forget_armed && !deadline_active(now_ms(), forget_deadline_ms)) {
        forget_armed = false;
    }
    if (cap_scanning && !deadline_active(now_ms(), cap_deadline_ms)) {
        char text[112];
        int best;
        gap_stop_scan();
        cap_scanning = false;
        best = switch2_wake_capture_best_wake(&cap_table);
        if (best >= 0) {
            const switch2_wake_capture_entry_t *e = &cap_table.slot[best];
            uint8_t blob[WAKE_CAP_BLOB_SIZE];
            cap_saved_valid = false;
            memcpy(cap_saved.spoof, e->addr, 6);
            cap_saved.spoof_type = e->addr_type;
            memcpy(cap_saved.switch_mac, e->wake.switch_mac, 6);
            memcpy(cap_saved.payload, e->wake.payload, WAKE_CAP_ADV_SIZE);
            if (switch2_wake_capture_encode(&cap_saved, blob) &&
                store_tlv_store(NULL, switch2_wake_capture_tag(),
                                blob, (uint32_t)sizeof(blob))) {
                cap_saved_valid = true;
            }
            memset(blob, 0, sizeof(blob));
            snprintf(text, sizeof(text),
                     "CAP-DONE saved=%u spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                     cap_saved_valid ? 1u : 0u,
                     e->addr[0], e->addr[1], e->addr[2],
                     e->addr[3], e->addr[4], e->addr[5],
                     e->wake.switch_mac[0], e->wake.switch_mac[1],
                     e->wake.switch_mac[2], e->wake.switch_mac[3],
                     e->wake.switch_mac[4], e->wake.switch_mac[5]);
        } else {
            snprintf(text, sizeof(text), "CAP-DONE saved=0 seen=%u; no wake flag",
                     (unsigned)cap_table.used);
        }
        uart_put_line(text);
        cap_restore_adv("CAP");
    }
    if (beacon_active && !deadline_active(now_ms(), beacon_deadline_ms)) {
        beacon_active = false;
        gap_advertisements_enable(0);
        bdaddr_spoof(local_identity);
        uart_put_line("BCN-DONE; address restored");
        cap_restore_adv("BCN");
    }
    result = switch2_wake_tick(now_ms(), &actions);
    if (actions != WAKE_ACTION_NONE) apply_actions("TIMER", result, actions);
    btstack_run_loop_set_timer(timer, POLL_MS);
    btstack_run_loop_add_timer(timer);
}

/* ★W5-2: Security Manager のできごとを観測する。
 * 「LE 接続は来るが属性を触らない」原因が暗号化待ちなのかを、
 * 推測ではなく実物のイベントで見分けるために置く。 */
static btstack_packet_callback_registration_t sm_events;

/* W5-W8: ATT サーバの接続・MTU・送信可能イベントを観測する。
 *   ATT_EVENT_CAN_SEND_NOW では保留中のコマンド応答を handle 0x001e へ送る。
 *   MTU 交換は Switch 2 の必須手順ではないため、来ないことを異常扱いしない。
 *   受信・応答件数は CMD 行と ATT-CAN-SEND 行で個別に追跡する。
 */
static void att_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    char line[112];
    uint8_t ev;
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    ev = hci_event_packet_get_type(packet);
    att_event_count++;
    if (ev == ATT_EVENT_CAN_SEND_NOW) {
        if (cmd_response_pending && le_connection != HCI_CON_HANDLE_INVALID) {
            att_server_notify(le_connection, WAKE_CMD_RESPONSE_HANDLE,
                              cmd_pending_response, cmd_pending_response_len);
            cmd_response_pending = false;
        }
        snprintf(line, sizeof(line), "ATT-CAN-SEND rsp=%u n=%lu",
                 (unsigned)cmd_pending_response_len, (unsigned long)att_event_count);
    } else
    if (ev == ATT_EVENT_MTU_EXCHANGE_COMPLETE) {
        att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        snprintf(line, sizeof(line), "ATT-MTU mtu=%u n=%lu",
                 (unsigned)att_mtu, (unsigned long)att_event_count);
    } else if (ev == ATT_EVENT_CONNECTED) {
        snprintf(line, sizeof(line), "ATT-CONNECTED n=%lu",
                 (unsigned long)att_event_count);
    } else if (ev == ATT_EVENT_DISCONNECTED) {
        snprintf(line, sizeof(line), "ATT-DISCONNECTED n=%lu",
                 (unsigned long)att_event_count);
    } else {
        snprintf(line, sizeof(line), "ATT-EV ev=%02x len=%u n=%lu",
                 ev, (unsigned)size, (unsigned long)att_event_count);
    }
    uart_put_line(line);
}

static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    char line[112];
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        uart_put_line("SM just-works request -> accept");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_PAIRING_STARTED:
        uart_put_line("SM pairing started");
        break;
    case SM_EVENT_PAIRING_COMPLETE:
        snprintf(line, sizeof(line), "SM pairing complete status=%02x reason=%02x",
                 sm_event_pairing_complete_get_status(packet),
                 sm_event_pairing_complete_get_reason(packet));
        uart_put_line(line);
        break;
    case SM_EVENT_REENCRYPTION_STARTED:
        uart_put_line("SM reencryption started");
        break;
    case SM_EVENT_REENCRYPTION_COMPLETE:
        snprintf(line, sizeof(line), "SM reencryption complete status=%02x",
                 sm_event_reencryption_complete_get_status(packet));
        uart_put_line(line);
        break;
    case SM_EVENT_IDENTITY_RESOLVING_STARTED:
        uart_put_line("SM identity resolving started (0xcd)");
        break;
    case SM_EVENT_IDENTITY_RESOLVING_FAILED:
        uart_put_line("SM identity resolving failed (0xce) - peer is unknown");
        break;
    case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
        uart_put_line("SM identity resolving succeeded (0xcf)");
        break;
    case SM_EVENT_IDENTITY_CREATED:
        uart_put_line("SM identity created");
        break;
    default:
        snprintf(line, sizeof(line), "SM event=%02x",
                 hci_event_packet_get_type(packet));
        uart_put_line(line);
        break;
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    uint32_t actions = 0u;
    wake_result_t result;
    switch2_wake_config_t config = app_make_config();
    char boot_text[128];
    bool verified;
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    /* ★W5: LE の接続・切断を観測する。 */
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            /* ★★★2026/09/03 W8-a: 接続してきた本体のアドレスを出す。
             *   ★Wake 広告には『本体の BD_ADDR を逆順で載せる』決まりがあり、
             *   その値を知る手段が無いと Wake を試せない。
             *   ★★実物 switch2Lib.c も同じ場所で peer_address を控えている。
             *   ★★★秘密値ではない（公開されるアドレス）のでそのまま出す。 */
            char conn_line[112];
            bd_addr_t peer_addr;
            uint8_t peer_type;
            le_connection =
                gap_subevent_le_connection_complete_get_connection_handle(packet);
            gap_subevent_le_connection_complete_get_peer_address(packet, peer_addr);
            peer_type =
                gap_subevent_le_connection_complete_get_peer_address_type(packet);
            memcpy(last_peer_addr, peer_addr, sizeof(last_peer_addr));
            last_peer_type = peer_type;
            last_peer_valid = true;
            cmd_clear_pending_response();
            switch2_wake_cmd_set_peer(&cmd_state, peer_type, peer_addr);
            /* 2026/09/04: 接続直後の標準パラメータ更新要求を廃止。
             * 純正は送らない (ndeadly hurdle #4) し、Switch2側がベンダーHCIで
             * 5msに決めるため衝突して切断される恐れがある。 */
            le_conn_ms = now_ms();
            snprintf(conn_line, sizeof(conn_line),
                     "LE-CONN handle=%04x type=%u peer=%02x%02x%02x%02x%02x%02x t=%lu",
                     le_connection, (unsigned)peer_type,
                     peer_addr[0], peer_addr[1], peer_addr[2],
                     peer_addr[3], peer_addr[4], peer_addr[5],
                     (unsigned long)le_conn_ms);
            uart_put_line(conn_line);
        }
        return;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        char disc_line[112];
        snprintf(disc_line, sizeof(disc_line),
                 "LE-DISC reason=%02x dt=%lu rd=%lu wr=%lu drd=%lu dwr=%lu cmd=%lu",
                 hci_event_disconnection_complete_get_reason(packet),
                 (unsigned long)(now_ms() - le_conn_ms),
                 (unsigned long)att_read_count,
                 (unsigned long)att_write_count,
                 (unsigned long)att_desc_read_count,
                 (unsigned long)att_desc_write_count,
                 (unsigned long)att_cmd_write_count);
        uart_put_line(disc_line);
        cmd_clear_pending_response();
        le_connection = HCI_CON_HANDLE_INVALID;
        if (adv.peer_known && (cmd_state.ltk_valid || store.meta_loaded)) {
            (void)switch2_wake_adv_publish_reconnect(&adv);
        }
        return;
    }
    case GAP_EVENT_ADVERTISING_REPORT: {
        /* キャプチャ中だけ拾う。通常時は無視して UART を汚さない。 */
        if (cap_scanning && packet_type == HCI_EVENT_PACKET) {
            bd_addr_t raddr;
            uint8_t rtype;
            int rssi;
            uint8_t dlen;
            const uint8_t *data;
            switch2_wake_capture_hit_t hit;
            gap_event_advertising_report_get_address(packet, raddr);
            rtype = gap_event_advertising_report_get_address_type(packet);
            rssi = (int)gap_event_advertising_report_get_rssi(packet);
            dlen = gap_event_advertising_report_get_data_length(packet);
            data = gap_event_advertising_report_get_data(packet);
            if (switch2_wake_capture_parse(data, dlen, &hit)) {
                char cap_line[128];
                int idx = switch2_wake_capture_offer(&cap_table, raddr,
                                                     rtype, rssi, &hit);
                snprintf(cap_line, sizeof(cap_line),
                         "CAP mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x flag=%02x sw=%02x%02x%02x%02x%02x%02x rssi=%d idx=%d",
                         raddr[0], raddr[1], raddr[2],
                         raddr[3], raddr[4], raddr[5],
                         (unsigned)rtype, (unsigned)hit.pid,
                         (unsigned)hit.flag,
                         hit.switch_mac[0], hit.switch_mac[1],
                         hit.switch_mac[2], hit.switch_mac[3],
                         hit.switch_mac[4], hit.switch_mac[5],
                         rssi, idx);
                uart_put_line(cap_line);
            }
        }
        return;
    }
    case BTSTACK_EVENT_STATE:
        break;
    default:
        /* ★★★2026/09/03 W5-4: 段8-b1 と同じ手を採る。
         *   ここまで私は5回続けて推測で外した（SM設定・authreq・GATT構成・
         *   READ値・Descriptor write）。★どれも『繋がるのに GATT が来ない』を
         *   動かせなかった。★★段8-b1 のときも同じ地点で止まり、HCI のやり取りを
         *   そのまま出して解決した。★★★推測をやめ、実物を見る。
         *   ★接続後に本体が何を送ってきているかを、番号のまま全部出す。 */
        {
            char ev_line[128];
            uint8_t ev = hci_event_packet_get_type(packet);
            if ((ev == HCI_EVENT_LE_META) && (size >= 12u) &&
                (packet[2] == HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-PARAM-DONE status=%02x interval=%u latency=%u timeout=%u",
                         packet[3],
                         (unsigned)little_endian_read_16(packet, 6),
                         (unsigned)little_endian_read_16(packet, 8),
                         (unsigned)little_endian_read_16(packet, 10));
                uart_put_line(ev_line);
            }
            if ((ev == HCI_EVENT_LE_META) && (size >= 20u) &&
                (packet[2] == HCI_SUBEVENT_LE_CONNECTION_COMPLETE)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-INTERVAL interval=%u latency=%u timeout=%u",
                         (unsigned)little_endian_read_16(packet, 14),
                         (unsigned)little_endian_read_16(packet, 16),
                         (unsigned)little_endian_read_16(packet, 18));
                uart_put_line(ev_line);
            }
            if ((ev == HCI_EVENT_LE_META) && (size >= 33u) &&
                (packet[2] == HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-INTERVAL interval=%u latency=%u timeout=%u",
                         (unsigned)little_endian_read_16(packet, 26),
                         (unsigned)little_endian_read_16(packet, 28),
                         (unsigned)little_endian_read_16(packet, 30));
                uart_put_line(ev_line);
            }
            uint16_t i;
            uint16_t n;
            int pos;
            /* 通常は要約行だけ出す。生ダンプは D で切り替える。 */
            if (!hci_verbose) {
                return;
            }
            n = (size > 14u) ? 14u : size;
            pos = snprintf(ev_line, sizeof(ev_line),
                           "[HCI] ev=%02x len=%u data=", ev, (unsigned)size);
            for (i = 0u; (i < n) && (pos > 0) &&
                 ((unsigned)pos + 3u < sizeof(ev_line)); i++) {
                pos += snprintf(&ev_line[pos],
                                sizeof(ev_line) - (unsigned)pos,
                                "%02x ", packet[i]);
            }
            uart_put_line(ev_line);
        }
        return;
    }
    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
    if (stack_ready) return;
    stack_ready = true;
    cmd_start_selftest();
    /* ★2026/09/03 W5-6: ATT の構築とサーバ開始は main() へ移した（実物と同じ順序）。 */

    /* W4: TLV と LE Device DB が使える状態になってから照合する（初期化順4）。 */
    btstack_tlv_get_instance(&tlv_impl, &tlv_context);
    switch2_wake_store_init(&store, &store_port);
    gap_local_bd_addr(local_identity);
    (void)switch2_wake_store_load(&store);
    verified = switch2_wake_store_verify(&store, local_identity) == WAKE_STORE_OK;
    if (verified) {
        switch2_wake_adv_set_peer(&adv, store.meta.peer_identity);
    }
    snprintf(boot_text, sizeof(boot_text),
             "BOOT meta=%s db=%d verified=%u reason=%s",
             store.meta_loaded ? app_meta_state_name(store.meta.state) : "NONE",
             store.db_index, verified ? 1u : 0u,
             switch2_wake_store_result_name(store.last_result));
    uart_put_line(boot_text);
    /* Bill-git1 流の保存済みキャプチャを復元する。 */
    {
        uint8_t blob[WAKE_CAP_BLOB_SIZE];
        uint32_t got;
        memset(blob, 0, sizeof(blob));
        got = store_tlv_get(NULL, switch2_wake_capture_tag(),
                            blob, (uint32_t)sizeof(blob));
        cap_saved_valid = switch2_wake_capture_decode(blob, got, &cap_saved);
        memset(blob, 0, sizeof(blob));
        uart_put_line(cap_saved_valid ? "CAP-SAVED loaded" : "CAP-SAVED none");
    }

    result = switch2_wake_init(&config);
    if (result == WAKE_RESULT_OK) {
        result = switch2_wake_start(verified, now_ms(), &actions);
    }
    apply_actions("BOOT", result, actions);
    if (verified && adv.peer_known) {
        (void)switch2_wake_adv_publish_reconnect(&adv);
    }
    uart_put_line("READY W5-W8: W | P | I | K ARM/CONFIRM | S | V 0/1 | T | X | C [s] | L | B | D");
}

int main(void) {
    switch2_wake_adv_port_t port = {
        NULL, port_set_params, port_set_data, port_enable
    };
    stdio_init_all();
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    /* 起動前に届いていたゴミを捨てる。残っていると初回入力へ付着し、
     * 必ず 1 回だけ ERR になる。 */
    while (uart_is_readable(UART_ID)) {
        (void)uart_getc(UART_ID);
    }
#ifdef WAKE_BUTTON_GPIO
    gpio_init(WAKE_BUTTON_GPIO);
    gpio_set_dir(WAKE_BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(WAKE_BUTTON_GPIO);
#endif
    switch2_wake_uart_parser_init(&uart_parser);
#ifdef WAKE_BUTTON_GPIO
    switch2_wake_button_init(&button);
#endif
    uart_put_line("=== pico-wake W5 ATT probe ===");
    if (switch2_wake_adv_init(&adv, &port) != WAKE_ADV_RESULT_OK) {
        uart_put_line("FATAL adv init");
        while (true) tight_loop_contents();
    }
    switch2_wake_cmd_init(&cmd_state, &cmd_port);
    if (cyw43_arch_init() != 0) {
        uart_put_line("FATAL cyw43_arch_init");
        while (true) tight_loop_contents();
    }
    /* ★★★2026/09/03: switch2Lib.c は gap_discoverable_control(0) と
     *   gap_connectable_control(0) で BR/EDR を不可視にしているが、
     *   ★この2つは Classic 専用の API であり、LE 専用ビルドには宣言が無い
     *   （実機ビルドで implicit declaration になった）。
     *   ★★本ターゲットは pico_btstack_ble だけを繋いでおり Classic を
     *   一切初期化しないため、そもそも BR/EDR は動かない。
     *   ★★★よってこの2行は不要である（実物は Classic も含むビルドなので要る）。 */

    l2cap_init();
    sm_init();
    /* ★★★一次資料の明言（ndeadly bluetooth_interface.md）:
     *   "The controllers implement their own (Pseudo)-Out-Of-Band pairing
     *    procedure over the HID command interface instead of using the standard
     *    Security Manager Protocol (SMP), which is not supported at all."
     *   "Attempting to pair controllers using SMP (as many platforms do
     *    automatically) will cause the controller to terminate the connection."
     *
     * ★つまり SMP からペアリングを始めてはならない。
     *   IO capability だけは『画面もボタンも無い』と正しく名乗っておくが、
     *   ★★bonding を要求しない（SMP を能動的に始めない）。
     *   鍵交換は 0x0014 への 0x15 コマンドで行う（W6 の仕事）。 */
    /* ★★★2026/09/03 W5-6: ATT の初期化を hci_power_control(ON) の『前』へ移す。
     *   ★実物 switch2Lib.c は btstack_main() の中で build_gatt_db() と
     *   att_server_init() と att_server_register_packet_handler() を済ませ、
     *   ★★最後に hci_power_control(HCI_POWER_ON) を呼んでいる。
     *   ★★★私は HCI_STATE_WORKING を待ってから呼んでいた。
     *   実機では ATT-CONNECTED は出たが MTU 交換が来ず、reads=0 のままだった。
     *   ★『本体が送っていない』のか『届いても処理されていない』のかを
     *   区別するため、まず実物と同じ順序に揃える。 */
    {
        switch2_wake_att_result_t att_res =
            switch2_wake_att_db_build(&att_port, &att_report);
        char att_line[128];
        snprintf(att_line, sizeof(att_line),
                 "ATT build=%s last=%04x size=%u step=%u max=%u",
                 switch2_wake_att_result_name(att_res), att_report.last_handle,
                 (unsigned)att_report.db_size, (unsigned)att_report.failed_step,
                 (unsigned)MAX_ATT_DB_SIZE);
        uart_put_line(att_line);
        if (att_res != WAKE_ATT_OK) {
            snprintf(att_line, sizeof(att_line),
                     "FATAL ATT DB expected=%04x actual=%04x",
                     att_report.expected_handle, att_report.actual_handle);
            uart_put_line(att_line);
            while (true) tight_loop_contents();   /* 広告を開始しない */
        }
        att_server_init(att_db_util_get_address(),
                        &att_read_callback, &att_write_callback);
        att_server_register_packet_handler(&att_packet_handler);
    }
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0);
    sm_register_ltk_callback(&get_ltk_callback);
    sm_events.callback = &sm_packet_handler;
    sm_add_event_handler(&sm_events);
    hci_events.callback = &packet_handler;
    hci_add_event_handler(&hci_events);
    btstack_run_loop_set_timer_handler(&poll_timer, &poll_handler);
    btstack_run_loop_set_timer(&poll_timer, POLL_MS);
    btstack_run_loop_add_timer(&poll_timer);
    /* 2026/09/04: ATT無音の可視化。USBシリアルへHCI/L2CAP/ATTダンプを出す。
     * UART(アプリログ)とUSB(BTstackダンプ)の2系統になる。 */
    hci_dump_init(hci_dump_embedded_stdout_get_instance());
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    while (true) {
        poll_uart();
        sleep_ms(1);
    }
}
