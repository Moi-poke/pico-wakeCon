#include "app_config.h"
switch2_wake_config_t app_make_config(void) {
    const switch2_wake_config_t config = {
        60000u, 20000u, 30000u, 20000u, 5000u, 10000u
    };
    return config;
}
const char *app_meta_state_name(uint8_t state) {
    switch (state) {
    case WAKE_META_STATE_PENDING: return "PENDING";
    case WAKE_META_STATE_VERIFIED: return "VERIFIED";
    default: return "EMPTY";
    }
}
