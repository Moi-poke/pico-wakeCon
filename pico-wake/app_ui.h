#ifndef APP_UI_H
#define APP_UI_H
#include "switch2_wake.h"
#include "switch2_wake_input.h"
#ifdef __cplusplus
extern "C" {
#endif
void apply_actions(const char *name, wake_result_t result, uint32_t actions);
void show_status(void);
void cap_restore_adv(const char *name);
void bdaddr_spoof(const uint8_t canonical[6]);
void cap_show_list(void);
void handle_command(switch2_wake_input_command_t *command);
#ifdef __cplusplus
}
#endif
#endif
