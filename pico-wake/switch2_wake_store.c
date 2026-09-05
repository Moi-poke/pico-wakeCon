#include "switch2_wake_store.h"
#include "switch2_wake_util.h"

#include <string.h>

/* WAKE_DESIGN 24章の実装。
 * 方針:
 *   1. WakeMeta は LTK/IRK を持たない。鍵は BTstack の LE Device DB だけが持つ。
 *   2. TLV と LE Device DB への到達は port 経由に限る（机上検査で差し替えるため）。
 *   3. 不一致・破損では DB を自動で書き換えない。WakeMeta だけを落とす。 */

#define WAKE_META_OFF_SCHEMA 0u
#define WAKE_META_OFF_STATE 1u
#define WAKE_META_OFF_SOURCE 2u
#define WAKE_META_OFF_LOCAL 3u
#define WAKE_META_OFF_PEER_TYPE 9u
#define WAKE_META_OFF_PEER_ADDR 10u
#define WAKE_META_OFF_RESERVED 16u
#define WAKE_META_OFF_CRC 19u

uint32_t switch2_wake_store_tag(void)
{
    return ((uint32_t)WAKE_META_TAG_0 << 24) |
           ((uint32_t)WAKE_META_TAG_1 << 16) |
           ((uint32_t)WAKE_META_TAG_2 << 8) |
           (uint32_t)WAKE_META_TAG_3;
}

/* CRC-32 (IEEE 802.3, reflected, 初期値 0xFFFFFFFF, 最後に反転)。
 * 表を持たずビット単位で回す。23 byte しか通さないので速度は問題にならない。 */
