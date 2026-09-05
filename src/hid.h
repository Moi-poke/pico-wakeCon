#ifndef WAKECON_HID_H
#define WAKECON_HID_H

/* Switch 1 Pro Controller 入力・HID 応答・送信。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t probe_btn[3];
extern uint8_t probe_lx, probe_ly, probe_rx, probe_ry;
extern uint32_t probe_btn_press_count;
extern bool probe_btn_was_down;
void probe_input_reset(void);

extern uint16_t probe_hid_cid;
extern bool probe_full_mode;
extern uint8_t probe_report_timer;
extern uint32_t probe_empty_sent;
extern uint32_t probe_reply_sent;
extern uint32_t probe_state_sent;
extern uint32_t probe_out_report_count;
extern uint8_t probe_subcmd_seen[];
extern uint8_t probe_subcmd_seen_n;
extern uint16_t probe_out_len_min;
extern uint16_t probe_out_len_max;
extern uint8_t probe_player_id;
extern bool probe_imu_enabled;
extern bool probe_vibration_enabled;
extern uint8_t probe_input_mode;
extern uint8_t probe_hci_state_arg;
extern uint32_t probe_hci_state_count;
extern bool probe_send_now_wanted;
extern uint32_t probe_color_set_count;

uint16_t probe_build_reply(uint8_t ack, uint8_t subcmd);
void probe_report_handler(uint16_t cid, hid_report_type_t report_type,
                          uint16_t report_id, int report_size,
                          uint8_t *report);
void probe_hid_reset(void);
void probe_request_send(void);
void probe_can_send_now(void);
uint32_t probe_send_interval_ms(void);
/* 200ms 無通信で中立化する番犬。不正行でも線は生きているため時刻は進める。 */
void probe_watchdog_feed(uint32_t now_ms);
void probe_watchdog_poll(uint32_t now_ms);
int probe_parse_s_line(const char *s, int len);
int probe_parse_color_line(const char *s, int len);

#ifdef __cplusplus
}
#endif

#endif
