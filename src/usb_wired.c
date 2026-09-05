/* 有線 Pro Controller の状態機と入力レポート送信 (TinyUSB glue)。
 * OUT EP の 80 xx 受口は src/usb_descriptors.c から MOVE したもの。
 * 応答バイトは usb_build_81_reply() (src/usb_hid.h) が唯一の源。
 * 80 04 完了前は入力を送らない。80 05・抜線で handshake を落とす。
 * Input 0x30/64B の内訳・周期は実測未確認の仮置き。T1ハードで確定する。 */

#include <string.h>

#include "pico/time.h"
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
static usb_wired_stats_t wired_stats;

void usb_wired_init(void)
{
    wired_enabled = false;
    handshake_done = false;
    last_input_ms = 0u;
    /* Task 3 の記述子をリンク GC から生かし列挙させる。 */
    (void)tud_init(BOARD_TUD_RHPORT);
    /* SOF 計数はバス生存の証拠 (ホストがフレームを回しているか)。
     * 1ms 毎に tud_sof_cb が来る。 */
    tud_sof_cb_enable(true);
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

void usb_wired_get_stats(usb_wired_stats_t *st)
{
    if (st == NULL) {
        return;
    }
    *st = wired_stats;
}

/* 有線 Input 0x30 は実機配置 (2wiCC ControllerData 互換)。
 * 12B 状態 + 36B IMU(無効のため 0) + 15B 埋め。mac/player は使わない。 */
static void build_input_report(uint8_t out[USB_WIRED_INPUT_LEN])
{
    usb_sub_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.btn[0] = probe_btn[0];
    ctx.btn[1] = probe_btn[1];
    ctx.btn[2] = probe_btn[2];
    ctx.lx = probe_lx;
    ctx.ly = probe_ly;
    ctx.rx = probe_rx;
    ctx.ry = probe_ry;
    ctx.timer =
        (uint8_t)(to_ms_since_boot(get_absolute_time()) >> 5);
    usb_build_30_report(&ctx, out);
}

void usb_wired_pump(void)
{
    tud_task();
}

void usb_wired_reconnect(void)
{
    tud_disconnect();
    sleep_ms(50);
    tud_connect();
}

/* 応答の遅延送出用 (2wiCC 方式)。コールバック内では積むだけにし、
 * 送信はタスク側で行う。コールバック内送信は control 転送の完了と
 * 競合しうるため。単一スロット (ホストは stop-and-wait のため十分)。 */
static uint8_t pend_resp[64];
static uint8_t pend_resp_id;
static bool pend_resp_valid;
static uint8_t usb_player;

void usb_wired_task(uint32_t now_ms)
{
    uint8_t report[USB_WIRED_INPUT_LEN];
    tud_task();
    /* 保留中の応答を先に送る (2wiCC の special first 相当)。
     * 送れなければ次 tick に持ち越す。 */
    if (pend_resp_valid) {
        if (tud_hid_ready() &&
            tud_hid_report(pend_resp_id, &pend_resp[1],
                           (uint16_t)(sizeof(pend_resp) - 1u))) {
            pend_resp_valid = false;
            if (pend_resp_id == USB_WIRED_REPORT_ID_REPLY) {
                wired_stats.tx81++;
            } else {
                wired_stats.tx21++;
            }
        }
        return;
    }
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
        wired_stats.in30++;
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
    uint8_t mac_rev[6];
    uint8_t sub;
    uint8_t i;
    int n;
    (void)instance;
    (void)report_type;
    if (buffer == NULL || bufsize == 0u) {
        return;
    }
    /* 受信経路の内訳 (80 xx 以外も数える。0x01 系の有無も見える)。 */
    if (report_id != 0u) {
        wired_stats.ctl_rx++;
    } else {
        wired_stats.ep_rx++;
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
    if (req_len < 2u) {
        return;
    }
    /* 81 01 / 0x21 応答の MAC は設計メモの逆順格納に従う。
     * bd_addr_to_str が配列順表示のため反転して渡す。 */
    for (i = 0; i < 6u; i++) {
        mac_rev[i] = probe_addr[5u - i];
    }
    if (req[0] == 0x80u) {
        sub = req[1];
        wired_stats.rx80++;
        wired_stats.last80 = sub;
        wired_stats.hist[0] = wired_stats.hist[1];
        wired_stats.hist[1] = wired_stats.hist[2];
        wired_stats.hist[2] = wired_stats.hist[3];
        wired_stats.hist[3] = sub;
        /* W 0 中のホスト雑音で handshake を立てない (応答自体は返す)。 */
        if (wired_enabled && sub == 0x04u) {
            handshake_done = true;
        } else if (wired_enabled && sub == 0x05u) {
            handshake_done = false;
        }
        /* 応答は積むだけにする。送信は usb_wired_task 側で行う。 */
        n = usb_build_81_reply(req, (int)req_len, pend_resp,
                               (int)sizeof(pend_resp), mac_rev,
                               USB_WIRED_PROVISIONAL_DEV_TYPE);
        if (n <= 0) {
            return; /* 不正入力のみ送らない。応答は常に 64B (ID+63)。 */
        }
        pend_resp_id = USB_WIRED_REPORT_ID_REPLY;
        pend_resp_valid = true;
    } else if (req[0] == 0x01u) {
        /* 0x01 サブコマンド (2wiCC 通り)。0x10 は振動のみで無応答。 */
        usb_sub_ctx_t ctx;
        if (req_len < 11) {
            return;
        }
        sub = req[10];
        wired_stats.hist01[0] = wired_stats.hist01[1];
        wired_stats.hist01[1] = wired_stats.hist01[2];
        wired_stats.hist01[2] = wired_stats.hist01[3];
        wired_stats.hist01[3] = sub;
        if (sub == 0x10u) {
            return;
        }
        /* 0x03 mode 0x30 も full 開始合図にする (BT の full 化と同義)。
         * 80 04 が来ないホストへの備え。W 0 中は立てない。 */
        if (wired_enabled && sub == 0x03u && req_len >= 12 &&
            req[11] == 0x30u) {
            handshake_done = true;
        }
        if (sub == 0x30u && req_len >= 12) {
            usb_player = req[11];
        }
        ctx.btn[0] = probe_btn[0];
        ctx.btn[1] = probe_btn[1];
        ctx.btn[2] = probe_btn[2];
        ctx.lx = probe_lx;
        ctx.ly = probe_ly;
        ctx.rx = probe_rx;
        ctx.ry = probe_ry;
        ctx.timer =
            (uint8_t)(to_ms_since_boot(get_absolute_time()) >> 5);
        memcpy(ctx.mac, mac_rev, 6);
        ctx.player = usb_player;
        n = usb_build_21_reply(req, (int)req_len, pend_resp,
                               (int)sizeof(pend_resp), &ctx);
        if (n <= 0) {
            return;
        }
        pend_resp_id = 0x21u;
        pend_resp_valid = true;
    } else if (req[0] != 0x10u) {
        /* 0x80/0x01/0x10 以外は応答なし。未知 ID は計数だけ残す
         * (UART 連打を避けるため。BT 側の OUT id= 行に相当)。 */
        wired_stats.unk_id = req[0];
        wired_stats.unk_len = (uint8_t)(req_len > 255 ? 255 : req_len);
        wired_stats.unk_n++;
    }
}

/* 装着で計数。tud_mounted() が立つ直前の bus reset/configure 由来。 */
void tud_mount_cb(void)
{
    wired_stats.mount++;
}

/* 抜線で handshake を落とす。次セッションは 80 04 からやり直し。 */
void tud_umount_cb(void)
{
    wired_stats.unmount++;
    handshake_done = false;
}

/* SOF 到達 = ホストがバスにフレームを流している (列挙前でも進む)。
 * サスペンド中は止まる。いずれも TinyUSB の weak 既定の上書き。 */
void tud_sof_cb(uint32_t frame_count)
{
    (void)frame_count;
    wired_stats.sof++;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    wired_stats.susp++;
}

void tud_resume_cb(void)
{
    wired_stats.resm++;
}
