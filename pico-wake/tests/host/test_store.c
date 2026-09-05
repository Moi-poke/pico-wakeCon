#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "switch2_wake_store.h"
#include "switch2_wake_util.h"

/* TLV mock: single 23-byte blob. */
static uint8_t tlv_blob[23];
static bool tlv_has;
static uint32_t tlv_tag;

/* LE Device DB mock: N=8 array. */
#define DB_N 8
static int db_type[DB_N];
static uint8_t db_addr[DB_N][6];
static bool db_ltk[DB_N];

static uint32_t mock_tlv_get(void *ctx, uint32_t tag, uint8_t *out,
                             uint32_t out_size) {
    uint32_t n;
    (void)ctx;
    if (!tlv_has || tag != tlv_tag) {
        return 0u;
    }
    n = out_size > sizeof(tlv_blob) ? (uint32_t)sizeof(tlv_blob) : out_size;
    memcpy(out, tlv_blob, n);
    return n;
}

static bool mock_tlv_store(void *ctx, uint32_t tag, const uint8_t *data,
                           uint32_t size) {
    uint32_t n;
    (void)ctx;
    n = size > sizeof(tlv_blob) ? (uint32_t)sizeof(tlv_blob) : size;
    memcpy(tlv_blob, data, n);
    tlv_tag = tag;
    tlv_has = true;
    return true;
}

static void mock_tlv_delete(void *ctx, uint32_t tag) {
    (void)ctx;
    (void)tag;
    tlv_has = false;
}

static int mock_db_max_count(void *ctx) {
    (void)ctx;
    return DB_N;
}

static void mock_db_info(void *ctx, int index, int *addr_type, uint8_t *addr) {
    (void)ctx;
    *addr_type = db_type[index];
    memcpy(addr, db_addr[index], 6);
}

static bool mock_db_has_ltk(void *ctx, int index) {
    (void)ctx;
    return db_ltk[index];
}

static int mock_db_add(void *ctx, int addr_type, const uint8_t *addr,
                       const uint8_t *irk) {
    int i;
    (void)ctx;
    (void)irk;
    for (i = 0; i < DB_N; i++) {
        if (db_type[i] < 0) {
            db_type[i] = addr_type;
            memcpy(db_addr[i], addr, 6);
            db_ltk[i] = false;
            return i;
        }
    }
    return -1;
}

static bool mock_db_set_ltk(void *ctx, int index, const uint8_t *ltk) {
    (void)ctx;
    (void)ltk;
    db_ltk[index] = true;
    return true;
}

static void mock_db_remove(void *ctx, int index) {
    (void)ctx;
    db_type[index] = -1;
    db_ltk[index] = false;
}

static switch2_wake_store_port_t make_port(void) {
    switch2_wake_store_port_t p;
    memset(&p, 0, sizeof(p));
    p.tlv_get = mock_tlv_get;
    p.tlv_store = mock_tlv_store;
    p.tlv_delete = mock_tlv_delete;
    p.db_max_count = mock_db_max_count;
    p.db_info = mock_db_info;
    p.db_has_ltk = mock_db_has_ltk;
    p.db_add = mock_db_add;
    p.db_set_ltk = mock_db_set_ltk;
    p.db_remove = mock_db_remove;
    p.ctx = NULL;
    return p;
}

static void reset_mocks(void) {
    int i;
    memset(tlv_blob, 0, sizeof(tlv_blob));
    tlv_has = false;
    tlv_tag = 0u;
    for (i = 0; i < DB_N; i++) {
        db_type[i] = -1;
        memset(db_addr[i], 0, 6);
        db_ltk[i] = false;
    }
}

