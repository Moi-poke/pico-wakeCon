/* src/ui_line.c: 日本語コメントを残す。Pico・BTstack 非依存を保つ。 */
#include "ui_line.h"

void ui_line_reset(ui_line_acc_t *st)
{
    if (st == NULL) {
        return;
    }
    st->len = 0;
}

int ui_line_feed(ui_line_acc_t *st, char c)
{
    if (st == NULL) {
        return UI_LINE_INCOMPLETE;
    }
    if (c == '\n' || c == '\r') {
        if (st->len <= 0) {
            return UI_LINE_INCOMPLETE;
        }
        st->buf[st->len] = '\0';
        return UI_LINE_READY;
    }
    if (st->len >= UI_LINE_MAX - 1) {
        st->len = 0;
        return UI_LINE_OVERFLOW;
    }
    st->buf[st->len++] = c;
    return UI_LINE_INCOMPLETE;
}

int ui_w_allowed(ui_src_t src)
{
    return src == UI_SRC_UART ? 1 : 0;
}
