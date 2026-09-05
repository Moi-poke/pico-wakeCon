#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_util.h"

int main(void) {
    /* 1. secure_zero */
    {
        uint8_t b[32];
        memset(b, 0xA5, sizeof(b));
        switch2_wake_secure_zero(b, 16);
        {
            int i;
            for (i = 0; i < 16; i++) { assert(b[i] == 0u); }
            for (i = 16; i < 32; i++) { assert(b[i] == 0xA5u); }
        }
        memset(b, 0xA5, sizeof(b));
        switch2_wake_secure_zero(b, 0);
        {
            int i;
            for (i = 0; i < 32; i++) { assert(b[i] == 0xA5u); }
        }
    }
    /* 2. is_all_zero */
    {
        uint8_t b[8];
        memset(b, 0, sizeof(b));
        assert(switch2_wake_is_all_zero(b, sizeof(b)) == true);
        b[3] = 1u;
        assert(switch2_wake_is_all_zero(b, sizeof(b)) == false);
        assert(switch2_wake_is_all_zero(b, 0) == true);
    }
    /* 3. reverse16 */
    {
        uint8_t in[16];
        uint8_t out[16];
        int i;
        for (i = 0; i < 16; i++) { in[i] = (uint8_t)i; }
        switch2_wake_reverse16(in, out);
        for (i = 0; i < 16; i++) { assert(out[i] == (uint8_t)(15 - i)); }
    }
    /* 4. hex_value */
    {
        assert(switch2_wake_hex_value('0') == 0);
        assert(switch2_wake_hex_value('9') == 9);
        assert(switch2_wake_hex_value('a') == 10);
        assert(switch2_wake_hex_value('f') == 15);
        assert(switch2_wake_hex_value('A') == 10);
        assert(switch2_wake_hex_value('F') == 15);
        assert(switch2_wake_hex_value('g') == -1);
        assert(switch2_wake_hex_value(' ') == -1);
        assert(switch2_wake_hex_value('\0') == -1);
    }
    /* 5. parse_hex */
    {
        uint8_t out[3];
        memset(out, 0, sizeof(out));
        assert(switch2_wake_parse_hex("001122", out, 3) == true);
        assert(out[0] == 0x00u && out[1] == 0x11u && out[2] == 0x22u);
        assert(switch2_wake_parse_hex("abc", out, 2) == false);
        assert(switch2_wake_parse_hex("zz", out, 1) == false);
        assert(switch2_wake_parse_hex(NULL, out, 1) == false);
        assert(switch2_wake_parse_hex("00", NULL, 1) == false);
    }
    printf("util ok\n");
    return 0;
}
