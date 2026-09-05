#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
#include "hardware/uart.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "switch2_wake.h"
#include "switch2_wake_adv.h"
#include "switch2_wake_input.h"
#include "switch2_wake_store.h"
#include "switch2_wake_att_db.h"
#include "switch2_wake_cmd.h"
#include "switch2_wake_capture.h"
#include "app_state.h"
#include "app_config.h"

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUD 115200
#define POLL_MS 1
#define BUTTON_POLL_MS 10
#define FORGET_CONFIRM_MS 5000u

switch2_wake_adv_t adv;
switch2_wake_uart_parser_t uart_parser;
switch2_wake_store_t store;
bd_addr_t local_identity;
switch2_wake_att_report_t att_report;
hci_con_handle_t le_connection = HCI_CON_HANDLE_INVALID;
/* W5 観測用のカウンタ。Switch 2 が何を触ったかを数える。 */
uint32_t att_read_count;
uint32_t att_write_count;
uint32_t att_desc_read_count;
uint16_t att_last_read_handle;
uint16_t att_last_write_handle;
uint16_t att_cccd_response;
uint16_t att_cccd_input;        /* ★2026/09/03 追加 0x000f 入力購読 */
uint32_t att_desc_write_count;  /* ★2026/09/03 追加 Descriptor への write */
uint32_t att_cmd_write_count;   /* ★2026/09/03 追加 0x0014/0016/0018 */
uint32_t le_conn_ms;            /* ★2026/09/03 追加 接続からの経過を測る */
uint32_t att_event_count;       /* ★2026/09/03 追加 ATT イベントの総数 */
uint16_t att_mtu;               /* ★2026/09/03 追加 交換された MTU */
bd_addr_t last_peer_addr;       /* ★2026/09/03 追加 接続してきた本体のアドレス */
uint8_t last_peer_type;         /* ★同 アドレス種別（0=public 1=random） */
bool last_peer_valid;           /* ★同 一度でも接続があったか */
#ifdef WAKE_BUTTON_GPIO
switch2_wake_button_t button;
#endif
btstack_timer_source_t poll_timer;
btstack_packet_callback_registration_t hci_events;
bd_addr_t null_addr;
bool stack_ready;
#ifdef WAKE_BUTTON_GPIO
uint32_t button_poll_at_ms;
#endif
bool forget_armed;
uint32_t forget_deadline_ms;
bool hci_verbose;

/* Bill-git1 流キャプチャ＋リプレイ用。 */
switch2_wake_capture_table_t cap_table;
bool cap_scanning;
uint32_t cap_deadline_ms;
switch2_wake_capture_saved_t cap_saved;
bool cap_saved_valid;
bool beacon_active;
uint32_t beacon_deadline_ms;

/* Switch 2 独自コマンド・非同期応答。秘密値はログへ出さない。 */
switch2_wake_cmd_t cmd_state;
btstack_crypto_aes128_t cmd_aes_request;
switch2_wake_cmd_aes_done_t cmd_aes_done;
void *cmd_aes_done_ctx;
uint8_t cmd_aes_output[16];
uint8_t cmd_aes_key[16];       /* 非同期完了まで入力を保持する */
uint8_t cmd_aes_plain[16];     /* 呼出し元のスタックを参照しない */
uint8_t cmd_pending_response[WAKE_CMD_RESPONSE_MAX];
uint16_t cmd_pending_response_len;
bool cmd_response_pending;
uint32_t cmd_bond_ok;
uint32_t cmd_registered_ok;
static void reset_core(bool verified, uint32_t *actions, wake_result_t *result);

static void cmd_clear_pending_response(void) {
    memset(cmd_pending_response, 0, sizeof(cmd_pending_response));
    cmd_pending_response_len = 0u;
    cmd_response_pending = false;
    cmd_aes_done = NULL;
    cmd_aes_done_ctx = NULL;
    memset(cmd_aes_key, 0, sizeof(cmd_aes_key));
    memset(cmd_aes_plain, 0, sizeof(cmd_aes_plain));
    memset(cmd_aes_output, 0, sizeof(cmd_aes_output));
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool deadline_active(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) < 0;
}

static void uart_put_line(const char *text) {
    uart_puts(UART_ID, text);
    uart_puts(UART_ID, "\r\n");
}

static int port_set_params(void *context, uint16_t min, uint16_t max) {
    (void)context;
    gap_advertisements_set_params(min, max, 0x00, 0x00, null_addr, 0x07, 0x00);
    return 0;
}

static int port_set_data(void *context, const uint8_t *data, uint8_t length) {
    (void)context;
    gap_advertisements_set_data(length, (uint8_t *)data);
    return 0;
}

static int port_enable(void *context, bool enabled) {
    (void)context;
    gap_advertisements_enable(enabled ? 1 : 0);
    return 0;
}

/* --- W4: BTstack を store の port へ橋渡しする ---
 * 鍵は LE Device DB だけが持ち、WakeMeta は別タグの TLV に置く（24章）。 */
static const btstack_tlv_t *tlv_impl;
static void *tlv_context;

static uint32_t store_tlv_get(void *ctx, uint32_t tag, uint8_t *out,
                              uint32_t out_size) {
    (void)ctx;
    if (tlv_impl == NULL) return 0u;
    return (uint32_t)tlv_impl->get_tag(tlv_context, tag, out, out_size);
}

