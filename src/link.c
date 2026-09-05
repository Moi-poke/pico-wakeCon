/* 自アドレス 7C:BB:8A＋基板固有3B。電源で変わると覚え直しになるため固定。
 * 種類ごとに末尾をずらす。同じ MAC で種類を変えるとペア情報が混ざる。
 * 再生時は Joy-Con MAC へ一時偽装し、必ず元に戻す(Classic の身元の保全)。 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "cap.h"
#include "hid.h"
#include "link.h"
#include "store.h"
#include "switch_hid.h"
#include "type.h"
#include "ui.h"

bd_addr_t probe_addr;

void link_init(void)
{
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    probe_addr[0] = SWITCH_OUI_0;
    probe_addr[1] = SWITCH_OUI_1;
    probe_addr[2] = SWITCH_OUI_2;
    probe_addr[3] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3];
    probe_addr[4] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2];
    probe_addr[5] = (uint8_t)(id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1] +
                              type_get());
}

bd_addr_t probe_host_addr;
bool probe_host_known;
bool probe_outgoing_tried;
uint32_t probe_reconnect_tries;
uint8_t probe_reconnect_report;
bool probe_reconnect_pending;
uint32_t probe_giveup_count;
uint32_t probe_connected_at_ms;
uint8_t probe_ssp_count;

#define RECONNECT_RETRY_MS 5000u
#define RECONNECT_GIVEUP_MS 15000u
static uint32_t outgoing_at_ms;

void link_reconnect_handler(btstack_timer_source_t *ts)
{
    if (probe_outgoing_tried && probe_hid_cid == 0u && outgoing_at_ms != 0u) {
        uint32_t waited =
            to_ms_since_boot(get_absolute_time()) - outgoing_at_ms;
        if (waited > RECONNECT_GIVEUP_MS) {
            probe_outgoing_tried = false;
            outgoing_at_ms = 0u;
            probe_giveup_count++;
        }
    }
    /* 再生中は繋ぎに行かない。偽装 MAC で名乗るのを防ぐ。 */
    if (probe_beacon) {
        btstack_run_loop_set_timer(ts, 1000);
        btstack_run_loop_add_timer(ts);
        return;
    }
    if (probe_hid_cid == 0u && !probe_outgoing_tried && probe_host_known) {
        uint16_t cid = 0u;
        uint8_t st;
        probe_reconnect_tries++;
        probe_outgoing_tried = true;
        st = hid_device_connect(probe_host_addr, &cid);
        if (st != ERROR_CODE_SUCCESS) {
            probe_outgoing_tried = false;
            outgoing_at_ms = 0u;
        } else {
            outgoing_at_ms = to_ms_since_boot(get_absolute_time());
        }
        probe_reconnect_report = st;
        probe_reconnect_pending = true;
    }
    btstack_run_loop_set_timer(ts, RECONNECT_RETRY_MS);
    btstack_run_loop_add_timer(ts);
}

void link_note_disconnected(void)
{
    probe_hid_cid = 0u;
    probe_outgoing_tried = false;
    outgoing_at_ms = 0u;
}

void link_mark_connected(void)
{
    probe_outgoing_tried = true;
    outgoing_at_ms = 0u;
}

int link_key_count(void)
{
    int count = 0;
    bd_addr_t addr;
    link_key_t key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;
    if (gap_link_key_iterator_init(&it) == 0) {
        return -1;
    }
    while (gap_link_key_iterator_get_next(&it, addr, key, &type)) {
        count++;
    }
    gap_link_key_iterator_done(&it);
    return count;
}

/* ---- 取込 ---- */

cap_table_t probe_cap_table;
cap_saved_t probe_cap_saved;
bool probe_cap_valid;
bool probe_scanning;
bool probe_beacon;
static uint32_t cap_deadline_ms;
static uint32_t beacon_deadline_ms;
static hci_con_handle_t le_handle = HCI_CON_HANDLE_INVALID;
static bd_addr_t null_addr;

bool link_cap_start(uint8_t seconds)
{
    char text[64];
    if (probe_scanning || probe_beacon) {
        probe_line("C ERR BUSY");
        return false;
    }
    if (probe_hid_cid != 0u) {
        probe_line("C ERR CONNECTED");
        return false;
    }
    cap_table_init(&probe_cap_table);
    gap_set_scan_params(0u, 0x0030u, 0x0030u, 0u);
    gap_set_scan_duplicate_filter(false);
    gap_start_scan();
    probe_scanning = true;
    cap_deadline_ms =
        to_ms_since_boot(get_absolute_time()) + (uint32_t)seconds * 1000u;
    snprintf(text, sizeof(text), "CAP-START %us. press HOME on Joy-Con",
             (unsigned)seconds);
    probe_line(text);
    return true;
}

void link_cap_report(const uint8_t *packet)
{
    bd_addr_t raddr;
    uint8_t rtype;
    int rssi;
    uint8_t dlen;
    const uint8_t *data;
    cap_hit_t hit;
    char text[128];
    int idx;
    if (!probe_scanning) {
        return;
    }
    gap_event_advertising_report_get_address(packet, raddr);
    rtype = gap_event_advertising_report_get_address_type(packet);
    rssi = (int)gap_event_advertising_report_get_rssi(packet);
    dlen = gap_event_advertising_report_get_data_length(packet);
    data = gap_event_advertising_report_get_data(packet);
    if (!cap_parse(data, dlen, &hit)) {
        return;
    }
    idx = cap_offer(&probe_cap_table, raddr, rtype, rssi, &hit);
    snprintf(text, sizeof(text),
             "CAP mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x flag=%02x rssi=%d idx=%d",
             raddr[0], raddr[1], raddr[2], raddr[3], raddr[4], raddr[5],
             (unsigned)rtype, (unsigned)hit.pid, (unsigned)hit.flag,
             rssi, idx);
    probe_line(text);
}

