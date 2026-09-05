/* S:姿勢 N:解放 O:色 C:取込 L:一覧 B:再生 ?:状態 D/M:表示 K:鍵 P:疎通 W:有線/無線。
 * 1文字命令の誤認を避けるため、S 行の途中で区切る受付は持たない。
 * D（HCI 生ログ）と M（監視表示）は既定で off。 */

#include <stdio.h>
#include <string.h>

#include "pico/stdio.h"
#include "pico/error.h"
#include "pico/time.h"
#include "hardware/uart.h"
#include "cap.h"
#include "hid.h"
#include "link.h"
#include "spi.h"
#include "store.h"
#include "ui.h"
#include "usb_wired.h"
#include "util.h"

#define UART_ID uart0
#define LINE_MAX 64

bool probe_hci_verbose;
bool probe_monitor;

uint32_t probe_uart_rx_count;
uint32_t probe_s_line_ok;
uint32_t probe_s_line_ng;
uint32_t probe_n_line_ok;
uint32_t probe_line_over;

void probe_line(const char *text)
{
    uart_puts(UART_ID, text);
    uart_puts(UART_ID, "\r\n");
    printf("%s\r\n", text);
}

static char line_buf[LINE_MAX];
static int line_len;

static int read_one_char(void)
{
    if (uart_is_readable(UART_ID)) {
        return (int)(unsigned char)uart_getc(UART_ID);
    }
    {
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            return ch;
        }
    }
    return -1;
}

static void cmd_s(void)
{
    if (probe_parse_s_line(line_buf, line_len)) {
        probe_s_line_ok++;
        probe_request_send();
    } else {
        probe_s_line_ng++;
    }
}

static void cmd_n(void)
{
    probe_input_reset();
    probe_n_line_ok++;
    probe_request_send();
}

static void cmd_o(void)
{
    /* 色。C とは別の命令: O <本体> <ボタン> <左> <右>。
     * 先頭1字は読飛ばす。応答は4色すべて返す。
     * PC 側は "color " 接頭で成功を判定する。 */
    char m[112];
    if (probe_parse_color_line(line_buf, line_len)) {
        store_color();
        util_format_color(m, sizeof(m));
        probe_line(m);
    } else {
        probe_line("usage: O <body> <btn> <left> <right> (hex)");
    }
}

static void cmd_c(void)
{
    /* C [秒]。取込。 */
    unsigned long seconds = 15u;
    if (line_len > 1) {
        int p = 1;
        seconds = 0u;
        while (p < line_len && line_buf[p] == ' ') {
            p++;
        }
        if (p >= line_len) {
            probe_s_line_ng++;
            return;
        }
        while (p < line_len && line_buf[p] >= '0' &&
               line_buf[p] <= '9') {
            seconds = seconds * 10u +
                (unsigned long)(line_buf[p] - '0');
            p++;
        }
        if (p != line_len || seconds < 1u || seconds > 60u) {
            probe_s_line_ng++;
            return;
        }
    }
    if (link_cap_start((uint8_t)seconds)) {
        /* 開始行は link 側が出す。 */
    }
}

static void cmd_l(void)
{
    char m[112];
    uint8_t i;
    uint8_t n = link_cap_used();
    for (i = 0u; i < n; i++) {
        const cap_entry_t *e = link_cap_entry(i);
        if (e == NULL) {
            continue;
        }
        snprintf(m, sizeof(m),
                 "list %u mac=%02x%02x%02x%02x%02x%02x pid=%04x wake=%u rssi=%d n=%lu",
                 (unsigned)i,
                 e->addr[0], e->addr[1], e->addr[2],
                 e->addr[3], e->addr[4], e->addr[5],
                 (unsigned)e->wake.pid, e->has_wake ? 1u : 0u,
                 e->rssi, (unsigned long)e->sightings);
        probe_line(m);
    }
    if (probe_cap_valid) {
        snprintf(m, sizeof(m),
                 "saved spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                 probe_cap_saved.spoof[0], probe_cap_saved.spoof[1],
                 probe_cap_saved.spoof[2], probe_cap_saved.spoof[3],
                 probe_cap_saved.spoof[4], probe_cap_saved.spoof[5],
                 probe_cap_saved.switch_mac[0],
                 probe_cap_saved.switch_mac[1],
                 probe_cap_saved.switch_mac[2],
                 probe_cap_saved.switch_mac[3],
                 probe_cap_saved.switch_mac[4],
                 probe_cap_saved.switch_mac[5]);
    } else {
        snprintf(m, sizeof(m), "saved none");
    }
    probe_line(m);
}

