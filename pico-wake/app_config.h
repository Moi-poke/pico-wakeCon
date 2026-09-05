#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#include <stdint.h>
#include "switch2_wake.h"
#include "switch2_wake_store.h"
#ifdef __cplusplus
extern "C" {
#endif
switch2_wake_config_t app_make_config(void);
const char *app_meta_state_name(uint8_t state);
#define APP_UART_ID uart0
#define APP_UART_TX_PIN 0
#define APP_UART_RX_PIN 1
#define APP_UART_BAUD 115200
#define APP_POLL_MS 1
#define APP_BUTTON_POLL_MS 10
#define APP_FORGET_CONFIRM_MS 5000u
#ifdef __cplusplus
}
#endif
#endif
