#ifndef APP_PORTS_CMD_H
#define APP_PORTS_CMD_H
#include <stdbool.h>
#include <stdint.h>
#include "btstack.h"
#include "switch2_wake_cmd.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const switch2_wake_cmd_port_t cmd_port;
void cmd_clear_pending_response(void);
bool get_ltk_callback(hci_con_handle_t handle, uint8_t address_type, bd_addr_t addr, uint8_t *ltk);
void cmd_start_selftest(void);
#ifdef __cplusplus
}
#endif
#endif
