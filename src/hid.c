/* 入力状態・HID 応答・送信。Switch 1 Pro Controller 仕様。
 * 出典: dekuNukem/Nintendo_Switch_Reverse_Engineering /
 *   GP2040-CE SwitchProDriver / nxbt Example Pairing Session。
 * バイト値の意味は資料通り。でたらめ値は送らない。 */

#include <stdio.h>
#include <string.h>

#include "hid.h"
#include "spi.h"
#include "link.h"
#include "store.h"
#include "ui.h"

/* ---- 入力状態 ---- */
uint8_t probe_btn[3];
uint8_t probe_lx = 0x80, probe_ly = 0x80;
uint8_t probe_rx = 0x80, probe_ry = 0x80;
uint32_t probe_btn_press_count;
bool probe_btn_was_down;

/* 有線 USB 報告用の影。S 行の PC 側値をそのまま保つ。
 * BT 側（probe_btn 等）は変換後の値なので、有線報告には使えない。 */
uint16_t probe_pc_buttons;
uint8_t probe_pc_hat;
uint8_t probe_pc_lx = 0x80, probe_pc_ly = 0x80;
uint8_t probe_pc_rx = 0x80, probe_pc_ry = 0x80;

void probe_input_reset(void)
{
    probe_btn[0] = 0u;
    probe_btn[1] = 0u;
    probe_btn[2] = 0u;
    probe_lx = 0x80u;
    probe_ly = 0x80u;
    probe_rx = 0x80u;
    probe_ry = 0x80u;
    probe_btn_was_down = false;
    probe_pc_buttons = 0u;
    probe_pc_hat = 8u;
    probe_pc_lx = 0x80u;
    probe_pc_ly = 0x80u;
    probe_pc_rx = 0x80u;
    probe_pc_ry = 0x80u;
}

/* PC 側 8bit(0-255,0x80 中立) → BT 12bit 2軸3B。
 * y は上下が逆なので 12bit 化してから反転する。 */
void probe_pack_stick(uint8_t x8, uint8_t y8, uint8_t *out)
{
    uint16_t x = (uint16_t)x8 << 4;
    uint16_t y = (uint16_t)4096 - ((uint16_t)y8 << 4);
    if (y > 4095u) {
        y = 4095u;
    }
    out[0] = (uint8_t)(x & 0xFFu);
    out[1] = (uint8_t)(((x >> 8) & 0x0Fu) | ((y & 0x0Fu) << 4));
    out[2] = (uint8_t)((y >> 4) & 0xFFu);
}

/* ---- 送信状態 ---- */
uint16_t probe_hid_cid;
bool probe_full_mode;
uint8_t probe_report_timer;
uint32_t probe_empty_sent;
uint32_t probe_reply_sent;
uint32_t probe_state_sent;
uint32_t probe_out_report_count;
uint8_t probe_subcmd_seen[24];
uint8_t probe_subcmd_seen_n;
uint16_t probe_out_len_min = 0xFFFFu;
uint16_t probe_out_len_max;
uint8_t probe_player_id;
bool probe_imu_enabled;
bool probe_vibration_enabled;
uint8_t probe_input_mode = 0x3Fu;
uint8_t probe_hci_state_arg = 0xFFu;
uint32_t probe_hci_state_count;
bool probe_send_now_wanted;

#define REPLY_MAX 64u
static uint8_t reply_buf[REPLY_MAX];
static uint16_t reply_len;
uint32_t probe_color_set_count;

/* 200ms 無通信で中立化する。PC 断でも押しっぱなしを残さない最後の砦。
 * 既に中立なら何もしない。解除時は WD を1行返し PC が時刻を測れる。 */
#define WATCHDOG_MS 200u
static uint32_t watch_last_ms;
static bool watch_fed;

void probe_watchdog_feed(uint32_t now_ms)
{
    watch_last_ms = now_ms;
    watch_fed = true;
}

void probe_watchdog_poll(uint32_t now_ms)
{
    if (!watch_fed) {
        return;
    }
    if ((int32_t)(now_ms - watch_last_ms) < (int32_t)WATCHDOG_MS) {
        return;
    }
    if (probe_btn[0] == 0u && probe_btn[1] == 0u && probe_btn[2] == 0u &&
        probe_lx == 0x80u && probe_ly == 0x80u &&
        probe_rx == 0x80u && probe_ry == 0x80u) {
        return;
    }
    probe_input_reset();
    probe_request_send();
    probe_line("WD");
}

