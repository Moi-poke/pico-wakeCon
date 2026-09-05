#include "switch2_wake_capture.h"
#include "switch2_wake_util.h"

#include <string.h>

uint32_t switch2_wake_capture_tag(void)
{
    return ((uint32_t)WAKE_CAP_TAG_0 << 24) |
           ((uint32_t)WAKE_CAP_TAG_1 << 16) |
           ((uint32_t)WAKE_CAP_TAG_2 << 8) |
           (uint32_t)WAKE_CAP_TAG_3;
}

void switch2_wake_capture_table_init(switch2_wake_capture_table_t *table)
{
    if (table != NULL) {
        memset(table, 0, sizeof(*table));
    }
}

bool switch2_wake_capture_parse(const uint8_t *adv, uint8_t adv_len,
                                switch2_wake_capture_hit_t *hit)
{
    uint8_t pos = 0u;
    bool saw_manufacturer = false;
    if (adv == NULL || hit == NULL) {
        return false;
    }
    memset(hit, 0, sizeof(*hit));
    while ((uint16_t)pos + 1u < (uint16_t)adv_len) {
        uint8_t field_len = adv[pos];
        uint8_t field_type;
        const uint8_t *field;
        if (field_len == 0u) {
            break;
        }
        if ((uint16_t)pos + 1u + (uint16_t)field_len > (uint16_t)adv_len) {
            return false;
        }
        field_type = adv[pos + 1u];
        field = &adv[pos + 2u];
        if (field_type == 0xffu) {
            uint16_t pid;
            uint8_t i;
            /* 2 + 26 の構成だけ受け付ける（Switch 2 コントローラ形式）。 */
            if (field_len != (uint8_t)(WAKE_CAP_MFG_SIZE + 1u)) {
                return false;
            }
            if (field[0] != 0x53u || field[1] != 0x05u) {
                return false;
            }
            if (field[5] != 0x7eu || field[6] != 0x05u) {
                return false;
            }
            pid = (uint16_t)field[7] | ((uint16_t)field[8] << 8);
            hit->pid = pid;
            hit->flag = field[11];
            /* 本体 MAC は空中順（反転済み）で載る。表記順へ戻す。 */
            for (i = 0u; i < 6u; i++) {
                hit->switch_mac[i] = field[12u + (5u - i)];
            }
            if (adv_len > WAKE_CAP_ADV_SIZE) {
                return false;
            }
            memcpy(hit->payload, adv, adv_len);
            hit->payload_len = adv_len;
            saw_manufacturer = true;
        }
        pos = (uint8_t)(pos + 1u + field_len);
    }
    return saw_manufacturer;
}

int switch2_wake_capture_offer(switch2_wake_capture_table_t *table,
                               const uint8_t addr[6], uint8_t addr_type,
                               int rssi,
                               const switch2_wake_capture_hit_t *hit)
{
    uint8_t i;
    if (table == NULL || addr == NULL || hit == NULL) {
        return -1;
    }
    for (i = 0u; i < table->used; i++) {
        if (table->slot[i].addr_type == addr_type &&
            memcmp(table->slot[i].addr, addr, 6) == 0) {
            table->slot[i].sightings++;
            if (rssi > table->slot[i].rssi) {
                table->slot[i].rssi = rssi;
            }
            if (hit->flag == WAKE_CAP_TYPE_WAKE) {
                table->slot[i].has_wake = true;
                table->slot[i].wake = *hit;
            }
            return (int)i;
        }
    }
    if (table->used >= WAKE_CAP_TABLE_SIZE) {
        return -1;
    }
    i = table->used++;
    memcpy(table->slot[i].addr, addr, 6);
    table->slot[i].addr_type = addr_type;
    table->slot[i].rssi = rssi;
    table->slot[i].sightings = 1u;
    if (hit->flag == WAKE_CAP_TYPE_WAKE) {
        table->slot[i].has_wake = true;
        table->slot[i].wake = *hit;
    } else {
        table->slot[i].has_wake = false;
    }
    return (int)i;
}

int switch2_wake_capture_best_wake(const switch2_wake_capture_table_t *table)
{
    uint8_t i;
    int best = -1;
    if (table == NULL) {
        return -1;
    }
    for (i = 0u; i < table->used; i++) {
        if (!table->slot[i].has_wake) {
            continue;
        }
        if (switch2_wake_is_all_zero(table->slot[i].wake.switch_mac, 6)) {
            continue;
        }
        if (best < 0 || table->slot[i].rssi > table->slot[best].rssi) {
            best = (int)i;
        }
    }
    return best;
}

bool switch2_wake_capture_encode(const switch2_wake_capture_saved_t *saved,
                                 uint8_t *blob)
{
    if (saved == NULL || blob == NULL) {
        return false;
    }
    if (switch2_wake_is_all_zero(saved->spoof, 6)) {
        return false;
    }
    memset(blob, 0, WAKE_CAP_BLOB_SIZE);
    memcpy(&blob[0], saved->spoof, 6);
    blob[6] = saved->spoof_type;
    memcpy(&blob[7], saved->switch_mac, 6);
    memcpy(&blob[13], saved->payload, WAKE_CAP_ADV_SIZE);
    blob[13u + 16u] = WAKE_CAP_TYPE_WAKE;
    return true;
}

bool switch2_wake_capture_decode(const uint8_t *blob, uint32_t length,
                                 switch2_wake_capture_saved_t *saved)
{
    if (blob == NULL || saved == NULL) {
        return false;
    }
    if (length != WAKE_CAP_BLOB_SIZE) {
        return false;
    }
    memset(saved, 0, sizeof(*saved));
    memcpy(saved->spoof, &blob[0], 6);
    saved->spoof_type = blob[6];
    memcpy(saved->switch_mac, &blob[7], 6);
    memcpy(saved->payload, &blob[13], WAKE_CAP_ADV_SIZE);
    if (switch2_wake_is_all_zero(saved->spoof, 6)) {
        return false;
    }
    if (saved->payload[16] != WAKE_CAP_TYPE_WAKE) {
        return false;
    }
    return true;
}
