#ifndef WAKECON_UI_LINE_H
#define WAKECON_UI_LINE_H

/* 発信元タグ付き行蓄積。Pico・BTstack 非依存。ホストテスト可能に保つ。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_LINE_MAX 64
#define UI_LINE_INCOMPLETE 0
#define UI_LINE_READY 1
#define UI_LINE_OVERFLOW 2

typedef enum {
    UI_SRC_UART = 0,
    UI_SRC_CDC = 1
} ui_src_t;

typedef struct {
    char buf[UI_LINE_MAX];
    int len;
} ui_line_acc_t;

void ui_line_reset(ui_line_acc_t *st);
int ui_line_feed(ui_line_acc_t *st, char c);
int ui_w_allowed(ui_src_t src);

#ifdef __cplusplus
}
#endif

#endif
