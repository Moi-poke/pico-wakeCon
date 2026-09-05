/* tests/host/test_usb.c: test_cap.c と同形式。BTstack/Pico不要。 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "usb_hid.h"

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

int main(void)
{
    uint8_t out[64];
    const uint8_t mac[6] = {0x7c, 0xbb, 0x8a, 0x11, 0x22, 0x33};
    int n;
    uint8_t r01[] = {0x80, 0x01};
    uint8_t r02[] = {0x80, 0x02};
    uint8_t r03[] = {0x80, 0x03};
    uint8_t r04[] = {0x80, 0x04};
    uint8_t r05[] = {0x80, 0x05};
    uint8_t r06[] = {0x80, 0x06};

    /* 80 01 は全 10B (81 01 00 <type> <mac6>) を返す。type は入力の写し。 */
    n = usb_build_81_reply(r01, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 10 && out[0] == 0x81u && out[1] == 0x01u && out[2] == 0x00u);
    CHECK(out[3] == 0x03u && memcmp(&out[4], mac, 6) == 0);
    n = usb_build_81_reply(r02, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x02u);
    n = usb_build_81_reply(r03, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x03u);
    n = usb_build_81_reply(r04, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x04u);
    n = usb_build_81_reply(r05, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x05u);
    n = usb_build_81_reply(r06, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x06u);
    /* handshake 判定の真偽。 */
    CHECK(usb_req_is_handshake(r01, 2));
    {
        uint8_t noths[] = {0x81, 0x01};
        CHECK(!usb_req_is_handshake(noths, 2));
        CHECK(!usb_req_is_handshake(r01, 1));
        CHECK(!usb_req_is_handshake(NULL, 2));
    }
    /* NULL 引数は 0 を返す。 */
    CHECK(usb_build_81_reply(NULL, 2, out, sizeof(out), mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r01, 2, NULL, sizeof(out), mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r01, 2, out, sizeof(out), NULL, 0x03u) == 0);
    /* out が短い場合は 0 を返す。 */
    CHECK(usb_build_81_reply(r01, 2, out, 9, mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r02, 2, out, 1, mac, 0x03u) == 0);
    /* 91/92 は未対応=0 を返す (でたらめ値を返さない)。 */
    {
        uint8_t r91[] = {0x80, 0x91, 0x00};
        uint8_t r92[] = {0x80, 0x92};
        uint8_t rff[] = {0x80, 0xFF};
        CHECK(usb_build_81_reply(r91, 3, out, sizeof(out), mac, 0x03u) == 0);
        CHECK(usb_build_81_reply(r92, 2, out, sizeof(out), mac, 0x03u) == 0);
        CHECK(usb_build_81_reply(rff, 2, out, sizeof(out), mac, 0x03u) == 0);
    }
    if (fails == 0) { printf("OK usb\n"); }
    return fails != 0;
}
