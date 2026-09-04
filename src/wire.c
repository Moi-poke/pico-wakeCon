/* 有線モードの USB HID デバイス。
 *
 * Switch へ USB で直結し、8 バイトの入力報告を送る。報告の並びは
 * buttons(2) hat(1) lx ly rx ry(4) vendor(1) で、入力状態は S 行の
 * PC 側値（probe_pc_*）をそのまま使う。BT 側の変換値は使わない。
 *
 * USB の記述子はモードで替える。有線では HID＋CDC の複合にする。
 * CDC は PC に繋いだときの操作口（W 0 で無線へ戻す）であり、
 * Switch に繋いだときは相手が使わない。HID を先頭に据え置くのは、
 * Switch が HID を先頭に期待する可能性があるため。
 * 無線では CDC のみ（PC 向け）である。列挙は起動後に起きるため、
 * 起動時に読んだモードで固定される。切替えは再起動で行う。
 *
 * 有線の VID/PID は実機の有線コントローラと合わせる。合わないと
 * Switch が無視するためである。 */

#include "wire.h"

#include <string.h>

#include "tusb.h"
#include "pico/error.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "mode.h"

/* hid.c が持つ S 行の PC 側値。hid.h は btstack.h を引くため、
 * 同じ TU では TinyUSB の同名型と衝突する。値の型だけ要るので
 * ここで宣言する（実体は hid.c）。 */
extern uint16_t probe_pc_buttons;
extern uint8_t probe_pc_hat;
extern uint8_t probe_pc_lx, probe_pc_ly, probe_pc_rx, probe_pc_ry;

/* HORI POKKEN TOURNAMENT DX と同じ名乗り。 */
#define WIRE_VID 0x0F0Du
#define WIRE_PID 0x0092u

#define CDC_VID 0x2E8Au
#define CDC_PID 0x000Au

#define WIRE_REPORT_LEN 8u
#define WIRE_EP_IN 0x84u
#define WIRE_EP_INTERVAL 8u

/* 8 バイト報告の記述子。
 * 内訳: ボタン16bit + hat(4bit) + 埋め(4bit) + 軸4B + 予備1B = 64bit。
 * hat は Null 付きなので 8（中立）は無値として扱われる。
 * const を付けない。USB の DMA が直接読むため SRAM に置く。
 * フラッシュに置くと、BOOTSEL 読み（CS 操作）の最中に DMA が読んで
 * バスフォルトになる。 */
static uint8_t wire_report_desc[] = {
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (1)
    0x29, 0x10,        // Usage Maximum (16)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x10,        // Report Count (16)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x39,        // Usage (Hat switch)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x07,        // Logical Maximum (7)
    0x75, 0x04,        // Report Size (4)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x42,        // Input (Data,Var,Abs,Null)
    0x75, 0x04,        // Report Size (4)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x01,        // Input (Const, padding)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x09, 0x33,        // Usage (Rx)
    0x09, 0x34,        // Usage (Ry)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x04,        // Report Count (4)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined)
    0x09, 0x01,        // Usage (0x01)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0xC0,              // End Collection
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL_WIRELESS,
};
/* 有線は HID 複合。HID を 0 に据え置き、CDC を後ろへ足す。
 * Switch が HID を先頭に期待する可能性があるため順序を変えない。
 * EP 番号は方向ごとに重ねない（CDC 通知 0x81・CDC IN 0x83 のため
 * HID IN は 0x84）。 */
#define ITF_NUM_HID_WIRED 0
#define ITF_NUM_CDC_WIRED 1
#define ITF_NUM_CDC_DATA_WIRED 2
#define ITF_NUM_TOTAL_WIRED 3

#define EPNUM_CDC_NOTIF 0x81u
#define EPNUM_CDC_OUT   0x02u
#define EPNUM_CDC_IN    0x83u

#define CONFIG_TOTAL_LEN_WIRELESS \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define CONFIG_TOTAL_LEN_WIRED \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

static uint8_t serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static void wire_make_serial(void)
{
    static bool done = false;
    pico_unique_board_id_t id;
    size_t i;
    if (done) {
        return;
    }
    done = true;
    pico_get_unique_board_id(&id);
    for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        serial_str[2 * i] = (uint8_t)"0123456789ABCDEF"
            [(id.id[i] >> 4) & 0x0Fu];
        serial_str[2 * i + 1] = (uint8_t)"0123456789ABCDEF"
            [id.id[i] & 0x0Fu];
    }
    serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = 0;
}

/* ---- TinyUSB 記述子 ---- */

