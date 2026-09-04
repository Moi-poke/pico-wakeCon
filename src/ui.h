#ifndef UI_H
#define UI_H

/* UART/USB 出力窓と1行コマンド、定期表示。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"

#ifdef __cplusplus
extern "C" {
#endif

void probe_line(const char *text);
extern bool probe_hci_verbose;
extern bool probe_monitor;

extern uint32_t probe_uart_rx_count;
extern uint32_t probe_s_line_ok;
extern uint32_t probe_s_line_ng;
extern uint32_t probe_n_line_ok;
extern uint32_t probe_line_over;
extern uint32_t probe_color_set_count;

void probe_uart_task(void);
void probe_heartbeat_handler(btstack_timer_source_t *ts);
void probe_show_status(void);

#ifdef __cplusplus
}
#endif

#endif
