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
    uint8_t r02[] = {0x80, 0x02};
    uint8_t r04[] = {0x80, 0x04};

    n = usb_build_81_reply(r02, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x02u);
    n = usb_build_81_reply(r04, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x04u);
    /* 91/92 は未対応=0 を返す (でたらめ値を返さない)。 */
    {
        uint8_t r91[] = {0x80, 0x91, 0x00};
        CHECK(usb_build_81_reply(r91, 3, out, sizeof(out), mac, 0x03u) == 0);
    }
    if (fails == 0) { printf("OK usb\n"); }
    return fails != 0;
}