uint8_t const *tud_descriptor_device_cb(void)
{
    /* 列挙は起動後なので、起動時のモードで固定される。 */
    static tusb_desc_device_t desc_wireless = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = TUSB_CLASS_MISC,
        .bDeviceSubClass = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = CDC_VID,
        .idProduct = CDC_PID,
        .bcdDevice = 0x0100,
        .iManufacturer = 0x01,
        .iProduct = 0x02,
        .iSerialNumber = 0x03,
        .bNumConfigurations = 0x01,
    };
    static tusb_desc_device_t desc_wired = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = WIRE_VID,
        .idProduct = WIRE_PID,
        .bcdDevice = 0x0100,
        .iManufacturer = 0x01,
        .iProduct = 0x02,
        .iSerialNumber = 0x03,
        .bNumConfigurations = 0x01,
    };
    return (uint8_t const *)(mode_is_wired() ? &desc_wired : &desc_wireless);
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    static uint8_t desc_wireless[CONFIG_TOTAL_LEN_WIRELESS];
    static uint8_t desc_wired[CONFIG_TOTAL_LEN_WIRED];
    static bool built = false;
    uint8_t *p;
    (void)index;
    if (!built) {
        built = true;
        p = desc_wireless;
        memcpy(p, (uint8_t[]){ TUD_CONFIG_DESCRIPTOR(
                     1, ITF_NUM_TOTAL_WIRELESS, 0,
                     CONFIG_TOTAL_LEN_WIRELESS, 0x80, 100) },
               TUD_CONFIG_DESC_LEN);
        p += TUD_CONFIG_DESC_LEN;
        memcpy(p, (uint8_t[]){ TUD_CDC_DESCRIPTOR(
                     ITF_NUM_CDC, 0, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, 64) },
               TUD_CDC_DESC_LEN);
        p = desc_wired;
        memcpy(p, (uint8_t[]){ TUD_CONFIG_DESCRIPTOR(
                     1, ITF_NUM_TOTAL_WIRED, 0,
                     CONFIG_TOTAL_LEN_WIRED, 0x80, 100) },
               TUD_CONFIG_DESC_LEN);
        p += TUD_CONFIG_DESC_LEN;
        memcpy(p, (uint8_t[]){ TUD_HID_DESCRIPTOR(
                     ITF_NUM_HID_WIRED, 0, HID_ITF_PROTOCOL_NONE,
                     sizeof(wire_report_desc), WIRE_EP_IN,
                     CFG_TUD_HID_EP_BUFSIZE, WIRE_EP_INTERVAL) },
               TUD_HID_DESC_LEN);
        p += TUD_HID_DESC_LEN;
        memcpy(p, (uint8_t[]){ TUD_CDC_DESCRIPTOR(
                     ITF_NUM_CDC_WIRED, 0, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, 64) },
               TUD_CDC_DESC_LEN);
    }
    return mode_is_wired() ? desc_wired : desc_wireless;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    /* 文字列の実体は SRAM に置く。リテラルのままだとフラッシュに置かれ、
     * USB の DMA が読む最中に BOOTSEL 読みが CS を触ると止まる。 */
    static uint16_t desc_str[32];
    static char text_buf[32];
    const char *text = NULL;
    size_t len = 0;
    size_t i;
    (void)langid;
    wire_make_serial();
    if (index == 0) {
        desc_str[0] = (TUSB_DESC_STRING << 8) | (2u * 1u + 2u);
        desc_str[1] = 0x0409u;
        return desc_str;
    }
    if (mode_is_wired()) {
        if (index == 1) {
            text = "HORI CO.,LTD.";
        } else if (index == 2) {
            text = "POKKEN TOURNAMENT DX";
        } else if (index == 3) {
            text = (const char *)serial_str;
        }
    } else {
        if (index == 1) {
            text = "Raspberry Pi";
        } else if (index == 2) {
            text = "Pico 2 W wakecon";
        } else if (index == 3) {
            text = (const char *)serial_str;
        }
    }
    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    if (len > 31u) {
        len = 31u;
    }
    memcpy(text_buf, text, len);
    text_buf[len] = 0;
    for (i = 0; i < len; i++) {
        desc_str[1 + i] = (uint16_t)text_buf[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * len + 2u));
    return desc_str;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return wire_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    /* Switch 側からの取得要求には現状をそのまま返す。 */
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)reqlen;
    buffer[0] = (uint8_t)(probe_pc_buttons & 0xFFu);
    buffer[1] = (uint8_t)((probe_pc_buttons >> 8) & 0xFFu);
    buffer[2] = (uint8_t)((probe_pc_hat & 0x0Fu) | 0x00u);
    buffer[3] = probe_pc_lx;
    buffer[4] = probe_pc_ly;
    buffer[5] = probe_pc_rx;
    buffer[6] = probe_pc_ry;
    buffer[7] = 0u;
    return WIRE_REPORT_LEN;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    /* 有線報告に Switch からの出力は無い。受けて捨てる。 */
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/* ---- 報告の送信 ---- */

