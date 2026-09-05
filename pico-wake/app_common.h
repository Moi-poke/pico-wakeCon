#ifndef APP_COMMON_H
#define APP_COMMON_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void uart_put_line(const char *text);
uint32_t now_ms(void);
bool deadline_active(uint32_t now, uint32_t deadline);
#ifdef __cplusplus
}
#endif
#endif
