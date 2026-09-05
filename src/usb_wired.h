#ifndef USB_WIRED_H
#define USB_WIRED_H

/* 有線 Pro Controller の TinyUSB glue 公開 IF (有効化・状態・タスク)。
 * 実体は src/usb_wired.c。入力状態は既存 hid (probe_btn/lx/ly/rx/ry) を使う。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_wired_init(void);
void usb_wired_task(uint32_t now_ms);
void usb_wired_set_enabled(bool en);
bool usb_wired_is_enabled(void);
bool usb_wired_is_configured(void);
bool usb_wired_handshake_done(void);

#ifdef __cplusplus
}
#endif

#endif