static void cmd_b(void)
{
    link_beacon_start();
}

static void cmd_x(void)
{
    if (probe_scanning || probe_beacon) {
        probe_line("X ERR BUSY");
        return;
    }
    link_cap_clear();
}

static void cmd_status(void)
{
    probe_show_status();
}

static void cmd_d(void)
{
    probe_hci_verbose = !probe_hci_verbose;
    probe_line(probe_hci_verbose ? "hci verbose on" : "hci verbose off");
}

static void cmd_m(void)
{
    probe_monitor = !probe_monitor;
    probe_line(probe_monitor ? "monitor on" : "monitor off");
}

static void cmd_k(void)
{
    gap_delete_all_link_keys();
    probe_line("link keys deleted. unpair on Switch too");
}

static void cmd_p(void)
{
    probe_line("PONG");
}

static void report_usb_line(void)
{
    /* 有線/USB の状態行。st 行の書式は触らず別行に分離する。
     * 前半 "usb en= cfg= hs=" の書式は変えない (後方に診断計数を足すのみ)。 */
    char m[192];
    usb_wired_stats_t uws;
    usb_wired_get_stats(&uws);
    snprintf(m, sizeof(m), "usb en=%u cfg=%u hs=%u mnt=%lu umnt=%lu rx80=%lu last=%02x tx81=%lu tx21=%lu in30=%lu sof=%lu sus=%lu rsm=%lu ep=%lu ct=%lu h=%02x%02x%02x%02x h1=%02x%02x%02x%02x",
             usb_wired_is_enabled() ? 1u : 0u,
             usb_wired_is_configured() ? 1u : 0u,
             usb_wired_handshake_done() ? 1u : 0u,
             (unsigned long)uws.mount, (unsigned long)uws.unmount,
             (unsigned long)uws.rx80, uws.last80,
             (unsigned long)uws.tx81, (unsigned long)uws.tx21,
             (unsigned long)uws.in30,
             (unsigned long)uws.sof, (unsigned long)uws.susp,
             (unsigned long)uws.resm,
             (unsigned long)uws.ep_rx, (unsigned long)uws.ctl_rx,
             uws.hist[0], uws.hist[1], uws.hist[2], uws.hist[3],
             uws.hist01[0], uws.hist01[1], uws.hist01[2],
             uws.hist01[3]);
    probe_line(m);
}

static void cmd_w(void)
{
    /* W[ 0|1]: 0=無線/BT、1=有線/USB。引数なしは現在値表示。
     * 数字の解釈は cmd_c のパターン踏襲。不正・範囲外は usage 行のみで状態不変。 */
    unsigned long v = 0u;
    int digits = 0;
    int p;
    if (line_len <= 1) {
        report_usb_line();
        return;
    }
    p = 1;
    while (p < line_len && line_buf[p] == ' ') {
        p++;
    }
    while (p < line_len && line_buf[p] >= '0' &&
           line_buf[p] <= '9') {
        v = v * 10u + (unsigned long)(line_buf[p] - '0');
        p++;
        digits++;
    }
    if (digits == 0 || p != line_len || v > 1u) {
        probe_line("usage: W [0|1]");
        return;
    }
    /* Classic 側の始末 (接続中なら能動切断＋待ち受け停止) も link 側で行う。
     * 発信抑止だけでは Switch からの呼び直しを受けて再接続するため。
     * モードは Flash に残し、次回起動時に復元する (起動時から電波OFF)。 */
    usb_wired_set_enabled(v == 1u);
    link_apply_wired_mode(v == 1u);
    store_wired(v == 1u);
    report_usb_line();
}

typedef void (*ui_cmd_fn)(void);
typedef struct {
    char c;
    ui_cmd_fn fn;
} ui_cmd_t;

static const ui_cmd_t UI_CMDS[] = {
    { 'S', cmd_s },
    { 'N', cmd_n },
    { 'O', cmd_o },
    { 'C', cmd_c },
    { 'L', cmd_l },
    { 'B', cmd_b },
    { 'X', cmd_x },
    { '?', cmd_status },
    { 'D', cmd_d },
    { 'M', cmd_m },
    { 'K', cmd_k },
    { 'P', cmd_p },
    { 'p', cmd_p },
    { 'W', cmd_w },
};

