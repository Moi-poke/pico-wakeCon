#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_cmd.h"
#include "switch2_wake_util.h"

/* Port mock (no BTstack). */
static uint8_t emit_buf[160];
static uint16_t emit_len;
static uint8_t got_local[6] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
static uint8_t aes_out_canned[16] = {
    0x13u, 0x57u, 0x9Bu, 0xDFu, 0x02u, 0x46u, 0x8Au, 0xCEu,
    0x11u, 0x55u, 0x99u, 0xDDu, 0x22u, 0x66u, 0xAAu, 0xEDu
};
static uint8_t stored_peer[6];
static uint8_t stored_type;
static bool store_bond_ret = true;
static int registered_cnt;
static uint8_t last_obs_cmd;
static uint8_t last_obs_sub;
static uint8_t aes_key_saved[16];
static uint8_t aes_plain_saved[16];
static int aes_calls;

static bool mock_get_local(void *ctx, uint8_t addr[6]) {
    (void)ctx;
    memcpy(addr, got_local, 6);
    return true;
}

static bool mock_start_aes(void *ctx, const uint8_t key[16],
                           const uint8_t plain[16],
                           switch2_wake_cmd_aes_done_t done, void *done_ctx) {
    (void)ctx;
    memcpy(aes_key_saved, key, 16);
    memcpy(aes_plain_saved, plain, 16);
    aes_calls++;
    done(done_ctx, aes_out_canned);
    return true;
}

static bool mock_store_bond(void *ctx, uint8_t peer_type, const uint8_t peer[6],
                            const uint8_t ltk_natural[16]) {
    (void)ctx;
    (void)ltk_natural;
    stored_type = peer_type;
    memcpy(stored_peer, peer, 6);
    return store_bond_ret;
}

static bool mock_read_memory(void *ctx, uint32_t address, uint8_t *out,
                             uint8_t length) {
    (void)ctx;
    (void)address;
    (void)out;
    (void)length;
    return false;
}

static void mock_emit(void *ctx, const uint8_t *packet, uint16_t length) {
    uint16_t n;
    (void)ctx;
    n = length > sizeof(emit_buf) ? (uint16_t)sizeof(emit_buf) : length;
    memcpy(emit_buf, packet, n);
    emit_len = n;
}

static void mock_registered(void *ctx) {
    (void)ctx;
    registered_cnt++;
}

static void mock_observe(void *ctx, uint8_t cmd, uint8_t sub,
                         uint16_t request_len, uint16_t response_len) {
    (void)ctx;
    (void)request_len;
    (void)response_len;
    last_obs_cmd = cmd;
    last_obs_sub = sub;
}

static switch2_wake_cmd_port_t make_port(void) {
    switch2_wake_cmd_port_t p;
    memset(&p, 0, sizeof(p));
    p.ctx = NULL;
    p.get_local_addr = mock_get_local;
    p.start_aes = mock_start_aes;
    p.store_bond = mock_store_bond;
    p.read_memory = mock_read_memory;
    p.emit_response = mock_emit;
    p.on_registered = mock_registered;
    p.observe = mock_observe;
    return p;
}

static void reset_mocks(void) {
    memset(emit_buf, 0, sizeof(emit_buf));
    emit_len = 0;
    memset(stored_peer, 0, sizeof(stored_peer));
    stored_type = 0;
    store_bond_ret = true;
    registered_cnt = 0;
    last_obs_cmd = 0;
    last_obs_sub = 0;
    memset(aes_key_saved, 0, sizeof(aes_key_saved));
    memset(aes_plain_saved, 0, sizeof(aes_plain_saved));
    aes_calls = 0;
}

/* Request builder: 8-byte header {cmd,0,0,sub,0,0,0,0} + data. */
static uint16_t build_req(uint8_t cmd, uint8_t sub, const uint8_t *data,
                          uint16_t data_len, uint8_t *out) {
    out[0] = cmd;
    out[1] = 0;
    out[2] = 0;
    out[3] = sub;
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    if (data_len > 0u && data != NULL) {
        memcpy(&out[8], data, data_len);
    }
    return (uint16_t)(8u + data_len);
}

