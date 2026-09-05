#ifndef USB_HID_H
#define USB_HID_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type);
bool usb_req_is_handshake(const uint8_t *req, int req_len);

/* 0x01 サブコマンド応答 (64B の 0x21 レポート) の入力文脈。
 * btn は BT 順 3B、stick は PC 側 8bit 値、mac は自アドレス。 */
typedef struct {
    uint8_t btn[3];
    uint8_t lx, ly, rx, ry;
    uint8_t timer;
    uint8_t mac[6];
    uint8_t player;
} usb_sub_ctx_t;

/* 0x01 xx → 64B の 0x21 応答。02/03/10/30/40/48 と既定 ack。
 * 成功時 64、不正時 0。Pico/BTstack 非依存。 */
int usb_build_21_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const usb_sub_ctx_t *ctx);
#ifdef __cplusplus
}
#endif
#endif
