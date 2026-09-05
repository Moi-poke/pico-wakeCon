#include "switch2_wake.h"
#include "switch2_wake_util.h"

#include <string.h>

typedef struct {
    switch2_wake_config_t config;
    switch2_wake_snapshot_t public_state;
    uint8_t a1[16];
    uint8_t a2[16];
    uint8_t candidate_ltk[16];
    uint8_t response[SWITCH2_WAKE_RESPONSE_MAX];
    size_t response_len;
    uint32_t response_generation;
    int le_db_index;
    bool initialized;
} wake_context_t;

static wake_context_t ctx;

static const char *const state_names[WAKE_STATE_COUNT] = {
    "BOOT", "UNBONDED", "PAIR_ADVERTISING", "PAIR_EXCHANGE",
    "BOND_PENDING", "WAIT_RECONNECT", "VERIFY_INIT", "READY",
    "WAKE_ADVERTISING", "WAKE_VERIFY", "ERROR_RECOVERY"
};

static void clear_secrets(void) {
    switch2_wake_secure_zero(ctx.a1, sizeof(ctx.a1));
    switch2_wake_secure_zero(ctx.a2, sizeof(ctx.a2));
    switch2_wake_secure_zero(ctx.candidate_ltk, sizeof(ctx.candidate_ltk));
    switch2_wake_secure_zero(ctx.response, sizeof(ctx.response));
    ctx.response_len = 0u;
    ctx.response_generation = 0u;
    ctx.public_state.response_pending = false;
}

static bool valid_config(const switch2_wake_config_t *config) {
    return config != NULL && config->pair_advertising_ms > 0u &&
           config->pair_exchange_ms > 0u && config->reconnect_ms > 0u &&
           config->verify_init_ms > 0u && config->wake_advertising_ms > 0u &&
           config->wake_verify_ms > 0u;
}

static void disarm_timer(void) {
    ctx.public_state.timer_kind = WAKE_TIMER_NONE;
    ctx.public_state.timer_deadline_ms = 0u;
    ctx.public_state.timer_armed = false;
    ctx.public_state.wake_extension_used = false;
}

static void arm_timer(wake_timer_kind_t kind, uint32_t now_ms,
                      uint32_t duration_ms) {
    ctx.public_state.timer_kind = kind;
    ctx.public_state.timer_deadline_ms = now_ms + duration_ms;
    ctx.public_state.timer_armed = true;
}

static void enter_state(wake_state_t state) {
    ctx.public_state.state = state;
}

static bool event_is_stale(const switch2_wake_event_t *event) {
    return event->generation != SWITCH2_WAKE_GENERATION_ANY &&
           event->generation != ctx.public_state.connection_generation;
}

wake_result_t switch2_wake_init(const switch2_wake_config_t *config) {
    if (!valid_config(config)) {
        return WAKE_RESULT_INVALID;
    }
    switch2_wake_secure_zero(&ctx, sizeof(ctx));
    ctx.config = *config;
    ctx.le_db_index = -1;
    ctx.public_state.state = WAKE_STATE_BOOT;
    ctx.public_state.timer_kind = WAKE_TIMER_NONE;
    ctx.initialized = true;
    return WAKE_RESULT_OK;
}

