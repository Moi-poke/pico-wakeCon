#ifndef SWITCH2_WAKE_ADV_H
#define SWITCH2_WAKE_ADV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "switch2_wake.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWITCH2_WAKE_ADV_SIZE 31u
#define SWITCH2_WAKE_ADV_INTERVAL 0x0030u
#define SWITCH2_WAKE_ADV_TYPE_OFFSET 16u
#define SWITCH2_WAKE_ADV_PEER_OFFSET 17u
#define SWITCH2_WAKE_ADV_TYPE_NORMAL 0x00u
#define SWITCH2_WAKE_ADV_TYPE_WAKE 0x81u

typedef enum {
    WAKE_ADV_RESULT_OK = 0,
    WAKE_ADV_RESULT_INVALID,
    WAKE_ADV_RESULT_NO_PEER,
    WAKE_ADV_RESULT_PORT_ERROR,
    WAKE_ADV_RESULT_CONFLICT
} switch2_wake_adv_result_t;

typedef struct {
    void *context;
    int (*set_params)(void *context, uint16_t interval_min,
                      uint16_t interval_max);
    int (*set_data)(void *context, const uint8_t *data, uint8_t length);
    int (*enable)(void *context, bool enabled);
} switch2_wake_adv_port_t;

typedef struct {
    switch2_wake_adv_port_t port;
    uint8_t payload[SWITCH2_WAKE_ADV_SIZE];
    uint8_t peer[6];
    bool peer_known;
    bool enabled;
    bool wake_mode;
    uint32_t apply_count;
} switch2_wake_adv_t;

switch2_wake_adv_result_t switch2_wake_adv_init(
    switch2_wake_adv_t *adv, const switch2_wake_adv_port_t *port);
switch2_wake_adv_result_t switch2_wake_adv_set_peer(
    switch2_wake_adv_t *adv, const uint8_t peer[6]);
void switch2_wake_adv_clear_peer(switch2_wake_adv_t *adv);
switch2_wake_adv_result_t switch2_wake_adv_apply(
    switch2_wake_adv_t *adv, uint32_t actions);
/* ボンド済み再接続用: peerを載せるがtypeは0x00。 */
switch2_wake_adv_result_t switch2_wake_adv_publish_reconnect(
    switch2_wake_adv_t *adv);
const uint8_t *switch2_wake_adv_payload(const switch2_wake_adv_t *adv);
bool switch2_wake_adv_is_wake(const switch2_wake_adv_t *adv);

#ifdef __cplusplus
}
#endif

#endif
