/* 有線 / 無線モードの管理。
 *
 * 切替えは BOOTSEL ボタンの長押し（2 秒）で行う。通電直後に押さえたまま
 * にすると USB ブートモードに入るため、起動してから押すこと。
 * モードは Flash に保存し、再起動で反映する。再起動方式なのは、
 * USB の機能（CDC / HID）と BTstack の初期化を確実に切り替えるため。
 *
 * LED（Pico 2 W 本体）は無線モードで点灯、有線モードで消灯する。
 * 長押し中は点滅し、切替え直前に速く 3 回点滅する。 */

#include "mode.h"

#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/platform.h"

#include "store.h"
#include "ui.h"

/* BOOTSEL はフラッシュの CS（QSPI_SS）に繋がっている。起動時以外は
 * スイッチとして読める。CS を一瞬だけ Hi-Z 入力にし、パッド入力を
 * 直接読む。SIO 経由ではなく STATUS の INFROMPAD を見る。
 * フラッシュ実行と被ると落ちるため RAM 上・割込み禁止で読む。 */
#define BOOTSEL_CS_INDEX 1u
#define MODE_HOLD_MS 2000u
#define MODE_BLINK_MS 200u

static uint8_t current_mode = WAKECON_MODE_WIRELESS;
static bool holding = false;
static uint32_t hold_start_ms = 0;
static bool hold_armed = false;
static uint32_t blink_last_ms = 0;
static bool blink_on = false;
static int8_t led_last = -1;

bool __not_in_flash_func(mode_bootsel_held)(void)
{
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[BOOTSEL_CS_INDEX].ctrl,
                    (uint32_t)GPIO_OVERRIDE_LOW
                        << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) {
    }
    /* ボタンは押下で Low になる。パッド入力を直接見る。 */
    bool pressed = !(ioqspi_hw->io[BOOTSEL_CS_INDEX].status &
                     IO_QSPI_GPIO_QSPI_SS_STATUS_INFROMPAD_BITS);
    hw_write_masked(&ioqspi_hw->io[BOOTSEL_CS_INDEX].ctrl,
                    (uint32_t)GPIO_OVERRIDE_NORMAL
                        << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
}

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
    /* 100Hz で叩くと CYW43 の SPI が塞がるため、変化時だけ書く。 */
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
    mode_led_apply();
    probe_line(mode_is_wired() ? "mode=wired (LED off)"
                               : "mode=wireless (LED on)");
    probe_line("hold BOOTSEL 2s to switch mode");
}

static void mode_blink_party(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(150);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(150);
    }
}

void mode_poll(uint32_t now_ms)
{
    /* 10ms 周期で呼ばれるが、読むのは 200ms ごとにする。
     * フラッシュの CS を触るため、頻度は低いほど安全。
     * 2 秒長押しの検出には十分。点滅（200ms）も同じ刻みで動く。 */
    static uint8_t div = 0;
    bool held;
    if ((++div % 20u) != 0u) {
        return;
    }
    held = mode_bootsel_held();
    if (!held) {
        holding = false;
        hold_start_ms = 0;
        hold_armed = false;
        /* LED は変化時だけ書く。毎回書くと CYW43 の SPI が塞がり、
         * HCI 通信の最中にぶつかってループが止まる。
         * 書くのは起動時・押下開始・解放・切替えの4場面だけ。 */
        mode_led_apply();
        return;
    }
    if (!holding) {
        holding = true;
        hold_start_ms = now_ms;
        blink_last_ms = now_ms;
        blink_on = false;
        /* 押下開始は LED 変化として書く。点滅の起点。 */
        led_last = -1;
        mode_led_apply();
        return;
    }
    /* 長押し中は点滅して受付中を知らせる。押している間の
     * 短い期間だけ直接書く。常時の定期書きはしない。 */
    if (!hold_armed && (now_ms - blink_last_ms) >= MODE_BLINK_MS) {
        blink_last_ms = now_ms;
        blink_on = !blink_on;
        led_last = blink_on ? 1 : 0;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_on ? 1 : 0);
    }
    if ((now_ms - hold_start_ms) >= MODE_HOLD_MS) {
        hold_armed = true;
        current_mode = mode_is_wired() ? WAKECON_MODE_WIRELESS
                                       : WAKECON_MODE_WIRED;
        store_mode_save(current_mode);
        probe_line(mode_is_wired() ? "switch to wired. reboot"
                                   : "switch to wireless. reboot");
        mode_blink_party();
        led_last = mode_is_wired() ? 0 : 1;
        watchdog_reboot(0u, 0u, 0u);
        while (1) {
        }
    }
}