wake_result_t switch2_wake_dispatch(const switch2_wake_event_t *event,
                                    uint32_t *actions) {
    wake_state_t state;
    if (!ctx.initialized || event == NULL || actions == NULL ||
        event->type >= WAKE_EVENT_COUNT || event->source > WAKE_SOURCE_TIMER) {
        return WAKE_RESULT_INVALID;
    }
    *actions = WAKE_ACTION_NONE;
    if (event_is_stale(event)) {
        ctx.public_state.stale_event++;
        return WAKE_RESULT_OK;
    }
    state = ctx.public_state.state;

    if (event->type == WAKE_EVENT_FORGET) {
        clear_secrets();
        disarm_timer();
        ctx.public_state.connected = false;
        ctx.public_state.encrypted = false;
        ctx.public_state.cccd_basic = false;
        ctx.public_state.peer_known = false;
        enter_state(WAKE_STATE_UNBONDED);
        *actions = WAKE_ACTION_ADV_STOP | WAKE_ACTION_ERASE_BOND;
        return WAKE_RESULT_OK;
    }
    if (event->type == WAKE_EVENT_BT_ERROR) {
        clear_secrets();
        disarm_timer();
        enter_state(WAKE_STATE_ERROR_RECOVERY);
        *actions = WAKE_ACTION_ADV_STOP | WAKE_ACTION_DISCONNECT;
        return WAKE_RESULT_PROTOCOL_ERROR;
    }

    switch (state) {
        case WAKE_STATE_BOOT:
            if (event->type != WAKE_EVENT_STACK_WORKING &&
                event->type != WAKE_EVENT_START) {
                return WAKE_RESULT_BUSY;
            }
            ctx.public_state.peer_known = event->flag;
            enter_state(event->flag ? WAKE_STATE_READY : WAKE_STATE_UNBONDED);
            if (!event->flag) {
                *actions = WAKE_ACTION_ADV_STANDARD;
            }
            return WAKE_RESULT_OK;

        case WAKE_STATE_UNBONDED:
            if (event->type == WAKE_EVENT_WAKE_REQUEST) {
                ctx.public_state.wake_requested++;
                ctx.public_state.wake_rejected++;
             return WAKE_RESULT_NO_BOND;
            }
            if (event->type == WAKE_EVENT_PAIR_REQUEST) {
                ctx.public_state.pair_attempt++;
                enter_state(WAKE_STATE_PAIR_ADVERTISING);
                arm_timer(WAKE_TIMER_PAIR_ADVERTISING, event->now_ms,
                          ctx.config.pair_advertising_ms);
                *actions = WAKE_ACTION_ADV_STANDARD;
                return WAKE_RESULT_ACCEPTED;
            }
            if (event->type == WAKE_EVENT_IMPORT_BOND) {
                ctx.public_state.peer_known = true;
                enter_state(WAKE_STATE_BOND_PENDING);
                arm_timer(WAKE_TIMER_RECONNECT, event->now_ms,
                          ctx.config.reconnect_ms);
                *actions = WAKE_ACTION_STORE_PENDING | WAKE_ACTION_ADV_STANDARD;
                return WAKE_RESULT_ACCEPTED;
            }
            return WAKE_RESULT_BUSY;

        case WAKE_STATE_PAIR_ADVERTISING:
            if (event->type == WAKE_EVENT_LE_CONNECTED) {
                disarm_timer();
                ctx.public_state.connection_generation++;
                ctx.public_state.connected = true;
                enter_state(WAKE_STATE_PAIR_EXCHANGE);
                arm_timer(WAKE_TIMER_PAIR_EXCHANGE, event->now_ms,
                          ctx.config.pair_exchange_ms);
                return WAKE_RESULT_ACCEPTED;
            }
            break;

        case WAKE_STATE_PAIR_EXCHANGE:
            if (event->type == WAKE_EVENT_ATT_WRITE ||
                event->type == WAKE_EVENT_CCCD_WRITE) {
                return WAKE_RESULT_ACCEPTED;
            }
            if (event->type == WAKE_EVENT_LE_DISCONNECTED) {
                clear_secrets();
                disarm_timer();
                ctx.public_state.connected = false;
                enter_state(WAKE_STATE_UNBONDED);
                *actions = WAKE_ACTION_ADV_STANDARD;
                return WAKE_RESULT_OK;
            }
            break;

        case WAKE_STATE_BOND_PENDING:
        case WAKE_STATE_WAIT_RECONNECT:
            if (event->type == WAKE_EVENT_LE_CONNECTED) {
                ctx.public_state.connection_generation++;
                ctx.public_state.connected = true;
                return WAKE_RESULT_ACCEPTED;
            }
            if (event->type == WAKE_EVENT_ENCRYPTED &&
                ctx.public_state.connected) {
                disarm_timer();
                ctx.public_state.encrypted = true;
                ctx.public_state.encrypt_ok++;
                enter_state(WAKE_STATE_VERIFY_INIT);
                arm_timer(WAKE_TIMER_VERIFY_INIT, event->now_ms,
                          ctx.config.verify_init_ms);
                return WAKE_RESULT_ACCEPTED;
            }
            break;

        case WAKE_STATE_VERIFY_INIT:
            if (event->type == WAKE_EVENT_ATT_WRITE && event->flag &&
                ctx.public_state.encrypted) {
                disarm_timer();
                clear_secrets();
                ctx.public_state.peer_known = true;
                ctx.public_state.pair_complete++;
                enter_state(WAKE_STATE_READY);
                *actions = WAKE_ACTION_NOTIFY | WAKE_ACTION_STORE_VERIFIED;
                return WAKE_RESULT_OK;
            }
            break;

        case WAKE_STATE_READY:
            if (event->type == WAKE_EVENT_WAKE_REQUEST) {
                ctx.public_state.wake_requested++;
                if (ctx.public_state.connected) {
                    ctx.public_state.wake_rejected++;
                    return WAKE_RESULT_CONNECTED;
                }
                if (!ctx.public_state.peer_known) {
                    ctx.public_state.wake_rejected++;
                    return WAKE_RESULT_NO_BOND;
                }
                ctx.public_state.wake_started++;
                enter_state(WAKE_STATE_WAKE_ADVERTISING);
                arm_timer(WAKE_TIMER_WAKE_ADVERTISING, event->now_ms,
                          ctx.config.wake_advertising_ms);
                *actions = WAKE_ACTION_ADV_WAKE;
                return WAKE_RESULT_ACCEPTED;
            }
            if (event->type == WAKE_EVENT_PAIR_REQUEST) {
                ctx.public_state.pair_attempt++;
                enter_state(WAKE_STATE_PAIR_ADVERTISING);
                arm_timer(WAKE_TIMER_PAIR_ADVERTISING, event->now_ms,
                          ctx.config.pair_advertising_ms);
                *actions = WAKE_ACTION_ADV_STANDARD;
                return WAKE_RESULT_ACCEPTED;
            }
            break;

        case WAKE_STATE_WAKE_ADVERTISING:
            if (event->type == WAKE_EVENT_WAKE_REQUEST) {
                ctx.public_state.wake_requested++;
                if (ctx.public_state.wake_extension_used) {
                    return WAKE_RESULT_BUSY;
                }
                ctx.public_state.wake_extension_used = true;
                ctx.public_state.timer_deadline_ms =
                    event->now_ms + ctx.config.wake_advertising_ms;
                return WAKE_RESULT_ACCEPTED;
            }
            if (event->type == WAKE_EVENT_LE_CONNECTED) {
                disarm_timer();
                ctx.public_state.connection_generation++;
                ctx.public_state.connected = true;
                enter_state(WAKE_STATE_WAKE_VERIFY);
                arm_timer(WAKE_TIMER_WAKE_VERIFY, event->now_ms,
                          ctx.config.wake_verify_ms);
                *actions = WAKE_ACTION_ADV_STOP;
                return WAKE_RESULT_ACCEPTED;
            }
            break;

        case WAKE_STATE_WAKE_VERIFY:
            if (event->type == WAKE_EVENT_ENCRYPTED ||
                event->type == WAKE_EVENT_ATT_WRITE) {
                disarm_timer();
                enter_state(WAKE_STATE_READY);
                return WAKE_RESULT_OK;
            }
            break;

        case WAKE_STATE_ERROR_RECOVERY:
            if (event->type == WAKE_EVENT_START) {
                enter_state(WAKE_STATE_BOOT);
                return WAKE_RESULT_OK;
            }
            break;

        default:
            return WAKE_RESULT_INVALID;
    }

    if (event->type == WAKE_EVENT_LE_DISCONNECTED) {
        clear_secrets();
        disarm_timer();
        ctx.public_state.connected = false;
        ctx.public_state.encrypted = false;
        ctx.public_state.cccd_basic = false;
        enter_state(ctx.public_state.peer_known ? WAKE_STATE_BOND_PENDING
                                                : WAKE_STATE_UNBONDED);
        *actions = WAKE_ACTION_ADV_STANDARD;
        return WAKE_RESULT_OK;
    }
    return WAKE_RESULT_BUSY;
}

