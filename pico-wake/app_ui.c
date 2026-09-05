#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "switch2_wake.h"
#include "switch2_wake_adv.h"
#include "switch2_wake_input.h"
#include "switch2_wake_store.h"
#include "switch2_wake_capture.h"
#include "app_state.h"
#include "app_config.h"
#include "app_common.h"
#include "app_ports_cmd.h"
#include "app_ui.h"

/* Moved verbatim from pico_wake.c; keeps the original macro name. */
#define FORGET_CONFIRM_MS APP_FORGET_CONFIRM_MS

void apply_actions(const char *name, wake_result_t result,
                          uint32_t actions) {
    char text[128];
    switch2_wake_adv_result_t adv_result = switch2_wake_adv_apply(&adv, actions);
    if ((actions & WAKE_ACTION_DISCONNECT) != 0u &&
        le_connection != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(le_connection);
    }
    snprintf(text, sizeof(text), "%s core=%d adv=%d actions=0x%02lx",
             name, (int)result, (int)adv_result, (unsigned long)actions);
    uart_put_line(text);
}

void show_status(void) {
    switch2_wake_snapshot_t snapshot;
    char text[256];
    const uint8_t *payload = switch2_wake_adv_payload(&adv);
    if (switch2_wake_snapshot(&snapshot) != WAKE_RESULT_OK) {
        uart_put_line("S ERR not initialized");
        return;
    }
    snprintf(text, sizeof(text),
             "S state=%s peer=%u adv=%u wake=%u type=%02x count=%lu req=%lu start=%lu timeout=%lu rx_ok=%lu rx_ng=%lu rx_over=%lu button=%u",
             switch2_wake_state_name(snapshot.state), adv.peer_known ? 1u : 0u,
             adv.enabled ? 1u : 0u, switch2_wake_adv_is_wake(&adv) ? 1u : 0u,
             payload[SWITCH2_WAKE_ADV_TYPE_OFFSET], (unsigned long)adv.apply_count,
             (unsigned long)snapshot.wake_requested,
             (unsigned long)snapshot.wake_started,
             (unsigned long)snapshot.wake_timeout,
             (unsigned long)uart_parser.accepted,
             (unsigned long)uart_parser.rejected,
             (unsigned long)uart_parser.overflow,
#ifdef WAKE_BUTTON_GPIO
             1u
#else
             0u
#endif
    );
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "STORE meta=%s db=%d src=%u last=%s",
             store.meta_loaded ? app_meta_state_name(store.meta.state) : "NONE",
             store.db_index, (unsigned)store.meta.source,
             switch2_wake_store_result_name(store.last_result));
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "ATT db=%u last=%04x reads=%lu writes=%lu conn=%u enc=%u",
             (unsigned)att_report.db_size, att_report.last_handle,
             (unsigned long)att_read_count, (unsigned long)att_write_count,
             (le_connection == HCI_CON_HANDLE_INVALID) ? 0u : 1u,
             (le_connection == HCI_CON_HANDLE_INVALID) ? 0u
                 : (unsigned)gap_encryption_key_size(le_connection));
    uart_put_line(text);
    /* ★2026/09/03 追加: Descriptor とコマンド経路を別に数える。
     *   read/write が『どの種類の属性に来たか』が分かれば原因を絞れる。 */
    snprintf(text, sizeof(text),
             "ATT2 desc_rd=%lu desc_wr=%lu cmd=%lu cccd_rsp=%04x cccd_in=%04x attev=%lu mtu=%u",
             (unsigned long)att_desc_read_count, (unsigned long)att_desc_write_count,
             (unsigned long)att_cmd_write_count, att_cccd_response, att_cccd_input,
             (unsigned long)att_event_count, (unsigned)att_mtu);
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "CMD rx=%lu rsp=%lu ng=%lu last=%02x/%02x fp=%04x bond=%lu reg=%lu",
             (unsigned long)cmd_state.received, (unsigned long)cmd_state.responded,
             (unsigned long)cmd_state.rejected, cmd_state.last_cmd, cmd_state.last_sub,
             switch2_wake_cmd_fingerprint(&cmd_state),
             (unsigned long)cmd_bond_ok, (unsigned long)cmd_registered_ok);
    uart_put_line(text);

    /* ★★★2026/09/03 W8-a: Wake 広告の宛先に使う本体アドレスを出す。
     *   ★T コマンドへ渡す12桁は、この PEER の並びをそのまま使う。 */
    if (last_peer_valid) {
        snprintf(text, sizeof(text),
                 "PEER type=%u addr=%02x%02x%02x%02x%02x%02x",
                 (unsigned)last_peer_type,
                 last_peer_addr[0], last_peer_addr[1], last_peer_addr[2],
                 last_peer_addr[3], last_peer_addr[4], last_peer_addr[5]);
    } else {
        snprintf(text, sizeof(text),
                 "PEER none (connect once from the console to learn it)");
    }
    uart_put_line(text);
}

