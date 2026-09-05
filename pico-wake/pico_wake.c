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
#include "app_common.h"
#include "app_ports_adv.h"
#include "app_ports_store.h"
#include "app_ports_cmd.h"
#include "app_ports_att.h"
#include "app_att.h"
#include "app_ui.h"
#include "app_loop.h"

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
