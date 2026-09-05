/* cap host test: parse/offer/best/encode/decode. BTstack/Pico不要。 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cap.h"

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

int main(void)
{
    /* Company 0x0553 / VID 0x057E を含む31B広告の最小構成。 */
    uint8_t adv[31];
    cap_hit_t hit;
    cap_table_t tab;
    cap_saved_t saved;
    uint8_t blob[CAP_BLOB_SIZE];
    uint8_t addr[6] = {0x98, 0xe2, 0x55, 0xb1, 0x28, 0x5b};
    int idx, best;

    memset(adv, 0, sizeof(adv));
    adv[0] = 27u; adv[1] = 0xffu;          /* len=27 type=FF */
    adv[2] = 0x53u; adv[3] = 0x05u;        /* company */
    adv[7] = 0x7eu; adv[8] = 0x05u;        /* vid */
    adv[9] = 0x66u; adv[10] = 0x20u;       /* pid 0x2066 */
    adv[13] = 0x81u;                       /* flag wake */
    adv[14] = 0x01u; adv[15] = 0x02u; adv[16] = 0x03u;
    adv[17] = 0x04u; adv[18] = 0x05u; adv[19] = 0x06u;
    adv[29] = 0u;                          /* 終端 */

    CHECK(cap_parse(adv, 31u, &hit));
    CHECK(hit.pid == 0x2066u);
    CHECK(hit.flag == 0x81u);
    CHECK(hit.switch_mac[0] == 0x06u && hit.switch_mac[5] == 0x01u);

    adv[2] = 0x00u;
    CHECK(!cap_parse(adv, 31u, &hit));
    adv[2] = 0x53u;

    cap_table_init(&tab);
    CHECK(cap_parse(adv, 31u, &hit));
    idx = cap_offer(&tab, addr, 0u, -45, &hit);
    CHECK(idx == 0);
    CHECK(tab.used == 1u);
    idx = cap_offer(&tab, addr, 0u, -40, &hit);
    CHECK(idx == 0 && tab.slot[0].rssi == -40 && tab.slot[0].sightings == 2u);
    best = cap_best_wake(&tab);
    CHECK(best == 0);

    memset(&saved, 0, sizeof(saved));
    memcpy(saved.spoof, addr, 6);
    saved.spoof_type = 0u;
    memset(saved.switch_mac, 0x11, 6);
    memcpy(saved.payload, adv, 31);
    CHECK(cap_encode(&saved, blob));
    memset(&saved, 0, sizeof(saved));
    CHECK(cap_decode(blob, CAP_BLOB_SIZE, &saved));
    CHECK(saved.payload[16] == 0x81u);
    memset(&saved, 0, sizeof(saved));
    CHECK(!cap_decode(blob, 10u, &saved));
    memset(saved.spoof, 0, 6);
    CHECK(!cap_encode(&saved, blob));

    if (fails == 0) {
        printf("PASS cap\n");
        return 0;
    }
    printf("FAIL cap: %d\n", fails);
    return 1;
}