void cap_restore_adv(const char *name) {
    uint32_t actions = 0u;
    wake_result_t result;
    switch2_wake_adv_result_t adv_result;
    if (adv.peer_known) {
        adv_result = switch2_wake_adv_publish_reconnect(&adv);
        result = WAKE_RESULT_OK;
    } else {
        actions = WAKE_ACTION_ADV_STANDARD;
        adv_result = switch2_wake_adv_apply(&adv, actions);
        result = WAKE_RESULT_OK;
    }
    {
        char text[96];
        snprintf(text, sizeof(text), "%s core=%d adv=%d actions=0x%02lx",
                 name, (int)result, (int)adv_result, (unsigned long)actions);
        uart_put_line(text);
    }
}

/* CYW43 の public アドレスを上書きする（RPi の btmgmt public-addr 相当）。
 * 表記順で渡し、HCI へは LSB 先行で送る。 */
void bdaddr_spoof(const uint8_t canonical[6]) {
    uint8_t pkt[9];
    int i;
    pkt[0] = 0x01u;
    pkt[1] = 0xfcu;
    pkt[2] = 0x06u;
    for (i = 0; i < 6; i++) {
        pkt[3 + i] = canonical[5 - i];
    }
    hci_send_cmd_packet(pkt, (int)sizeof(pkt));
    hci_send_cmd(&hci_read_bd_addr);
}

void cap_show_list(void) {
    char text[128];
    uint8_t i;
    for (i = 0u; i < cap_table.used; i++) {
        const switch2_wake_capture_entry_t *e = &cap_table.slot[i];
        snprintf(text, sizeof(text),
                 "CAP-LIST %u mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x wake=%u rssi=%d n=%lu",
                 (unsigned)i,
                 e->addr[0], e->addr[1], e->addr[2],
                 e->addr[3], e->addr[4], e->addr[5],
                 (unsigned)e->addr_type, (unsigned)e->wake.pid,
                 e->has_wake ? 1u : 0u, e->rssi,
                 (unsigned long)e->sightings);
        uart_put_line(text);
    }
    if (cap_saved_valid) {
        snprintf(text, sizeof(text),
                 "CAP-SAVED spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                 cap_saved.spoof[0], cap_saved.spoof[1], cap_saved.spoof[2],
                 cap_saved.spoof[3], cap_saved.spoof[4], cap_saved.spoof[5],
                 cap_saved.switch_mac[0], cap_saved.switch_mac[1],
                 cap_saved.switch_mac[2], cap_saved.switch_mac[3],
                 cap_saved.switch_mac[4], cap_saved.switch_mac[5]);
    } else {
        snprintf(text, sizeof(text), "CAP-SAVED none");
    }
    uart_put_line(text);
}

