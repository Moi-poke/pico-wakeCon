/* 有線 / 無線モードの管理。
 *
 * 切替えはシリアル（または USB CDC）の W コマンドで行う。
 * モードは Flash に保存し、再起動で反映する。再起動方式なのは、
 * USB の機能（CDC / HID）と BTstack の初期化を確実に切り替えるため。
 *
 * LED（Pico 2 W 本体）は無線モードで点灯、有線モードで消灯する。
 * 書くのは起動時だけ。動作中に CYW43 へ書くと HCI 通信とぶつかる。
 *
 * 救済: UART 変換器を持たない人が有線へ切替えると、操作口が無くなる。
 * その場合は電源の入れ直しを 4 回繰り返す（各回 60 秒以内に切る）。
 * 短い起動が続くと無線へ戻る。60 秒動けば回数は消える。
 *
 * BOOTSEL ボタンでの切替えは行わない。ボタンを読むにはフラッシュの
 * CS を触る必要があり、RP2350 では動作中に触ると止まる。 */

#include "mode.h"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"

#include "store.h"
#include "ui.h"

static uint8_t current_mode = WAKECON_MODE_WIRELESS;
static int8_t led_last = -1;
static uint8_t rescue_secs = 0;

/* 救済の条件。短い起動がこの回数続くと無線へ戻る。 */
#define RESCUE_BOOTS 4u
/* この秒数動けば回数を消す。通常利用では消え続ける。 */
#define RESCUE_CLEAR_S 60u

uint8_t mode_get(void)
{
    return current_mode;
}

bool mode_is_wired(void)
{
    return current_mode == WAKECON_MODE_WIRED;
}

void mode_led_apply(void)
{
    /* 変化時だけ書く。動作中の書込みは HCI 通信とぶつかる。 */
    if (led_last == (int8_t)(mode_is_wired() ? 0 : 1)) {
        return;
    }
    led_last = (int8_t)(mode_is_wired() ? 0 : 1);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_last);
}

void mode_boot(void)
{
    uint8_t saved = WAKECON_MODE_WIRELESS;
    if (store_mode_load(&saved) && saved == WAKECON_MODE_WIRED) {
        current_mode = WAKECON_MODE_WIRED;
    } else {
        current_mode = WAKECON_MODE_WIRELESS;
    }
    if (mode_is_wired()) {
        /* 救済の数え上げ。短い起動が続けば無線へ戻る。 */
        uint8_t tries = 0u;
        store_rescue_load(&tries);
        tries++;
        if (tries >= RESCUE_BOOTS) {
            current_mode = WAKECON_MODE_WIRELESS;
            store_mode_save(current_mode);
            store_rescue_save(0u);
            probe_line("rescue: back to wireless");
        } else {
            store_rescue_save(tries);
        }
    }
    mode_led_apply();
    probe_line(mode_is_wired() ? "mode=wired (LED off)"
                               : "mode=wireless (LED on)");
    probe_line("W 0/1 to switch mode (reboots)");
}

void mode_second_tick(void)
{
    /* 1 秒ごとに呼ぶ。長く動けば救済の回数を消す。 */
    if (!mode_is_wired()) {
        return;
    }
    if ((++rescue_secs % RESCUE_CLEAR_S) != 0u) {
        return;
    }
    rescue_secs = 0u;
    store_rescue_save(0u);
}

void mode_request_switch(uint8_t want)
{
    /* W コマンドから呼ぶ。保存して再起動する。戻らない。
     * 明示の切替えでは救済の回数を消す。 */
    store_rescue_save(0u);
    store_mode_save(want);
    probe_line(want ? "mode=wired. reboot" : "mode=wireless. reboot");
    sleep_ms(200);
    watchdog_reboot(0u, 0u, 0u);
    while (1) {
    }
}
