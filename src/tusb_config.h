/* TinyUSB の設定。本プロジェクト用。
 *
 * SDK の既定（pico_stdio_usb 付属）は CDC のみであり、HID が要る本
 * プロジェクトではそのまま使えない。include 順でこちらが優先される
 * ため（-I src が先頭）、全 TU でこの設定になる。
 * 中身は SDK の設定を土台に HID を足したものである。 */

#ifndef WAKECON_TUSB_CONFIG_H
#define WAKECON_TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)

#define CFG_TUD_CDC (1)

// CDC FIFO size of TX and RX
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

// CDC Endpoint transfer buffer size, more is faster
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

#define CFG_TUD_HID (1)

#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE 64
#endif

// We use a vendor specific interface but with our own driver
#define CFG_TUD_VENDOR (0)

#endif
