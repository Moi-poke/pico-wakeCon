#ifndef SWITCH2_WAKE_H
#define SWITCH2_WAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWITCH2_WAKE_RESPONSE_MAX 25u
#define SWITCH2_WAKE_META_SCHEMA 1u
#define SWITCH2_WAKE_GENERATION_ANY 0u

typedef enum {
    WAKE_STATE_BOOT = 0,
    WAKE_STATE_UNBONDED,
    WAKE_STATE_PAIR_ADVERTISING,
    WAKE_STATE_PAIR_EXCHANGE,
    WAKE_STATE_BOND_PENDING,
    WAKE_STATE_WAIT_RECONNECT,
    WAKE_STATE_VERIFY_INIT,
    WAKE_STATE_READY,
    WAKE_STATE_WAKE_ADVERTISING,
    WAKE_STATE_WAKE_VERIFY,
    WAKE_STATE_ERROR_RECOVERY,
    WAKE_STATE_COUNT
} wake_state_t;

typedef enum {
    WAKE_EVENT_STACK_WORKING = 0,
    WAKE_EVENT_START,
    WAKE_EVENT_PAIR_REQUEST,
    WAKE_EVENT_IMPORT_BOND,
    WAKE_EVENT_WAKE_REQUEST,
    WAKE_EVENT_FORGET,
    WAKE_EVENT_LE_CONNECTED,
    WAKE_EVENT_LE_DISCONNECTED,
    WAKE_EVENT_ATT_WRITE,
    WAKE_EVENT_CCCD_WRITE,
    WAKE_EVENT_ENCRYPTED,
    WAKE_EVENT_TIMER,
    WAKE_EVENT_BT_ERROR,
    WAKE_EVENT_COUNT
} wake_event_type_t;

typedef enum {
    WAKE_SOURCE_INTERNAL = 0,
    WAKE_SOURCE_UART,
    WAKE_SOURCE_BUTTON,
    WAKE_SOURCE_BLE,
    WAKE_SOURCE_TIMER
} wake_source_t;

typedef enum {
    WAKE_RESULT_OK = 0,
    WAKE_RESULT_ACCEPTED,
    WAKE_RESULT_BUSY,
    WAKE_RESULT_NO_BOND,
    WAKE_RESULT_CONNECTED,
    WAKE_RESULT_INVALID,
    WAKE_RESULT_NOT_ARMED,
    WAKE_RESULT_STORAGE_ERROR,
    WAKE_RESULT_PROTOCOL_ERROR
} wake_result_t;

typedef enum {
    WAKE_TIMER_NONE = 0,
    WAKE_TIMER_PAIR_ADVERTISING,
    WAKE_TIMER_PAIR_EXCHANGE,
    WAKE_TIMER_RECONNECT,
    WAKE_TIMER_VERIFY_INIT,
    WAKE_TIMER_WAKE_ADVERTISING,
    WAKE_TIMER_WAKE_VERIFY
} wake_timer_kind_t;

typedef enum {
    WAKE_META_NONE = 0,
    WAKE_META_PENDING,
    WAKE_META_VERIFIED
} wake_meta_state_t;

enum {
    WAKE_ACTION_NONE = 0,
    WAKE_ACTION_ADV_STANDARD = 1u << 0,
    WAKE_ACTION_ADV_WAKE = 1u << 1,
    WAKE_ACTION_ADV_STOP = 1u << 2,
    WAKE_ACTION_DISCONNECT = 1u << 3,
    WAKE_ACTION_ERASE_BOND = 1u << 4,
    WAKE_ACTION_STORE_PENDING = 1u << 5,
    WAKE_ACTION_STORE_VERIFIED = 1u << 6,
    WAKE_ACTION_NOTIFY = 1u << 7
};

typedef struct {
    uint32_t pair_advertising_ms;
    uint32_t pair_exchange_ms;
    uint32_t reconnect_ms;
    uint32_t verify_init_ms;
    uint32_t wake_advertising_ms;
    uint32_t wake_verify_ms;
} switch2_wake_config_t;

typedef struct {
    uint8_t schema;
    wake_meta_state_t state;
    wake_source_t source;
    uint8_t local_identity[6];
    uint8_t peer_type;
    uint8_t peer_identity[6];
} switch2_wake_meta_t;

typedef struct {
    wake_event_type_t type;
    wake_source_t source;
    uint32_t generation;
    uint32_t now_ms;
    bool flag;
    uint16_t handle;
    const uint8_t *data;
    size_t data_len;
} switch2_wake_event_t;

typedef struct {
    wake_state_t state;
    wake_timer_kind_t timer_kind;
    uint32_t connection_generation;
    uint32_t timer_deadline_ms;
    uint32_t wake_requested;
    uint32_t wake_started;
    uint32_t wake_rejected;
    uint32_t wake_timeout;
    uint32_t pair_attempt;
    uint32_t pair_complete;
    uint32_t encrypt_ok;
    uint32_t stale_event;
    bool connected;
    bool encrypted;
    bool cccd_basic;
    bool peer_known;
    bool timer_armed;
    bool wake_extension_used;
    bool response_pending;
} switch2_wake_snapshot_t;

wake_result_t switch2_wake_init(const switch2_wake_config_t *config);
wake_result_t switch2_wake_dispatch(const switch2_wake_event_t *event,
                                    uint32_t *actions);
wake_result_t switch2_wake_start(bool verified_bond, uint32_t now_ms,
                                 uint32_t *actions);
wake_result_t switch2_wake_request(wake_source_t source, uint32_t now_ms,
                                   uint32_t *actions);
wake_result_t switch2_wake_pair_request(wake_source_t source, uint32_t now_ms,
                                        uint32_t *actions);
wake_result_t switch2_wake_forget(wake_source_t source, uint32_t *actions);
wake_result_t switch2_wake_tick(uint32_t now_ms, uint32_t *actions);
wake_result_t switch2_wake_snapshot(switch2_wake_snapshot_t *out);
const char *switch2_wake_state_name(wake_state_t state);

#ifdef SWITCH2_WAKE_TEST
bool switch2_wake_test_secrets_zero(void);
void switch2_wake_test_fill_secrets(uint8_t value);
#endif

#ifdef __cplusplus
}
#endif

#endif
