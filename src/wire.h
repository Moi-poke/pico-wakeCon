#ifndef WAKECON_WIRE_H
#define WAKECON_WIRE_H

/* 有線モードの USB HID デバイス。
 * Switch へ USB で直結し、8 バイトの入力報告を送る。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 10ms 周期で呼ぶ。TinyUSB の駆動と報告の送信を行う。 */
void wire_task(void);

/* 有線報告 1 件を組み立てて送る。変化がなければ送らない。 */
void wire_send_if_changed(void);

/* USB CDC の簡易ドライバを stdio へ登録する。無線モードの PC 接続用。 */
void wire_stdio_init(void);

#ifdef __cplusplus
}
#endif

#endif
