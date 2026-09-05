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

/* USB バス側の診断計数。列挙〜応答のどこで止まるかを見る。
 * mount/unmount はホストの bus reset/configure 由来。rx80/last80/tx81 は
 * 80 xx 受信と 81 応答。in30 は入力レポート送出。W 0 中も数える。 */
typedef struct {
    uint32_t mount;
    uint32_t unmount;
    uint32_t rx80;
    uint8_t last80;
    uint32_t tx81;
    uint32_t in30;
} usb_wired_stats_t;
void usb_wired_get_stats(usb_wired_stats_t *st);

#ifdef __cplusplus
}
#endif

#endif
