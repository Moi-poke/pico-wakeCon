#ifndef SWITCH2_WAKE_CMD_H
#define SWITCH2_WAKE_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKE_CMD_LTK_SIZE 16u
#define WAKE_CMD_ADDR_SIZE 6u
#define WAKE_CMD_REQUEST_HEADER_SIZE 8u
#define WAKE_CMD_PAD_0016 33u
#define WAKE_CMD_RESPONSE_PAD 14u
#define WAKE_CMD_RESPONSE_MAX 160u

#define WAKE_CMD_HANDLE_BASIC 0x0014u
#define WAKE_CMD_HANDLE_RUMBLE 0x0016u
#define WAKE_CMD_HANDLE_LARGE 0x0018u
#define WAKE_CMD_RESPONSE_HANDLE 0x001eu

/* AES は BTstack では非同期。完了関数へ ciphertext 16 byte を渡す。 */
typedef void (*switch2_wake_cmd_aes_done_t)(void *done_ctx, const uint8_t out[16]);

typedef struct {
    void *ctx;
    bool (*get_local_addr)(void *ctx, uint8_t addr[6]);
    bool (*start_aes)(void *ctx, const uint8_t key[16],
                      const uint8_t plain[16],
                      switch2_wake_cmd_aes_done_t done, void *done_ctx);
    bool (*store_bond)(void *ctx, uint8_t peer_type, const uint8_t peer[6],
                       const uint8_t ltk_natural[16]);
    bool (*read_memory)(void *ctx, uint32_t address, uint8_t *out, uint8_t length);
    void (*emit_response)(void *ctx, const uint8_t *packet, uint16_t length);
    void (*on_registered)(void *ctx);
    void (*observe)(void *ctx, uint8_t cmd, uint8_t sub,
                    uint16_t request_len, uint16_t response_len);
} switch2_wake_cmd_port_t;

typedef struct {
    switch2_wake_cmd_port_t port;
    uint8_t peer[6];
    uint8_t peer_type;
    uint8_t ltk[16];              /* 自然順。ログへ出してはならない */
    uint8_t aes_key[16];
    uint8_t aes_plain[16];
    uint8_t aes_out[16];
    uint8_t response[WAKE_CMD_RESPONSE_MAX];
    bool peer_valid;
    bool ltk_valid;
    bool aes_busy;
    bool registration_complete;
    uint32_t received;
    uint32_t responded;
    uint32_t rejected;
    uint8_t last_cmd;
    uint8_t last_sub;
    uint16_t last_response_len;
    int bond_index;
} switch2_wake_cmd_t;

typedef enum {
    WAKE_CMD_OK = 0,
    WAKE_CMD_IGNORED,
    WAKE_CMD_SHORT,
    WAKE_CMD_BUSY,
    WAKE_CMD_PORT_ERROR
} switch2_wake_cmd_result_t;

void switch2_wake_cmd_init(switch2_wake_cmd_t *state,
                           const switch2_wake_cmd_port_t *port);
void switch2_wake_cmd_set_peer(switch2_wake_cmd_t *state, uint8_t peer_type,
                               const uint8_t peer[6]);
void switch2_wake_cmd_clear_pairing(switch2_wake_cmd_t *state);
bool switch2_wake_cmd_get_ltk_for_peer(const switch2_wake_cmd_t *state,
                                           uint8_t peer_type,
                                           const uint8_t peer[6],
                                           uint8_t out[16]);
switch2_wake_cmd_result_t switch2_wake_cmd_write(
    switch2_wake_cmd_t *state, uint16_t handle,
    const uint8_t *buffer, uint16_t size);
bool switch2_wake_cmd_get_ltk(const switch2_wake_cmd_t *state, uint8_t out[16]);
uint16_t switch2_wake_cmd_fingerprint(const switch2_wake_cmd_t *state);
bool switch2_wake_cmd_selftest_values(uint8_t out_ltk[16],
                                      uint8_t out_key[16], uint8_t out_plain[16]);
const char *switch2_wake_cmd_result_name(switch2_wake_cmd_result_t result);

#ifdef __cplusplus
}
#endif

#endif
