/* 純粋ヘルパー。BTstack 非依存。cap/hid/ui の重複を集約。 */

#include <stdio.h>
#include <string.h>

#include "util.h"
#include "spi.h"

bool util_is_zero(const uint8_t *d, size_t n)
{
    size_t i;
    uint8_t acc = 0u;
    if (d == NULL) {
        return true;
    }
    for (i = 0u; i < n; i++) {
        acc |= d[i];
    }
    return acc == 0u;
}

int util_hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool util_parse_hex(const char *s, int len, uint32_t *out)
{
    uint32_t v = 0u;
    int k;
    if (s == NULL || out == NULL) {
        return false;
    }
    if (len <= 0 || len > 8) {
        return false;
    }
    for (k = 0; k < len; k++) {
        int d = util_hex_val(s[k]);
        if (d < 0) {
            return false;
        }
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}

/* s[0..len) を空白分割し start 位置から want 個の16進値を読む。 */
int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want)
{
    int idx = 0;
    int p = start;
    if (s == NULL || v == NULL || want <= 0) {
        return 0;
    }
    while (idx < want) {
        int st;
        while (p < len && s[p] == ' ') {
            p++;
        }
        st = p;
        while (p < len && s[p] != ' ') {
            p++;
        }
        if (p == st) {
            return 0;
        }
        if (!util_parse_hex(&s[st], p - st, &v[idx])) {
            return 0;
        }
        idx++;
    }
    return 1;
}

void util_pack_stick_12bit(uint8_t x8, uint8_t y8, uint8_t *out)
{
    uint16_t x = (uint16_t)x8 << 4;
    uint16_t y = (uint16_t)4096 - ((uint16_t)y8 << 4);
    if (y > 4095u) {
        y = 4095u;
    }
    out[0] = (uint8_t)(x & 0xFFu);
    out[1] = (uint8_t)(((x >> 8) & 0x0Fu) | ((y & 0x0Fu) << 4));
    out[2] = (uint8_t)((y >> 4) & 0xFFu);
}

/* O応答と状態表示で重複していた色フォーマット。書式は従来通り。 */
void util_format_color(char *m, size_t n)
{
    if (m == NULL || n < 64u) {
        return;
    }
    snprintf(m, n,
             "color body=%02x%02x%02x btn=%02x%02x%02x"
             " left=%02x%02x%02x right=%02x%02x%02x",
             spi_color_6050[0], spi_color_6050[1], spi_color_6050[2],
             spi_color_6050[3], spi_color_6050[4], spi_color_6050[5],
             spi_color_6050[6], spi_color_6050[7], spi_color_6050[8],
             spi_color_6050[9], spi_color_6050[10], spi_color_6050[11]);
}
