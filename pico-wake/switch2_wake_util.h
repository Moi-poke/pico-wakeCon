#ifndef SWITCH2_WAKE_UTIL_H
#define SWITCH2_WAKE_UTIL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void switch2_wake_secure_zero(void *ptr, size_t len);
bool switch2_wake_is_all_zero(const uint8_t *data, size_t len);
void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16]);
int switch2_wake_hex_value(char ch);
bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