static bool store_tlv_store(void *ctx, uint32_t tag, const uint8_t *data,
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

static const switch2_wake_store_port_t store_port = {
    store_tlv_get, store_tlv_store, store_tlv_delete,
    store_db_max_count, store_db_info, store_db_has_ltk,
    store_db_add, store_db_set_ltk, store_db_remove, NULL
};

/* --- W5-W8: 独自コマンドを BTstack へ橋渡しする --- */
static bool cmd_get_local_addr(void *ctx, uint8_t addr[6]) {
    (void)ctx; gap_local_bd_addr(addr); return true;
}

static void cmd_aes_complete(void *arg) {
    switch2_wake_cmd_aes_done_t done = cmd_aes_done;
    void *done_ctx = cmd_aes_done_ctx;
    (void)arg;
    cmd_aes_done = NULL;
    cmd_aes_done_ctx = NULL;
    if (done != NULL) done(done_ctx, cmd_aes_output);
    memset(cmd_aes_key, 0, sizeof(cmd_aes_key));
    memset(cmd_aes_plain, 0, sizeof(cmd_aes_plain));
    memset(cmd_aes_output, 0, sizeof(cmd_aes_output));
}

static bool cmd_start_aes(void *ctx, const uint8_t key[16],
                          const uint8_t plain[16],
                          switch2_wake_cmd_aes_done_t done, void *done_ctx) {
    (void)ctx;
    if (key == NULL || plain == NULL || done == NULL || cmd_aes_done != NULL) return false;
    memcpy(cmd_aes_key, key, sizeof(cmd_aes_key));
    memcpy(cmd_aes_plain, plain, sizeof(cmd_aes_plain));
    cmd_aes_done = done;
    cmd_aes_done_ctx = done_ctx;
    btstack_crypto_aes128_encrypt(&cmd_aes_request, cmd_aes_key,
                                  cmd_aes_plain, cmd_aes_output,
                                  &cmd_aes_complete, NULL);
    return true;
}

static bool cmd_store_bond(void *ctx, uint8_t peer_type, const uint8_t peer[6],
                           const uint8_t ltk_natural[16]) {
    uint8_t ltk_wire[16]; unsigned int i;
    switch2_wake_store_result_t stored;
    (void)ctx;
    for (i = 0u; i < 16u; i++) ltk_wire[i] = ltk_natural[15u - i];
    stored = switch2_wake_store_import(&store, local_identity, peer_type,
                                       peer, ltk_wire, NULL);
    memset(ltk_wire, 0, sizeof(ltk_wire));
    if (stored != WAKE_STORE_OK) return false;
    switch2_wake_adv_set_peer(&adv, peer); cmd_bond_ok++; return true;
}

static bool cmd_read_memory(void *ctx, uint32_t address, uint8_t *out, uint8_t length) {
    char line[80];
    (void)ctx; (void)out;
    snprintf(line, sizeof(line), "CMD-MEM addr=%08lx len=%u unsupported",
             (unsigned long)address, (unsigned)length);
    uart_put_line(line);
    return false; /* 空ACKを返す。必要アドレスはこの観測で確定する。 */
}

static void cmd_emit_response(void *ctx, const uint8_t *packet, uint16_t length) {
    (void)ctx;
    if (packet == NULL || length > sizeof(cmd_pending_response)) return;
    memcpy(cmd_pending_response, packet, length);
    cmd_pending_response_len = length; cmd_response_pending = true;
    if (le_connection != HCI_CON_HANDLE_INVALID)
        att_server_request_can_send_now_event(le_connection);
}

static void cmd_on_registered(void *ctx) {
    uint32_t actions = 0u; wake_result_t result;
    (void)ctx;
    if (switch2_wake_store_commit(&store) != WAKE_STORE_OK) return;
    cmd_registered_ok++; reset_core(true, &actions, &result);
}

static void cmd_observe(void *ctx, uint8_t cmd, uint8_t sub,
                        uint16_t request_len, uint16_t response_len) {
    char line[112]; (void)ctx;
    snprintf(line, sizeof(line), "CMD cmd=%02x sub=%02x req=%u rsp=%u",
             cmd, sub, (unsigned)request_len, (unsigned)response_len);
    uart_put_line(line);
}

static void cmd_selftest_done(void *ctx, const uint8_t out[16]) {
    static const uint8_t expected[16] = {
        0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,
        0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed
    };
    (void)ctx;
    uart_put_line(memcmp(out, expected, 16u) == 0
        ? "SELFTEST XOR=OK AES=OK" : "SELFTEST XOR=OK AES=NG");
}

static void cmd_start_selftest(void) {
    uint8_t ltk[16], key[16], plain[16];
    if (!switch2_wake_cmd_selftest_values(ltk, key, plain)) {
        uart_put_line("SELFTEST XOR=NG AES=SKIP"); return;
    }
    if (!cmd_start_aes(NULL, key, plain, cmd_selftest_done, NULL))
        uart_put_line("SELFTEST XOR=OK AES=BUSY");
    memset(ltk,0,sizeof(ltk)); memset(key,0,sizeof(key)); memset(plain,0,sizeof(plain));
}

static const switch2_wake_cmd_port_t cmd_port = {
    NULL, cmd_get_local_addr, cmd_start_aes, cmd_store_bond, cmd_read_memory,
    cmd_emit_response, cmd_on_registered, cmd_observe
};

static bool get_ltk_callback(hci_con_handle_t handle, uint8_t address_type,
                             bd_addr_t addr, uint8_t *ltk) {
    int i, count, entry_type; bd_addr_t entry_addr;
    (void)handle;
    if (switch2_wake_cmd_get_ltk_for_peer(&cmd_state, address_type, addr, ltk)) return true;
    count = le_device_db_max_count();
    for (i = 0; i < count; i++) {
        le_device_db_info(i, &entry_type, entry_addr, NULL);
        if (entry_type != (int)address_type || memcmp(entry_addr, addr, 6u) != 0) continue;
        le_device_db_encryption_get(i, NULL, NULL, ltk, NULL, NULL, NULL, NULL);
        return true;
    }
    return false;
}

/* --- W5: att_db_util を builder の port へ橋渡しする --- */
static void att_port_init(void *ctx) { (void)ctx; att_db_util_init(); }

static uint16_t att_port_svc128(void *ctx, const uint8_t *uuid128) {
    (void)ctx;
    return att_db_util_add_service_uuid128(uuid128);
}

static uint16_t att_port_svc16(void *ctx, uint16_t uuid16) {
    (void)ctx;
    return att_db_util_add_service_uuid16(uuid16);
}

static uint16_t att_port_char128(void *ctx, const uint8_t *uuid128,
                                 uint16_t properties, uint8_t read_perm,
                                 uint8_t write_perm, uint8_t *data,
                                 uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_characteristic_uuid128(uuid128, properties,
                                                  read_perm, write_perm,
                                                  data, data_len);
}

static uint16_t att_port_char16(void *ctx, uint16_t uuid16,
                                uint16_t properties, uint8_t read_perm,
                                uint8_t write_perm, uint8_t *data,
                                uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_characteristic_uuid16(uuid16, properties,
                                                 read_perm, write_perm,
                                                 data, data_len);
}

static uint16_t att_port_desc128(void *ctx, const uint8_t *uuid128,
                                 uint16_t properties, uint8_t read_perm,
                                 uint8_t write_perm, uint8_t *data,
                                 uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_descriptor_uuid128(uuid128, properties,
                                              read_perm, write_perm,
                                              data, data_len);
}

static uint16_t att_port_size(void *ctx) { (void)ctx; return att_db_util_get_size(); }

static const switch2_wake_att_port_t att_port = {
    att_port_init, att_port_svc128, att_port_svc16,
    att_port_char128, att_port_char16, att_port_desc128,
    att_port_size, NULL
};

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

static uint16_t att_read_callback(hci_con_handle_t con_handle,
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
    case 0x0003u:
        return att_read_callback_handle_blob(value_unknown_read1,
                                             (uint16_t)sizeof(value_unknown_read1),
                                             offset, buffer, buffer_size);
    case 0x0007u:
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

static int att_write_callback(hci_con_handle_t con_handle,
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
         (attribute_handle == 0x000fu))) {
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

static void apply_actions(const char *name, wake_result_t result,
                          uint32_t actions) {
    char text[128];
    switch2_wake_adv_result_t adv_result = switch2_wake_adv_apply(&adv, actions);
    if ((actions & WAKE_ACTION_DISCONNECT) != 0u &&
        le_connection != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(le_connection);
    }
    snprintf(text, sizeof(text), "%s core=%d adv=%d actions=0x%02lx",
             name, (int)result, (int)adv_result, (unsigned long)actions);
    uart_put_line(text);
}

static void show_status(void) {
    switch2_wake_snapshot_t snapshot;
    char text[256];
    const uint8_t *payload = switch2_wake_adv_payload(&adv);
    if (switch2_wake_snapshot(&snapshot) != WAKE_RESULT_OK) {
        uart_put_line("S ERR not initialized");
        return;
    }
    snprintf(text, sizeof(text),
             "S state=%s peer=%u adv=%u wake=%u type=%02x count=%lu req=%lu start=%lu timeout=%lu rx_ok=%lu rx_ng=%lu rx_over=%lu button=%u",
             switch2_wake_state_name(snapshot.state), adv.peer_known ? 1u : 0u,
             adv.enabled ? 1u : 0u, switch2_wake_adv_is_wake(&adv) ? 1u : 0u,
             payload[SWITCH2_WAKE_ADV_TYPE_OFFSET], (unsigned long)adv.apply_count,
             (unsigned long)snapshot.wake_requested,
             (unsigned long)snapshot.wake_started,
             (unsigned long)snapshot.wake_timeout,
             (unsigned long)uart_parser.accepted,
             (unsigned long)uart_parser.rejected,
             (unsigned long)uart_parser.overflow,
#ifdef WAKE_BUTTON_GPIO
             1u
#else
             0u
#endif
    );
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "STORE meta=%s db=%d src=%u last=%s",
             store.meta_loaded ? app_meta_state_name(store.meta.state) : "NONE",
             store.db_index, (unsigned)store.meta.source,
             switch2_wake_store_result_name(store.last_result));
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "ATT db=%u last=%04x reads=%lu writes=%lu conn=%u enc=%u",
             (unsigned)att_report.db_size, att_report.last_handle,
             (unsigned long)att_read_count, (unsigned long)att_write_count,
             (le_connection == HCI_CON_HANDLE_INVALID) ? 0u : 1u,
             (le_connection == HCI_CON_HANDLE_INVALID) ? 0u
                 : (unsigned)gap_encryption_key_size(le_connection));
    uart_put_line(text);
    /* ★2026/09/03 追加: Descriptor とコマンド経路を別に数える。
     *   read/write が『どの種類の属性に来たか』が分かれば原因を絞れる。 */
    snprintf(text, sizeof(text),
             "ATT2 desc_rd=%lu desc_wr=%lu cmd=%lu cccd_rsp=%04x cccd_in=%04x attev=%lu mtu=%u",
             (unsigned long)att_desc_read_count, (unsigned long)att_desc_write_count,
             (unsigned long)att_cmd_write_count, att_cccd_response, att_cccd_input,
             (unsigned long)att_event_count, (unsigned)att_mtu);
    uart_put_line(text);
    snprintf(text, sizeof(text),
             "CMD rx=%lu rsp=%lu ng=%lu last=%02x/%02x fp=%04x bond=%lu reg=%lu",
             (unsigned long)cmd_state.received, (unsigned long)cmd_state.responded,
             (unsigned long)cmd_state.rejected, cmd_state.last_cmd, cmd_state.last_sub,
             switch2_wake_cmd_fingerprint(&cmd_state),
             (unsigned long)cmd_bond_ok, (unsigned long)cmd_registered_ok);
    uart_put_line(text);

    /* ★★★2026/09/03 W8-a: Wake 広告の宛先に使う本体アドレスを出す。
     *   ★T コマンドへ渡す12桁は、この PEER の並びをそのまま使う。 */
    if (last_peer_valid) {
        snprintf(text, sizeof(text),
                 "PEER type=%u addr=%02x%02x%02x%02x%02x%02x",
                 (unsigned)last_peer_type,
                 last_peer_addr[0], last_peer_addr[1], last_peer_addr[2],
                 last_peer_addr[3], last_peer_addr[4], last_peer_addr[5]);
    } else {
        snprintf(text, sizeof(text),
                 "PEER none (connect once from the console to learn it)");
    }
    uart_put_line(text);
}

