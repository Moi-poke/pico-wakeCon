#include "switch2_wake_input.h"
#include "switch2_wake_util.h"

#include <string.h>

#define BUTTON_STABLE_SAMPLES 5u
#define BUTTON_WAKE_MIN_MS 50u
#define BUTTON_PAIR_MIN_MS 2000u
#define BUTTON_FORGET_MIN_MS 10000u

void switch2_wake_input_command_clear(switch2_wake_input_command_t *command) {
    if (command != NULL) switch2_wake_secure_zero(command, sizeof(*command));
}

static size_t split_tokens(char *text, char *tokens[], size_t max_tokens) {
    size_t count = 0u;
    char *p = text;
    while (*p != '\0') {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        if (count == max_tokens) return max_tokens + 1u;
        tokens[count++] = p;
        while (*p != '\0' && *p != ' ') ++p;
        if (*p != '\0') *p++ = '\0';
    }
    return count;
}

static bool parse_peer_type(const char *text, uint8_t *value) {
    if (strcmp(text, "0") == 0 || strcmp(text, "public") == 0) {
        *value = 0u;
        return true;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "random") == 0) {
        *value = 1u;
        return true;
    }
    return false;
}

bool switch2_wake_parse_line(const char *line,
                             switch2_wake_input_command_t *command) {
    char copy[SWITCH2_WAKE_INPUT_LINE_MAX];
    char *tokens[7];
    size_t count;
    if (line == NULL || command == NULL || strlen(line) >= sizeof(copy)) return false;
    switch2_wake_input_command_clear(command);
    memcpy(copy, line, strlen(line) + 1u);
    count = split_tokens(copy, tokens, 7u);
    if (count == 1u && strcmp(tokens[0], "W") == 0) command->kind = WAKE_INPUT_WAKE;
    else if (count == 1u && strcmp(tokens[0], "P") == 0) command->kind = WAKE_INPUT_PAIR;
    else if (count == 1u && strcmp(tokens[0], "S") == 0) command->kind = WAKE_INPUT_STATUS;
    else if (count == 1u && strcmp(tokens[0], "C") == 0) {
        command->kind = WAKE_INPUT_CAPTURE;
        command->capture_seconds = 15u;
    } else if (count == 2u && strcmp(tokens[0], "C") == 0) {
        unsigned long seconds = 0u;
        size_t k = 0u;
        while (tokens[1][k] != '\0') {
            if (tokens[1][k] < '0' || tokens[1][k] > '9') {
                break;
            }
            seconds = seconds * 10u + (unsigned long)(tokens[1][k] - '0');
            k++;
        }
        if (tokens[1][k] != '\0' || seconds < 1u || seconds > 60u) {
            switch2_wake_input_command_clear(command);
            command->kind = WAKE_INPUT_INVALID;
            switch2_wake_secure_zero(copy, sizeof(copy));
            return false;
        }
        command->kind = WAKE_INPUT_CAPTURE;
        command->capture_seconds = (uint8_t)seconds;
    } else if (count == 1u && strcmp(tokens[0], "L") == 0) {
        command->kind = WAKE_INPUT_CAP_LIST;
    } else if (count == 1u && strcmp(tokens[0], "B") == 0) {
        command->kind = WAKE_INPUT_BEACON;
    } else if (count == 1u && strcmp(tokens[0], "D") == 0) {
        command->kind = WAKE_INPUT_DEBUG;
    } else if (count == 1u && strcmp(tokens[0], "X") == 0) {
        command->kind = WAKE_INPUT_TEST_CLEAR;
    } else if (count == 2u && strcmp(tokens[0], "T") == 0 &&
             switch2_wake_parse_hex(tokens[1], command->peer_identity, 6u)) command->kind = WAKE_INPUT_TEST_PEER;
    else if (count == 2u && strcmp(tokens[0], "V") == 0 &&
             (strcmp(tokens[1], "0") == 0 || strcmp(tokens[1], "1") == 0)) {
        command->kind = WAKE_INPUT_ADV_ENABLE;
        command->adv_enabled = tokens[1][0] == '1';
    } else if (count == 2u && strcmp(tokens[0], "K") == 0 &&
               strcmp(tokens[1], "ARM") == 0) command->kind = WAKE_INPUT_FORGET_ARM;
    else if (count == 2u && strcmp(tokens[0], "K") == 0 &&
               strcmp(tokens[1], "CONFIRM") == 0) command->kind = WAKE_INPUT_FORGET_CONFIRM;
    else if (count == 6u && strcmp(tokens[0], "I") == 0 &&
             switch2_wake_parse_hex(tokens[1], command->local_identity, 6u) &&
             parse_peer_type(tokens[2], &command->peer_type) &&
             switch2_wake_parse_hex(tokens[3], command->peer_identity, 6u) &&
             switch2_wake_parse_hex(tokens[4], command->ltk, 16u) &&
             (strcmp(tokens[5], "-") == 0 || switch2_wake_parse_hex(tokens[5], command->irk, 16u))) {
        command->kind = WAKE_INPUT_IMPORT_BOND;
        command->irk_present = strcmp(tokens[5], "-") != 0;
    } else {
        switch2_wake_input_command_clear(command);
        command->kind = WAKE_INPUT_INVALID;
        switch2_wake_secure_zero(copy, sizeof(copy));
        return false;
    }
    switch2_wake_secure_zero(copy, sizeof(copy));
    return true;
}