uint32_t switch2_wake_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int bit;
    if (data == NULL) {
        return 0u;
    }
    for (i = 0u; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void switch2_wake_store_init(switch2_wake_store_t *store,
                             const switch2_wake_store_port_t *port)
{
    if (store == NULL) {
        return;
    }
    memset(store, 0, sizeof(*store));
    if (port != NULL) {
        store->port = *port;
    }
    store->db_index = -1;
    store->last_result = WAKE_STORE_ERR_NO_META;
}

bool switch2_wake_meta_encode(const switch2_wake_meta_t *meta, uint8_t *blob)
{
    uint32_t crc;
    if ((meta == NULL) || (blob == NULL)) {
        return false;
    }
    memset(blob, 0, WAKE_META_BLOB_SIZE);
    blob[WAKE_META_OFF_SCHEMA] = meta->schema;
    blob[WAKE_META_OFF_STATE] = (uint8_t)meta->state;
    blob[WAKE_META_OFF_SOURCE] = (uint8_t)meta->source;
    memcpy(&blob[WAKE_META_OFF_LOCAL], meta->local_identity, 6);
    blob[WAKE_META_OFF_PEER_TYPE] = meta->peer_type;
    memcpy(&blob[WAKE_META_OFF_PEER_ADDR], meta->peer_identity, 6);
    /* reserved は 0 のまま */
    crc = switch2_wake_crc32(blob, WAKE_META_OFF_CRC);
    blob[WAKE_META_OFF_CRC + 0u] = (uint8_t)(crc & 0xFFu);
    blob[WAKE_META_OFF_CRC + 1u] = (uint8_t)((crc >> 8) & 0xFFu);
    blob[WAKE_META_OFF_CRC + 2u] = (uint8_t)((crc >> 16) & 0xFFu);
    blob[WAKE_META_OFF_CRC + 3u] = (uint8_t)((crc >> 24) & 0xFFu);
    return true;
}

switch2_wake_store_result_t switch2_wake_meta_decode(const uint8_t *blob,
                                                    uint32_t length,
                                                    switch2_wake_meta_t *meta)
{
    uint32_t stored;
    uint32_t actual;
    if ((blob == NULL) || (meta == NULL)) {
        return WAKE_STORE_ERR_ARG;
    }
    if (length != WAKE_META_BLOB_SIZE) {
        return WAKE_STORE_ERR_NO_META;
    }
    /* CRC を schema より先に見る。壊れた blob の schema は信用できないため。 */
    stored = (uint32_t)blob[WAKE_META_OFF_CRC + 0u] |
             ((uint32_t)blob[WAKE_META_OFF_CRC + 1u] << 8) |
             ((uint32_t)blob[WAKE_META_OFF_CRC + 2u] << 16) |
             ((uint32_t)blob[WAKE_META_OFF_CRC + 3u] << 24);
    actual = switch2_wake_crc32(blob, WAKE_META_OFF_CRC);
    if (stored != actual) {
        return WAKE_STORE_ERR_CRC;
    }
    if (blob[WAKE_META_OFF_SCHEMA] != (uint8_t)WAKE_META_SCHEMA) {
        return WAKE_STORE_ERR_SCHEMA;
    }
    if (blob[WAKE_META_OFF_STATE] > (uint8_t)WAKE_META_STATE_VERIFIED) {
        return WAKE_STORE_ERR_STATE;
    }
    memset(meta, 0, sizeof(*meta));
    meta->schema = blob[WAKE_META_OFF_SCHEMA];
    meta->state = (wake_meta_state_t)blob[WAKE_META_OFF_STATE];
    meta->source = (wake_source_t)blob[WAKE_META_OFF_SOURCE];
    memcpy(meta->local_identity, &blob[WAKE_META_OFF_LOCAL], 6);
    meta->peer_type = blob[WAKE_META_OFF_PEER_TYPE];
    memcpy(meta->peer_identity, &blob[WAKE_META_OFF_PEER_ADDR], 6);
    return WAKE_STORE_OK;
}

switch2_wake_store_result_t switch2_wake_store_load(switch2_wake_store_t *store)
{
    uint8_t blob[WAKE_META_BLOB_SIZE];
    uint32_t length;
    switch2_wake_store_result_t result;
    if (store == NULL) {
        return WAKE_STORE_ERR_ARG;
    }
    store->meta_loaded = false;
    memset(&store->meta, 0, sizeof(store->meta));
    if (store->port.tlv_get == NULL) {
        store->last_result = WAKE_STORE_ERR_ARG;
        return store->last_result;
    }
    memset(blob, 0, sizeof(blob));
    length = store->port.tlv_get(store->port.ctx, switch2_wake_store_tag(),
                                 blob, (uint32_t)sizeof(blob));
    if (length == 0u) {
        store->last_result = WAKE_STORE_ERR_NO_META;
        return store->last_result;
    }
    result = switch2_wake_meta_decode(blob, length, &store->meta);
    if (result != WAKE_STORE_OK) {
        memset(&store->meta, 0, sizeof(store->meta));
        store->last_result = result;
        return result;
    }
    store->meta_loaded = true;
    store->last_result = WAKE_STORE_OK;
    return WAKE_STORE_OK;
}

switch2_wake_store_result_t switch2_wake_store_save(switch2_wake_store_t *store,
                                                   const switch2_wake_meta_t *meta)
{
    uint8_t blob[WAKE_META_BLOB_SIZE];
    switch2_wake_meta_t local;
    if ((store == NULL) || (meta == NULL)) {
        return WAKE_STORE_ERR_ARG;
    }
    if (store->port.tlv_store == NULL) {
        store->last_result = WAKE_STORE_ERR_ARG;
        return store->last_result;
    }
    local = *meta;
    local.schema = (uint8_t)WAKE_META_SCHEMA;
    if (!switch2_wake_meta_encode(&local, blob)) {
        store->last_result = WAKE_STORE_ERR_ARG;
        return store->last_result;
    }
    if (!store->port.tlv_store(store->port.ctx, switch2_wake_store_tag(),
                               blob, (uint32_t)sizeof(blob))) {
        store->store_fail++;
        store->last_result = WAKE_STORE_ERR_TLV_WRITE;
        return store->last_result;
    }
    store->meta = local;
    store->meta_loaded = true;
    store->last_result = WAKE_STORE_OK;
    return WAKE_STORE_OK;
}

/* WakeMeta の peer と一致する LE Device DB entry を探す。見つからなければ -1。 */
static int wake_store_find_db_entry(switch2_wake_store_t *store,
                                    uint8_t peer_type,
                                    const uint8_t *peer_identity,
                                    switch2_wake_store_result_t *why)
{
    int max_count;
    int i;
    bool saw_addr_match = false;
    if ((store->port.db_max_count == NULL) || (store->port.db_info == NULL)) {
        *why = WAKE_STORE_ERR_ARG;
        return -1;
    }
    max_count = store->port.db_max_count(store->port.ctx);
    for (i = 0; i < max_count; i++) {
        int addr_type = -1;
        uint8_t addr[6];
        memset(addr, 0, sizeof(addr));
        store->port.db_info(store->port.ctx, i, &addr_type, addr);
        if (addr_type < 0) {
            continue;
        }
        if (memcmp(addr, peer_identity, 6) != 0) {
            continue;
        }
        saw_addr_match = true;
        if ((uint8_t)addr_type != peer_type) {
            continue;
        }
        *why = WAKE_STORE_OK;
        return i;
    }
    /* address は一致したが type だけ違う場合を区別して返す（診断のため） */
    *why = saw_addr_match ? WAKE_STORE_ERR_PEER_TYPE : WAKE_STORE_ERR_NO_DB_ENTRY;
    return -1;
}

switch2_wake_store_result_t switch2_wake_store_verify(switch2_wake_store_t *store,
                                                     const uint8_t *local_identity)
{
    switch2_wake_store_result_t why = WAKE_STORE_OK;
    int index;
    if ((store == NULL) || (local_identity == NULL)) {
        return WAKE_STORE_ERR_ARG;
    }
    store->db_index = -1;
    if (!store->meta_loaded) {
        store->last_result = WAKE_STORE_ERR_NO_META;
        return store->last_result;
    }
    if (store->meta.state != WAKE_META_STATE_VERIFIED) {
        store->last_result = WAKE_STORE_ERR_STATE;
        return store->last_result;
    }
    if (memcmp(store->meta.local_identity, local_identity, 6) != 0) {
        store->last_result = WAKE_STORE_ERR_LOCAL_IDENTITY;
        return store->last_result;
    }
    index = wake_store_find_db_entry(store, store->meta.peer_type,
                                     store->meta.peer_identity, &why);
    if (index < 0) {
        store->last_result = why;
        return store->last_result;
    }
    if (store->port.db_has_ltk == NULL) {
        store->last_result = WAKE_STORE_ERR_ARG;
        return store->last_result;
    }
    if (!store->port.db_has_ltk(store->port.ctx, index)) {
        store->last_result = WAKE_STORE_ERR_NO_LTK;
        return store->last_result;
    }
    store->db_index = index;
    store->last_result = WAKE_STORE_OK;
    return WAKE_STORE_OK;
}

switch2_wake_store_result_t switch2_wake_store_import(switch2_wake_store_t *store,
                                                     const uint8_t *local_identity,
                                                     uint8_t peer_type,
                                                     const uint8_t *peer_identity,
                                                     const uint8_t *ltk,
                                                     const uint8_t *irk)
{
    uint8_t irk_buffer[16];
    switch2_wake_meta_t meta;
    switch2_wake_store_result_t result;
    int index;

    if ((store == NULL) || (local_identity == NULL) ||
        (peer_identity == NULL) || (ltk == NULL)) {
        return WAKE_STORE_ERR_ARG;
    }
    /* LTK が全0なら投入しない。暗号化再接続に使えないため。 */
    if (switch2_wake_is_all_zero(ltk, 16)) {
        store->last_result = WAKE_STORE_ERR_NO_LTK;
        return store->last_result;
    }
    if ((store->port.db_add == NULL) || (store->port.db_set_ltk == NULL)) {
        store->last_result = WAKE_STORE_ERR_ARG;
        return store->last_result;
    }

    /* U04（24章）: IRK は任意。省略時は全0を渡す。
     * sm.c は identity 一致で先に成立し、全0 IRK の entry は AH 計算を skip するだけ。 */
    memset(irk_buffer, 0, sizeof(irk_buffer));
    if (irk != NULL) {
        memcpy(irk_buffer, irk, sizeof(irk_buffer));
    }

    index = store->port.db_add(store->port.ctx, (int)peer_type,
                               peer_identity, irk_buffer);
    switch2_wake_secure_zero(irk_buffer, sizeof(irk_buffer));
    if (index < 0) {
        store->last_result = WAKE_STORE_ERR_DB_ADD;
        return store->last_result;
    }
    if (!store->port.db_set_ltk(store->port.ctx, index, ltk)) {
        /* 鍵を入れられないまま entry を残さない */
        if (store->port.db_remove != NULL) {
            store->port.db_remove(store->port.ctx, index);
        }
        store->last_result = WAKE_STORE_ERR_DB_ADD;
        return store->last_result;
    }

    memset(&meta, 0, sizeof(meta));
    meta.schema = (uint8_t)WAKE_META_SCHEMA;
    meta.state = WAKE_META_STATE_PENDING;
    meta.source = WAKE_SOURCE_UART;
    memcpy(meta.local_identity, local_identity, 6);
    meta.peer_type = peer_type;
    memcpy(meta.peer_identity, peer_identity, 6);

    result = switch2_wake_store_save(store, &meta);
    switch2_wake_secure_zero(&meta, sizeof(meta));
    if (result != WAKE_STORE_OK) {
        if (store->port.db_remove != NULL) {
            store->port.db_remove(store->port.ctx, index);
        }
        return result;
    }
    store->db_index = index;
    store->last_result = WAKE_STORE_OK;
    return WAKE_STORE_OK;
}

switch2_wake_store_result_t switch2_wake_store_commit(switch2_wake_store_t *store)
{
    switch2_wake_meta_t meta;
    switch2_wake_store_result_t result;
    if (store == NULL) {
        return WAKE_STORE_ERR_ARG;
    }
    if (!store->meta_loaded) {
        store->last_result = WAKE_STORE_ERR_NO_META;
        return store->last_result;
    }
    /* PENDING からのみ昇格する。EMPTY を飛び級で VERIFIED にしない（不変条件4）。 */
    if (store->meta.state != WAKE_META_STATE_PENDING) {
        store->last_result = WAKE_STORE_ERR_STATE;
        return store->last_result;
    }
    meta = store->meta;
    meta.state = WAKE_META_STATE_VERIFIED;
    result = switch2_wake_store_save(store, &meta);
    switch2_wake_secure_zero(&meta, sizeof(meta));
    return result;
}

switch2_wake_store_result_t switch2_wake_store_forget(switch2_wake_store_t *store)
{
    switch2_wake_store_result_t why = WAKE_STORE_OK;
    int index;
    if (store == NULL) {
        return WAKE_STORE_ERR_ARG;
    }
    /* 消去は明示操作なので、meta が読めていれば対応する DB entry も消す。 */
    if (store->meta_loaded) {
        index = wake_store_find_db_entry(store, store->meta.peer_type,
                                         store->meta.peer_identity, &why);
        if ((index >= 0) && (store->port.db_remove != NULL)) {
            store->port.db_remove(store->port.ctx, index);
        }
    }
    if (store->port.tlv_delete != NULL) {
        store->port.tlv_delete(store->port.ctx, switch2_wake_store_tag());
    }
    switch2_wake_secure_zero(&store->meta, sizeof(store->meta));
    store->meta_loaded = false;
    store->db_index = -1;
    store->last_result = WAKE_STORE_OK;
    return WAKE_STORE_OK;
}

const char *switch2_wake_store_result_name(switch2_wake_store_result_t result)
{
    switch (result) {
    case WAKE_STORE_OK: return "OK";
    case WAKE_STORE_ERR_NO_META: return "NO_META";
    case WAKE_STORE_ERR_SCHEMA: return "SCHEMA";
    case WAKE_STORE_ERR_CRC: return "CRC";
    case WAKE_STORE_ERR_STATE: return "STATE";
    case WAKE_STORE_ERR_LOCAL_IDENTITY: return "LOCAL_IDENTITY";
    case WAKE_STORE_ERR_NO_DB_ENTRY: return "NO_DB_ENTRY";
    case WAKE_STORE_ERR_PEER_TYPE: return "PEER_TYPE";
    case WAKE_STORE_ERR_PEER_ADDR: return "PEER_ADDR";
    case WAKE_STORE_ERR_NO_LTK: return "NO_LTK";
    case WAKE_STORE_ERR_TLV_WRITE: return "TLV_WRITE";
    case WAKE_STORE_ERR_DB_ADD: return "DB_ADD";
    case WAKE_STORE_ERR_ARG: return "ARG";
    default: return "UNKNOWN";
    }
}
