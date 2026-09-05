#include <stdbool.h>
#include <stdint.h>
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "app_common.h"

#define APP_UART_ID uart0

void uart_put_line(const char *text) {
    uart_puts(APP_UART_ID, text);
    uart_puts(APP_UART_ID, "\r\n");
}

uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

bool deadline_active(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) < 0;
}
