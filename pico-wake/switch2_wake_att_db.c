#include "switch2_wake_att_db.h"

#include <stddef.h>

/* WAKE_DESIGN 20章の属性表と 22章の呼出し順の実装。
 *
 * UUID は一次資料（ndeadly bluetooth_interface.md）から写した。
 * BTstack は 128-bit UUID を『そのままの並び』で受け取るため、
 * 表記 00c5af5d-1964-4e30-8f51-1956f96bd280 を先頭から順に並べる。
 *
 * ★U09（独自 Descriptor の権限）は一次資料に記載が無い（Properties 欄が '-'）。
 *   推測を1つだけ入れ、READ のみ・長さ0で置く。実機で読まれるかを観測して確定する。
 *   でたらめな『値』は返さない（BTGOAL 31章の教訓）。長さ0なので中身は無い。 */

#define ATT_PROP_READ 0x02u
#define ATT_PROP_WRITE 0x08u
#define ATT_PROP_WRITE_WITHOUT_RESPONSE 0x04u
#define ATT_PROP_NOTIFY 0x10u
#define ATT_PROP_DYNAMIC 0x100u

#define ATT_SEC_NONE 0u

/* --- Service 1: 00c5af5d-…d280（0x0001-0x0007） --- */
static const uint8_t UUID_SVC_A[16] = {
    0x00,0xc5,0xaf,0x5d,0x19,0x64,0x4e,0x30,
    0x8f,0x51,0x19,0x56,0xf9,0x6b,0xd2,0x80};
static const uint8_t UUID_A_281[16] = {
    0x00,0xc5,0xaf,0x5d,0x19,0x64,0x4e,0x30,
    0x8f,0x51,0x19,0x56,0xf9,0x6b,0xd2,0x81};
static const uint8_t UUID_A_282[16] = {
    0x00,0xc5,0xaf,0x5d,0x19,0x64,0x4e,0x30,
    0x8f,0x51,0x19,0x56,0xf9,0x6b,0xd2,0x82};
static const uint8_t UUID_A_283[16] = {
    0x00,0xc5,0xaf,0x5d,0x19,0x64,0x4e,0x30,
    0x8f,0x51,0x19,0x56,0xf9,0x6b,0xd2,0x83};

/* --- Service 2: ab7de9be-…7fd0（0x0008-0x002a） --- */
static const uint8_t UUID_SVC_B[16] = {
    0xab,0x7d,0xe9,0xbe,0x89,0xfe,0x49,0xad,
    0x82,0x8f,0x11,0x8f,0x09,0xdf,0x7f,0xd0};
static const uint8_t UUID_B_7FD2[16] = {   /* common input report 0x05 */
    0xab,0x7d,0xe9,0xbe,0x89,0xfe,0x49,0xad,
    0x82,0x8f,0x11,0x8f,0x09,0xdf,0x7f,0xd2};
static const uint8_t UUID_B_7FDE[16] = {   /* unknown read/notify */
    0xab,0x7d,0xe9,0xbe,0x89,0xfe,0x49,0xad,
    0x82,0x8f,0x11,0x8f,0x09,0xdf,0x7f,0xde};
static const uint8_t UUID_B_7FDF[16] = {   /* unknown output */
    0xab,0x7d,0xe9,0xbe,0x89,0xfe,0x49,0xad,
    0x82,0x8f,0x11,0x8f,0x09,0xdf,0x7f,0xdf};

/* --- Pro 固有（ペルソナ = Pro Controller ベース版） --- */
static const uint8_t UUID_PRO_INPUT[16] = {   /* 7492866c-… input report 0x09 */
    0x74,0x92,0x86,0x6c,0xec,0x3e,0x46,0x19,
    0x82,0x58,0x32,0x75,0x5f,0xfc,0xc0,0xf8};
static const uint8_t UUID_PRO_VIBRATION[16] = {   /* cc483f51-… */
    0xcc,0x48,0x3f,0x51,0x92,0x58,0x42,0x7d,
    0xa9,0x39,0x63,0x0c,0x31,0xf7,0x2b,0x05};
