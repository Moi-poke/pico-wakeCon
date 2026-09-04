#ifndef WAKECON_MODE_H
#define WAKECON_MODE_H

/* 有線 / 無線モードの管理。
 * BOOTSEL ボタン長押しで切替え、Flash に保存、再起動で反映する。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKECON_MODE_WIRELESS 0u
#define WAKECON_MODE_WIRED    1u

/* 起動時に呼ぶ。保存を読んで LED と表示を整える。 */
void mode_boot(void);

/* 現在のモード。 */
uint8_t mode_get(void);
bool mode_is_wired(void);

/* 10ms 周期で呼ぶ。BOOTSEL が HOLD_MS 連続したら切替えて再起動する。 */
void mode_poll(uint32_t now_ms);

/* LED へ反映する。無線は点灯、有線は消灯。 */
void mode_led_apply(void);

/* BOOTSEL ボタンの状態を読む。RAM 上で実行する。 */
bool mode_bootsel_held(void);

#ifdef __cplusplus
}
#endif

#endif
