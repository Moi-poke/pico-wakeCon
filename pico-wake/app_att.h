#ifndef APP_ATT_H
#define APP_ATT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "btstack.h"
#ifdef __cplusplus
extern "C" {
#endif
#define APP_ATT_HANDLE_UNKNOWN_READ1 0x0003u
#define APP_ATT_HANDLE_UNKNOWN_READ2 0x0007u
#define APP_ATT_HANDLE_CCCD_INPUT 0x000fu
uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
int att_write_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
#ifdef __cplusplus
}
#endif
#endif
