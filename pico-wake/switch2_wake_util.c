#include "switch2_wake_util.h"
#include <string.h>
void switch2_wake_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0u) { *p++ = 0u; }
}
bool switch2_wake_is_all_zero(const uint8_t *data, size_t len) {
    uint8_t acc = 0u; size_t i;
    for (i = 0u; i < len; i++) { acc |= data[i]; }
    return acc == 0u;
}
void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16]) {
    unsigned int i;
    for (i = 0u; i < 16u; i++) { out[i] = in[15u - i]; }
}
int switch2_wake_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}
bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size) {
    size_t i;
    if (text == NULL || out == NULL) { return false; }
    if (strlen(text) != size * 2u) { return false; }
    for (i = 0u; i < size; ++i) {
        int hi = switch2_wake_hex_value(text[i * 2u]);
        int lo = switch2_wake_hex_value(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) { return false; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}
