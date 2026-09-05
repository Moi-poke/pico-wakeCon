/* util host test: hex/zero/split/color. BTstack/Pico不要。 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "util.h"

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

int main(void)
{
    uint32_t v = 0u;
    uint32_t t[6];
    char m[128];
    uint8_t z[4] = {0, 0, 0, 0};
    uint8_t nz[4] = {0, 1, 0, 0};

    CHECK(util_hex_val('0') == 0);
    CHECK(util_hex_val('9') == 9);
    CHECK(util_hex_val('a') == 10);
    CHECK(util_hex_val('F') == 15);
    CHECK(util_hex_val('g') == -1);

    CHECK(util_parse_hex("1a", 2, &v) && v == 0x1au);
    CHECK(!util_parse_hex("zz", 2, &v));
    CHECK(!util_parse_hex("", 0, &v));
    CHECK(!util_parse_hex("123456789", 9, &v));

    CHECK(util_is_zero(z, 4));
    CHECK(!util_is_zero(nz, 4));

    CHECK(util_split_tokens("S 4 8 80 80 80 80", 16, 1, t, 6));
    CHECK(t[0] == 0x4u && t[1] == 0x8u && t[2] == 0x80u);
    CHECK(!util_split_tokens("S 4 ZZ", 7, 1, t, 6));
    CHECK(!util_split_tokens("S", 1, 1, t, 6));

    util_format_color(m, sizeof(m));
    CHECK(strncmp(m, "color body=", 11) == 0);

    if (fails == 0) {
        printf("PASS util\n");
        return 0;
    }
    printf("FAIL util: %d\n", fails);
    return 1;
}
