/* ビーコン再生・LE追跡。再生時は Joy-Con MAC へ一時偽装し、必ず元に戻す。 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "cap.h"
#include "hid.h"
#include "link.h"
#include "ui.h"

#define BEACON_MS 1500u
#define ADV_INTERVAL 0x0030u

bool probe_beacon;
static uint32_t beacon_deadline_ms;
static hci_con_handle_t le_handle = HCI_CON_HANDLE_INVALID;
static bd_addr_t null_addr;

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
    /* 電波を確保する (有線中の電源断から復帰する場合あり)。
     * 続く Classic 黙らせは再生専用のため update より後に置く。 */
    link_radio_update();
    /* Classic を黙らせる。inquiry/page 応答と再接続が LE 広告の
     * 電波時間を奪うため。終了後は link_radio_update が戻す。 */
    gap_discoverable_control(0);
    gap_connectable_control(0);
    bdaddr_spoof(probe_cap_saved.spoof);
    memcpy(payload, probe_cap_saved.payload, sizeof(payload));
    payload[16] = CAP_TYPE_WAKE;
    gap_advertisements_set_params(ADV_INTERVAL, ADV_INTERVAL, 0x00u, 0u, null_addr,
                                  0x07u, 0x00u);
    gap_advertisements_set_data((uint8_t)sizeof(payload), payload);
    gap_advertisements_enable(1);
    memset(payload, 0, sizeof(payload));
    probe_beacon = true;
    beacon_deadline_ms = to_ms_since_boot(get_absolute_time()) + BEACON_MS;
    probe_line("BCN-START 1.5s. keep Joy-Con off");
    return true;
}

/* 再生期限。アドレスを戻し、LE 接続が残れば切る。
 * Classic 待ち受け・電波は link_radio_update が決める (有線中は戻さない)。 */
void link_beacon_tick(uint32_t now_ms)
{
    if (probe_beacon &&
        (int32_t)(now_ms - beacon_deadline_ms) >= 0) {
        probe_beacon = false;
        gap_advertisements_enable(0);
        bdaddr_spoof(probe_addr);
        if (le_handle != HCI_CON_HANDLE_INVALID) {
            gap_disconnect(le_handle);
            le_handle = HCI_CON_HANDLE_INVALID;
        }
        probe_line("BCN-DONE. address restored");
        /* 有線中なら待ち受けを戻さず電波も止める。無線中は従来通り戻す。 */
        link_radio_update();
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
