/* S:姿勢 N:解放 O:色 C:取込 L:一覧 B:再生 ?:状態 D/M:表示 K:鍵 P:疎通。
 * S 行内文字の誤認を避けるため1文字口は持たない。既定は landmark のみ。 */

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

static void handle_line(void)
{
    char c0;
    char m[112];
    if (line_len <= 0) {
        return;
    }
    /* 不正行でも線は生きている。番犬の時刻は進める。 */
    probe_watchdog_feed(to_ms_since_boot(get_absolute_time()));
    line_buf[line_len] = '\0';
    c0 = line_buf[0];
    if (c0 == 'S') {
        if (probe_parse_s_line(line_buf, line_len)) {
            probe_s_line_ok++;
            probe_request_send();
        } else {
            probe_s_line_ng++;
        }
        return;
    }
    if (c0 == 'N') {
        probe_input_reset();
        probe_n_line_ok++;
        probe_request_send();
        return;
    }
    if (c0 == 'O') {
        /* 色。C とは別の命令: O <本体> <ボタン> <左> <右>。
         * 先頭1字は読飛ばす。応答は4色すべて返す。
         * PC 側は "color " 接頭で成功を判定する。 */
        if (probe_parse_color_line(line_buf, line_len)) {
            store_color();
            snprintf(m, sizeof(m),
                     "color body=%02x%02x%02x btn=%02x%02x%02x"
                     " left=%02x%02x%02x right=%02x%02x%02x",
                     spi_color_6050[0], spi_color_6050[1], spi_color_6050[2],
                     spi_color_6050[3], spi_color_6050[4], spi_color_6050[5],
                     spi_color_6050[6], spi_color_6050[7], spi_color_6050[8],
                     spi_color_6050[9], spi_color_6050[10], spi_color_6050[11]);
            probe_line(m);
        } else {
            probe_line("usage: O <body> <btn> <left> <right> (hex)");
        }
        return;
    }
    if (c0 == 'C') {
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
        return;
    }
    if (c0 == 'L') {
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
        return;
    }
    if (c0 == 'B') {
        link_beacon_start();
        return;
    }
    if (c0 == 'X') {
        if (probe_scanning || probe_beacon) {
            probe_line("X ERR BUSY");
            return;
        }
        link_cap_clear();
        return;
    }
    if (c0 == '?') {
        probe_show_status();
        return;
    }
    if (c0 == 'D') {
        probe_hci_verbose = !probe_hci_verbose;
        probe_line(probe_hci_verbose ? "hci verbose on" : "hci verbose off");
        return;
    }
    if (c0 == 'M') {
        probe_monitor = !probe_monitor;
        probe_line(probe_monitor ? "monitor on" : "monitor off");
        return;
    }
    if (c0 == 'K') {
        gap_delete_all_link_keys();
        probe_line("link keys deleted. unpair on Switch too");
        return;
    }
    if (c0 == 'P' || c0 == 'p') {
        probe_line("PONG");
        return;
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
        if (line_len == 0 && c != 'S' && c != 'N' && c != 'O' &&
            c != 'C' && c != 'L' && c != 'B' && c != '?' && c != 'X' &&
            c != 'D' && c != 'M' && c != 'K' &&
            c != 'P' && c != 'p') {
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
    snprintf(m, sizeof(m),
             "color body=%02x%02x%02x btn=%02x%02x%02x"
             " left=%02x%02x%02x right=%02x%02x%02x",
             spi_color_6050[0], spi_color_6050[1], spi_color_6050[2],
             spi_color_6050[3], spi_color_6050[4], spi_color_6050[5],
             spi_color_6050[6], spi_color_6050[7], spi_color_6050[8],
             spi_color_6050[9], spi_color_6050[10], spi_color_6050[11]);
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
