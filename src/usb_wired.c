/* 有線 Pro Controller の状態機と入力レポート送信 (TinyUSB glue)。
 * OUT EP の 80 xx 受口は src/usb_descriptors.c から MOVE したもの。
 * 応答バイトは usb_build_81_reply() (src/usb_hid.h) が唯一の源。
 * 80 04 完了前は入力を送らない。80 05・抜線で handshake を落とす。
 * Input 0x30/64B の内訳・周期は実測未確認の仮置き。T1ハードで確定する。 */

#include <string.h>

#include "tusb.h"

#include "usb_wired.h"
#include "usb_hid.h"

/* hid/link の状態を借りる。btstack.h と tusb.h は hid_report_type_t が衝突するため
 * hid.h/link.h は include しない (BTstack 依存は main/link/hid に閉じ込める)。
 * 所有元は src/hid.c・src/link_conn.c。読みのみ。書換時は grep で影響を確認する。 */
extern uint8_t probe_btn[3];
extern uint8_t probe_lx, probe_ly, probe_rx, probe_ry;
extern uint8_t probe_addr[6]; /* link.h の bd_addr_t (uint8_t[6]) と対応 */

/* 実測未確認: 有線 Input の Report ID/サイズは記述子由来の仮置き
 * (ID 0x30 + 63B = 64B = EP サイズ)。T1ハードで確定。 */
#define USB_WIRED_REPORT_ID_INPUT 0x30u
#define USB_WIRED_INPUT_LEN 64u
#define USB_WIRED_INPUT_PAYLOAD_LEN 63u
/* 実測未確認: 81 応答の IN report ID は 0x81 仮置き (OUT 0x80 に対応)。T1ハードで確定。 */
#define USB_WIRED_REPORT_ID_REPLY 0x81u
/* 実測未確認: 送信周期は暫定。BT 側 120Hz は持ち込まない。
 * 記述子 bInterval=8 に合わせた仮置き。T1 実測で確定。
 * 呼出しは uart_poll (10ms) のため実効は 10ms に量子化される。 */
#define USB_WIRED_PROVISIONAL_INTERVAL_MS 8u
/* 実測未確認: 81 01 応答の機種別 type 値は仮置き。T1ハードで確定。 */
#define USB_WIRED_PROVISIONAL_DEV_TYPE 0x03u

static bool wired_enabled;
static bool handshake_done; /* 80 04 受信で true。80 05・抜線で false に戻す。 */
static bool wired_inited;
static uint32_t last_input_ms;

void usb_wired_init(void)
{
    wired_enabled = false;
    handshake_done = false;
    last_input_ms = 0u;
    /* Task 3 の記述子をリンク GC から生かし列挙させる。 */
    (void)tud_init(BOARD_TUD_RHPORT);
    wired_inited = true;
}

void usb_wired_set_enabled(bool en)
{
    wired_enabled = en;
    if (!en) {
        handshake_done = false;
    }
}

bool usb_wired_is_enabled(void)
{
    return wired_enabled;
}

bool usb_wired_is_configured(void)
{
    return wired_inited && tud_mounted();
}

bool usb_wired_handshake_done(void)
{
    return handshake_done;
}

/* 実測未確認: 有線 Input 0x30/64B の内訳は記述子サイズ由来の仮置き。
 * ボタン 2B・スティック 4軸16bit LE・ハット 1B・残り 0 埋め。
 * BT 側の probe_pack_stick (12bit packed) は有線 16bit 欄に合わないため使わない。
 * spi_color_6050 の配置も T1 実測待ちのため載せない。T1ハードで確定する。 */