void switch2_wake_uart_parser_init(switch2_wake_uart_parser_t *parser) {
    if (parser != NULL) switch2_wake_secure_zero(parser, sizeof(*parser));
}

bool switch2_wake_uart_feed(switch2_wake_uart_parser_t *parser, char ch,
                            switch2_wake_input_command_t *command) {
    bool ok;
    if (parser == NULL || command == NULL) return false;
    if (ch == '\r') return false;
    if (ch != '\n') {
        /* 起動時や COM 開閉で混ざる制御文字は捨てる。混ざったまま
         * 先頭行へ付着すると初回入力が必ず NG になるため。 */
        if (ch < ' ' || ch > '~') return false;
        if (parser->dropping) return false;
        if (parser->length + 1u >= sizeof(parser->line)) {
            parser->dropping = true;
            parser->overflow++;
            return false;
        }
        parser->line[parser->length++] = ch;
        return false;
    }
    if (parser->dropping) {
        parser->dropping = false;
        parser->length = 0u;
        parser->rejected++;
        return false;
    }
    parser->line[parser->length] = '\0';
    if (parser->length == 0u) return false;
    ok = switch2_wake_parse_line(parser->line, command);
    switch2_wake_secure_zero(parser->line, sizeof(parser->line));
    parser->length = 0u;
    if (ok) parser->accepted++;
    else parser->rejected++;
    return true;
}

void switch2_wake_button_init(switch2_wake_button_t *button) {
    if (button != NULL) switch2_wake_secure_zero(button, sizeof(*button));
}

switch2_wake_button_event_t switch2_wake_button_sample(
    switch2_wake_button_t *button, bool pressed, uint32_t now_ms) {
    uint32_t duration;
    if (button == NULL) return WAKE_BUTTON_NONE;
    if (!button->initialized) {
        button->initialized = true;
        button->stable_pressed = pressed;
        button->candidate_pressed = pressed;
        button->stable_samples = BUTTON_STABLE_SAMPLES;
        if (pressed) button->pressed_at_ms = now_ms;
        return WAKE_BUTTON_NONE;
    }
    if (pressed != button->candidate_pressed) {
        button->candidate_pressed = pressed;
        button->stable_samples = 1u;
        return WAKE_BUTTON_NONE;
    }
    if (button->stable_samples < BUTTON_STABLE_SAMPLES) button->stable_samples++;
    if (button->stable_samples < BUTTON_STABLE_SAMPLES ||
        pressed == button->stable_pressed) return WAKE_BUTTON_NONE;
    button->stable_pressed = pressed;
    if (pressed) {
        button->pressed_at_ms = now_ms;
        return WAKE_BUTTON_NONE;
    }
    duration = now_ms - button->pressed_at_ms;
    if (duration >= BUTTON_FORGET_MIN_MS) return WAKE_BUTTON_FORGET;
    if (duration >= BUTTON_PAIR_MIN_MS) return WAKE_BUTTON_PAIR;
    if (duration >= BUTTON_WAKE_MIN_MS) return WAKE_BUTTON_WAKE;
    return WAKE_BUTTON_NONE;
}