void probe_hid_reset(void)
{
    probe_out_report_count = 0u;
    probe_subcmd_seen_n = 0u;
    probe_out_len_min = 0xFFFFu;
    probe_out_len_max = 0u;
    probe_report_timer = 0u;
    probe_empty_sent = 0u;
    probe_reply_sent = 0u;
    reply_len = 0u;
    probe_full_mode = false;
    probe_state_sent = 0u;
    probe_player_id = 0u;
    probe_imu_enabled = false;
    probe_vibration_enabled = false;
    probe_input_mode = 0x3Fu;
    probe_hci_state_arg = 0xFFu;
    probe_btn_press_count = 0u;
    probe_input_reset();
}

void probe_request_send(void)
{
    if (probe_hid_cid != 0u && probe_full_mode) {
        hid_device_request_can_send_now_event(probe_hid_cid);
        probe_send_now_wanted = true;
    }
}

uint32_t probe_send_interval_ms(void)
{
    return probe_full_mode ? 7u : 100u;
}

/* 応答の共通部 16B を作り、中身の書込位置を返す。
 * [3] 以降は 0x30 と同じ「今の姿勢」を載せる（固定値にしない）。 */
uint16_t probe_build_reply(uint8_t ack, uint8_t subcmd)
{
    reply_buf[0] = 0xA1u;
    reply_buf[1] = 0x21u;
    reply_buf[2] = probe_report_timer++;
    reply_buf[3] = 0x80u;
    reply_buf[4] = probe_btn[0];
    reply_buf[5] = probe_btn[1];
    reply_buf[6] = probe_btn[2];
    probe_pack_stick(probe_lx, probe_ly, &reply_buf[7]);
    probe_pack_stick(probe_rx, probe_ry, &reply_buf[10]);
    reply_buf[13] = 0x08u;
    reply_buf[14] = ack;
    reply_buf[15] = subcmd;
    return 16u;
}

/* 送信 3 種の優先度: 応答 > 0x30(14B, IMU 無し) > 空(3B)。 */
void probe_can_send_now(void)
{
    if (reply_len > 0u) {
        hid_device_send_interrupt_message(probe_hid_cid, reply_buf,
                                          reply_len);
        probe_reply_sent++;
        reply_len = 0u;
    } else if (probe_full_mode) {
        uint8_t f[14];
        f[0] = 0xA1u;
        f[1] = 0x30u;
        f[2] = probe_report_timer++;
        f[3] = 0x80u;
        f[4] = probe_btn[0];
        f[5] = probe_btn[1];
        f[6] = probe_btn[2];
        probe_pack_stick(probe_lx, probe_ly, &f[7]);
        probe_pack_stick(probe_rx, probe_ry, &f[10]);
        f[13] = 0x08u;
        hid_device_send_interrupt_message(probe_hid_cid, f, sizeof(f));
        probe_state_sent++;
    } else {
        uint8_t f[3];
        /* ペア前は 100ms の空レポートで生かす（資料の手順通り）。 */
        f[0] = 0xA1u;
        f[1] = 0x00u;
        f[2] = probe_report_timer++;
        hid_device_send_interrupt_message(probe_hid_cid, f, sizeof(f));
        probe_empty_sent++;
    }
    probe_send_now_wanted = false;
}

/* ---- S/N/C 行の解釈 ---- */

/* PC 側 USB HID 並び (switch_controller_plus.h enum Button)。 */
#define PC_Y       0x0001u
#define PC_B       0x0002u
#define PC_A       0x0004u
#define PC_X       0x0008u
#define PC_L       0x0010u
#define PC_R       0x0020u
#define PC_ZL      0x0040u
#define PC_ZR      0x0080u
#define PC_MINUS   0x0100u
#define PC_PLUS    0x0200u
#define PC_LCLICK  0x0400u
#define PC_RCLICK  0x0800u
#define PC_HOME    0x1000u
#define PC_CAPTURE 0x2000u

