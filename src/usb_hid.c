/* src/usb_hid.c: 日本語コメントは残す。でたらめ値は返さない。 */
#include <string.h>
#include "usb_hid.h"

bool usb_req_is_handshake(const uint8_t *req, int req_len)
{
    return req != NULL && req_len >= 2 && req[0] == 0x80u;
}

/* 80 01/02/03/04/05/06 のみ応答。91/92 等は 0 (未対応)。 */
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
    sub = req[1];
    /* Task 1 の対応表で各分岐の応答バイト列を確定させる。 */
    switch (sub) {
        case 0x01u: {
            /* 実測未確認: 81 01 応答の全バイトは仮置き (81 01 00 <type> <mac6>)。実測で確定後に固定化。 */
            if (out_max < 10) {
                return 0;
            }
            out[0] = 0x81u; out[1] = 0x01u; out[2] = 0x00u;
            out[3] = dev_type;
            memcpy(&out[4], mac, 6);
            return 10;
        }
        case 0x02u:
        case 0x03u:
        /* 実測未確認: 80 0x04 の応答有無はT1ハードで確定 */
        case 0x04u:
        /* 実測未確認: 80 0x05 の応答有無はT1ハードで確定 */
        case 0x05u:
        /* 実測未確認: 80 0x06 の応答有無はT1ハードで確定 */
        case 0x06u:
            if (out_max < 2) {
                return 0;
            }
            out[0] = 0x81u; out[1] = sub;
            return 2;
        default:
            return 0;
    }
}
