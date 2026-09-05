#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_adv.h"

static uint8_t last_data[SWITCH2_WAKE_ADV_SIZE];
static uint8_t last_len;
static int enable_calls;
static bool last_enabled;

static int fake_set_params(void *context, uint16_t interval_min, uint16_t interval_max) {
    (void)context;
    (void)interval_min;
    (void)interval_max;
    return 0;
}

static int fake_set_data(void *context, const uint8_t *data, uint8_t length) {
    (void)context;
    assert(data != NULL);
    assert(length == SWITCH2_WAKE_ADV_SIZE);
    memcpy(last_data, data, length);
    last_len = length;
    return 0;
}

static int fake_enable(void *context, bool enabled) {
    (void)context;
    enable_calls++;
    last_enabled = enabled;
    return 0;
}

static switch2_wake_adv_port_t make_port(void) {
    switch2_wake_adv_port_t port;
    memset(&port, 0, sizeof(port));
    port.context = NULL;
    port.set_params = fake_set_params;
    port.set_data = fake_set_data;
    port.enable = fake_enable;
    return port;
}

static void reset_fakes(void) {
    memset(last_data, 0, sizeof(last_data));
    last_len = 0u;
    enable_calls = 0;
    last_enabled = false;
}

int main(void) {
    switch2_wake_adv_t adv;
    switch2_wake_adv_port_t port = make_port();

    /* 1. init NULL adv -> INVALID; missing enable -> INVALID */
    assert(switch2_wake_adv_init(NULL, &port) == WAKE_ADV_RESULT_INVALID);
    {
        switch2_wake_adv_port_t bad = make_port();
        bad.enable = NULL;
        assert(switch2_wake_adv_init(&adv, &bad) == WAKE_ADV_RESULT_INVALID);
    }

    assert(switch2_wake_adv_init(&adv, &port) == WAKE_ADV_RESULT_OK);

    /* 2. apply NONE -> OK (no enable calls) */
    reset_fakes();
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_NONE) == WAKE_ADV_RESULT_OK);
    assert(enable_calls == 0);

    /* 3. ADV_WAKE without peer -> NO_PEER */
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_WAKE) == WAKE_ADV_RESULT_NO_PEER);

    /* 4. set_peer + ADV_WAKE -> payload checks + is_wake */
    {
        const uint8_t peer[6] = {1u, 2u, 3u, 4u, 5u, 6u};
        assert(switch2_wake_adv_set_peer(&adv, peer) == WAKE_ADV_RESULT_OK);
        reset_fakes();
        assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_WAKE) == WAKE_ADV_RESULT_OK);
        assert(last_data[16] == 0x81u);
        assert(last_data[17] == 6u && last_data[22] == 1u);
        assert(switch2_wake_adv_is_wake(&adv) == true);
    }

    /* 5. ADV_STANDARD -> OK, is_wake false */
    reset_fakes();
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STANDARD) == WAKE_ADV_RESULT_OK);
    assert(switch2_wake_adv_is_wake(&adv) == false);

    /* 6. (STANDARD|WAKE) -> CONFLICT */
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STANDARD | WAKE_ACTION_ADV_WAKE)
           == WAKE_ADV_RESULT_CONFLICT);

    /* 7. publish_reconnect without peer -> NO_PEER; with peer -> OK */
    {
        switch2_wake_adv_t adv2;
        assert(switch2_wake_adv_init(&adv2, &port) == WAKE_ADV_RESULT_OK);
        assert(switch2_wake_adv_publish_reconnect(&adv2) == WAKE_ADV_RESULT_NO_PEER);
        {
            const uint8_t peer[6] = {1u, 2u, 3u, 4u, 5u, 6u};
            const uint8_t *payload;
            assert(switch2_wake_adv_set_peer(&adv2, peer) == WAKE_ADV_RESULT_OK);
            reset_fakes();
            assert(switch2_wake_adv_publish_reconnect(&adv2) == WAKE_ADV_RESULT_OK);
            payload = switch2_wake_adv_payload(&adv2);
            assert(payload != NULL);
            assert(payload[16] == 0x00u);
            assert(switch2_wake_adv_is_wake(&adv2) == false);
        }
    }

    /* 8. ADV_STOP -> OK, enabled false */
    reset_fakes();
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STOP) == WAKE_ADV_RESULT_OK);
    assert(last_enabled == false);
    assert(adv.enabled == false);

    printf("adv ok\n");
    return 0;
}
