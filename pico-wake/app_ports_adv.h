#ifndef APP_PORTS_ADV_H
#define APP_PORTS_ADV_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int port_set_params(void *context, uint16_t min, uint16_t max);
int port_set_data(void *context, const uint8_t *data, uint8_t length);
int port_enable(void *context, bool enabled);
#ifdef __cplusplus
}
#endif
#endif
