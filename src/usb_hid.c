/* src/usb_hid.c: 日本語コメントは残す。でたらめ値は返さない。 */
#include <string.h>
#include "usb_hid.h"

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