void handle_command(switch2_wake_input_command_t *command) {
    uint32_t actions = 0u;
    wake_result_t result = WAKE_RESULT_OK;
    switch (command->kind) {
        case WAKE_INPUT_WAKE:
            result = switch2_wake_request(WAKE_SOURCE_UART, now_ms(), &actions);
            apply_actions("W", result, actions);
            break;
        case WAKE_INPUT_PAIR:
            result = switch2_wake_pair_request(WAKE_SOURCE_UART, now_ms(), &actions);
            apply_actions("P", result, actions);
            break;
        case WAKE_INPUT_STATUS:
            show_status();
            break;
        case WAKE_INPUT_ADV_ENABLE:
            actions = command->adv_enabled ? WAKE_ACTION_ADV_STANDARD
                                           : WAKE_ACTION_ADV_STOP;
            apply_actions(command->adv_enabled ? "V1" : "V0", result, actions);
            break;
        case WAKE_INPUT_FORGET_ARM:
            forget_armed = true;
            forget_deadline_ms = now_ms() + FORGET_CONFIRM_MS;
            uart_put_line("K ARMED; send K CONFIRM within 5 seconds");
            break;
        case WAKE_INPUT_FORGET_CONFIRM:
            if (!forget_armed || !deadline_active(now_ms(), forget_deadline_ms)) {
                forget_armed = false;
                uart_put_line("K ERR NOT_ARMED");
                break;
            }
            forget_armed = false;
            switch2_wake_adv_clear_peer(&adv);
            (void)switch2_wake_store_forget(&store);
            cmd_clear_pending_response();
            switch2_wake_cmd_clear_pairing(&cmd_state);
            result = switch2_wake_forget(WAKE_SOURCE_UART, &actions);
            apply_actions("K", result, actions);
            break;
        case WAKE_INPUT_TEST_PEER:
            switch2_wake_adv_set_peer(&adv, command->peer_identity);
            reset_core(true, &actions, &result);
            uart_put_line("T OK test peer only; this is not a verified bond");
            apply_actions("T", result, actions);
            break;
        case WAKE_INPUT_TEST_CLEAR:
            switch2_wake_adv_clear_peer(&adv);
            reset_core(false, &actions, &result);
            apply_actions("X", result, actions);
            break;
        case WAKE_INPUT_CAPTURE: {
            char text[64];
            if (cap_scanning || beacon_active) {
                uart_put_line("C ERR BUSY");
                break;
            }
            if (le_connection != HCI_CON_HANDLE_INVALID) {
                uart_put_line("C ERR CONNECTED");
                break;
            }
            switch2_wake_capture_table_init(&cap_table);
            (void)switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STOP);
            gap_set_scan_params(0u, 0x0030u, 0x0030u, 0u);
            gap_set_scan_duplicate_filter(false);
            gap_start_scan();
            cap_scanning = true;
            cap_deadline_ms = now_ms() + (uint32_t)command->capture_seconds * 1000u;
            snprintf(text, sizeof(text), "CAP-START dur=%us; press HOME on the Joy-Con",
                     (unsigned)command->capture_seconds);
            uart_put_line(text);
            break;
        }
        case WAKE_INPUT_CAP_LIST:
            cap_show_list();
            break;
        case WAKE_INPUT_DEBUG:
            hci_verbose = !hci_verbose;
            uart_put_line(hci_verbose ? "D verbose on" : "D verbose off");
            break;
        case WAKE_INPUT_BEACON: {
            uint8_t payload[WAKE_CAP_ADV_SIZE];
            if (!cap_saved_valid) {
                uart_put_line("B ERR NO_SAVE; run C first");
                break;
            }
            if (cap_scanning || beacon_active) {
                uart_put_line("B ERR BUSY");
                break;
            }
            if (le_connection != HCI_CON_HANDLE_INVALID) {
                uart_put_line("B ERR CONNECTED");
                break;
            }
            (void)switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STOP);
            bdaddr_spoof(cap_saved.spoof);
            memcpy(payload, cap_saved.payload, sizeof(payload));
            payload[16] = WAKE_CAP_TYPE_WAKE;
            gap_advertisements_set_data((uint8_t)sizeof(payload), payload);
            gap_advertisements_enable(1);
            beacon_active = true;
            beacon_deadline_ms = now_ms() + 1500u;
            uart_put_line("BCN-START 1.5s; keep the Joy-Con powered off");
            memset(payload, 0, sizeof(payload));
            break;
        }
        case WAKE_INPUT_IMPORT_BOND: {
            switch2_wake_store_result_t stored = switch2_wake_store_import(
                &store, command->local_identity, command->peer_type,
                command->peer_identity, command->ltk,
                command->irk_present ? command->irk : NULL);
            char reply[96];
            if (stored != WAKE_STORE_OK) {
                snprintf(reply, sizeof(reply), "I ERR %s",
                         switch2_wake_store_result_name(stored));
                uart_put_line(reply);
                break;
            }
            /* 投入直後は PENDING。暗号化と 0x0c/04 を見るまで Wake は許さない。 */
            switch2_wake_adv_set_peer(&adv, command->peer_identity);
            reset_core(false, &actions, &result);
            uart_put_line("I OK PENDING; encrypted reconnect is required before wake");
            apply_actions("I", result, actions);
            break;
        }
        default:
            uart_put_line("ERR commands: W | P | I ... | K ARM | K CONFIRM | S | V 0/1 | T <12hex> | X | C [s] | L | B | D");
            break;
    }
    switch2_wake_input_command_clear(command);
}
