#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_input.h"

static void check_parse(const char *line, bool expect_ok,
                        switch2_wake_input_kind_t expect_kind) {
    switch2_wake_input_command_t cmd;
    bool ok;
    memset(&cmd, 0xA5, sizeof(cmd));
    ok = switch2_wake_parse_line(line, &cmd);
    assert(ok == expect_ok);
    if (expect_ok) {
        assert(cmd.kind == expect_kind);
    }
}

/* Drive 6 identical samples; return transition event if any
   (fires on 5th stable sample, 6th returns NONE). */
static switch2_wake_button_event_t drive(switch2_wake_button_t *btn,
                                         bool pressed, uint32_t now_ms) {
    switch2_wake_button_event_t seen = WAKE_BUTTON_NONE;
    switch2_wake_button_event_t ev;
    int i;
    for (i = 0; i < 6; i++) {
        ev = switch2_wake_button_sample(btn, pressed, now_ms);
        if (ev != WAKE_BUTTON_NONE) {
            seen = ev;
        }
    }
    return seen;
}

int main(void) {
    /* parse_line table */
    check_parse("W", true, WAKE_INPUT_WAKE);
    check_parse("P", true, WAKE_INPUT_PAIR);
    check_parse("S", true, WAKE_INPUT_STATUS);
    check_parse("L", true, WAKE_INPUT_CAP_LIST);
    check_parse("B", true, WAKE_INPUT_BEACON);
    check_parse("D", true, WAKE_INPUT_DEBUG);
    check_parse("X", true, WAKE_INPUT_TEST_CLEAR);

    {
        switch2_wake_input_command_t cmd;
        assert(switch2_wake_parse_line("C", &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_CAPTURE);
        assert(cmd.capture_seconds == 15u);
        assert(switch2_wake_parse_line("C 30", &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_CAPTURE);
        assert(cmd.capture_seconds == 30u);
    }
    check_parse("C 0", false, WAKE_INPUT_INVALID);
    check_parse("C 61", false, WAKE_INPUT_INVALID);
    check_parse("C x", false, WAKE_INPUT_INVALID);

    {
        switch2_wake_input_command_t cmd;
        const uint8_t expect[6] = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u};
        assert(switch2_wake_parse_line("T 010203040506", &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_TEST_PEER);
        assert(memcmp(cmd.peer_identity, expect, 6) == 0);
    }
    check_parse("T zz", false, WAKE_INPUT_INVALID);

    {
        switch2_wake_input_command_t cmd;
        assert(switch2_wake_parse_line("V 0", &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_ADV_ENABLE);
        assert(cmd.adv_enabled == false);
        assert(switch2_wake_parse_line("V 1", &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_ADV_ENABLE);
        assert(cmd.adv_enabled == true);
    }
    check_parse("V 2", false, WAKE_INPUT_INVALID);

    check_parse("K ARM", true, WAKE_INPUT_FORGET_ARM);
    check_parse("K CONFIRM", true, WAKE_INPUT_FORGET_CONFIRM);
    check_parse("K x", false, WAKE_INPUT_INVALID);

    {
        switch2_wake_input_command_t cmd;
        assert(switch2_wake_parse_line(
                   "I 010203040506 1 AABBCCDDEEFF 00112233445566778899AABBCCDDEEFF -",
                   &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_IMPORT_BOND);
        assert(cmd.peer_type == 1u);
        assert(cmd.irk_present == false);
        assert(switch2_wake_parse_line(
                   "I 010203040506 1 AABBCCDDEEFF 00112233445566778899AABBCCDDEEFF 00112233445566778899AABBCCDDEEFF",
                   &cmd) == true);
        assert(cmd.kind == WAKE_INPUT_IMPORT_BOND);
        assert(cmd.irk_present == true);
    }
    check_parse("I short", false, WAKE_INPUT_INVALID);
    check_parse("Z", false, WAKE_INPUT_INVALID);

    /* uart_feed */
    {
        switch2_wake_uart_parser_t p;
        switch2_wake_input_command_t cmd;
        bool r;
        switch2_wake_uart_parser_init(&p);
        memset(&cmd, 0, sizeof(cmd));
        r = switch2_wake_uart_feed(&p, 'W', &cmd);
        assert(r == false);
        r = switch2_wake_uart_feed(&p, '\n', &cmd);
        assert(r == true);
        assert(cmd.kind == WAKE_INPUT_WAKE);
        assert(p.accepted == 1u);
    }
    {
        switch2_wake_uart_parser_t p;
        switch2_wake_input_command_t cmd;
        bool r;
        switch2_wake_uart_parser_init(&p);
        memset(&cmd, 0, sizeof(cmd));
        r = switch2_wake_uart_feed(&p, 'A', &cmd);
        assert(r == false);
        r = switch2_wake_uart_feed(&p, '\n', &cmd);
        /* feed returns true when a line completes regardless of parse ok */
        assert(r == true);
        assert(p.rejected == 1u);
        assert(p.accepted == 0u);
    }
    {
        switch2_wake_uart_parser_t p;
        switch2_wake_input_command_t cmd;
        switch2_wake_uart_parser_init(&p);
        memset(&cmd, 0, sizeof(cmd));
        assert(switch2_wake_uart_feed(&p, '\r', &cmd) == false);
    }
    {
        switch2_wake_uart_parser_t p;
        switch2_wake_input_command_t cmd;
        int i;
        bool r;
        switch2_wake_uart_parser_init(&p);
        memset(&cmd, 0, sizeof(cmd));
        for (i = 0; i < 200; i++) {
            r = switch2_wake_uart_feed(&p, 'A', &cmd);
            assert(r == false);
        }
        assert(p.dropping == true);
        assert(p.overflow == 1u);
        r = switch2_wake_uart_feed(&p, '\n', &cmd);
        assert(r == false);
        assert(p.rejected == 1u);
    }

    /* button */
    {
        switch2_wake_button_t b;
        switch2_wake_button_event_t ev;
        switch2_wake_button_init(&b);
        /* NOTE: brief expects 200ms -> PAIR, but source says
           PAIR_MIN=2000ms, so 200ms is WAKE. Follow source. */
        assert(switch2_wake_button_sample(&b, false, 0u) == WAKE_BUTTON_NONE);
        assert(drive(&b, true, 100u) == WAKE_BUTTON_NONE);
        ev = drive(&b, false, 300u);
        assert(ev == WAKE_BUTTON_WAKE);
    }
    {
        switch2_wake_button_t b;
        switch2_wake_button_event_t ev;
        switch2_wake_button_init(&b);
        assert(switch2_wake_button_sample(&b, false, 0u) == WAKE_BUTTON_NONE);
        assert(drive(&b, true, 1000u) == WAKE_BUTTON_NONE);
        ev = drive(&b, false, 1100u);
        assert(ev == WAKE_BUTTON_WAKE);
    }
    {
        switch2_wake_button_t b;
        switch2_wake_button_event_t ev;
        switch2_wake_button_init(&b);
        assert(switch2_wake_button_sample(&b, false, 0u) == WAKE_BUTTON_NONE);
        assert(drive(&b, true, 5000u) == WAKE_BUTTON_NONE);
        ev = drive(&b, false, 7200u);
        assert(ev == WAKE_BUTTON_PAIR);
    }
    {
        switch2_wake_button_t b;
        switch2_wake_button_event_t ev;
        switch2_wake_button_init(&b);
        assert(switch2_wake_button_sample(&b, false, 0u) == WAKE_BUTTON_NONE);
        assert(drive(&b, true, 20000u) == WAKE_BUTTON_NONE);
        ev = drive(&b, false, 31000u);
        assert(ev == WAKE_BUTTON_FORGET);
    }
    {
        switch2_wake_button_t b;
        switch2_wake_button_event_t ev;
        int i;
        switch2_wake_button_init(&b);
        assert(switch2_wake_button_sample(&b, false, 0u) == WAKE_BUTTON_NONE);
        /* 5ms blip: only 2 press samples (not yet stable), then release */
        for (i = 0; i < 2; i++) {
            assert(switch2_wake_button_sample(&b, true, 40000u) == WAKE_BUTTON_NONE);
        }
        ev = drive(&b, false, 40005u);
        assert(ev == WAKE_BUTTON_NONE);
    }

    printf("input ok\n");
    return 0;
}