static void reset_core(bool verified, uint32_t *actions,
                       wake_result_t *result) {
    switch2_wake_config_t config = app_make_config();
    *result = switch2_wake_init(&config);
    if (*result == WAKE_RESULT_OK) {
        *result = switch2_wake_start(verified, now_ms(), actions);
    }
}

static void cap_restore_adv(const char *name) {
    uint32_t actions = 0u;
    wake_result_t result;
    switch2_wake_adv_result_t adv_result;
    if (adv.peer_known) {
        adv_result = switch2_wake_adv_publish_reconnect(&adv);
        result = WAKE_RESULT_OK;
    } else {
        actions = WAKE_ACTION_ADV_STANDARD;
        adv_result = switch2_wake_adv_apply(&adv, actions);
        result = WAKE_RESULT_OK;
    }
    {
        char text[96];
        snprintf(text, sizeof(text), "%s core=%d adv=%d actions=0x%02lx",
                 name, (int)result, (int)adv_result, (unsigned long)actions);
        uart_put_line(text);
    }
}

/* CYW43 の public アドレスを上書きする（RPi の btmgmt public-addr 相当）。
 * 表記順で渡し、HCI へは LSB 先行で送る。 */
static void bdaddr_spoof(const uint8_t canonical[6]) {
    uint8_t pkt[9];
    int i;
    pkt[0] = 0x01u;
    pkt[1] = 0xfcu;
    pkt[2] = 0x06u;
    for (i = 0; i < 6; i++) {
        pkt[3 + i] = canonical[5 - i];
    }
    hci_send_cmd_packet(pkt, (int)sizeof(pkt));
    hci_send_cmd(&hci_read_bd_addr);
}

