#ifndef SWITCH2_WAKE_STORE_H
#define SWITCH2_WAKE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "switch2_wake.h"

/* WakeMeta の保存仕様は WAKE_DESIGN 24章を単一原本とする。
 * 秘密値(LTK/IRK)はここへ保存しない。鍵は BTstack の LE Device DB だけが持つ。 */

#define WAKE_META_TAG_0 'W'
#define WAKE_META_TAG_1 'M'
#define WAKE_META_TAG_2 'K'
#define WAKE_META_TAG_3 '1'

#define WAKE_META_SCHEMA 1u
#define WAKE_META_BLOB_SIZE 23u
#define WAKE_META_RESERVED_SIZE 3u

/* 状態値も W1 の wake_meta_state_t が原本。名前だけ store 側の綴りへ揃える。 */
#define WAKE_META_STATE_EMPTY WAKE_META_NONE
#define WAKE_META_STATE_PENDING WAKE_META_PENDING
#define WAKE_META_STATE_VERIFIED WAKE_META_VERIFIED

typedef enum {
    WAKE_META_SOURCE_UNKNOWN = 0,
    WAKE_META_SOURCE_PAIR = 1,
    WAKE_META_SOURCE_IMPORT = 2
} switch2_wake_meta_source_t;

/* 照合の失敗理由。診断へ出すのは理由だけで、鍵の中身は出さない。 */
typedef enum {
    WAKE_STORE_OK = 0,
    WAKE_STORE_ERR_NO_META,
    WAKE_STORE_ERR_SCHEMA,
    WAKE_STORE_ERR_CRC,
    WAKE_STORE_ERR_STATE,
    WAKE_STORE_ERR_LOCAL_IDENTITY,
    WAKE_STORE_ERR_NO_DB_ENTRY,
    WAKE_STORE_ERR_PEER_TYPE,
    WAKE_STORE_ERR_PEER_ADDR,
    WAKE_STORE_ERR_NO_LTK,
    WAKE_STORE_ERR_TLV_WRITE,
    WAKE_STORE_ERR_DB_ADD,
    WAKE_STORE_ERR_ARG
} switch2_wake_store_result_t;

/* WakeMeta の型は W1 の switch2_wake.h が単一原本である。
 * ここで再定義すると型衝突になるため、include して使う。 */

/* --- port: 実装は pico_wake.c 側が BTstack へ橋渡しする --- */
typedef struct {
    /* TLV: 成功なら読めた byte 数、無ければ 0 を返す */
    uint32_t (*tlv_get)(void *ctx, uint32_t tag, uint8_t *out, uint32_t out_size);
    /* TLV: 成功で true */
    bool (*tlv_store)(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size);
    void (*tlv_delete)(void *ctx, uint32_t tag);
    /* LE Device DB 走査 */
    int (*db_max_count)(void *ctx);
    void (*db_info)(void *ctx, int index, int *addr_type, uint8_t *addr);
    /* LTK が全0でないとき true。LTK の値そのものは返させない（V12） */
    bool (*db_has_ltk)(void *ctx, int index);
    /* 投入用。irk が NULL なら全0を渡す実装とする */
    int (*db_add)(void *ctx, int addr_type, const uint8_t *addr, const uint8_t *irk);
    bool (*db_set_ltk)(void *ctx, int index, const uint8_t *ltk);
    void (*db_remove)(void *ctx, int index);
    void *ctx;
} switch2_wake_store_port_t;

typedef struct {
    switch2_wake_store_port_t port;
    switch2_wake_meta_t meta;
    bool meta_loaded;
    int db_index;
    switch2_wake_store_result_t last_result;
    uint32_t store_fail;
} switch2_wake_store_t;

uint32_t switch2_wake_store_tag(void);
uint32_t switch2_wake_crc32(const uint8_t *data, size_t length);
void switch2_wake_store_init(switch2_wake_store_t *store,
                             const switch2_wake_store_port_t *port);

/* blob 化と復元。23 byte 固定。 */
bool switch2_wake_meta_encode(const switch2_wake_meta_t *meta, uint8_t *blob);
switch2_wake_store_result_t switch2_wake_meta_decode(const uint8_t *blob,
                                                    uint32_t length,
                                                    switch2_wake_meta_t *meta);

switch2_wake_store_result_t switch2_wake_store_load(switch2_wake_store_t *store);
switch2_wake_store_result_t switch2_wake_store_save(switch2_wake_store_t *store,
                                                   const switch2_wake_meta_t *meta);

/* 起動時の照合。LE DB と WakeMeta の両方が一致したときだけ OK。 */
switch2_wake_store_result_t switch2_wake_store_verify(switch2_wake_store_t *store,
                                                     const uint8_t *local_identity);

/* import: DB へ登録し WakeMeta を PENDING で保存する。ltk は必須、irk は NULL 可。 */
switch2_wake_store_result_t switch2_wake_store_import(switch2_wake_store_t *store,
                                                     const uint8_t *local_identity,
                                                     uint8_t peer_type,
                                                     const uint8_t *peer_identity,
                                                     const uint8_t *ltk,
                                                     const uint8_t *irk);

/* 0x0c/04 観測後に PENDING -> VERIFIED へ昇格する。 */
switch2_wake_store_result_t switch2_wake_store_commit(switch2_wake_store_t *store);

/* 消去。WakeMeta を消し、対応する DB entry も明示的に消す。 */
switch2_wake_store_result_t switch2_wake_store_forget(switch2_wake_store_t *store);

const char *switch2_wake_store_result_name(switch2_wake_store_result_t result);

#endif
