#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"
#include <string.h>
#include "app_state.h"
#include "switch2_wake_store.h"
#include "app_ports_store.h"

/* --- W4: BTstack を store の port へ橋渡しする ---
 * 鍵は LE Device DB だけが持ち、WakeMeta は別タグの TLV に置く（24章）。 */
const btstack_tlv_t *tlv_impl;
void *tlv_context;

uint32_t store_tlv_get(void *ctx, uint32_t tag, uint8_t *out,
                               uint32_t out_size) {
    (void)ctx;
    if (tlv_impl == NULL) return 0u;
    return (uint32_t)tlv_impl->get_tag(tlv_context, tag, out, out_size);
}

bool store_tlv_store(void *ctx, uint32_t tag, const uint8_t *data,
                            uint32_t size) {
    (void)ctx;
    if (tlv_impl == NULL) return false;
    return tlv_impl->store_tag(tlv_context, tag, (uint8_t *)data, size) == 0;
}

static void store_tlv_delete(void *ctx, uint32_t tag) {
    (void)ctx;
    if (tlv_impl != NULL) tlv_impl->delete_tag(tlv_context, tag);
}

static int store_db_max_count(void *ctx) {
    (void)ctx;
    return le_device_db_max_count();
}

static void store_db_info(void *ctx, int index, int *addr_type, uint8_t *addr) {
    (void)ctx;
    le_device_db_info(index, addr_type, addr, NULL);
}

/* LTK の有無だけを返す。鍵そのものは外へ出さない（V12）。
 * sm_is_null_key は BTstack 内部の関数なので、ここで同じ判定を自前で持つ。 */
static bool store_db_has_ltk(void *ctx, int index) {
    sm_key_t ltk;
    bool present = false;
    unsigned int i;
    (void)ctx;
    memset(ltk, 0, sizeof(ltk));
    le_device_db_encryption_get(index, NULL, NULL, ltk, NULL, NULL, NULL, NULL);
    for (i = 0u; i < sizeof(ltk); i++) {
        if (ltk[i] != 0u) {
            present = true;
        }
    }
    memset(ltk, 0, sizeof(ltk));
    return present;
}

static int store_db_add(void *ctx, int addr_type, const uint8_t *addr,
                        const uint8_t *irk) {
    sm_key_t irk_value;
    int index;
    (void)ctx;
    memset(irk_value, 0, sizeof(irk_value));
    if (irk != NULL) memcpy(irk_value, irk, sizeof(irk_value));
    index = le_device_db_add(addr_type, (uint8_t *)addr, irk_value);
    memset(irk_value, 0, sizeof(irk_value));
    return index;
}

/* EDIV=0 / RAND=0 で登録する（SW2LIB 9章・14章）。 */
static bool store_db_set_ltk(void *ctx, int index, const uint8_t *ltk) {
    uint8_t rand[8];
    sm_key_t key;
    (void)ctx;
    memset(rand, 0, sizeof(rand));
    memcpy(key, ltk, sizeof(key));
    le_device_db_encryption_set(index, 0u, rand, key, 16, 0, 0, 1);
    memset(key, 0, sizeof(key));
    return true;
}

static void store_db_remove(void *ctx, int index) {
    (void)ctx;
    le_device_db_remove(index);
}

const switch2_wake_store_port_t store_port = {
    store_tlv_get, store_tlv_store, store_tlv_delete,
    store_db_max_count, store_db_info, store_db_has_ltk,
    store_db_add, store_db_set_ltk, store_db_remove, NULL
};
