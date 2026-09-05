#ifndef SWITCH2_WAKE_CAPTURE_H
#define SWITCH2_WAKE_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Joy-Con 2 / Pro 2 の wake ビーコンを取り込む（Bill-git1 流）。
 * 31 byte 広告の配置は ndeadly bluetooth_interface.md と同一。
 * このモジュールは BTstack に依存しない。GAP 報告の解釈だけを行う。 */

#define WAKE_CAP_ADV_SIZE 31u
#define WAKE_CAP_MFG_SIZE 26u
#define WAKE_CAP_TABLE_SIZE 8u
#define WAKE_CAP_TYPE_WAKE 0x81u
#define WAKE_CAP_BLOB_SIZE 47u

#define WAKE_CAP_TAG_0 'W'
#define WAKE_CAP_TAG_1 'C'
#define WAKE_CAP_TAG_2 'P'
#define WAKE_CAP_TAG_3 '1'

typedef struct {
    uint16_t pid;
    uint8_t flag;
    uint8_t switch_mac[6];
    uint8_t payload[WAKE_CAP_ADV_SIZE];
    uint8_t payload_len;
} switch2_wake_capture_hit_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int rssi;
    uint32_t sightings;
    bool has_wake;
    switch2_wake_capture_hit_t wake;
} switch2_wake_capture_entry_t;

typedef struct {
    switch2_wake_capture_entry_t slot[WAKE_CAP_TABLE_SIZE];
    uint8_t used;
} switch2_wake_capture_table_t;

typedef struct {
    uint8_t spoof[6];
    uint8_t spoof_type;
    uint8_t switch_mac[6];
    uint8_t payload[WAKE_CAP_ADV_SIZE];
} switch2_wake_capture_saved_t;

uint32_t switch2_wake_capture_tag(void);
void switch2_wake_capture_table_init(switch2_wake_capture_table_t *table);
/* adv 全体（AD 構造の並び）を受け、任天堂コントローラ広告なら true。 */
bool switch2_wake_capture_parse(const uint8_t *adv, uint8_t adv_len,
                                switch2_wake_capture_hit_t *hit);
/* 表へ投入。空きが無ければ -1。RSSI は強い方を残す。 */
int switch2_wake_capture_offer(switch2_wake_capture_table_t *table,
                               const uint8_t addr[6], uint8_t addr_type,
                               int rssi,
                               const switch2_wake_capture_hit_t *hit);
/* wake 済み（flag 0x81 かつ本体 MAC 非ゼロ）の最良 slot。無ければ -1。 */
int switch2_wake_capture_best_wake(const switch2_wake_capture_table_t *table);
bool switch2_wake_capture_encode(const switch2_wake_capture_saved_t *saved,
                                 uint8_t *blob);
bool switch2_wake_capture_decode(const uint8_t *blob, uint32_t length,
                                 switch2_wake_capture_saved_t *saved);

#ifdef __cplusplus
}
#endif

#endif
