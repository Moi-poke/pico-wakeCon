#ifndef SWITCH2_WAKE_INPUT_H
#define SWITCH2_WAKE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SWITCH2_WAKE_INPUT_LINE_MAX 160u

typedef enum {
    WAKE_INPUT_NONE = 0,
    WAKE_INPUT_WAKE,
    WAKE_INPUT_PAIR,
    WAKE_INPUT_IMPORT_BOND,
    WAKE_INPUT_FORGET_ARM,
    WAKE_INPUT_FORGET_CONFIRM,
    WAKE_INPUT_STATUS,
    WAKE_INPUT_ADV_ENABLE,
    WAKE_INPUT_TEST_PEER,
    WAKE_INPUT_TEST_CLEAR,
    WAKE_INPUT_CAPTURE,
    WAKE_INPUT_CAP_LIST,
    WAKE_INPUT_BEACON,
    WAKE_INPUT_DEBUG,
    WAKE_INPUT_INVALID
} switch2_wake_input_kind_t;

typedef struct {
    switch2_wake_input_kind_t kind;
    uint8_t local_identity[6];
    uint8_t peer_type;
    uint8_t peer_identity[6];
    uint8_t ltk[16];
    uint8_t irk[16];
    bool irk_present;
    bool adv_enabled;
    uint8_t capture_seconds;
} switch2_wake_input_command_t;

typedef struct {
    char line[SWITCH2_WAKE_INPUT_LINE_MAX];
    size_t length;
    bool dropping;
    uint32_t accepted;
    uint32_t rejected;
    uint32_t overflow;
} switch2_wake_uart_parser_t;

typedef enum {
    WAKE_BUTTON_NONE = 0,
    WAKE_BUTTON_WAKE,
    WAKE_BUTTON_PAIR,
    WAKE_BUTTON_FORGET
} switch2_wake_button_event_t;

typedef struct {
    bool initialized;
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t stable_samples;
    uint32_t pressed_at_ms;
} switch2_wake_button_t;

void switch2_wake_uart_parser_init(switch2_wake_uart_parser_t *parser);
bool switch2_wake_uart_feed(switch2_wake_uart_parser_t *parser, char ch,
                            switch2_wake_input_command_t *command);
bool switch2_wake_parse_line(const char *line,
                             switch2_wake_input_command_t *command);
void switch2_wake_input_command_clear(switch2_wake_input_command_t *command);
void switch2_wake_button_init(switch2_wake_button_t *button);
switch2_wake_button_event_t switch2_wake_button_sample(
    switch2_wake_button_t *button, bool pressed, uint32_t now_ms);

#endif