static void cap_show_list(void) {
    char text[128];
    uint8_t i;
    for (i = 0u; i < cap_table.used; i++) {
        const switch2_wake_capture_entry_t *e = &cap_table.slot[i];
        snprintf(text, sizeof(text),
                 "CAP-LIST %u mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x wake=%u rssi=%d n=%lu",
                 (unsigned)i,
                 e->addr[0], e->addr[1], e->addr[2],
                 e->addr[3], e->addr[4], e->addr[5],
                 (unsigned)e->addr_type, (unsigned)e->wake.pid,
                 e->has_wake ? 1u : 0u, e->rssi,
                 (unsigned long)e->sightings);
        uart_put_line(text);
    }
    if (cap_saved_valid) {
        snprintf(text, sizeof(text),
                 "CAP-SAVED spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                 cap_saved.spoof[0], cap_saved.spoof[1], cap_saved.spoof[2],
                 cap_saved.spoof[3], cap_saved.spoof[4], cap_saved.spoof[5],
                 cap_saved.switch_mac[0], cap_saved.switch_mac[1],
                 cap_saved.switch_mac[2], cap_saved.switch_mac[3],
                 cap_saved.switch_mac[4], cap_saved.switch_mac[5]);
    } else {
        snprintf(text, sizeof(text), "CAP-SAVED none");
    }
    uart_put_line(text);
}

static void handle_command(switch2_wake_input_command_t *command) {
    uint32_t actions = 0u;
    wake_result_t result = WAKE_RESULT_OK;
    switch (command->kind) {
        case WAKE_INPUT_WAKE:
            result = switch2_wake_request(WAKE_SOURCE_UART, now_ms(), &actions);
            apply_actions("W", result, actions);
            break;
        case WAKE_INPUT_PAIR:
            result = switch2_wake_pair_request(WAKE_SOURCE_UART, now_ms(), &actions);
            apply_actions("P", result, actions);
            break;
        case WAKE_INPUT_STATUS:
            show_status();
            break;
        case WAKE_INPUT_ADV_ENABLE:
            actions = command->adv_enabled ? WAKE_ACTION_ADV_STANDARD
                                           : WAKE_ACTION_ADV_STOP;
            apply_actions(command->adv_enabled ? "V1" : "V0", result, actions);
            break;
        case WAKE_INPUT_FORGET_ARM:
            forget_armed = true;
            forget_deadline_ms = now_ms() + FORGET_CONFIRM_MS;
            uart_put_line("K ARMED; send K CONFIRM within 5 seconds");
            break;
        case WAKE_INPUT_FORGET_CONFIRM:
            if (!forget_armed || !deadline_active(now_ms(), forget_deadline_ms)) {
                forget_armed = false;
                uart_put_line("K ERR NOT_ARMED");
                break;
            }
            forget_armed = false;
            switch2_wake_adv_clear_peer(&adv);
            (void)switch2_wake_store_forget(&store);
            cmd_clear_pending_response();
            switch2_wake_cmd_clear_pairing(&cmd_state);
            result = switch2_wake_forget(WAKE_SOURCE_UART, &actions);
            apply_actions("K", result, actions);
            break;
        case WAKE_INPUT_TEST_PEER:
            switch2_wake_adv_set_peer(&adv, command->peer_identity);
            reset_core(true, &actions, &result);
            uart_put_line("T OK test peer only; this is not a verified bond");
            apply_actions("T", result, actions);
            break;
        case WAKE_INPUT_TEST_CLEAR:
            switch2_wake_adv_clear_peer(&adv);
            reset_core(false, &actions, &result);
            apply_actions("X", result, actions);
            break;
        case WAKE_INPUT_CAPTURE: {
            char text[64];
            if (cap_scanning || beacon_active) {
                uart_put_line("C ERR BUSY");
                break;
            }
            if (le_connection != HCI_CON_HANDLE_INVALID) {
                uart_put_line("C ERR CONNECTED");
                break;
            }
            switch2_wake_capture_table_init(&cap_table);
            (void)switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STOP);
            gap_set_scan_params(0u, 0x0030u, 0x0030u, 0u);
            gap_set_scan_duplicate_filter(false);
            gap_start_scan();
            cap_scanning = true;
            cap_deadline_ms = now_ms() + (uint32_t)command->capture_seconds * 1000u;
            snprintf(text, sizeof(text), "CAP-START dur=%us; press HOME on the Joy-Con",
                     (unsigned)command->capture_seconds);
            uart_put_line(text);
            break;
        }
        case WAKE_INPUT_CAP_LIST:
            cap_show_list();
            break;
        case WAKE_INPUT_DEBUG:
            hci_verbose = !hci_verbose;
            uart_put_line(hci_verbose ? "D verbose on" : "D verbose off");
            break;
        case WAKE_INPUT_BEACON: {
            uint8_t payload[WAKE_CAP_ADV_SIZE];
            if (!cap_saved_valid) {
                uart_put_line("B ERR NO_SAVE; run C first");
                break;
            }
            if (cap_scanning || beacon_active) {
                uart_put_line("B ERR BUSY");
                break;
            }
            if (le_connection != HCI_CON_HANDLE_INVALID) {
                uart_put_line("B ERR CONNECTED");
                break;
            }
            (void)switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_STOP);
            bdaddr_spoof(cap_saved.spoof);
            memcpy(payload, cap_saved.payload, sizeof(payload));
            payload[16] = WAKE_CAP_TYPE_WAKE;
            gap_advertisements_set_data((uint8_t)sizeof(payload), payload);
            gap_advertisements_enable(1);
            beacon_active = true;
            beacon_deadline_ms = now_ms() + 1500u;
            uart_put_line("BCN-START 1.5s; keep the Joy-Con powered off");
            memset(payload, 0, sizeof(payload));
            break;
        }
        case WAKE_INPUT_IMPORT_BOND: {
            switch2_wake_store_result_t stored = switch2_wake_store_import(
                &store, command->local_identity, command->peer_type,
                command->peer_identity, command->ltk,
                command->irk_present ? command->irk : NULL);
            char reply[96];
            if (stored != WAKE_STORE_OK) {
                snprintf(reply, sizeof(reply), "I ERR %s",
                         switch2_wake_store_result_name(stored));
                uart_put_line(reply);
                break;
            }
            /* 投入直後は PENDING。暗号化と 0x0c/04 を見るまで Wake は許さない。 */
            switch2_wake_adv_set_peer(&adv, command->peer_identity);
            reset_core(false, &actions, &result);
            uart_put_line("I OK PENDING; encrypted reconnect is required before wake");
            apply_actions("I", result, actions);
            break;
        }
        default:
            uart_put_line("ERR commands: W | P | I ... | K ARM | K CONFIRM | S | V 0/1 | T <12hex> | X | C [s] | L | B | D");
            break;
    }
    switch2_wake_input_command_clear(command);
}