static bool ui_cmd_is_known(char c)
{
    unsigned i;
    for (i = 0u; i < sizeof(UI_CMDS) / sizeof(UI_CMDS[0]); i++) {
        if (UI_CMDS[i].c == c) {
            return true;
        }
    }
    return false;
}

static void handle_line(void)
{
    char c0;
    unsigned i;
    if (line_len <= 0) {
        return;
    }
    /* 不正行でも線は生きている。番犬の時刻は進める。 */
    probe_watchdog_feed(to_ms_since_boot(get_absolute_time()));
    line_buf[line_len] = '\0';
    c0 = line_buf[0];
    for (i = 0u; i < sizeof(UI_CMDS) / sizeof(UI_CMDS[0]); i++) {
        if (UI_CMDS[i].c == c0) {
            UI_CMDS[i].fn();
            return;
        }
    }
    probe_s_line_ng++;
}

void probe_uart_task(void)
{
    int ci;
    uint32_t now;
    while ((ci = read_one_char()) >= 0) {
        char c = (char)ci;
        probe_uart_rx_count++;
        if (c == '\n' || c == '\r') {
            handle_line();
            line_len = 0;
            continue;
        }
        if (line_len >= LINE_MAX - 1) {
            probe_line_over++;
            line_len = 0;
            continue;
        }
        if (line_len == 0 && !ui_cmd_is_known(c)) {
            continue;
        }
        line_buf[line_len++] = c;
    }
    now = to_ms_since_boot(get_absolute_time());
    link_poll(now);
    probe_watchdog_poll(now);
}

void probe_show_status(void)
{
    char m[128];
    snprintf(m, sizeof(m),
             "st host=%u cid=%u full=%u keys=%d saved=%u scan=%u bcn=%u",
             probe_host_known ? 1u : 0u,
             (unsigned)probe_hid_cid, probe_full_mode ? 1u : 0u,
             link_key_count(), probe_cap_valid ? 1u : 0u,
             probe_scanning ? 1u : 0u, probe_beacon ? 1u : 0u);
    probe_line(m);
    /* 現在の本体色。O で変えた内容が残っているかここで確かめられる。
     * 形式は O の応答と同じ "color " 接頭にする。 */
    util_format_color(m, sizeof(m));
    probe_line(m);
    if (probe_cap_valid) {
        snprintf(m, sizeof(m),
                 "saved spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                 probe_cap_saved.spoof[0], probe_cap_saved.spoof[1],
                 probe_cap_saved.spoof[2], probe_cap_saved.spoof[3],
                 probe_cap_saved.spoof[4], probe_cap_saved.spoof[5],
                 probe_cap_saved.switch_mac[0],
                 probe_cap_saved.switch_mac[1],
                 probe_cap_saved.switch_mac[2],
                 probe_cap_saved.switch_mac[3],
                 probe_cap_saved.switch_mac[4],
                 probe_cap_saved.switch_mac[5]);
        probe_line(m);
    }
    report_usb_line();
}

void probe_heartbeat_handler(btstack_timer_source_t *ts)
{
    char msg[144];
    probe_uart_task();
    if (probe_reconnect_pending) {
        probe_reconnect_pending = false;
        snprintf(msg, sizeof(msg), "reconnect try=%lu rc=0x%02x",
                 (unsigned long)probe_reconnect_tries,
                 probe_reconnect_report);
        probe_line(msg);
    }
    if (probe_monitor) {
        snprintf(msg, sizeof(msg),
                 "mon btn=%02x%02x%02x cid=%u full=%u out=%lu kinds=%u st30=%lu",
                 probe_btn[0], probe_btn[1], probe_btn[2],
                 (unsigned)probe_hid_cid, (unsigned)probe_full_mode,
                 (unsigned long)probe_out_report_count,
                 probe_subcmd_seen_n,
                 (unsigned long)probe_state_sent);
        probe_line(msg);
        snprintf(msg, sizeof(msg),
                 "mon empty=%lu reply=%lu press=%lu ok=%lu ng=%lu ov=%lu",
                 (unsigned long)probe_empty_sent,
                 (unsigned long)probe_reply_sent,
                 (unsigned long)probe_btn_press_count,
                 (unsigned long)probe_s_line_ok,
                 (unsigned long)probe_s_line_ng,
                 (unsigned long)probe_line_over);
        probe_line(msg);
    }
    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}
