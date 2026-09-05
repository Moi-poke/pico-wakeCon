#ifndef APP_PORTS_STORE_H
#define APP_PORTS_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include "btstack_tlv.h"
#include "switch2_wake_store.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const btstack_tlv_t *tlv_impl;
extern void *tlv_context;
extern const switch2_wake_store_port_t store_port;
uint32_t store_tlv_get(void *ctx, uint32_t tag, uint8_t *out, uint32_t out_size);
bool store_tlv_store(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size);
#ifdef __cplusplus
}
#endif
#endif
