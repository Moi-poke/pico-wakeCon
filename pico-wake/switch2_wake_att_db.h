#ifndef SWITCH2_WAKE_ATT_DB_H
#define SWITCH2_WAKE_ATT_DB_H

#include <stdbool.h>
#include <stdint.h>

/* Switch 2 Pro Controller（★末尾4属性を含む版）の ATT DB を att_db_util で再現する。
 * 単一原本は WAKE_DESIGN 20章の属性表と 22章の呼出し順である。
 * ここには handle を定数で書かず、構築結果を実行時に検算する（22章 B394）。 */

/* 一次資料で確定している要の handle。検査でのみ使う。 */
#define WAKE_ATT_HANDLE_FIRST 0x0001u
#define WAKE_ATT_HANDLE_COMMON_INPUT 0x000au
#define WAKE_ATT_HANDLE_PRO_INPUT 0x000eu
#define WAKE_ATT_HANDLE_COMMAND_WRITE 0x0014u
#define WAKE_ATT_HANDLE_RESPONSE_NOTIFY 0x001au
#define WAKE_ATT_HANDLE_RESPONSE_CCCD 0x001bu
#define WAKE_ATT_HANDLE_LAST 0x0038u

/* 22章の★30段。表の順序そのものを識別子にする。 */
#define WAKE_ATT_STEP_COUNT 30u

/* ATT DB の上限。btstack_config.h の MAX_ATT_DB_SIZE と揃える。
 * ★実測: 128-bit UUID が 15 特性 + 6 descriptor あるため約 1040 byte 必要。
 *   BTstack の1属性は size2+flags2+handle2+uuid(2|16)+value である（att_db_util.c）。 */
#define WAKE_ATT_DB_MAX_SIZE 1536u

/* ★2026/09/02 実機: 段13 で 0x0017（期待 0x0018）になった。
 *   原因は btstack_config.h が古く MAX_ATT_DB_SIZE が 512 のままだったこと。
 *   段12 で 505 byte、段13 で 554 byte となり 512 をまたぐため、宣言だけ入って
 *   値が入らず handle が 1 しか進まなかった。
 *   同じことを二度起こさないよう、コンパイル時に止める。 */
#include "btstack_config.h"
#if defined(MAX_ATT_DB_SIZE) && (MAX_ATT_DB_SIZE < 1024)
#error "MAX_ATT_DB_SIZE is too small for the Switch 2 ATT DB (needs about 991 bytes). Re-export btstack_config.h."
#endif

/* ★★★2026/09/03: ペルソナを『ベース版』から『末尾4属性を含む版』へ変更した。
 *   根拠は switch2Lib.c（実物・実機キャプチャ値つき）である。
 *   0x002c rumble2 / 0x002e input_extra(+CCCD 002f) / 0x0030 desc / 0x0032 cmd_rumble2
 *   の4属性が要り、その結果 Generic Access 以降が +8 されて 0x0033 から始まる。
 *   ★W0 の時点でこの4属性と +8 offset の存在自体は把握していたが、
 *   『Wake に不要』と判断して外していた。推測ではなく判断の誤りである。
 *   ★★独自 Descriptor の権限（U09）も同時に決着した: READ | WRITE | DYNAMIC。
 *   本体は入力購読の直前に 0x0010 へ 0x85 を書き、拒否すると諦めて切断する。 */


typedef enum {
    WAKE_ATT_OK = 0,
    WAKE_ATT_ERR_STEP_HANDLE,   /* ある段の戻り handle が期待と違う */
    WAKE_ATT_ERR_LAST_HANDLE,   /* 最終 handle が 0x0038 でない */
    WAKE_ATT_ERR_SIZE,          /* DB サイズが上限を超えた */
    WAKE_ATT_ERR_PORT           /* port が未設定 */
} switch2_wake_att_result_t;

/* 構築結果。失敗した段を人が特定できるようにする。 */
typedef struct {
    switch2_wake_att_result_t result;
    uint8_t failed_step;        /* 1〜30。成功時は 0 */
    uint16_t expected_handle;
    uint16_t actual_handle;
    uint16_t last_handle;
    uint16_t db_size;
} switch2_wake_att_report_t;

/* att_db_util への port。机上検査では偽実装を差し込む。 */
typedef struct {
    void (*init)(void *ctx);
    uint16_t (*add_service_uuid128)(void *ctx, const uint8_t *uuid128);
    uint16_t (*add_service_uuid16)(void *ctx, uint16_t uuid16);
    uint16_t (*add_characteristic_uuid128)(void *ctx, const uint8_t *uuid128,
                                           uint16_t properties,
                                           uint8_t read_permission,
                                           uint8_t write_permission,
                                           uint8_t *data, uint16_t data_len);
    uint16_t (*add_characteristic_uuid16)(void *ctx, uint16_t uuid16,
                                          uint16_t properties,
                                          uint8_t read_permission,
                                          uint8_t write_permission,
                                          uint8_t *data, uint16_t data_len);
    uint16_t (*add_descriptor_uuid128)(void *ctx, const uint8_t *uuid128,
                                       uint16_t properties,
                                       uint8_t read_permission,
                                       uint8_t write_permission,
                                       uint8_t *data, uint16_t data_len);
    uint16_t (*get_size)(void *ctx);
    void *ctx;
} switch2_wake_att_port_t;

/* ★30段を順に実行し、各段の戻り handle を検算する。
 * 1段でも食い違えば即座に止め、どの段かを report へ残す。 */
switch2_wake_att_result_t switch2_wake_att_db_build(
    const switch2_wake_att_port_t *port, switch2_wake_att_report_t *report);

/* 22章の表そのもの。検査から参照して期待値の二重管理を防ぐ。 */
uint16_t switch2_wake_att_expected_handle(uint8_t step);
const char *switch2_wake_att_result_name(switch2_wake_att_result_t result);

#endif