static const uint8_t UUID_COMMAND[16] = {   /* 649d4ac9-… 0x0014 */
    0x64,0x9d,0x4a,0xc9,0x8e,0xb7,0x4e,0x6c,
    0xaf,0x44,0x1e,0xa5,0x4f,0xe5,0xf0,0x05};
static const uint8_t UUID_PRO_RUMBLE_CMD[16] = {   /* 3dacbc7e-… */
    0x3d,0xac,0xbc,0x7e,0x69,0x55,0x40,0xb5,
    0x8e,0xaf,0x6f,0x98,0x09,0xe8,0xb3,0x79};
static const uint8_t UUID_LARGE_CMD[16] = {   /* 4147423d-… firmware update */
    0x41,0x47,0x42,0x3d,0xfd,0xae,0x4d,0xf7,
    0xa4,0xf7,0xd2,0x3e,0x5d,0xf5,0x9f,0x8d};
static const uint8_t UUID_RESPONSE[16] = {   /* c765a961-… 0x001a */
    0xc7,0x65,0xa9,0x61,0xd9,0xd8,0x4d,0x36,
    0xa2,0x0a,0x53,0x15,0xb1,0x11,0x83,0x6a};
static const uint8_t UUID_PRO_RESPONSE2[16] = {   /* 506d9f7d-… 0x001e Pro */
    0x50,0x6d,0x9f,0x7d,0x42,0x78,0x4e,0x95,
    0xa5,0x49,0x32,0x6b,0xa7,0x76,0x57,0xe0};
static const uint8_t UUID_UNKNOWN_NOTIFY[16] = {   /* d3bd69d2-… 0x0022 */
    0xd3,0xbd,0x69,0xd2,0x84,0x1c,0x42,0x41,
    0xab,0x15,0xf8,0x6f,0x40,0x6d,0x2a,0x80};
static const uint8_t UUID_PRO_INPUT2[16] = {   /* ★2026/09/03 追加 0x002e input_extra */
    0x74,0x92,0x86,0x6c,0xec,0x3e,0x46,0x19,
    0x82,0x58,0x32,0x75,0x5f,0xfc,0xc0,0xf9};
static const uint8_t UUID_PRO_VIBRATION2[16] = {   /* ★2026/09/03 追加 0x002c rumble2 */
    0xcc,0x48,0x3f,0x51,0x92,0x58,0x42,0x7d,
    0xa9,0x39,0x63,0x0c,0x31,0xf7,0x2b,0x06};
static const uint8_t UUID_PRO_RUMBLE_CMD2[16] = {   /* ★2026/09/03 追加 0x0032 cmd_rumble2 */
    0x3d,0xac,0xbc,0x7e,0x69,0x55,0x40,0xb5,
    0x8e,0xaf,0x6f,0x98,0x09,0xe8,0xb3,0x80};


/* ★★★2026/09/03: 標準 GAP の値は『静的』に載せる（実物と同じ）。
 *   ★実物 switch2Lib.c は uuid16 の2件に ATT_PROPERTY_DYNAMIC を付けず、
 *   値そのものを att_db_util へ渡している。
 *   ★★実物のコメント: 『uuid128 の characteristic には全て DYNAMIC が要る』
 *   → ★★★裏を返せば uuid16 には要らない、という意味である。
 *   ★私は DYNAMIC を付けて read callback から返していたが、
 *   実機では GATT 探索が始まらなかった（No.72/73）。 */
static const uint8_t GAP_DEVICE_NAME[] = "Pro Controller";
static const uint8_t GAP_APPEARANCE[2] = { 0x00u, 0x00u };
/* --- 独自 Descriptor（★U09 決着: READ|WRITE|DYNAMIC / 2026-09-03） --- */
static const uint8_t UUID_DESC_RATE[16] = {   /* 679d5510-… Set Report Rate? */
    0x67,0x9d,0x55,0x10,0x5a,0x24,0x4d,0xee,
    0x95,0x57,0x95,0xdf,0x80,0x48,0x6e,0xcb};
