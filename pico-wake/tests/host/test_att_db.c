#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_att_db.h"

/* Counter mock keyed to the module's own expected-handle table. */
static int call_idx;
static int flag_armed;

static void m_init(void *ctx) {
    (void)ctx;
    call_idx = 0;
}

static uint16_t m_next(void) {
    uint8_t step = (uint8_t)(call_idx + 1);
    uint16_t h = switch2_wake_att_expected_handle(step);
    if (flag_armed != 0 && step == 13u) {
        h = 0x0017u;
    }
    call_idx++;
    return h;
}

static uint16_t m_svc128(void *ctx, const uint8_t *uuid128) {
    (void)ctx;
    (void)uuid128;
    return m_next();
}

static uint16_t m_svc16(void *ctx, uint16_t uuid16) {
    (void)ctx;
    (void)uuid16;
    return m_next();
}

static uint16_t m_chr128(void *ctx, const uint8_t *uuid128, uint16_t properties,
                         uint8_t read_perm, uint8_t write_perm,
                         uint8_t *data, uint16_t data_len) {
    (void)ctx;
    (void)uuid128;
    (void)properties;
    (void)read_perm;
    (void)write_perm;
    (void)data;
    (void)data_len;
    return m_next();
}

static uint16_t m_chr16(void *ctx, uint16_t uuid16, uint16_t properties,
                        uint8_t read_perm, uint8_t write_perm,
                        uint8_t *data, uint16_t data_len) {
    (void)ctx;
    (void)uuid16;
    (void)properties;
    (void)read_perm;
    (void)write_perm;
    (void)data;
    (void)data_len;
    return m_next();
}

static uint16_t m_desc128(void *ctx, const uint8_t *uuid128,
                          uint16_t properties, uint8_t read_perm,
                          uint8_t write_perm, uint8_t *data,
                          uint16_t data_len) {
    (void)ctx;
    (void)uuid128;
    (void)properties;
    (void)read_perm;
    (void)write_perm;
    (void)data;
    (void)data_len;
    return m_next();
}

static uint16_t m_size(void *ctx) {
    (void)ctx;
    return 900u;
}

static switch2_wake_att_port_t make_port(void) {
    switch2_wake_att_port_t p;
    memset(&p, 0, sizeof(p));
    p.init = m_init;
    p.add_service_uuid128 = m_svc128;
    p.add_service_uuid16 = m_svc16;
    p.add_characteristic_uuid128 = m_chr128;
    p.add_characteristic_uuid16 = m_chr16;
    p.add_descriptor_uuid128 = m_desc128;
    p.get_size = m_size;
    p.ctx = NULL;
    return p;
}

int main(void) {
    switch2_wake_att_port_t port;
    switch2_wake_att_report_t rep;

    port = make_port();

    /* 1. full build succeeds */
    flag_armed = 0;
    memset(&rep, 0, sizeof(rep));
    assert(switch2_wake_att_db_build(&port, &rep) == WAKE_ATT_OK);
    assert(rep.failed_step == 0u);
    assert(rep.last_handle == 0x0038u);
    assert(rep.db_size == 900u);

    /* 2. expected-handle spot checks + out of range */
    assert(switch2_wake_att_expected_handle(1u) == 0x0001u);
    assert(switch2_wake_att_expected_handle(13u) == 0x0018u);
    assert(switch2_wake_att_expected_handle(23u) == 0x002Cu);
    assert(switch2_wake_att_expected_handle(30u) == 0x0038u);
    assert(switch2_wake_att_expected_handle(0u) == 0u);
    assert(switch2_wake_att_expected_handle(31u) == 0u);

    /* 3. failure injection at step 13 (2026/09/02 incident mirror) */
    flag_armed = 1;
    memset(&rep, 0, sizeof(rep));
    assert(switch2_wake_att_db_build(&port, &rep) == WAKE_ATT_ERR_STEP_HANDLE);
    assert(rep.failed_step == 13u);
    assert(rep.expected_handle == 0x0018u);
    assert(rep.actual_handle == 0x0017u);
    flag_armed = 0;

    /* 4. NULL port (and NULL report) */
    memset(&rep, 0, sizeof(rep));
    assert(switch2_wake_att_db_build(NULL, &rep) == WAKE_ATT_ERR_PORT);
    assert(switch2_wake_att_db_build(&port, NULL) == WAKE_ATT_ERR_PORT);

    /* 5. result names */
    assert(strcmp(switch2_wake_att_result_name(WAKE_ATT_OK), "OK") == 0);
    assert(strcmp(switch2_wake_att_result_name(WAKE_ATT_ERR_STEP_HANDLE),
        "STEP_HANDLE") == 0);

    printf("att_db ok\n");
    return 0;
}
