/* エミュレートするコントローラの種類。
 *
 * Pro Controller と Joy-Con (L/R) を切り替える。種類で変わるのは
 * 名乗り（SDP・GAP・USB の VID/PID・機器種別）と入力の写像だけであり、
 * ペアリング手順・サブコマンド応答・0x30 報告の形は共通である。
 * ボタン配置は公開仕様通り。右側バイト・左側バイト・共有バイトの
 * 3 バイト構成であり、Pro は両側、Joy-Con は片側だけ使う。 */

#include "type.h"

#include "hardware/watchdog.h"
#include "pico/time.h"

#include "store.h"
#include "ui.h"

static uint8_t current_type = WAKECON_TYPE_PRO;

uint8_t type_get(void)
{
    return current_type;
}

bool type_is_pro(void)
{
    return current_type == WAKECON_TYPE_PRO;
}

bool type_is_jcl(void)
{
    return current_type == WAKECON_TYPE_JCL;
}

bool type_is_jcr(void)
{
    return current_type == WAKECON_TYPE_JCR;
}

uint16_t type_product_id(void)
{
    if (type_is_jcl()) {
        return 0x2006u;
    }
    if (type_is_jcr()) {
        return 0x2007u;
    }
    return 0x2009u;
}

uint8_t type_device_byte(void)
{
    if (type_is_jcl()) {
        return 0x01u;
    }
    if (type_is_jcr()) {
        return 0x02u;
    }
    return 0x03u;
}

const char *type_gap_name(void)
{
    if (type_is_jcl()) {
        return "Joy-Con (L)";
    }
    if (type_is_jcr()) {
        return "Joy-Con (R)";
    }
    return "Pro Controller";
}

const char *type_short_name(void)
{
    if (type_is_jcl()) {
        return "jcl";
    }
    if (type_is_jcr()) {
        return "jcr";
    }
    return "pro";
}

void type_boot(void)
{
    uint8_t saved = WAKECON_TYPE_PRO;
    if (store_type_load(&saved) && saved <= WAKECON_TYPE_JCR) {
        current_type = saved;
    } else {
        current_type = WAKECON_TYPE_PRO;
    }
    probe_line(type_is_pro() ? "type=pro" :
               type_is_jcl() ? "type=jcl (Joy-Con L)" : "type=jcr (Joy-Con R)");
    probe_line("T 0/1/2 to switch type (reboots)");
}

void type_request_switch(uint8_t want)
{
    /* T コマンドから呼ぶ。保存して再起動する。戻らない。 */
    if (want > WAKECON_TYPE_JCR) {
        probe_line("usage: T [0 pro | 1 jcl | 2 jcr]");
        return;
    }
    store_type_save(want);
    probe_line(want == WAKECON_TYPE_PRO ? "type=pro. reboot" :
               want == WAKECON_TYPE_JCL ? "type=jcl. reboot" : "type=jcr. reboot");
    sleep_ms(200);
    watchdog_reboot(0u, 0u, 0u);
    while (1) {
    }
}
