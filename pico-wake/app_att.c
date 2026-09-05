#include <stdio.h>
#include <string.h>

#include "btstack.h"

#include "switch2_wake.h"
#include "switch2_wake_att_db.h"
#include "switch2_wake_cmd.h"
#include "app_state.h"
#include "app_common.h"
#include "app_att.h"

/* ★★★2026/09/03: switch2Lib.c（実物・実機キャプチャ）に合わせて実値を返す。
 *   実物のコメント: 『本体は接続後すぐ、用途不明サービスの READ を UUID 指定で読む。
 *   ここが空だと本体は先へ進まない』。
 *   従来は長さ0を返していた。中身を捏造しない方針（BTGOAL 31章）でそうしたが、
 *   本体にとっては『壊れている』と同じであり、そこが W5 停滞の原因だった。
 *   ★ここで返すのは推測値ではなく、利用者に出所を確認済みの実機キャプチャ値である。 */
static const uint8_t value_unknown_read1[] = {
    0x04u, 0x00u, 0x05u, 0x00u, 0x01u, 0x01u, 0x00u };
static const uint8_t value_unknown_read2[] = {
    0x44u, 0x3du, 0xd0u, 0x15u, 0xf8u, 0x9eu, 0x93u, 0x2eu };


/* 独自 Descriptor（U09 決着）。本体が書いた1 byte をそのまま返す。
 * 実物の att_read_callback_handle_byte と同じ振る舞いである。 */
#define WAKE_DESC_TABLE_SIZE 7u
static const uint16_t wake_desc_handles[WAKE_DESC_TABLE_SIZE] = {
    0x000cu, 0x0010u, 0x001cu, 0x0020u, 0x0024u, 0x0028u, 0x0030u };
static uint8_t wake_desc_values[WAKE_DESC_TABLE_SIZE];

/* 表に無ければ WAKE_DESC_TABLE_SIZE を返す（見つからなかったことを示す）。 */
static uint8_t wake_desc_index(uint16_t handle) {
    uint8_t i;
    for (i = 0u; i < WAKE_DESC_TABLE_SIZE; i++) {
        if (wake_desc_handles[i] == handle) {
            return i;
        }
    }
    return WAKE_DESC_TABLE_SIZE;
}

uint16_t att_read_callback(hci_con_handle_t con_handle,
                                  uint16_t attribute_handle, uint16_t offset,
                                  uint8_t *buffer, uint16_t buffer_size) {
    char text[96];
    uint8_t desc_index;
    (void)con_handle;
    att_read_count++;
    att_last_read_handle = attribute_handle;

    desc_index = wake_desc_index(attribute_handle);
    if (desc_index < WAKE_DESC_TABLE_SIZE) {
        att_desc_read_count++;
        snprintf(text, sizeof(text), "ATT-READ-DESC handle=%04x val=%02x n=%lu",
                 attribute_handle, (unsigned)wake_desc_values[desc_index],
                 (unsigned long)att_desc_read_count);
        uart_put_line(text);
        return att_read_callback_handle_byte(wake_desc_values[desc_index],
                                             offset, buffer, buffer_size);
    }

    snprintf(text, sizeof(text), "ATT-READ handle=%04x n=%lu",
             attribute_handle, (unsigned long)att_read_count);
    uart_put_line(text);

    switch (attribute_handle) {
    case APP_ATT_HANDLE_UNKNOWN_READ1:
        return att_read_callback_handle_blob(value_unknown_read1,
                                             (uint16_t)sizeof(value_unknown_read1),
                                             offset, buffer, buffer_size);
    case APP_ATT_HANDLE_UNKNOWN_READ2:
        return att_read_callback_handle_blob(value_unknown_read2,
                                             (uint16_t)sizeof(value_unknown_read2),
                                             offset, buffer, buffer_size);
    /* ★★★2026/09/03: Device Name(0x0035) / Appearance(0x0037) はここから外した。
     *   ★実物 switch2Lib.c は uuid16 の2件に DYNAMIC を付けず、値を
     *   att_db_util へ直接載せている。DYNAMIC を外すと本 callback は呼ばれず、
     *   ATT サーバが DB 内の値をそのまま返す。
     *   ★値の定義は switch2_wake_att_db.c 側へ移した（重複を避ける）。
     *   ★静的値方式は実物 BTstack と同一。MTU 交換の有無は合否に使わない。 */


    default:
        return 0u;   /* 値を持たない handle。ここは捏造しない */
    }
}

int att_write_callback(hci_con_handle_t con_handle,
                              uint16_t attribute_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size) {
    char text[128];
    uint8_t desc_index;
    (void)con_handle;
    (void)offset;
    if (transaction_mode != ATT_TRANSACTION_MODE_NONE) {
        return 0;   /* prepared write は使わない */
    }
    att_write_count++;
    att_last_write_handle = attribute_handle;

    /* ★★★2026/09/03: 独自 Descriptor への書き込みを受理する。
     *   実物のコメント: 『本体は入力レポートを購読する直前に 0x0010 へ 0x85 を書く。
     *   書き込みを拒否すると本体はそこで諦めて切断する』。 */
    desc_index = wake_desc_index(attribute_handle);
    if (desc_index < WAKE_DESC_TABLE_SIZE) {
        if ((buffer != NULL) && (buffer_size > 0u)) {
            wake_desc_values[desc_index] = buffer[0];
        }
        att_desc_write_count++;
        snprintf(text, sizeof(text), "ATT-WRITE-DESC handle=%04x val=%02x n=%lu",
                 attribute_handle,
                 (unsigned)((buffer_size > 0u) ? buffer[0] : 0u),
                 (unsigned long)att_desc_write_count);
        uart_put_line(text);
        return 0;
    }

    /* コマンドは観測して捨てず、実物と同じ応答状態機械へ渡す。 */
    if ((attribute_handle == WAKE_CMD_HANDLE_BASIC) ||
        (attribute_handle == WAKE_CMD_HANDLE_RUMBLE) ||
        (attribute_handle == WAKE_CMD_HANDLE_LARGE)) {
        switch2_wake_cmd_result_t cmd_result;
        att_cmd_write_count++;
        cmd_result = switch2_wake_cmd_write(&cmd_state, attribute_handle,
                                            buffer, buffer_size);
        snprintf(text, sizeof(text), "ATT-CMD handle=%04x len=%u result=%s n=%lu",
                 attribute_handle, (unsigned)buffer_size,
                 switch2_wake_cmd_result_name(cmd_result),
                 (unsigned long)att_cmd_write_count);
        uart_put_line(text); return 0;
    }

    /* CCCD への購読は接続成立の指標である（11章 A170）。 */
    if ((buffer != NULL) && (buffer_size >= 2u) &&
        ((attribute_handle == WAKE_ATT_HANDLE_RESPONSE_CCCD) ||
         (attribute_handle == APP_ATT_HANDLE_CCCD_INPUT))) {
        uint16_t value = (uint16_t)(buffer[0] | (buffer[1] << 8));
        if (attribute_handle == WAKE_ATT_HANDLE_RESPONSE_CCCD) {
            att_cccd_response = value;
        } else {
            att_cccd_input = value;
        }
        snprintf(text, sizeof(text), "ATT-CCCD handle=%04x value=%04x",
                 attribute_handle, value);
    } else {
        snprintf(text, sizeof(text), "ATT-WRITE handle=%04x len=%u n=%lu",
                 attribute_handle, (unsigned)buffer_size,
                 (unsigned long)att_write_count);
    }
    uart_put_line(text);
    return 0;
}