static void poll_uart(void) {
    switch2_wake_input_command_t command;
    while (uart_is_readable(UART_ID)) {
        char ch = (char)uart_getc(UART_ID);
        if (switch2_wake_uart_feed(&uart_parser, ch, &command)) {
            handle_command(&command);
        }
    }
}

#ifdef WAKE_BUTTON_GPIO
static void handle_button(switch2_wake_button_event_t event) {
    uint32_t actions = 0u;
    wake_result_t result;
    if (event == WAKE_BUTTON_WAKE) {
        result = switch2_wake_request(WAKE_SOURCE_BUTTON, now_ms(), &actions);
        apply_actions("BUTTON-W", result, actions);
    } else if (event == WAKE_BUTTON_PAIR) {
        result = switch2_wake_pair_request(WAKE_SOURCE_BUTTON, now_ms(), &actions);
        apply_actions("BUTTON-P", result, actions);
    } else if (event == WAKE_BUTTON_FORGET) {
        switch2_wake_adv_clear_peer(&adv);
        (void)switch2_wake_store_forget(&store);
        cmd_clear_pending_response();
        switch2_wake_cmd_clear_pairing(&cmd_state);
        result = switch2_wake_forget(WAKE_SOURCE_BUTTON, &actions);
        apply_actions("BUTTON-K", result, actions);
    }
}
#endif

static void poll_handler(btstack_timer_source_t *timer) {
    uint32_t actions = 0u;
    wake_result_t result;
    poll_uart();
#ifdef WAKE_BUTTON_GPIO
    if ((int32_t)(now_ms() - button_poll_at_ms) >= 0) {
        button_poll_at_ms = now_ms() + BUTTON_POLL_MS;
        handle_button(switch2_wake_button_sample(
        &button, gpio_get(WAKE_BUTTON_GPIO) == 0, now_ms()));
    }
#endif
    if (forget_armed && !deadline_active(now_ms(), forget_deadline_ms)) {
        forget_armed = false;
    }
    if (cap_scanning && !deadline_active(now_ms(), cap_deadline_ms)) {
        char text[112];
        int best;
        gap_stop_scan();
        cap_scanning = false;
        best = switch2_wake_capture_best_wake(&cap_table);
        if (best >= 0) {
            const switch2_wake_capture_entry_t *e = &cap_table.slot[best];
            uint8_t blob[WAKE_CAP_BLOB_SIZE];
            cap_saved_valid = false;
            memcpy(cap_saved.spoof, e->addr, 6);
            cap_saved.spoof_type = e->addr_type;
            memcpy(cap_saved.switch_mac, e->wake.switch_mac, 6);
            memcpy(cap_saved.payload, e->wake.payload, WAKE_CAP_ADV_SIZE);
            if (switch2_wake_capture_encode(&cap_saved, blob) &&
                store_tlv_store(NULL, switch2_wake_capture_tag(),
                                blob, (uint32_t)sizeof(blob))) {
                cap_saved_valid = true;
            }
            memset(blob, 0, sizeof(blob));
            snprintf(text, sizeof(text),
                     "CAP-DONE saved=%u spoof=%02x%02x%02x%02x%02x%02x sw=%02x%02x%02x%02x%02x%02x",
                     cap_saved_valid ? 1u : 0u,
                     e->addr[0], e->addr[1], e->addr[2],
                     e->addr[3], e->addr[4], e->addr[5],
                     e->wake.switch_mac[0], e->wake.switch_mac[1],
                     e->wake.switch_mac[2], e->wake.switch_mac[3],
                     e->wake.switch_mac[4], e->wake.switch_mac[5]);
        } else {
            snprintf(text, sizeof(text), "CAP-DONE saved=0 seen=%u; no wake flag",
                     (unsigned)cap_table.used);
        }
        uart_put_line(text);
        cap_restore_adv("CAP");
    }
    if (beacon_active && !deadline_active(now_ms(), beacon_deadline_ms)) {
        beacon_active = false;
        gap_advertisements_enable(0);
        bdaddr_spoof(local_identity);
        uart_put_line("BCN-DONE; address restored");
        cap_restore_adv("BCN");
    }
    result = switch2_wake_tick(now_ms(), &actions);
    if (actions != WAKE_ACTION_NONE) apply_actions("TIMER", result, actions);
    btstack_run_loop_set_timer(timer, POLL_MS);
    btstack_run_loop_add_timer(timer);
}