static const uint8_t UUID_DESC_UNKNOWN[16] = {   /* b746df8c-… Unknown */
    0xb7,0x46,0xdf,0x8c,0xf3,0x58,0x49,0x5b,
    0x9c,0xd2,0xe3,0xbb,0xed,0xa4,0xf9,0x79};

/* 22章の期待 handle。段番号 1〜★30 で引く。
 * characteristic は value handle、service と descriptor は宣言 handle。 */
static const uint16_t EXPECTED[WAKE_ATT_STEP_COUNT] = {
    0x0001u, 0x0003u, 0x0005u, 0x0007u,          /* 1-4  service A と3特性 */
    0x0008u, 0x000au, 0x000cu,                    /* 5-7  service B / 共通入力 / rate desc */
    0x000eu, 0x0010u,                             /* 8-9  Pro 入力 / rate desc */
    0x0012u, 0x0014u, 0x0016u, 0x0018u,           /* 10-13 出力4種 */
    0x001au, 0x001cu,                             /* 14-15 基本応答 / unknown desc */
    0x001eu, 0x0020u,                             /* 16-17 拡張応答 / unknown desc */
    0x0022u, 0x0024u,                             /* 18-19 unknown notify / desc */
    0x0026u, 0x0028u,                             /* 20-21 unknown read+notify / rate desc */
    0x002au,                                      /* 22    unknown output */
    0x002cu, 0x002eu, 0x0030u, 0x0032u,           /* ★23-26 2026/09/03 追加した末尾4属性 */
    0x0033u, 0x0035u, 0x0037u,                    /* 27-29 1800 / 2a00 / 2a01（★+8 された） */
    0x0038u                                       /* 30    1801（子属性なし） */
};

uint16_t switch2_wake_att_expected_handle(uint8_t step)
{
    if ((step < 1u) || (step > WAKE_ATT_STEP_COUNT)) {
        return 0u;
    }
    return EXPECTED[step - 1u];
}

const char *switch2_wake_att_result_name(switch2_wake_att_result_t result)
{
    switch (result) {
    case WAKE_ATT_OK: return "OK";
    case WAKE_ATT_ERR_STEP_HANDLE: return "STEP_HANDLE";
    case WAKE_ATT_ERR_LAST_HANDLE: return "LAST_HANDLE";
    case WAKE_ATT_ERR_SIZE: return "SIZE";
    case WAKE_ATT_ERR_PORT: return "PORT";
    default: return "UNKNOWN";
    }
}

/* 1段ぶんの検算。期待と違えば report へ残して false を返す。 */
static bool check_step(switch2_wake_att_report_t *report, uint8_t step,
                       uint16_t actual)
{
    uint16_t expected = switch2_wake_att_expected_handle(step);
    if (actual == expected) {
        return true;
    }
    report->result = WAKE_ATT_ERR_STEP_HANDLE;
    report->failed_step = step;
    report->expected_handle = expected;
    report->actual_handle = actual;
    return false;
}