int main(void) {
    switch2_wake_cmd_port_t port;
    switch2_wake_cmd_t s;
    uint8_t pkt[64];
    uint8_t data[32];
    uint8_t out[16];
    uint8_t ltk[16];
    uint8_t key[16];
    uint8_t plain[16];
    uint8_t peer[6] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
    uint16_t len;
    int i;

    memset(pkt, 0, sizeof(pkt));
    port = make_port();

    /* 1. handle filtering, NULL buffer, SHORT dispatch */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    assert(switch2_wake_cmd_write(&s, 0x0019u, pkt, 8u) == WAKE_CMD_IGNORED);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, NULL, 0u)
        == WAKE_CMD_PORT_ERROR);
    pkt[0] = 0x01u;
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, 1u)
        == WAKE_CMD_SHORT);
    pkt[0] = 0x01u;
    pkt[1] = 0x02u;
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_RUMBLE, pkt, 2u)
        == WAKE_CMD_SHORT);

    /* 2. 0x15/01 pairing hello */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    len = build_req(0x15u, 0x01u, NULL, 0u, pkt);
    assert(len == 8u);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    assert(emit_buf[14] == 0x15u);
    assert(emit_buf[17] == 0x01u);
    assert(emit_buf[22] == 0x01u);
    assert(emit_buf[23] == 0x04u);
    assert(emit_buf[24] == 0x01u);
    assert(emit_buf[25] == 0xFFu);
    assert(emit_buf[26] == 0xEEu);
    assert(emit_buf[27] == 0xDDu);
    assert(emit_buf[28] == 0xCCu);
    assert(emit_buf[29] == 0xBBu);
    assert(emit_buf[30] == 0xAAu);
    assert(last_obs_cmd == 0x15u);
    assert(last_obs_sub == 0x01u);

    /* 3. 0x15/04 LTK establish */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    memset(data, 0, sizeof(data));
    data[0] = 0xAAu;
    len = build_req(0x15u, 0x04u, data, 5u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_SHORT);
    data[0] = 0xAAu;
    for (i = 0; i < 16; i++) {
        data[1 + i] = (uint8_t)(i + 1u);
    }
    len = build_req(0x15u, 0x04u, data, 17u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    assert(switch2_wake_cmd_get_ltk(&s, out) == true);
    assert(switch2_wake_cmd_fingerprint(&s) != 0u);
    assert(emit_buf[17] == 0x04u);
    assert(emit_buf[22] == 0x01u);

    /* 4. 0x15/02 AES step: SHORT without LTK, OK after 0x15/04 */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    memset(data, 0, sizeof(data));
    data[0] = 0xBBu;
    for (i = 0; i < 16; i++) {
        data[1 + i] = (uint8_t)(0x10u + i);
    }
    len = build_req(0x15u, 0x02u, data, 17u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_SHORT);
    data[0] = 0xAAu;
    for (i = 0; i < 16; i++) {
        data[1 + i] = (uint8_t)(i + 1u);
    }
    len = build_req(0x15u, 0x04u, data, 17u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    reset_mocks();
    data[0] = 0xBBu;
    for (i = 0; i < 16; i++) {
        data[1 + i] = (uint8_t)(0x10u + i);
    }
    len = build_req(0x15u, 0x02u, data, 17u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    assert(aes_calls == 1);
    assert(emit_buf[17] == 0x02u);
    assert(emit_buf[22] == 0x01u);
    assert(memcmp(&emit_buf[23], aes_out_canned, 16) == 0);

    /* 5. 0x15/03 bond store + peer-gated LTK */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    data[0] = 0xAAu;
    for (i = 0; i < 16; i++) {
        data[1 + i] = (uint8_t)(i + 1u);
    }
    len = build_req(0x15u, 0x04u, data, 17u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    len = build_req(0x15u, 0x03u, NULL, 0u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_PORT_ERROR);
    switch2_wake_cmd_set_peer(&s, 1u, peer);
    len = build_req(0x15u, 0x03u, NULL, 0u, pkt);
    assert(switch2_wake_cmd_write(&s, WAKE_CMD_HANDLE_BASIC, pkt, len)
        == WAKE_CMD_OK);
    assert(stored_type == 1u);
    assert(memcmp(stored_peer, peer, 6) == 0);
    assert(emit_buf[17] == 0x03u);
    assert(emit_buf[22] == 0x01u);
    assert(switch2_wake_cmd_get_ltk_for_peer(&s, 0u, peer, out) == false);
    assert(switch2_wake_cmd_get_ltk_for_peer(&s, 1u, peer, out) == true);

    /* 6. selftest values + result names */
    assert(switch2_wake_cmd_selftest_values(ltk, key, plain) == true);
    assert(switch2_wake_cmd_selftest_values(NULL, key, plain) == false);
    assert(switch2_wake_is_all_zero(key, 16) == false);
    assert(switch2_wake_is_all_zero(plain, 16) == false);
    assert(memcmp(key, plain, 16) != 0);
    assert(strcmp(switch2_wake_cmd_result_name(WAKE_CMD_OK), "OK") == 0);
    assert(strcmp(switch2_wake_cmd_result_name(WAKE_CMD_IGNORED), "IGNORED") == 0);
    assert(strcmp(switch2_wake_cmd_result_name(WAKE_CMD_SHORT), "SHORT") == 0);
    assert(strcmp(switch2_wake_cmd_result_name(WAKE_CMD_BUSY), "BUSY") == 0);
    assert(strcmp(switch2_wake_cmd_result_name(WAKE_CMD_PORT_ERROR),
        "PORT_ERROR") == 0);

    /* 7. fingerprint without LTK */
    reset_mocks();
    switch2_wake_cmd_init(&s, &port);
    assert(switch2_wake_cmd_fingerprint(&s) == 0u);
    assert(switch2_wake_cmd_get_ltk(&s, out) == false);

    printf("cmd ok\n");
    return 0;
}