/* ★W5-2: Security Manager のできごとを観測する。
 * 「LE 接続は来るが属性を触らない」原因が暗号化待ちなのかを、
 * 推測ではなく実物のイベントで見分けるために置く。 */
static btstack_packet_callback_registration_t sm_events;

/* W5-W8: ATT サーバの接続・MTU・送信可能イベントを観測する。
 *   ATT_EVENT_CAN_SEND_NOW では保留中のコマンド応答を handle 0x001e へ送る。
 *   MTU 交換は Switch 2 の必須手順ではないため、来ないことを異常扱いしない。
 *   受信・応答件数は CMD 行と ATT-CAN-SEND 行で個別に追跡する。
 */
static void att_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    char line[112];
    uint8_t ev;
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    ev = hci_event_packet_get_type(packet);
    att_event_count++;
    if (ev == ATT_EVENT_CAN_SEND_NOW) {
        if (cmd_response_pending && le_connection != HCI_CON_HANDLE_INVALID) {
            att_server_notify(le_connection, WAKE_CMD_RESPONSE_HANDLE,
                              cmd_pending_response, cmd_pending_response_len);
            cmd_response_pending = false;
        }
        snprintf(line, sizeof(line), "ATT-CAN-SEND rsp=%u n=%lu",
                 (unsigned)cmd_pending_response_len, (unsigned long)att_event_count);
    } else
    if (ev == ATT_EVENT_MTU_EXCHANGE_COMPLETE) {
        att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        snprintf(line, sizeof(line), "ATT-MTU mtu=%u n=%lu",
                 (unsigned)att_mtu, (unsigned long)att_event_count);
    } else if (ev == ATT_EVENT_CONNECTED) {
        snprintf(line, sizeof(line), "ATT-CONNECTED n=%lu",
                 (unsigned long)att_event_count);
    } else if (ev == ATT_EVENT_DISCONNECTED) {
        snprintf(line, sizeof(line), "ATT-DISCONNECTED n=%lu",
                 (unsigned long)att_event_count);
    } else {
        snprintf(line, sizeof(line), "ATT-EV ev=%02x len=%u n=%lu",
                 ev, (unsigned)size, (unsigned long)att_event_count);
    }
    uart_put_line(line);
}

