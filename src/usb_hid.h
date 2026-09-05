#ifndef USB_HID_H
#define USB_HID_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 80 xx → 81 xx 応答組立。Pico/BTstack 非依存。 */
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type);
bool usb_req_is_handshake(const uint8_t *req, int req_len);
#ifdef __cplusplus
}
#endif
#endif