static void build_input_report(uint8_t out[USB_WIRED_INPUT_LEN])
{
    uint16_t v;
    memset(out, 0, USB_WIRED_INPUT_LEN);
    out[0] = USB_WIRED_REPORT_ID_INPUT;
    out[1] = probe_btn[0];
    out[2] = probe_btn[1];
    /* TODO(T1): btn[2] の割付け未確認のため送らない。ハットは中立固定。T1ハードで確定。 */
    /* 8bit (0x80 中立) → 16bit LE 仮置き ((v<<8)|v で 0x80→0x8080。0x8000 中心・Y 反転は未確定)。
     * BT 側 probe_pack_stick との対応も T1ハードで確定。Y は実測未確認のため素通し。 */
    v = (uint16_t)(((uint16_t)probe_lx << 8) | probe_lx);
    out[3] = (uint8_t)(v & 0xFFu);
    out[4] = (uint8_t)(v >> 8);
    v = (uint16_t)(((uint16_t)probe_ly << 8) | probe_ly);
    out[5] = (uint8_t)(v & 0xFFu);
    out[6] = (uint8_t)(v >> 8);
    v = (uint16_t)(((uint16_t)probe_rx << 8) | probe_rx);
    out[7] = (uint8_t)(v & 0xFFu);
    out[8] = (uint8_t)(v >> 8);
    v = (uint16_t)(((uint16_t)probe_ry << 8) | probe_ry);
    out[9] = (uint8_t)(v & 0xFFu);
    out[10] = (uint8_t)(v >> 8);
    out[11] = 0x08u; /* 実測未確認: ハット無操作の仮置き */
}

void usb_wired_task(uint32_t now_ms)
{
    uint8_t report[USB_WIRED_INPUT_LEN];
    tud_task();
    /* 80 04 完了前は送らない。 */
    if (!wired_enabled || !wired_inited || !tud_mounted() || !handshake_done) {
        return;
    }
    if ((uint32_t)(now_ms - last_input_ms) < USB_WIRED_PROVISIONAL_INTERVAL_MS) {
        return;
    }
    if (!tud_hid_ready()) {
        return;
    }
    build_input_report(report);
    if (tud_hid_report(USB_WIRED_REPORT_ID_INPUT, &report[1],
                       USB_WIRED_INPUT_PAYLOAD_LEN)) {
        last_input_ms = now_ms;
    }
}

/* OUT EP (80 xx ハンドシェイク) の受口。src/usb_descriptors.c から MOVE。
 * 応答バイトは usb_build_81_reply() のみが作る。0 返却は「送らない」。 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    uint8_t req[65];
    uint16_t req_len;
    uint8_t reply[64];
    uint8_t mac_rev[6];
    uint8_t sub;
    uint8_t i;
    int n;
    (void)instance;
    (void)report_type;
    if (buffer == NULL || bufsize == 0u) {
        return;
    }
    /* OUT EP 経路は先頭 1B が生の report ID (0x80)。
     * control 経路は report_id 引数に分離される。両方受ける。 */
    if (report_id != 0u) {
        if (bufsize > (uint16_t)sizeof(req) - 1u) {
            return;
        }
        req[0] = report_id;
        memcpy(&req[1], buffer, bufsize);
        req_len = (uint16_t)(bufsize + 1u);
    } else {
        if (bufsize > (uint16_t)sizeof(req)) {
            return;
        }
        memcpy(req, buffer, bufsize);
        req_len = bufsize;
    }
    if (req_len < 2u || req[0] != 0x80u) {
        return;
    }
    sub = req[1];
    /* W 0 中のホスト雑音で handshake を立てない (応答自体は返す)。 */
    if (wired_enabled && sub == 0x04u) {
        handshake_done = true;
    } else if (wired_enabled && sub == 0x05u) {
        handshake_done = false;
    }
    /* 81 01 応答の MAC は設計メモの逆順格納に従う。
     * bd_addr_to_str が配列順表示のため反転して渡す。 */
    for (i = 0; i < 6u; i++) {
        mac_rev[i] = probe_addr[5u - i];
    }
    n = usb_build_81_reply(req, (int)req_len, reply, (int)sizeof(reply),
                           mac_rev, USB_WIRED_PROVISIONAL_DEV_TYPE);
    if (n < 2) {
        return; /* 未対応 (91/92 等) は送らない */
    }
    if (!tud_hid_ready()) {
        return;
    }
    /* 実測未確認: 短い 81 応答 (2B/10B) をそのまま送る仮置き。64B パディング有無は T1ハードで確定。 */
    tud_hid_report(USB_WIRED_REPORT_ID_REPLY, &reply[1], (uint16_t)(n - 1));
}

/* 抜線で handshake を落とす。次セッションは 80 04 からやり直し。 */
void tud_umount_cb(void)
{
    handshake_done = false;
}