wake_result_t switch2_wake_start(bool verified_bond, uint32_t now_ms,
                                 uint32_t *actions) {
    const switch2_wake_event_t event = {
        WAKE_EVENT_STACK_WORKING, WAKE_SOURCE_INTERNAL,
        SWITCH2_WAKE_GENERATION_ANY, now_ms, verified_bond, 0u, NULL, 0u
    };
    return switch2_wake_dispatch(&event, actions);
}

wake_result_t switch2_wake_request(wake_source_t source, uint32_t now_ms,
                                   uint32_t *actions) {
    const switch2_wake_event_t event = {
        WAKE_EVENT_WAKE_REQUEST, source, SWITCH2_WAKE_GENERATION_ANY,
        now_ms, false, 0u, NULL, 0u
    };
    return switch2_wake_dispatch(&event, actions);
}

wake_result_t switch2_wake_pair_request(wake_source_t source, uint32_t now_ms,
                                        uint32_t *actions) {
    const switch2_wake_event_t event = {
        WAKE_EVENT_PAIR_REQUEST, source, SWITCH2_WAKE_GENERATION_ANY,
        now_ms, false, 0u, NULL, 0u
    };
    return switch2_wake_dispatch(&event, actions);
}

wake_result_t switch2_wake_forget(wake_source_t source, uint32_t *actions) {
    const switch2_wake_event_t event = {
        WAKE_EVENT_FORGET, source, SWITCH2_WAKE_GENERATION_ANY,
        0u, false, 0u, NULL, 0u
    };
    return switch2_wake_dispatch(&event, actions);
}

