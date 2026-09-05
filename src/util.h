#ifndef WAKECON_UTIL_H
#define WAKECON_UTIL_H

/* 純粋ヘルパー集約。BTstack 非依存。ホストテスト可能に保つ。 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool util_is_zero(const uint8_t *d, size_t n);
int util_hex_val(char c);
bool util_parse_hex(const char *s, int len, uint32_t *out);
int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want);
void util_format_color(char *m, size_t n);
/* PC 側 8bit(0-255,0x80 中立) → 12bit 2軸3B。BT/USB 共通の実機形式。
 * y は上下が逆なので 12bit 化してから反転する。 */
void util_pack_stick_12bit(uint8_t x8, uint8_t y8, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
