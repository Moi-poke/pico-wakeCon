#ifndef APP_LOOP_H
#define APP_LOOP_H
#include "btstack.h"
#include "switch2_wake_input.h"
#ifdef __cplusplus
extern "C" {
#endif
extern btstack_packet_callback_registration_t sm_events;
void poll_uart(void);
#ifdef WAKE_BUTTON_GPIO
void handle_button(switch2_wake_button_event_t event);
#endif
void poll_handler(btstack_timer_source_t *timer);
void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
void sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
#ifdef __cplusplus
}
#endif
#endif
