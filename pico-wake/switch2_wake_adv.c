#include "switch2_wake_adv.h"

#include <string.h>

static const uint8_t standard_payload[SWITCH2_WAKE_ADV_SIZE] = {
    0x02, 0x01, 0x06,
    0x1b, 0xff, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7e, 0x05, 0x69, 0x20,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool valid_port(const switch2_wake_adv_port_t *port) {
    return port != NULL && port->set_params != NULL &&
           port->set_data != NULL && port->enable != NULL;
}

static void make_standard(switch2_wake_adv_t *adv) {
    memcpy(adv->payload, standard_payload, sizeof(adv->payload));
    adv->wake_mode = false;
}

static switch2_wake_adv_result_t make_addressed(switch2_wake_adv_t *adv, uint8_t type) {
    size_t i;
    if (!adv->peer_known) return WAKE_ADV_RESULT_NO_PEER;
    memcpy(adv->payload, standard_payload, sizeof(adv->payload));
    adv->payload[SWITCH2_WAKE_ADV_TYPE_OFFSET] = type;
    for (i = 0u; i < sizeof(adv->peer); ++i) {
        adv->payload[SWITCH2_WAKE_ADV_PEER_OFFSET + i] =
            adv->peer[sizeof(adv->peer) - 1u - i];
    }
    adv->wake_mode = type == SWITCH2_WAKE_ADV_TYPE_WAKE;
    return WAKE_ADV_RESULT_OK;
}

static switch2_wake_adv_result_t make_wake(switch2_wake_adv_t *adv) {
    return make_addressed(adv, SWITCH2_WAKE_ADV_TYPE_WAKE);
}

static switch2_wake_adv_result_t publish(switch2_wake_adv_t *adv) {
    if (adv->port.set_params(adv->port.context, SWITCH2_WAKE_ADV_INTERVAL,
                             SWITCH2_WAKE_ADV_INTERVAL) != 0) {
        return WAKE_ADV_RESULT_PORT_ERROR;
    }
    if (adv->port.set_data(adv->port.context, adv->payload,
                           SWITCH2_WAKE_ADV_SIZE) != 0) {
        return WAKE_ADV_RESULT_PORT_ERROR;
    }
    if (adv->port.enable(adv->port.context, true) != 0) {
        return WAKE_ADV_RESULT_PORT_ERROR;
    }
    adv->enabled = true;
    adv->apply_count++;
    return WAKE_ADV_RESULT_OK;
}

switch2_wake_adv_result_t switch2_wake_adv_init(
    switch2_wake_adv_t *adv, const switch2_wake_adv_port_t *port) {
    if (adv == NULL || !valid_port(port)) {
        return WAKE_ADV_RESULT_INVALID;
    }
    memset(adv, 0, sizeof(*adv));
    adv->port = *port;
    make_standard(adv);
    return WAKE_ADV_RESULT_OK;
}

switch2_wake_adv_result_t switch2_wake_adv_set_peer(
    switch2_wake_adv_t *adv, const uint8_t peer[6]) {
    if (adv == NULL || peer == NULL) {
        return WAKE_ADV_RESULT_INVALID;
    }
    memcpy(adv->peer, peer, sizeof(adv->peer));
    adv->peer_known = true;
    return WAKE_ADV_RESULT_OK;
}

void switch2_wake_adv_clear_peer(switch2_wake_adv_t *adv) {
    if (adv == NULL) {
        return;
    }
    memset(adv->peer, 0, sizeof(adv->peer));
    adv->peer_known = false;
    make_standard(adv);
}

switch2_wake_adv_result_t switch2_wake_adv_apply(
    switch2_wake_adv_t *adv, uint32_t actions) {
    const uint32_t adv_actions = actions &
        (WAKE_ACTION_ADV_STANDARD | WAKE_ACTION_ADV_WAKE |
         WAKE_ACTION_ADV_STOP);
    switch2_wake_adv_result_t result;
    if (adv == NULL) {
        return WAKE_ADV_RESULT_INVALID;
    }
    if (adv_actions == WAKE_ACTION_NONE) {
        return WAKE_ADV_RESULT_OK;
    }
    if ((adv_actions & (adv_actions - 1u)) != 0u) {
        return WAKE_ADV_RESULT_CONFLICT;
    }
    if (adv_actions == WAKE_ACTION_ADV_STOP) {
        if (adv->port.enable(adv->port.context, false) != 0) {
            return WAKE_ADV_RESULT_PORT_ERROR;
        }
        adv->enabled = false;
        adv->wake_mode = false;
        adv->apply_count++;
        return WAKE_ADV_RESULT_OK;
    }
    if (adv_actions == WAKE_ACTION_ADV_WAKE) {
        result = make_wake(adv);
        if (result != WAKE_ADV_RESULT_OK) {
            return result;
        }
    } else {
        make_standard(adv);
    }
    return publish(adv);
}

switch2_wake_adv_result_t switch2_wake_adv_publish_reconnect(
    switch2_wake_adv_t *adv) {
    switch2_wake_adv_result_t result;
    if (adv == NULL) return WAKE_ADV_RESULT_INVALID;
    result = make_addressed(adv, SWITCH2_WAKE_ADV_TYPE_NORMAL);
    if (result != WAKE_ADV_RESULT_OK) return result;
    return publish(adv);
}

const uint8_t *switch2_wake_adv_payload(const switch2_wake_adv_t *adv) {
    return adv == NULL ? NULL : adv->payload;
}

bool switch2_wake_adv_is_wake(const switch2_wake_adv_t *adv) {
    return adv != NULL && adv->wake_mode;
}
