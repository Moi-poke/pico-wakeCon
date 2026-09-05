#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake.h"

static switch2_wake_config_t make_config(void) {
    switch2_wake_config_t cfg;
    cfg.pair_advertising_ms = 60000u;
    cfg.pair_exchange_ms = 20000u;
    cfg.reconnect_ms = 30000u;
    cfg.verify_init_ms = 20000u;
    cfg.wake_advertising_ms = 5000u;
    cfg.wake_verify_ms = 10000u;
    return cfg;
}

static void make_event(switch2_wake_event_t *ev, wake_event_type_t type,
                       wake_source_t source, uint32_t generation, uint32_t now_ms) {
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->source = source;
    ev->generation = generation;
    ev->now_ms = now_ms;
    ev->flag = false;
    ev->handle = 0u;
    ev->data = NULL;
    ev->data_len = 0u;
}

int main(void) {
    switch2_wake_config_t cfg = make_config();
    uint32_t actions = 0u;
    switch2_wake_snapshot_t snap;

    /* 1. init NULL -> INVALID, zero config -> INVALID, NULL actions -> INVALID */
    assert(switch2_wake_init(NULL) == WAKE_RESULT_INVALID);
    {
        switch2_wake_config_t zero;
        switch2_wake_event_t ev;
        memset(&zero, 0, sizeof(zero));
        assert(switch2_wake_init(&zero) == WAKE_RESULT_INVALID);
        assert(switch2_wake_init(&cfg) == WAKE_RESULT_OK);
        make_event(&ev, WAKE_EVENT_WAKE_REQUEST, WAKE_SOURCE_UART,
                   SWITCH2_WAKE_GENERATION_ANY, 0u);
        assert(switch2_wake_dispatch(&ev, NULL) == WAKE_RESULT_INVALID);
    }

    /* 2. init valid -> OK, start(false) -> OK + ADV_STANDARD + UNBONDED */
    assert(switch2_wake_init(&cfg) == WAKE_RESULT_OK);
    actions = 0xFFFFFFFFu;
    assert(switch2_wake_start(false, 0u, &actions) == WAKE_RESULT_OK);
    assert(actions == WAKE_ACTION_ADV_STANDARD);
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.state == WAKE_STATE_UNBONDED);

    /* 3. pair_request -> ACCEPTED + PAIR_ADVERTISING + timer armed */
    actions = 0u;
    assert(switch2_wake_pair_request(WAKE_SOURCE_UART, 0u, &actions) == WAKE_RESULT_ACCEPTED);
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.state == WAKE_STATE_PAIR_ADVERTISING);
    assert(snap.timer_armed == true);

    /* 4. fresh init+start(false): request -> NO_BOND + counters */
    assert(switch2_wake_init(&cfg) == WAKE_RESULT_OK);
    assert(switch2_wake_start(false, 0u, &actions) == WAKE_RESULT_OK);
    actions = 0u;
    assert(switch2_wake_request(WAKE_SOURCE_UART, 0u, &actions) == WAKE_RESULT_NO_BOND);
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.wake_requested == 1u);
    assert(snap.wake_rejected == 1u);

    /* 5. forget -> OK + STOP|ERASE + UNBONDED */
    actions = 0u;
    assert(switch2_wake_forget(WAKE_SOURCE_UART, &actions) == WAKE_RESULT_OK);
    assert(actions == (WAKE_ACTION_ADV_STOP | WAKE_ACTION_ERASE_BOND));
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.state == WAKE_STATE_UNBONDED);

    /* 6. stale generation: PAIR_ADVERTISING + LE_CONNECTED (gen 1),
       then LE_DISCONNECTED with stale gen 99 -> stale_event==1, unchanged */
    {
        switch2_wake_event_t ev_conn;
        switch2_wake_event_t ev_stale;
        wake_state_t before;
        assert(switch2_wake_init(&cfg) == WAKE_RESULT_OK);
        assert(switch2_wake_start(false, 0u, &actions) == WAKE_RESULT_OK);
        assert(switch2_wake_pair_request(WAKE_SOURCE_UART, 0u, &actions) == WAKE_RESULT_ACCEPTED);
        make_event(&ev_conn, WAKE_EVENT_LE_CONNECTED, WAKE_SOURCE_BLE,
                   SWITCH2_WAKE_GENERATION_ANY, 100u);
        assert(switch2_wake_dispatch(&ev_conn, &actions) == WAKE_RESULT_ACCEPTED);
        assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
        assert(snap.connection_generation == 1u);
        assert(snap.state == WAKE_STATE_PAIR_EXCHANGE);
        before = snap.state;
        /* NOTE: brief says "generation 0 (and !=0)"; 0 is GENERATION_ANY
           (never stale), so use 99 as the stale non-zero generation. */
        make_event(&ev_stale, WAKE_EVENT_LE_DISCONNECTED, WAKE_SOURCE_BLE,
                   99u, 200u);
        assert(switch2_wake_dispatch(&ev_stale, &actions) == WAKE_RESULT_OK);
        assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
        assert(snap.stale_event == 1u);
        assert(snap.state == before);
    }

    /* 7. start(true) -> READY (no ADV_STANDARD); READY+request -> WAKE path */
    assert(switch2_wake_init(&cfg) == WAKE_RESULT_OK);
    actions = 0xFFFFFFFFu;
    assert(switch2_wake_start(true, 0u, &actions) == WAKE_RESULT_OK);
    assert((actions & WAKE_ACTION_ADV_STANDARD) == 0u);
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.state == WAKE_STATE_READY);
    actions = 0u;
    assert(switch2_wake_request(WAKE_SOURCE_UART, 10u, &actions) == WAKE_RESULT_ACCEPTED);
    assert(actions == WAKE_ACTION_ADV_WAKE);
    assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
    assert(snap.state == WAKE_STATE_WAKE_ADVERTISING);

    /* 8. tick: deadline-1 -> NONE; deadline -> ADV_STANDARD + READY + timeout */
    {
        uint32_t deadline;
        assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
        deadline = snap.timer_deadline_ms;
        actions = 0xFFFFFFFFu;
        assert(switch2_wake_tick(deadline - 1u, &actions) == WAKE_RESULT_OK);
        assert(actions == WAKE_ACTION_NONE);
        actions = 0u;
        assert(switch2_wake_tick(deadline, &actions) == WAKE_RESULT_OK);
        assert(actions == WAKE_ACTION_ADV_STANDARD);
        assert(switch2_wake_snapshot(&snap) == WAKE_RESULT_OK);
        assert(snap.state == WAKE_STATE_READY);
        assert(snap.wake_timeout == 1u);
    }

    printf("core ok\n");
    return 0;
}