static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    char line[112];
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        uart_put_line("SM just-works request -> accept");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_PAIRING_STARTED:
        uart_put_line("SM pairing started");
        break;
    case SM_EVENT_PAIRING_COMPLETE:
        snprintf(line, sizeof(line), "SM pairing complete status=%02x reason=%02x",
                 sm_event_pairing_complete_get_status(packet),
                 sm_event_pairing_complete_get_reason(packet));
        uart_put_line(line);
        break;
    case SM_EVENT_REENCRYPTION_STARTED:
        uart_put_line("SM reencryption started");
        break;
    case SM_EVENT_REENCRYPTION_COMPLETE:
        snprintf(line, sizeof(line), "SM reencryption complete status=%02x",
                 sm_event_reencryption_complete_get_status(packet));
        uart_put_line(line);
        break;
    case SM_EVENT_IDENTITY_RESOLVING_STARTED:
        uart_put_line("SM identity resolving started (0xcd)");
        break;
    case SM_EVENT_IDENTITY_RESOLVING_FAILED:
        uart_put_line("SM identity resolving failed (0xce) - peer is unknown");
        break;
    case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
        uart_put_line("SM identity resolving succeeded (0xcf)");
        break;
    case SM_EVENT_IDENTITY_CREATED:
        uart_put_line("SM identity created");
        break;
    default:
        snprintf(line, sizeof(line), "SM event=%02x",
                 hci_event_packet_get_type(packet));
        uart_put_line(line);
        break;
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    uint32_t actions = 0u;
    wake_result_t result;
    switch2_wake_config_t config = app_make_config();
    char boot_text[128];
    bool verified;
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    /* ★W5: LE の接続・切断を観測する。 */
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            /* ★★★2026/09/03 W8-a: 接続してきた本体のアドレスを出す。
             *   ★Wake 広告には『本体の BD_ADDR を逆順で載せる』決まりがあり、
             *   その値を知る手段が無いと Wake を試せない。
             *   ★★実物 switch2Lib.c も同じ場所で peer_address を控えている。
             *   ★★★秘密値ではない（公開されるアドレス）のでそのまま出す。 */
            char conn_line[112];
            bd_addr_t peer_addr;
            uint8_t peer_type;
            le_connection =
                gap_subevent_le_connection_complete_get_connection_handle(packet);
            gap_subevent_le_connection_complete_get_peer_address(packet, peer_addr);
            peer_type =
                gap_subevent_le_connection_complete_get_peer_address_type(packet);
            memcpy(last_peer_addr, peer_addr, sizeof(last_peer_addr));
            last_peer_type = peer_type;
            last_peer_valid = true;
            cmd_clear_pending_response();
            switch2_wake_cmd_set_peer(&cmd_state, peer_type, peer_addr);
            /* 2026/09/04: 接続直後の標準パラメータ更新要求を廃止。
             * 純正は送らない (ndeadly hurdle #4) し、Switch2側がベンダーHCIで
             * 5msに決めるため衝突して切断される恐れがある。 */
            le_conn_ms = now_ms();
            snprintf(conn_line, sizeof(conn_line),
                     "LE-CONN handle=%04x type=%u peer=%02x%02x%02x%02x%02x%02x t=%lu",
                     le_connection, (unsigned)peer_type,
                     peer_addr[0], peer_addr[1], peer_addr[2],
                     peer_addr[3], peer_addr[4], peer_addr[5],
                     (unsigned long)le_conn_ms);
            uart_put_line(conn_line);
        }
        return;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        char disc_line[112];
        snprintf(disc_line, sizeof(disc_line),
                 "LE-DISC reason=%02x dt=%lu rd=%lu wr=%lu drd=%lu dwr=%lu cmd=%lu",
                 hci_event_disconnection_complete_get_reason(packet),
                 (unsigned long)(now_ms() - le_conn_ms),
                 (unsigned long)att_read_count,
                 (unsigned long)att_write_count,
                 (unsigned long)att_desc_read_count,
                 (unsigned long)att_desc_write_count,
                 (unsigned long)att_cmd_write_count);
        uart_put_line(disc_line);
        cmd_clear_pending_response();
        le_connection = HCI_CON_HANDLE_INVALID;
        if (adv.peer_known && (cmd_state.ltk_valid || store.meta_loaded)) {
            (void)switch2_wake_adv_publish_reconnect(&adv);
        }
        return;
    }
    case GAP_EVENT_ADVERTISING_REPORT: {
        /* キャプチャ中だけ拾う。通常時は無視して UART を汚さない。 */
        if (cap_scanning && packet_type == HCI_EVENT_PACKET) {
            bd_addr_t raddr;
            uint8_t rtype;
            int rssi;
            uint8_t dlen;
            const uint8_t *data;
            switch2_wake_capture_hit_t hit;
            gap_event_advertising_report_get_address(packet, raddr);
            rtype = gap_event_advertising_report_get_address_type(packet);
            rssi = (int)gap_event_advertising_report_get_rssi(packet);
            dlen = gap_event_advertising_report_get_data_length(packet);
            data = gap_event_advertising_report_get_data(packet);
            if (switch2_wake_capture_parse(data, dlen, &hit)) {
                char cap_line[128];
                int idx = switch2_wake_capture_offer(&cap_table, raddr,
                                                     rtype, rssi, &hit);
                snprintf(cap_line, sizeof(cap_line),
                         "CAP mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x flag=%02x sw=%02x%02x%02x%02x%02x%02x rssi=%d idx=%d",
                         raddr[0], raddr[1], raddr[2],
                         raddr[3], raddr[4], raddr[5],
                         (unsigned)rtype, (unsigned)hit.pid,
                         (unsigned)hit.flag,
                         hit.switch_mac[0], hit.switch_mac[1],
                         hit.switch_mac[2], hit.switch_mac[3],
                         hit.switch_mac[4], hit.switch_mac[5],
                         rssi, idx);
                uart_put_line(cap_line);
            }
        }
        return;
    }
    case BTSTACK_EVENT_STATE:
        break;
    default:
        /* ★★★2026/09/03 W5-4: 段8-b1 と同じ手を採る。
         *   ここまで私は5回続けて推測で外した（SM設定・authreq・GATT構成・
         *   READ値・Descriptor write）。★どれも『繋がるのに GATT が来ない』を
         *   動かせなかった。★★段8-b1 のときも同じ地点で止まり、HCI のやり取りを
         *   そのまま出して解決した。★★★推測をやめ、実物を見る。
         *   ★接続後に本体が何を送ってきているかを、番号のまま全部出す。 */
        {
            char ev_line[128];
            uint8_t ev = hci_event_packet_get_type(packet);
            if ((ev == HCI_EVENT_LE_META) && (size >= 12u) &&
                (packet[2] == HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-PARAM-DONE status=%02x interval=%u latency=%u timeout=%u",
                         packet[3],
                         (unsigned)little_endian_read_16(packet, 6),
                         (unsigned)little_endian_read_16(packet, 8),
                         (unsigned)little_endian_read_16(packet, 10));
                uart_put_line(ev_line);
            }
            if ((ev == HCI_EVENT_LE_META) && (size >= 20u) &&
                (packet[2] == HCI_SUBEVENT_LE_CONNECTION_COMPLETE)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-INTERVAL interval=%u latency=%u timeout=%u",
                         (unsigned)little_endian_read_16(packet, 14),
                         (unsigned)little_endian_read_16(packet, 16),
                         (unsigned)little_endian_read_16(packet, 18));
                uart_put_line(ev_line);
            }
            if ((ev == HCI_EVENT_LE_META) && (size >= 33u) &&
                (packet[2] == HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1)) {
                snprintf(ev_line, sizeof(ev_line),
                         "CONN-INTERVAL interval=%u latency=%u timeout=%u",
                         (unsigned)little_endian_read_16(packet, 26),
                         (unsigned)little_endian_read_16(packet, 28),
                         (unsigned)little_endian_read_16(packet, 30));
                uart_put_line(ev_line);
            }
            uint16_t i;
            uint16_t n;
            int pos;
            /* 通常は要約行だけ出す。生ダンプは D で切り替える。 */
            if (!hci_verbose) {
                return;
            }
            n = (size > 14u) ? 14u : size;
            pos = snprintf(ev_line, sizeof(ev_line),
                           "[HCI] ev=%02x len=%u data=", ev, (unsigned)size);
            for (i = 0u; (i < n) && (pos > 0) &&
                 ((unsigned)pos + 3u < sizeof(ev_line)); i++) {
                pos += snprintf(&ev_line[pos],
                                sizeof(ev_line) - (unsigned)pos,
                                "%02x ", packet[i]);
            }
            uart_put_line(ev_line);
        }
        return;
    }
    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
    if (stack_ready) return;
    stack_ready = true;
    cmd_start_selftest();
    /* ★2026/09/03 W5-6: ATT の構築とサーバ開始は main() へ移した（実物と同じ順序）。 */

    /* W4: TLV と LE Device DB が使える状態になってから照合する（初期化順4）。 */
    btstack_tlv_get_instance(&tlv_impl, &tlv_context);
    switch2_wake_store_init(&store, &store_port);
    gap_local_bd_addr(local_identity);
    (void)switch2_wake_store_load(&store);
    verified = switch2_wake_store_verify(&store, local_identity) == WAKE_STORE_OK;
    if (verified) {
        switch2_wake_adv_set_peer(&adv, store.meta.peer_identity);
    }
    snprintf(boot_text, sizeof(boot_text),
             "BOOT meta=%s db=%d verified=%u reason=%s",
             store.meta_loaded ? app_meta_state_name(store.meta.state) : "NONE",
             store.db_index, verified ? 1u : 0u,
             switch2_wake_store_result_name(store.last_result));
    uart_put_line(boot_text);
    /* Bill-git1 流の保存済みキャプチャを復元する。 */
    {
        uint8_t blob[WAKE_CAP_BLOB_SIZE];
        uint32_t got;
        memset(blob, 0, sizeof(blob));
        got = store_tlv_get(NULL, switch2_wake_capture_tag(),
                            blob, (uint32_t)sizeof(blob));
        cap_saved_valid = switch2_wake_capture_decode(blob, got, &cap_saved);
        memset(blob, 0, sizeof(blob));
        uart_put_line(cap_saved_valid ? "CAP-SAVED loaded" : "CAP-SAVED none");
    }

    result = switch2_wake_init(&config);
    if (result == WAKE_RESULT_OK) {
        result = switch2_wake_start(verified, now_ms(), &actions);
    }
    apply_actions("BOOT", result, actions);
    if (verified && adv.peer_known) {
        (void)switch2_wake_adv_publish_reconnect(&adv);
    }
    uart_put_line("READY W5-W8: W | P | I | K ARM/CONFIRM | S | V 0/1 | T | X | C [s] | L | B | D");
}

