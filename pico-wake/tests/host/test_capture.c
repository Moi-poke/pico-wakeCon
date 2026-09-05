#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_capture.h"

/* Build 31-byte adv per brief layout. */
static void build_adv(uint8_t adv[31], uint16_t pid, uint8_t flag,
                      const uint8_t switch_mac[6]) {
    int i;
    memset(adv, 0, 31);
    adv[0] = 0x02u;
    adv[1] = 0x01u;
    adv[2] = 0x06u;
    adv[3] = 0x1Bu;
    adv[4] = 0xFFu;
    adv[5] = 0x53u;
    adv[6] = 0x05u;
    adv[7] = 0x01u;
    adv[8] = 0x00u;
    adv[9] = 0x03u;
    adv[10] = 0x7Eu;
    adv[11] = 0x05u;
    adv[12] = (uint8_t)(pid & 0xFFu);
    adv[13] = (uint8_t)((pid >> 8) & 0xFFu);
    adv[14] = 0x00u;
    adv[15] = 0x01u;
    adv[16] = flag;
    /* switch MAC stored air-order (reversed) */
    for (i = 0; i < 6; i++) {
        adv[17 + i] = switch_mac[5 - i];
    }
    /* adv[23..30] remain zero pad (7 pad bytes + 1) */
}

static void make_hit(switch2_wake_capture_hit_t *hit, uint8_t flag,
                     const uint8_t mac[6], int fill_payload) {
    uint8_t adv[31];
    memset(hit, 0, sizeof(*hit));
    build_adv(adv, 0x1234u, flag, mac);
    if (fill_payload) {
        assert(switch2_wake_capture_parse(adv, 31u, hit) == true);
    } else {
        hit->flag = flag;
        memcpy(hit->switch_mac, mac, 6);
        memset(hit->payload, 0, sizeof(hit->payload));
        hit->payload[16] = 0x81u;
        hit->payload_len = 31u;
    }
}

