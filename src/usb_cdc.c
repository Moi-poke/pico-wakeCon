/* USB CDC コンソールの TinyUSB 接着部。日本語コメントを残す。 */
#include "tusb.h"
#include "usb_cdc.h"

bool usb_cdc_connected(void)
{
    return tud_cdc_connected();
}

int usb_cdc_read_char(void)
{
    uint8_t ch;
    if (tud_cdc_available() == 0u) {
        return -1;
    }
    if (tud_cdc_read(&ch, 1u) != 1u) {
        return -1;
    }
    return (int)ch;
}

void usb_cdc_write_line(const char *text)
{
    if (text == NULL || !tud_cdc_connected()) {
        return;
    }
    tud_cdc_write_str(text);
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}