switch2_wake_att_result_t switch2_wake_att_db_build(
    const switch2_wake_att_port_t *port, switch2_wake_att_report_t *report)
{
    switch2_wake_att_report_t local;
    uint16_t handle;
    uint8_t step = 0u;

    if (report == NULL) {
        return WAKE_ATT_ERR_PORT;
    }
    local.result = WAKE_ATT_OK;
    local.failed_step = 0u;
    local.expected_handle = 0u;
    local.actual_handle = 0u;
    local.last_handle = 0u;
    local.db_size = 0u;
    *report = local;

    if ((port == NULL) || (port->init == NULL) ||
        (port->add_service_uuid128 == NULL) ||
        (port->add_service_uuid16 == NULL) ||
        (port->add_characteristic_uuid128 == NULL) ||
        (port->add_characteristic_uuid16 == NULL) ||
        (port->add_descriptor_uuid128 == NULL) ||
        (port->get_size == NULL)) {
        report->result = WAKE_ATT_ERR_PORT;
        return report->result;
    }

    port->init(port->ctx);

/* 段を1つ進めて検算する。食い違えば即座に return する。 */
#define STEP(expr) \
    do { \
        step++; \
        handle = (expr); \
        if (!check_step(report, step, handle)) { \
            return report->result; \
        } \
    } while (0)

    /* 1-4: Service A と3つの特性 */
    STEP(port->add_service_uuid128(port->ctx, UUID_SVC_A));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_A_281,
        ATT_PROP_READ | ATT_PROP_DYNAMIC, ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_A_282,
        ATT_PROP_WRITE | ATT_PROP_DYNAMIC, ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_A_283,
        ATT_PROP_READ | ATT_PROP_DYNAMIC, ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 5-7: Service B、共通入力（CCCD 自動生成）、rate descriptor */
    STEP(port->add_service_uuid128(port->ctx, UUID_SVC_B));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_B_7FD2,
        ATT_PROP_READ | ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_RATE,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 8-9: Pro 入力（0x000e）と rate descriptor */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_INPUT,
        ATT_PROP_READ | ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_RATE,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 10-13: 出力4種（すべて write without response） */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_VIBRATION,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_COMMAND,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_RUMBLE_CMD,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_LARGE_CMD,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 14-15: 基本応答 notify（0x001a / CCCD 0x001b）と unknown descriptor */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_RESPONSE,
        ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_UNKNOWN,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 16-17: 拡張応答（Pro）と unknown descriptor */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_RESPONSE2,
        ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_UNKNOWN,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 18-19: unknown notify と unknown descriptor */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_UNKNOWN_NOTIFY,
        ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_UNKNOWN,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 20-21: unknown read+notify と rate descriptor */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_B_7FDE,
        ATT_PROP_READ | ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_RATE,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* 22: unknown output */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_B_7FDF,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));

    /* ★23-26: 2026/09/03 追加。switch2Lib.c（実物）が持つ末尾4属性。
     *   これが無いと Generic Access 以降の handle が 8 ずれる。 */
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_VIBRATION2,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_INPUT2,
        ATT_PROP_READ | ATT_PROP_NOTIFY | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_descriptor_uuid128(port->ctx, UUID_DESC_RATE,
        ATT_PROP_READ | ATT_PROP_WRITE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    STEP(port->add_characteristic_uuid128(port->ctx, UUID_PRO_RUMBLE_CMD2,
        ATT_PROP_WRITE_WITHOUT_RESPONSE | ATT_PROP_DYNAMIC,
        ATT_SEC_NONE, ATT_SEC_NONE, NULL, 0u));
    /* ★27-30: 標準 service（★+8 された）。1801 は子属性を持たない（20章）。 */
    STEP(port->add_service_uuid16(port->ctx, 0x1800u));
    STEP(port->add_characteristic_uuid16(port->ctx, 0x2a00u,
        ATT_PROP_READ, ATT_SEC_NONE, ATT_SEC_NONE,
        (uint8_t *)GAP_DEVICE_NAME, (uint16_t)(sizeof(GAP_DEVICE_NAME) - 1u)));
    STEP(port->add_characteristic_uuid16(port->ctx, 0x2a01u,
        ATT_PROP_READ, ATT_SEC_NONE, ATT_SEC_NONE,
        (uint8_t *)GAP_APPEARANCE, (uint16_t)sizeof(GAP_APPEARANCE)));
    STEP(port->add_service_uuid16(port->ctx, 0x1801u));

#undef STEP

    report->last_handle = handle;
    if (handle != WAKE_ATT_HANDLE_LAST) {
        report->result = WAKE_ATT_ERR_LAST_HANDLE;
        return report->result;
    }

    report->db_size = port->get_size(port->ctx);
    if (report->db_size > WAKE_ATT_DB_MAX_SIZE) {
        report->result = WAKE_ATT_ERR_SIZE;
        return report->result;
    }

    report->result = WAKE_ATT_OK;
    return WAKE_ATT_OK;
}