wake_result_t switch2_wake_tick(uint32_t now_ms, uint32_t *actions) {
    switch2_wake_event_t event;
    wake_state_t state;
    if (!ctx.initialized || actions == NULL) {
        return WAKE_RESULT_INVALID;
    }
    *actions = WAKE_ACTION_NONE;
    if (!ctx.public_state.timer_armed ||
        (int32_t)(now_ms - ctx.public_state.timer_deadline_ms) < 0) {
        return WAKE_RESULT_OK;
    }
    event = (switch2_wake_event_t){
        WAKE_EVENT_TIMER, WAKE_SOURCE_TIMER,
        ctx.public_state.connection_generation, now_ms, false, 0u, NULL, 0u
    };
    state = ctx.public_state.state;
    clear_secrets();
    disarm_timer();
    switch (state) {
        case WAKE_STATE_PAIR_ADVERTISING:
        case WAKE_STATE_PAIR_EXCHANGE:
            enter_state(WAKE_STATE_UNBONDED);
            *actions = WAKE_ACTION_ADV_STANDARD | WAKE_ACTION_DISCONNECT;
            return WAKE_RESULT_OK;
        case WAKE_STATE_BOND_PENDING:
            enter_state(WAKE_STATE_UNBONDED);
            *actions = WAKE_ACTION_ADV_STANDARD;
            return WAKE_RESULT_OK;
        case WAKE_STATE_WAIT_RECONNECT:
            enter_state(WAKE_STATE_PAIR_ADVERTISING);
            arm_timer(WAKE_TIMER_PAIR_ADVERTISING, event.now_ms,
                      ctx.config.pair_advertising_ms);
            *actions = WAKE_ACTION_ADV_STANDARD;
            return WAKE_RESULT_OK;
        case WAKE_STATE_VERIFY_INIT:
            enter_state(WAKE_STATE_BOND_PENDING);
            arm_timer(WAKE_TIMER_RECONNECT, event.now_ms,
                      ctx.config.reconnect_ms);
            *actions = WAKE_ACTION_ADV_STANDARD | WAKE_ACTION_DISCONNECT;
            return WAKE_RESULT_OK;
        case WAKE_STATE_WAKE_ADVERTISING:
            ctx.public_state.wake_timeout++;
            enter_state(WAKE_STATE_READY);
            *actions = WAKE_ACTION_ADV_STANDARD;
            return WAKE_RESULT_OK;
        case WAKE_STATE_WAKE_VERIFY:
            ctx.public_state.wake_timeout++;
            ctx.public_state.connected = false;
            ctx.public_state.encrypted = false;
            enter_state(WAKE_STATE_READY);
            *actions = WAKE_ACTION_ADV_STANDARD | WAKE_ACTION_DISCONNECT;
            return WAKE_RESULT_OK;
        default:
            return WAKE_RESULT_OK;
    }
}

wake_result_t switch2_wake_snapshot(switch2_wake_snapshot_t *out) {
    if (!ctx.initialized || out == NULL) {
        return WAKE_RESULT_INVALID;
    }
    *out = ctx.public_state;
    return WAKE_RESULT_OK;
}

const char *switch2_wake_state_name(wake_state_t state) {
    if (state >= WAKE_STATE_COUNT) {
        return "INVALID";
    }
    return state_names[state];
}

#ifdef SWITCH2_WAKE_TEST
bool switch2_wake_test_secrets_zero(void) {
    size_t i;
    for (i = 0u; i < sizeof(ctx.a1); ++i) if (ctx.a1[i] != 0u) return false;
    for (i = 0u; i < sizeof(ctx.a2); ++i) if (ctx.a2[i] != 0u) return false;
    for (i = 0u; i < sizeof(ctx.candidate_ltk); ++i)
        if (ctx.candidate_ltk[i] != 0u) return false;
    return true;
}

void switch2_wake_test_fill_secrets(uint8_t value) {
    memset(ctx.a1, value, sizeof(ctx.a1));
    memset(ctx.a2, value, sizeof(ctx.a2));
    memset(ctx.candidate_ltk, value, sizeof(ctx.candidate_ltk));
}
#endif
