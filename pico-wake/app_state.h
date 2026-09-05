#ifndef APP_STATE_H
#define APP_STATE_H
#include <stdbool.h>
#include <stdint.h>
#include "btstack.h"
#include "switch2_wake.h"
#include "switch2_wake_adv.h"
#include "switch2_wake_input.h"
#include "switch2_wake_store.h"
#include "switch2_wake_att_db.h"
#include "switch2_wake_cmd.h"
#include "switch2_wake_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
extern switch2_wake_adv_t adv;
extern switch2_wake_uart_parser_t uart_parser;
extern switch2_wake_store_t store;
extern bd_addr_t local_identity;
extern switch2_wake_att_report_t att_report;
extern hci_con_handle_t le_connection;
extern uint32_t att_read_count;
extern uint32_t att_write_count;
extern uint32_t att_desc_read_count;
extern uint16_t att_last_read_handle;
extern uint16_t att_last_write_handle;
extern uint16_t att_cccd_response;
extern uint16_t att_cccd_input;
extern uint32_t att_desc_write_count;
extern uint32_t att_cmd_write_count;
extern uint32_t le_conn_ms;
extern uint32_t att_event_count;
extern uint16_t att_mtu;
extern bd_addr_t last_peer_addr;
extern uint8_t last_peer_type;
extern bool last_peer_valid;
#ifdef WAKE_BUTTON_GPIO
extern switch2_wake_button_t button;
#endif
extern btstack_timer_source_t poll_timer;
extern btstack_packet_callback_registration_t hci_events;
extern bd_addr_t null_addr;
extern bool stack_ready;
#ifdef WAKE_BUTTON_GPIO
extern uint32_t button_poll_at_ms;
#endif
extern bool forget_armed;
extern uint32_t forget_deadline_ms;
extern bool hci_verbose;
extern switch2_wake_capture_table_t cap_table;
extern bool cap_scanning;
extern uint32_t cap_deadline_ms;
extern switch2_wake_capture_saved_t cap_saved;
extern bool cap_saved_valid;
extern bool beacon_active;
extern uint32_t beacon_deadline_ms;
extern switch2_wake_cmd_t cmd_state;
extern btstack_crypto_aes128_t cmd_aes_request;
extern switch2_wake_cmd_aes_done_t cmd_aes_done;
extern void *cmd_aes_done_ctx;
extern uint8_t cmd_aes_output[16];
extern uint8_t cmd_aes_key[16];
extern uint8_t cmd_aes_plain[16];
extern uint8_t cmd_pending_response[WAKE_CMD_RESPONSE_MAX];
extern uint16_t cmd_pending_response_len;
extern bool cmd_response_pending;
extern uint32_t cmd_bond_ok;
extern uint32_t cmd_registered_ok;
#ifdef __cplusplus
}
#endif
#endif
