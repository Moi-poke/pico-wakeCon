/* src/usb_hid.c: 日本語コメントは残す。でたらめ値は返さない。 */
#include <string.h>
#include "usb_hid.h"
#include "spi.h"
#include "util.h"

bool usb_req_is_handshake(const uint8_t *req, int req_len)
{
    return req != NULL && req_len >= 2 && req[0] == 0x80u;
}

/* 80 xx への応答は常に 64B ゼロパディングで返す。
 * 2wiCC (実働) が全応答を 64B (ID+63) で送る作りのため。
 * 短縮応答では Switch 2 が先に進まない実測 (last=02 で停止)。
 * 91/92・未知サブコマンドも 81 <sub> + 0 埋めで返す。
 * 無応答にするとホストが止まる。 */
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type)
{
    uint8_t sub;
    if (req == NULL || out == NULL || mac == NULL) {
        return 0;
    }
    if (!usb_req_is_handshake(req, req_len)) {
        return 0;
    }
    if (out_max < 64) {
        return 0;
    }
    sub = req[1];
    memset(out, 0, 64);
    switch (sub) {
        case 0x01u:
            /* 81 01 00 <type> <mac6> + 0 埋め。
             * Pro=0x03 は 2wiCC の kUsbDeviceTypeProController で確認。 */
            out[0] = 0x81u; out[1] = 0x01u; out[2] = 0x00u;
            out[3] = dev_type;
            memcpy(&out[4], mac, 6);
            return 64;
        case 0x02u:
        case 0x03u:
        case 0x04u:
        case 0x05u:
        case 0x06u:
            /* 04/05/06 の応答有無は 2wiCC の実働で確認 (R-T1-1 解消)。 */
            out[0] = 0x81u; out[1] = sub;
            return 64;
        default:
            out[0] = 0x81u; out[1] = sub;
            return 64;
    }
}

/* 0x21 応答の共通 12B (2wiCC ControllerData 互換)。
 * timer・電池・接続・姿勢は ctx から。電池は充電中+満充電固定。 */
static void usb_prefix_12(uint8_t *out, const usb_sub_ctx_t *ctx)
{
    out[0] = ctx->timer;
    out[1] = 0x91u;
    out[2] = ctx->btn[0];
    out[3] = (uint8_t)(ctx->btn[1] | 0x80u);
    out[4] = (uint8_t)(ctx->btn[2] & 0xCFu);
    util_pack_stick_12bit(ctx->lx, ctx->ly, &out[5]);
    util_pack_stick_12bit(ctx->rx, ctx->ry, &out[8]);
    out[11] = 0x09u;
}

/* 0x01 xx → 64B の 0x21 応答。02/03/10/30/40/48 と既定 ack。
 * サブコマンド部の並びは BT 応答と同値 (輸送非依存のため)。
 * 本文は 2wiCC の実働値 (機器情報の fw 03 48 等) に合わせる。 */
int usb_build_21_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const usb_sub_ctx_t *ctx)
{
    uint8_t sub;
    if (req == NULL || out == NULL || ctx == NULL) {
        return 0;
    }
    if (req_len < 11 || req[0] != 0x01u) {
        return 0;
    }
    if (out_max < 64) {
        return 0;
    }
    sub = req[10];
    memset(out, 0, 64);
    out[0] = 0x21u;
    usb_prefix_12(&out[1], ctx);
    switch (sub) {
        case 0x02u:
            out[13] = 0x82u;
            out[14] = 0x02u;
            out[15] = 0x03u;
            out[16] = 0x48u;
            out[17] = 0x03u;
            out[18] = 0x02u;
            memcpy(&out[19], ctx->mac, 6);
            out[25] = 0x01u;
            out[26] = 0x01u;
            return 64;
        case 0x03u:
            out[13] = 0x80u;
            out[14] = 0x03u;
            return 64;
        case 0x10u: {
            uint16_t addr;
            uint8_t want;
            const spi_entry_t *hit;
            if (req_len < 16) {
                return 0;
            }
            addr = (uint16_t)req[11] | ((uint16_t)req[12] << 8);
            want = req[15];
            hit = spi_find(addr);
            if (hit == NULL || want > hit->size) {
                /* 未知・不足は答えない。でたらめ校正値は渡さない。 */
                return 0;
            }
            out[13] = 0x90u;
            out[14] = 0x10u;
            out[15] = (uint8_t)(addr & 0xFFu);
            out[16] = (uint8_t)(addr >> 8);
            out[17] = 0x00u;
            out[18] = 0x00u;
            out[19] = want;
            memcpy(&out[20], hit->data, want);
            return 64;
        }
        case 0x30u:
        case 0x40u:
        case 0x48u:
            out[13] = 0x80u;
            out[14] = sub;
            return 64;
        default:
            out[13] = 0x80u;
            out[14] = sub;
            return 64;
    }
}