/* CYW43 の public アドレスを上書きする。表記順で渡し LSB 先行で送る。 */
static void bdaddr_spoof(const uint8_t canonical[6])
{
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

bool link_beacon_start(void)
{
    uint8_t payload[CAP_ADV_SIZE];
    if (!probe_cap_valid) {
        probe_line("B ERR NO_SAVE. run C first");
        return false;
    }
    if (probe_scanning || probe_beacon) {
        probe_line("B ERR BUSY");
        return false;
    }
    if (probe_hid_cid != 0u) {
        probe_line("B ERR CONNECTED");
        return false;
    }
    /* Classic を黙らせる。inquiry/page 応答と再接続が LE 広告の
     * 電波時間を奪うため。終了後に戻す。 */
    gap_discoverable_control(0);
    gap_connectable_control(0);
    bdaddr_spoof(probe_cap_saved.spoof);
    memcpy(payload, probe_cap_saved.payload, sizeof(payload));
    payload[16] = CAP_TYPE_WAKE;
    gap_advertisements_set_params(0x0030u, 0x0030u, 0x00u, 0u, null_addr,
                                  0x07u, 0x00u);
    gap_advertisements_set_data((uint8_t)sizeof(payload), payload);
    gap_advertisements_enable(1);
    memset(payload, 0, sizeof(payload));
    probe_beacon = true;
    beacon_deadline_ms = to_ms_since_boot(get_absolute_time()) + 1500u;
    probe_line("BCN-START 1.5s. keep Joy-Con off");
    return true;
}

uint8_t link_cap_used(void)
{
    return probe_cap_table.used;
}

const cap_entry_t *link_cap_entry(uint8_t i)
{
    if (i >= probe_cap_table.used) {
        return NULL;
    }
    return &probe_cap_table.slot[i];
}

/* 一覧と保存を捨てる。次回 C から取り直す。 */
void link_cap_clear(void)
{
    cap_table_init(&probe_cap_table);
    memset(&probe_cap_saved, 0, sizeof(probe_cap_saved));
    probe_cap_valid = false;
    store_cap_forget();
    probe_line("cap cleared");
}

void link_poll(uint32_t now_ms)
{
    /* 取込期限。 */
    if (probe_scanning &&
        (int32_t)(now_ms - cap_deadline_ms) >= 0) {
        char text[112];
        int best;
        gap_stop_scan();
        probe_scanning = false;
        best = cap_best_wake(&probe_cap_table);
        if (best >= 0) {
            const cap_entry_t *e = &probe_cap_table.slot[best];
            probe_cap_valid = false;
            memcpy(probe_cap_saved.spoof, e->addr, 6);
            probe_cap_saved.spoof_type = e->addr_type;
            memcpy(probe_cap_saved.switch_mac, e->wake.switch_mac, 6);
            memcpy(probe_cap_saved.payload, e->wake.payload, CAP_ADV_SIZE);
            if (store_cap_save()) {
                probe_cap_valid = true;
            }
            snprintf(text, sizeof(text),
                     "CAP-DONE saved=%u spoof=%02x%02x%02x%02x%02x%02x",
                     probe_cap_valid ? 1u : 0u,
                     e->addr[0], e->addr[1], e->addr[2],
                     e->addr[3], e->addr[4], e->addr[5]);
        } else {
            snprintf(text, sizeof(text), "CAP-DONE saved=0 seen=%u",
                     (unsigned)probe_cap_table.used);
        }
        probe_line(text);
    }
    /* 再生期限。アドレスと Classic を戻し、LE 接続が残れば切る。 */
    if (probe_beacon &&
        (int32_t)(now_ms - beacon_deadline_ms) >= 0) {
        probe_beacon = false;
        gap_advertisements_enable(0);
        bdaddr_spoof(probe_addr);
        gap_discoverable_control(1);
        gap_connectable_control(1);
        if (le_handle != HCI_CON_HANDLE_INVALID) {
            gap_disconnect(le_handle);
            le_handle = HCI_CON_HANDLE_INVALID;
        }
        probe_line("BCN-DONE. address restored");
    }
}

void link_le_packet(uint8_t packet_type, uint8_t *packet, uint16_t size)
{
    uint8_t ev;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    ev = hci_event_packet_get_type(packet);
    if (ev == HCI_EVENT_META_GAP &&
        hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
        le_handle =
            gap_subevent_le_connection_complete_get_connection_handle(packet);
        return;
    }
    if (ev == HCI_EVENT_DISCONNECTION_COMPLETE &&
        hci_event_disconnection_complete_get_connection_handle(packet) ==
            le_handle) {
        le_handle = HCI_CON_HANDLE_INVALID;
    }
}

bool link_is_le(hci_con_handle_t handle)
{
    return le_handle != HCI_CON_HANDLE_INVALID && handle == le_handle;
}
