#ifndef WAKECON_MODE_H
#define WAKECON_MODE_H

/* 有線 / 無線モードの管理。
 * 切替えは W コマンドで行い、Flash に保存、再起動で反映する。 */

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

/* LED へ反映する。無線は点灯、有線は消灯。変化時だけ書く。 */
void mode_led_apply(void);

/* モードを保存して再起動する。W コマンドから呼ぶ。戻らない。 */
void mode_request_switch(uint8_t want);

/* 1 秒ごとに呼ぶ。長く動けば救済の回数を消す。 */
void mode_second_tick(void);

#ifdef __cplusplus
}
#endif

#endif
