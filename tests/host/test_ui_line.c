/* tests/host/test_ui_line.c: test_cap.c と同形式。Pico/BTstack不要。 */
#include <stdio.h>
#include <string.h>
#include "ui_line.h"

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

int main(void)
{
    ui_line_acc_t st;
    int r;
    ui_line_reset(&st);
    CHECK(st.len == 0);
    /* "?\n" でREADY、内容と NUL 終端を確認 */
    r = ui_line_feed(&st, '?');
    CHECK(r == UI_LINE_INCOMPLETE);
    r = ui_line_feed(&st, '\n');
    CHECK(r == UI_LINE_READY);
    CHECK(st.len == 1 && strcmp(st.buf, "?") == 0);
    /* 空行の改行は INCOMPLETE のまま */
    ui_line_reset(&st);
    CHECK(ui_line_feed(&st, '\r') == UI_LINE_INCOMPLETE);
    CHECK(st.len == 0);
    /* 63 文字溜めて 64 文字目で OVERFLOW・破棄 */
    {
        int i;
        ui_line_reset(&st);
        for (i = 0; i < 63; i++) {
            CHECK(ui_line_feed(&st, 'A') == UI_LINE_INCOMPLETE);
        }
        CHECK(st.len == 63);
        CHECK(ui_line_feed(&st, 'B') == UI_LINE_OVERFLOW);
        CHECK(st.len == 0);
    }
    /* OVERFLOW 後に再蓄積できる */
    r = ui_line_feed(&st, 'P');
    CHECK(r == UI_LINE_INCOMPLETE);
    CHECK(ui_line_feed(&st, '\n') == UI_LINE_READY);
    CHECK(strcmp(st.buf, "P") == 0);
    /* NULL は INCOMPLETE */
    CHECK(ui_line_feed(NULL, 'x') == UI_LINE_INCOMPLETE);
    /* NULL reset は無害 (落ちなければよい) */
    ui_line_reset(NULL);
    /* 内容ありの '\r' は '\n' と同じく行確定 */
    ui_line_reset(&st);
    CHECK(ui_line_feed(&st, 'A') == UI_LINE_INCOMPLETE);
    CHECK(ui_line_feed(&st, '\r') == UI_LINE_READY);
    CHECK(strcmp(st.buf, "A") == 0);
    /* W ゲート述語 */
    CHECK(ui_w_allowed(UI_SRC_UART) == 1);
    CHECK(ui_w_allowed(UI_SRC_CDC) == 0);
    if (fails == 0) { printf("OK uiline\n"); }
    return fails != 0;
}