void wire_send_if_changed(void)
{
    static uint8_t last[WIRE_REPORT_LEN] = {
        0u, 0u, 8u, 0x80u, 0x80u, 0x80u, 0x80u, 0u,
    };
    uint8_t rep[WIRE_REPORT_LEN];
    if (!mode_is_wired()) {
        return;
    }
    if (!tud_mounted()) {
        return;
    }
    if (!tud_hid_ready()) {
        return;
    }
    rep[0] = (uint8_t)(probe_pc_buttons & 0xFFu);
    rep[1] = (uint8_t)((probe_pc_buttons >> 8) & 0xFFu);
    rep[2] = (uint8_t)(probe_pc_hat & 0x0Fu);
    rep[3] = probe_pc_lx;
    rep[4] = probe_pc_ly;
    rep[5] = probe_pc_rx;
    rep[6] = probe_pc_ry;
    rep[7] = 0u;
    if (memcmp(rep, last, sizeof(rep)) == 0) {
        return;
    }
    memcpy(last, rep, sizeof(rep));
    tud_hid_report(0, rep, sizeof(rep));
}

void wire_task(void)
{
    /* 初期化前は触らない。未初期化のまま tud_task へ入ると落ちる。 */
    if (!tud_inited()) {
        return;
    }
    tud_task();
    wire_send_if_changed();
}

/* ---- USB CDC の簡易ドライバ ----
 * SDK の stdio_usb は使わない。記述子を自前（モード別）にするため、
 * SDK の CDC 記述子と衝突するからである。読み書きの口だけ自前で持つ。
 *
 * 有線でも CDC を出す。USB を PC に繋いだときに操作口として使う
 * （W 0 で無線へ戻す）。Switch に繋いだときは相手が CDC を使わない
 * ため、出力は DTR のあるときだけ行う。DTR が無いのに送ると、
 * FIFO が詰まって 1 行ごとに待ちが発生する。 */

static bool cdc_usable(void)
{
    if (!tud_inited()) {
        return false;
    }
    if (!tud_mounted()) {
        return false;
    }
    if (!mode_is_wired()) {
        return true;
    }
    /* 有線で Switch に繋いだときは DTR が立たない。PC の端末は立てる
     * （pyserial は open 時に立てる）。Switch 相手に送らないための guard。 */
    return tud_cdc_connected();
}

static bool cdc_readable(void)
{
    if (!tud_inited()) {
        return false;
    }
    if (!tud_mounted()) {
        return false;
    }
    /* 読む側は DTR を問わない。届いたものだけ拾う。 */
    return true;
}

static void cdc_out_chars(const char *buf, int length)
{
    absolute_time_t deadline = make_timeout_time_ms(100);
    int i = 0;
    if (!cdc_usable()) {
        return;
    }
    while (i < length) {
        uint32_t avail;
        if (!cdc_usable()) {
            return;
        }
        tud_task();
        avail = tud_cdc_write_available();
        if (avail == 0u) {
            if (time_reached(deadline)) {
                return;
            }
            continue;
        }
        i += (int)tud_cdc_write(buf + i, (uint32_t)(length - i));
        tud_cdc_write_flush();
    }
}

static void cdc_out_flush(void)
{
    if (!cdc_usable()) {
        return;
    }
    tud_task();
    tud_cdc_write_flush();
}

static int cdc_in_chars(char *buf, int length)
{
    uint32_t n;
    if (!cdc_readable()) {
        return PICO_ERROR_NO_DATA;
    }
    tud_task();
    if (!tud_cdc_available()) {
        return PICO_ERROR_NO_DATA;
    }
    n = tud_cdc_read((uint8_t *)buf, (uint32_t)length);
    return n != 0u ? (int)n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t cdc_stdio_driver = {
    .out_chars = cdc_out_chars,
    .out_flush = cdc_out_flush,
    .in_chars = cdc_in_chars,
};

void wire_stdio_init(void)
{
    stdio_set_driver_enabled(&cdc_stdio_driver, true);
}

/* TinyUSB デバイスを開始する。stdio_usb を使わないため自前で呼ぶ。
 * 呼ばないと列挙も tud_task も動かず、USB が無反応になる。
 * ui.h は btstack.h を引いて TinyUSB と衝突するため、結果だけ返して
 * 表示は呼び出し側に任せる。 */
bool wire_usb_init(void)
{
    if (tud_inited()) {
        return true;
    }
    return tud_init(0);
}
