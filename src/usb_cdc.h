#ifndef WAKECON_USB_CDC_H
#define WAKECON_USB_CDC_H

/* USB CDC コンソール (TinyUSB glue)。tusb.h を外に出さない。
 * ui.c は btstack 由来の hid_report_type_t で tusb.h と衝突するため、
 * この窓口だけを使う。 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ホストが開いているか (マウント＋DTR)。送信可否の判定用。 */
bool usb_cdc_connected(void);
/* 1 文字読む。なければ -1。DTR の有無によらず読む (UART と対等にする)。 */
int usb_cdc_read_char(void);
/* 1 行書く (改行＋flush まで行う)。未接続なら何もしない。 */
void usb_cdc_write_line(const char *text);

#ifdef __cplusplus
}
#endif

#endif