static int parse_hex_one(const char *s, int len, uint32_t *out)
{
    uint32_t v = 0u;
    int k;
    if (len <= 0 || len > 8) {
        return 0;
    }
    for (k = 0; k < len; k++) {
        char c = s[k];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return 1;
}

static void pc_buttons_to_bt(uint16_t pc, uint8_t out[3])
{
    out[0] = 0u;
    out[1] = 0u;
    out[2] = 0u;
    if (pc & PC_Y) {
        out[0] |= 0x01u;
    }
    if (pc & PC_X) {
        out[0] |= 0x02u;
    }
    if (pc & PC_B) {
        out[0] |= 0x04u;
    }
    if (pc & PC_A) {
        out[0] |= 0x08u;
    }
    if (pc & PC_R) {
        out[0] |= 0x40u;
    }
    if (pc & PC_ZR) {
        out[0] |= 0x80u;
    }
    if (pc & PC_MINUS) {
        out[1] |= 0x01u;
    }
    if (pc & PC_PLUS) {
        out[1] |= 0x02u;
    }
    if (pc & PC_RCLICK) {
        out[1] |= 0x04u;
    }
    if (pc & PC_LCLICK) {
        out[1] |= 0x08u;
    }
    if (pc & PC_HOME) {
        out[1] |= 0x10u;
    }
    if (pc & PC_CAPTURE) {
        out[1] |= 0x20u;
    }
    if (pc & PC_L) {
        out[2] |= 0x40u;
    }
    if (pc & PC_ZL) {
        out[2] |= 0x80u;
    }
}

/* hat 0-7 → byte5 ビット。8 は中立。斜めは 2 ビット。 */
static uint8_t hat_to_bt(uint8_t hat)
{
    switch (hat) {
        case 0: return 0x02u;
        case 1: return 0x02u | 0x04u;
        case 2: return 0x04u;
        case 3: return 0x04u | 0x01u;
        case 4: return 0x01u;
        case 5: return 0x01u | 0x08u;
        case 6: return 0x08u;
        case 7: return 0x08u | 0x02u;
        default: return 0u;
    }
}

/* S <btn> <hat> <lx> <ly> <rx> <ry>。読めなければ何も変えない。 */
int probe_parse_s_line(const char *s, int len)
{
    uint32_t v[6];
    int idx = 0;
    int p = 1;
    while (idx < 6) {
        int start;
        while (p < len && s[p] == ' ') {
            p++;
        }
        start = p;
        while (p < len && s[p] != ' ') {
            p++;
        }
        if (p == start) {
            return 0;
        }
        if (!parse_hex_one(&s[start], p - start, &v[idx])) {
            return 0;
        }
        idx++;
    }
    pc_buttons_to_bt((uint16_t)v[0], probe_btn);
    probe_btn[2] |= hat_to_bt((uint8_t)v[1]);
    probe_lx = (uint8_t)v[2];
    probe_ly = (uint8_t)v[3];
    probe_rx = (uint8_t)v[4];
    probe_ry = (uint8_t)v[5];
    probe_pc_buttons = (uint16_t)v[0];
    probe_pc_hat = (uint8_t)v[1] > 8u ? 8u : (uint8_t)v[1];
    probe_pc_lx = (uint8_t)v[2];
    probe_pc_ly = (uint8_t)v[3];
    probe_pc_rx = (uint8_t)v[4];
    probe_pc_ry = (uint8_t)v[5];
    if ((probe_btn[0] | probe_btn[1] | probe_btn[2]) != 0u &&
        !probe_btn_was_down) {
        probe_btn_press_count++;
        probe_btn_was_down = true;
    } else if ((probe_btn[0] | probe_btn[1] | probe_btn[2]) == 0u) {
        probe_btn_was_down = false;
    }
    return 1;
}

/* O <本体> <ボタン> <左> <右>（各6桁16進）。欠ければ何も変えない。 */
int probe_parse_color_line(const char *s, int len)
{
    uint32_t v[4];
    int idx = 0;
    int p = 1;
    int k;
    while (idx < 4) {
        int start;
        while (p < len && s[p] == ' ') {
            p++;
        }
        start = p;
        while (p < len && s[p] != ' ') {
            p++;
        }
        if (p == start) {
            return 0;
        }
        if (!parse_hex_one(&s[start], p - start, &v[idx])) {
            return 0;
        }
        idx++;
    }
    for (k = 0; k < 4; k++) {
        spi_color_6050[k * 3 + 0] = (uint8_t)((v[k] >> 16) & 0xFFu);
        spi_color_6050[k * 3 + 1] = (uint8_t)((v[k] >> 8) & 0xFFu);
        spi_color_6050[k * 3 + 2] = (uint8_t)(v[k] & 0xFFu);
    }
    probe_color_set_count++;
    return 1;
}

/* ---- 出力レポート受信 ----
 * BTstack はレポート ID を外して渡す。sub=report[9]、addr=report[10..11]。
 * 宣言 48B より短い 0x01 を受けるため truncated 受理が前提。 */

static void note_subcmd(uint8_t sub)
{
    uint8_t k;
    for (k = 0u; k < probe_subcmd_seen_n; k++) {
        if (probe_subcmd_seen[k] == sub) {
            return;
        }
    }
    if (probe_subcmd_seen_n < (uint8_t)sizeof(probe_subcmd_seen)) {
        probe_subcmd_seen[probe_subcmd_seen_n++] = sub;
    }
}

/* ack 上位ニブルは中身の予告: 80 無し / 82 機器 / 83 トリガ / 90 SPI /
 * 81 ペア / B0 灯 / C0 IMU / D0 電圧。実物(GP2040)通り。 */
static void answer_subcmd(uint8_t sub, const uint8_t *report, int report_size)
{
    char msg[96];
    uint16_t p;
    int k;
    switch (sub) {
        case 0x00:
            p = probe_build_reply(0x80u, 0x00u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x01:
            p = probe_build_reply(0x81u, 0x01u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x02:
            /* 機器情報: fw 03 8B(実測値) / 種別 03(Pro) / 自アドレス。 */
            p = probe_build_reply(0x82u, 0x02u);
            reply_buf[p++] = 0x03u;
            reply_buf[p++] = 0x8Bu;
            reply_buf[p++] = 0x03u;
            reply_buf[p++] = 0x02u;
            for (k = 0; k < 6; k++) {
                reply_buf[p++] = probe_addr[k];
            }
            reply_buf[p++] = 0x01u;
            reply_buf[p++] = 0x02u;  /* 0x601B と同義: グリップ色まで使う */
            reply_len = p;
            break;
        case 0x03:
            probe_full_mode = true;
            if (report_size > 10) {
                probe_input_mode = report[10];
            }
            probe_line("  0x30 mode start");
            p = probe_build_reply(0x80u, 0x03u);
            reply_buf[p++] = probe_input_mode;
            reply_len = p;
            break;
        case 0x04:
            /* トリガ経過: uint16×7=14B(未押下=0)。 */
            p = probe_build_reply(0x83u, 0x04u);
            memset(&reply_buf[p], 0, 14);
            reply_len = p + 14u;
            break;
        case 0x05:
            /* ホスト記憶の有無。覚えていれば 0x01。嘘はつかない。 */
            p = probe_build_reply(0x80u, 0x05u);
            reply_buf[p++] = probe_host_known ? 0x01u : 0x00u;
            reply_len = p;
            break;
        case 0x06: {
            /* 電源指示: 00 寝ろ / 01 再接続 / 02 ペア / 04 再接続(HOME)。 */
            const char *what;
            if (report_size > 10) {
                probe_hci_state_arg = report[10];
            }
            probe_hci_state_count++;
            if (probe_hci_state_arg == 0x00u) {
                what = "sleep";
            } else if (probe_hci_state_arg == 0x01u) {
                what = "reconnect";
            } else if (probe_hci_state_arg == 0x02u) {
                what = "pair";
            } else if (probe_hci_state_arg == 0x04u) {
                what = "reconnect(HOME)";
            } else {
                what = "unknown";
            }
            snprintf(msg, sizeof(msg), "  0x06 power=0x%02x (%s)",
                     (unsigned)probe_hci_state_arg, what);
            probe_line(msg);
            reply_len = probe_build_reply(0x80u, 0x06u);
            break;
        }
        case 0x10: {
            uint16_t addr;
            uint8_t want;
            const spi_entry_t *hit;
            if (report_size < 15) {
                reply_len = 0u;
                break;
            }
            addr = (uint16_t)report[10] | ((uint16_t)report[11] << 8);
            want = report[14];
            hit = spi_find(addr);
            if (hit == NULL || want > hit->size) {
                /* 未知・不足は答えない。でたらめ校正値は渡さない。 */
                snprintf(msg, sizeof(msg),
                         "  SPI unknown addr=0x%04x size=%u (no reply)",
                         (unsigned)addr, (unsigned)want);
                probe_line(msg);
                reply_len = 0u;
                break;
            }
            p = probe_build_reply(0x90u, 0x10u);
            reply_buf[p++] = (uint8_t)(addr & 0xFFu);
            reply_buf[p++] = (uint8_t)(addr >> 8);
            reply_buf[p++] = 0x00u;
            reply_buf[p++] = 0x00u;
            reply_buf[p++] = want;
            memcpy(&reply_buf[p], hit->data, want);
            p = (uint16_t)(p + want);
            reply_len = p;
            break;
        }
        case 0x21:
            p = probe_build_reply(0x80u, 0x21u);
            memset(&reply_buf[p], 0, 34);
            reply_len = p + 34u;
            break;
        case 0x30:
            if (report_size > 10) {
                probe_player_id = report[10];
            }
            reply_len = probe_build_reply(0x80u, 0x30u);
            break;
        case 0x31:
            p = probe_build_reply(0xB0u, 0x31u);
            reply_buf[p++] = probe_player_id;
            reply_len = p;
            break;
        case 0x33:
            p = probe_build_reply(0x80u, 0x33u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x40:
            if (report_size > 10) {
                probe_imu_enabled = (report[10] != 0u);
            }
            p = probe_build_reply(0x80u, 0x40u);
            reply_buf[p++] = 0x00u;
            reply_len = p;
            break;
        case 0x43:
            p = probe_build_reply(0xC0u, 0x43u);
            reply_buf[p++] = (report_size > 10) ? report[10] : 0u;
            reply_buf[p++] = (report_size > 11) ? report[11] : 0u;
            reply_len = p;
            break;
        case 0x48:
            if (report_size > 10) {
                probe_vibration_enabled = (report[10] != 0u);
            }
            p = probe_build_reply(0x80u, 0x48u);
            reply_buf[p++] = 0x00u;
            reply_len = p;
            break;
        case 0x50:
            /* 電圧。電池無しのため満充電固定値。 */
            p = probe_build_reply(0xD0u, 0x50u);
            reply_buf[p++] = 0x83u;
            reply_buf[p++] = 0x06u;
            reply_len = p;
            break;
        default:
            reply_len = probe_build_reply(0x80u, sub);
            break;
    }
}

void probe_report_handler(uint16_t cid, hid_report_type_t report_type,
                          uint16_t report_id, int report_size,
                          uint8_t *report)
{
    char msg[96];
    (void)cid;
    (void)report_type;
    probe_out_report_count++;
    if (report_size > 0) {
        uint16_t len = (uint16_t)report_size;
        if (len < probe_out_len_min) {
            probe_out_len_min = len;
        }
        if (len > probe_out_len_max) {
            probe_out_len_max = len;
        }
    }
    if (report_id == 0x01u && report_size > 9) {
        uint8_t sub = report[9];
        note_subcmd(sub);
        if (sub == 0x10u && report_size > 11) {
            snprintf(msg, sizeof(msg),
                     "  SUB=0x%02x raw=%02x %02x %02x %02x %02x len=%d",
                     sub, report[10], report[11], report[12], report[13],
                     report[14], report_size);
        } else {
            snprintf(msg, sizeof(msg), "  SUB=0x%02x len=%d kinds=%u",
                     sub, report_size, probe_subcmd_seen_n);
        }
        probe_line(msg);
        answer_subcmd(sub, report, report_size);
        /* A1+ID+本体48=50B へ 0 埋めで揃える。 */
        if (reply_len > 0u) {
            const uint16_t want = 2u + 48u;
            if (reply_len < want) {
                memset(&reply_buf[reply_len], 0, want - reply_len);
                reply_len = want;
            }
            if (reply_len > REPLY_MAX) {
                reply_len = REPLY_MAX;
            }
        }
        if (reply_len > 0u && probe_hid_cid != 0u) {
            hid_device_request_can_send_now_event(probe_hid_cid);
        }
    } else {
        snprintf(msg, sizeof(msg), "  OUT id=0x%02x len=%d",
                 (unsigned)report_id, report_size);
        probe_line(msg);
    }
}
