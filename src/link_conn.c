/* 接続管理・再接続。Classic HID の身元保全が責務。 */

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hid.h"
#include "link.h"
#include "switch_hid.h"
#include "ui.h"
#include "usb_wired.h"

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
    probe_addr[5] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
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
    /* 有線モード中は Classic に出ていかない。二重認識防止。
     * タイマだけ繋ぎ直して周期を保つ (W 0 で通常輪に戻る)。 */
    if (usb_wired_is_enabled()) {
        btstack_run_loop_set_timer(ts, RECONNECT_RETRY_MS);
        btstack_run_loop_add_timer(ts);
        return;
    }
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