int main(void) {
    /* 1. parse valid */
    {
        uint8_t adv[31];
        const uint8_t mac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        const uint8_t air[6] = {0xFFu, 0xEEu, 0xDDu, 0xCCu, 0xBBu, 0xAAu};
        switch2_wake_capture_hit_t hit;
        int i;
        (void)air;
        build_adv(adv, 0x1234u, 0x81u, mac);
        assert(switch2_wake_capture_parse(adv, 31u, &hit) == true);
        assert(hit.pid == 0x1234u);
        assert(hit.flag == 0x81u);
        assert(memcmp(hit.switch_mac, mac, 6) == 0);
        assert(hit.payload_len == 31u);
        for (i = 0; i < 6; i++) {
            assert(hit.payload[17 + i] == air[i]);
        }
    }
    /* 2. company mismatch / truncated / NULL */
    {
        uint8_t adv[31];
        const uint8_t mac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        switch2_wake_capture_hit_t hit;
        build_adv(adv, 0x1234u, 0x81u, mac);
        adv[5] = 0x54u;
        assert(switch2_wake_capture_parse(adv, 31u, &hit) == false);
        build_adv(adv, 0x1234u, 0x81u, mac);
        assert(switch2_wake_capture_parse(adv, 10u, &hit) == false);
        assert(switch2_wake_capture_parse(NULL, 31u, &hit) == false);
        assert(switch2_wake_capture_parse(adv, 31u, NULL) == false);
    }
    /* 3. offer */
    {
        switch2_wake_capture_table_t table;
        const uint8_t addrA[6] = {0x01u, 0u, 0u, 0u, 0u, 0u};
        const uint8_t addrB[6] = {0x02u, 0u, 0u, 0u, 0u, 0u};
        const uint8_t mac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        switch2_wake_capture_hit_t hit;
        int idx;
        int i;
        switch2_wake_capture_table_init(&table);
        make_hit(&hit, 0x81u, mac, 1);
        idx = switch2_wake_capture_offer(&table, addrA, 0u, -50, &hit);
        assert(idx == 0);
        idx = switch2_wake_capture_offer(&table, addrA, 0u, -40, &hit);
        assert(idx == 0);
        assert(table.slot[0].sightings == 2u);
        assert(table.slot[0].rssi == -40);
        idx = switch2_wake_capture_offer(&table, addrB, 0u, -60, &hit);
        assert(idx == 1);
        for (i = 2; i < 8; i++) {
            uint8_t addr[6] = {(uint8_t)(0x10u + (uint8_t)i), 0u, 0u, 0u, 0u, 0u};
            idx = switch2_wake_capture_offer(&table, addr, 0u, -70, &hit);
            assert(idx == i);
        }
        {
            const uint8_t addr9[6] = {0x99u, 0u, 0u, 0u, 0u, 0u};
            assert(switch2_wake_capture_offer(&table, addr9, 0u, -70, &hit) == -1);
        }
    }
    /* 4. best_wake */
    {
        switch2_wake_capture_table_t table;
        const uint8_t addrA[6] = {0x0Au, 0u, 0u, 0u, 0u, 0u};
        const uint8_t addrB[6] = {0x0Bu, 0u, 0u, 0u, 0u, 0u};
        const uint8_t addrC[6] = {0x0Cu, 0u, 0u, 0u, 0u, 0u};
        const uint8_t mac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        const uint8_t zero[6] = {0u, 0u, 0u, 0u, 0u, 0u};
        switch2_wake_capture_hit_t hitA;
        switch2_wake_capture_hit_t hitB;
        switch2_wake_capture_hit_t hitC;
        switch2_wake_capture_table_init(&table);
        make_hit(&hitA, 0x81u, mac, 1);
        make_hit(&hitB, 0x81u, mac, 1);
        make_hit(&hitC, 0x81u, zero, 1);
        /* hitC built with zero mac: parse would give zero mac only if
           air bytes zero; force zero to test the skip path. */
        memset(hitC.switch_mac, 0, 6);
        assert(switch2_wake_capture_offer(&table, addrA, 0u, -60, &hitA) == 0);
        assert(switch2_wake_capture_offer(&table, addrB, 0u, -30, &hitB) == 1);
        assert(switch2_wake_capture_offer(&table, addrC, 0u, -10, &hitC) == 2);
        assert(switch2_wake_capture_best_wake(&table) == 1);
    }
    {
        switch2_wake_capture_table_t table;
        const uint8_t addrA[6] = {0x0Au, 0u, 0u, 0u, 0u, 0u};
        const uint8_t mac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        switch2_wake_capture_hit_t hit;
        switch2_wake_capture_table_init(&table);
        make_hit(&hit, 0x00u, mac, 0);
        hit.flag = 0x00u;
        assert(switch2_wake_capture_offer(&table, addrA, 0u, -50, &hit) == 0);
        assert(switch2_wake_capture_best_wake(&table) == -1);
    }
    /* 5. encode/decode */
    {
        switch2_wake_capture_saved_t saved;
        switch2_wake_capture_saved_t back;
        uint8_t blob[WAKE_CAP_BLOB_SIZE];
        const uint8_t spoof[6] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
        const uint8_t swmac[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
        int i;
        memset(&saved, 0, sizeof(saved));
        memcpy(saved.spoof, spoof, 6);
        saved.spoof_type = 1u;
        memcpy(saved.switch_mac, swmac, 6);
        for (i = 0; i < 31; i++) { saved.payload[i] = (uint8_t)i; }
        saved.payload[16] = 0x81u;
        assert(switch2_wake_capture_encode(&saved, blob) == true);
        memset(&back, 0, sizeof(back));
        assert(switch2_wake_capture_decode(blob, WAKE_CAP_BLOB_SIZE, &back) == true);
        assert(memcmp(back.spoof, spoof, 6) == 0);
        assert(back.spoof_type == 1u);
        assert(memcmp(back.switch_mac, swmac, 6) == 0);
        assert(memcmp(back.payload, saved.payload, 31) == 0);
    }
    {
        switch2_wake_capture_saved_t saved;
        uint8_t blob[WAKE_CAP_BLOB_SIZE];
        switch2_wake_capture_saved_t back;
        memset(&saved, 0, sizeof(saved));
        saved.spoof_type = 1u;
        assert(switch2_wake_capture_encode(&saved, blob) == false);
        /* payload[16] != 0x81 -> decode false */
        {
            const uint8_t spoof[6] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
            memset(blob, 0, sizeof(blob));
            memcpy(&blob[0], spoof, 6);
            blob[6] = 1u;
            blob[13 + 16] = 0x00u;
            assert(switch2_wake_capture_decode(blob, WAKE_CAP_BLOB_SIZE, &back) == false);
        }
        assert(switch2_wake_capture_decode(blob, 0u, &back) == false);
        assert(switch2_wake_capture_decode(blob, WAKE_CAP_BLOB_SIZE - 1u, &back) == false);
        assert(switch2_wake_capture_decode(NULL, WAKE_CAP_BLOB_SIZE, &back) == false);
        assert(switch2_wake_capture_decode(blob, WAKE_CAP_BLOB_SIZE, NULL) == false);
    }

    printf("capture ok\n");
    return 0;
}