int main(void) {
    switch2_wake_adv_port_t port = {
        NULL, port_set_params, port_set_data, port_enable
    };
    stdio_init_all();
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    /* 起動前に届いていたゴミを捨てる。残っていると初回入力へ付着し、
     * 必ず 1 回だけ ERR になる。 */
    while (uart_is_readable(UART_ID)) {
        (void)uart_getc(UART_ID);
    }
#ifdef WAKE_BUTTON_GPIO
    gpio_init(WAKE_BUTTON_GPIO);
    gpio_set_dir(WAKE_BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(WAKE_BUTTON_GPIO);
#endif
    switch2_wake_uart_parser_init(&uart_parser);
#ifdef WAKE_BUTTON_GPIO
    switch2_wake_button_init(&button);
#endif
    uart_put_line("=== pico-wake W5 ATT probe ===");
    if (switch2_wake_adv_init(&adv, &port) != WAKE_ADV_RESULT_OK) {
        uart_put_line("FATAL adv init");
        while (true) tight_loop_contents();
    }
    switch2_wake_cmd_init(&cmd_state, &cmd_port);
    if (cyw43_arch_init() != 0) {
        uart_put_line("FATAL cyw43_arch_init");
        while (true) tight_loop_contents();
    }
    /* ★★★2026/09/03: switch2Lib.c は gap_discoverable_control(0) と
     *   gap_connectable_control(0) で BR/EDR を不可視にしているが、
     *   ★この2つは Classic 専用の API であり、LE 専用ビルドには宣言が無い
     *   （実機ビルドで implicit declaration になった）。
     *   ★★本ターゲットは pico_btstack_ble だけを繋いでおり Classic を
     *   一切初期化しないため、そもそも BR/EDR は動かない。
     *   ★★★よってこの2行は不要である（実物は Classic も含むビルドなので要る）。 */

    l2cap_init();
    sm_init();
    /* ★★★一次資料の明言（ndeadly bluetooth_interface.md）:
     *   "The controllers implement their own (Pseudo)-Out-Of-Band pairing
     *    procedure over the HID command interface instead of using the standard
     *    Security Manager Protocol (SMP), which is not supported at all."
     *   "Attempting to pair controllers using SMP (as many platforms do
     *    automatically) will cause the controller to terminate the connection."
     *
     * ★つまり SMP からペアリングを始めてはならない。
     *   IO capability だけは『画面もボタンも無い』と正しく名乗っておくが、
     *   ★★bonding を要求しない（SMP を能動的に始めない）。
     *   鍵交換は 0x0014 への 0x15 コマンドで行う（W6 の仕事）。 */
    /* ★★★2026/09/03 W5-6: ATT の初期化を hci_power_control(ON) の『前』へ移す。
     *   ★実物 switch2Lib.c は btstack_main() の中で build_gatt_db() と
     *   att_server_init() と att_server_register_packet_handler() を済ませ、
     *   ★★最後に hci_power_control(HCI_POWER_ON) を呼んでいる。
     *   ★★★私は HCI_STATE_WORKING を待ってから呼んでいた。
     *   実機では ATT-CONNECTED は出たが MTU 交換が来ず、reads=0 のままだった。
     *   ★『本体が送っていない』のか『届いても処理されていない』のかを
     *   区別するため、まず実物と同じ順序に揃える。 */
    {
        switch2_wake_att_result_t att_res =
            switch2_wake_att_db_build(&att_port, &att_report);
        char att_line[128];
        snprintf(att_line, sizeof(att_line),
                 "ATT build=%s last=%04x size=%u step=%u max=%u",
                 switch2_wake_att_result_name(att_res), att_report.last_handle,
                 (unsigned)att_report.db_size, (unsigned)att_report.failed_step,
                 (unsigned)MAX_ATT_DB_SIZE);
        uart_put_line(att_line);
        if (att_res != WAKE_ATT_OK) {
            snprintf(att_line, sizeof(att_line),
                     "FATAL ATT DB expected=%04x actual=%04x",
                     att_report.expected_handle, att_report.actual_handle);
            uart_put_line(att_line);
            while (true) tight_loop_contents();   /* 広告を開始しない */
        }
        att_server_init(att_db_util_get_address(),
                        &att_read_callback, &att_write_callback);
        att_server_register_packet_handler(&att_packet_handler);
    }
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0);
    sm_register_ltk_callback(&get_ltk_callback);
    sm_events.callback = &sm_packet_handler;
    sm_add_event_handler(&sm_events);
    hci_events.callback = &packet_handler;
    hci_add_event_handler(&hci_events);
    btstack_run_loop_set_timer_handler(&poll_timer, &poll_handler);
    btstack_run_loop_set_timer(&poll_timer, POLL_MS);
    btstack_run_loop_add_timer(&poll_timer);
    /* 2026/09/04: ATT無音の可視化。USBシリアルへHCI/L2CAP/ATTダンプを出す。
     * UART(アプリログ)とUSB(BTstackダンプ)の2系統になる。 */
    hci_dump_init(hci_dump_embedded_stdout_get_instance());
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    while (true) {
        poll_uart();
        sleep_ms(1);
    }
}