static switch2_wake_meta_t make_meta(void) {
    switch2_wake_meta_t m = {1u, WAKE_META_PENDING, WAKE_SOURCE_UART,
        {1, 2, 3, 4, 5, 6}, 1u, {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    return m;
}

static void fix_crc(uint8_t *blob) {
    uint32_t crc = switch2_wake_crc32(blob, 19u);
    blob[19] = (uint8_t)(crc & 0xFFu);
    blob[20] = (uint8_t)((crc >> 8) & 0xFFu);
    blob[21] = (uint8_t)((crc >> 16) & 0xFFu);
    blob[22] = (uint8_t)((crc >> 24) & 0xFFu);
}

int main(void) {
    switch2_wake_store_port_t sport;
    switch2_wake_store_t st;
    switch2_wake_meta_t meta;
    switch2_wake_meta_t back;
    uint8_t blob[23];
    uint8_t bad[23];
    uint8_t local[6] = {1, 2, 3, 4, 5, 6};
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t wrong_local[6] = {9, 9, 9, 9, 9, 9};
    uint8_t zero_ltk[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t good_ltk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint32_t a;
    uint32_t b;

    sport = make_port();

    /* 1. tag + crc32 properties */
    assert(switch2_wake_store_tag() == 0x574D4B31u);
    assert(switch2_wake_crc32(NULL, 5u) == 0u);
    a = switch2_wake_crc32(local, 6u);
    b = switch2_wake_crc32(local, 6u);
    assert(a == b);
    local[0] ^= 0x01u;
    assert(switch2_wake_crc32(local, 6u) != a);
    local[0] ^= 0x01u;

    /* 2. encode/decode roundtrip + error paths */
    reset_mocks();
    meta = make_meta();
    assert(switch2_wake_meta_encode(&meta, blob) == true);
    assert(switch2_wake_meta_encode(NULL, blob) == false);
    memset(&back, 0, sizeof(back));
    assert(switch2_wake_meta_decode(blob, 23u, &back) == WAKE_STORE_OK);
    assert(back.schema == meta.schema);
    assert(back.state == meta.state);
    assert(back.source == meta.source);
    assert(memcmp(back.local_identity, meta.local_identity, 6) == 0);
    assert(back.peer_type == meta.peer_type);
    assert(memcmp(back.peer_identity, meta.peer_identity, 6) == 0);
    assert(switch2_wake_meta_decode(blob, 22u, &back) == WAKE_STORE_ERR_NO_META);
    memcpy(bad, blob, sizeof(bad));
    bad[5] ^= 0xFFu;
    assert(switch2_wake_meta_decode(bad, 23u, &back) == WAKE_STORE_ERR_CRC);
    memcpy(bad, blob, sizeof(bad));
    bad[0] = 9u;
    fix_crc(bad);
    assert(switch2_wake_meta_decode(bad, 23u, &back) == WAKE_STORE_ERR_SCHEMA);
    memcpy(bad, blob, sizeof(bad));
    bad[1] = 5u;
    fix_crc(bad);
    assert(switch2_wake_meta_decode(bad, 23u, &back) == WAKE_STORE_ERR_STATE);

    /* 3. load empty, save, load back */
    reset_mocks();
    switch2_wake_store_init(&st, &sport);
    assert(switch2_wake_store_load(&st) == WAKE_STORE_ERR_NO_META);
    meta = make_meta();
    assert(switch2_wake_store_save(&st, &meta) == WAKE_STORE_OK);
    assert(st.meta_loaded == true);
    assert(switch2_wake_store_load(&st) == WAKE_STORE_OK);
    assert(st.meta_loaded == true);
    assert(st.meta.schema == meta.schema);
    assert(st.meta.state == meta.state);
    assert(st.meta.source == meta.source);
    assert(memcmp(st.meta.local_identity, meta.local_identity, 6) == 0);
    assert(st.meta.peer_type == meta.peer_type);
    assert(memcmp(st.meta.peer_identity, meta.peer_identity, 6) == 0);

    /* 4. verify ladder */
    reset_mocks();
    switch2_wake_store_init(&st, &sport);
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_NO_META);
    meta = make_meta();
    assert(switch2_wake_store_save(&st, &meta) == WAKE_STORE_OK);
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_STATE);
    meta.state = WAKE_META_VERIFIED;
    assert(switch2_wake_store_save(&st, &meta) == WAKE_STORE_OK);
    assert(switch2_wake_store_verify(&st, wrong_local)
        == WAKE_STORE_ERR_LOCAL_IDENTITY);
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_NO_DB_ENTRY);
    db_type[0] = 0;
    memcpy(db_addr[0], peer, 6);
    db_ltk[0] = true;
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_PEER_TYPE);
    db_type[0] = 1;
    db_ltk[0] = false;
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_NO_LTK);
    db_ltk[0] = true;
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_OK);
    assert(st.db_index == 0);

    /* 5. import / commit / forget */
    reset_mocks();
    switch2_wake_store_init(&st, &sport);
    assert(switch2_wake_store_import(&st, local, 1u, peer, NULL, NULL)
        == WAKE_STORE_ERR_ARG);
    assert(switch2_wake_store_import(&st, local, 1u, peer, zero_ltk, NULL)
        == WAKE_STORE_ERR_NO_LTK);
    assert(switch2_wake_store_import(&st, local, 1u, peer, good_ltk, NULL)
        == WAKE_STORE_OK);
    assert(st.meta.state == WAKE_META_PENDING);
    assert(st.db_index >= 0);
    assert(switch2_wake_store_commit(&st) == WAKE_STORE_OK);
    assert(st.meta.state == WAKE_META_VERIFIED);
    assert(switch2_wake_store_commit(&st) == WAKE_STORE_ERR_STATE);
    assert(switch2_wake_store_forget(&st) == WAKE_STORE_OK);
    assert(switch2_wake_store_load(&st) == WAKE_STORE_ERR_NO_META);
    assert(switch2_wake_store_verify(&st, local) == WAKE_STORE_ERR_NO_META);

    /* 6. result names */
    assert(strcmp(switch2_wake_store_result_name(WAKE_STORE_OK), "OK") == 0);
    assert(strcmp(switch2_wake_store_result_name(WAKE_STORE_ERR_ARG), "ARG") == 0);
    assert(strcmp(switch2_wake_store_result_name(WAKE_STORE_ERR_CRC), "CRC") == 0);

    printf("store ok\n");
    return 0;
}
