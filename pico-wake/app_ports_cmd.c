#include <stdio.h>
#include <string.h>
#include "btstack.h"
#include "ble/le_device_db.h"
#include "switch2_wake.h"
#include "switch2_wake_adv.h"
#include "switch2_wake_store.h"
#include "switch2_wake_cmd.h"
#include "app_state.h"
#include "app_config.h"
#include "app_common.h"
#include "app_ports_cmd.h"

void cmd_clear_pending_response(void) {
    memset(cmd_pending_response, 0, sizeof(cmd_pending_response));
    cmd_pending_response_len = 0u;
    cmd_response_pending = false;
    cmd_aes_done = NULL;
    cmd_aes_done_ctx = NULL;
    memset(cmd_aes_key, 0, sizeof(cmd_aes_key));
    memset(cmd_aes_plain, 0, sizeof(cmd_aes_plain));
    memset(cmd_aes_output, 0, sizeof(cmd_aes_output));
}

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

void cmd_start_selftest(void) {
    uint8_t ltk[16], key[16], plain[16];
    if (!switch2_wake_cmd_selftest_values(ltk, key, plain)) {
        uart_put_line("SELFTEST XOR=NG AES=SKIP"); return;
    }
    if (!cmd_start_aes(NULL, key, plain, cmd_selftest_done, NULL))
        uart_put_line("SELFTEST XOR=OK AES=BUSY");
    memset(ltk,0,sizeof(ltk)); memset(key,0,sizeof(key)); memset(plain,0,sizeof(plain));
}

const switch2_wake_cmd_port_t cmd_port = {
    NULL, cmd_get_local_addr, cmd_start_aes, cmd_store_bond, cmd_read_memory,
    cmd_emit_response, cmd_on_registered, cmd_observe
};

bool get_ltk_callback(hci_con_handle_t handle, uint8_t address_type,
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
