# pico-wake リファクタリング設計書

日付: 2026-09-05
対象: `C:\Users\moilo\pico-wake`（Pico 2 W + BTstack BLE、Switch 2 Wakeエミュレーション）
方針: 動作厳密維持（behavior-preserving）、まず `pico_wake.c` 分割、ホスト単体テスト追加

## 1. 背景と目的

`pico_wake.c`（1327行）にBTstack橋渡し・ATTコールバック・UARTコマンド・GAP/SM/ATTイベント処理・ステータス表示・キャプチャ/ビーコン・`main()`初期化が集中している。他の `switch2_wake_*` モジュールはBTstack非依存で単体責務を持つが、`secure_zero`/`is_all_zero`/`reverse16`/hex解析の重複、マジックナンバー分散、テスト不在、放置ファイル（`CMakeLists copy.txt`、`__init__.py`、0byte `pico-wake.c`）がある。

目的は可読性・変更容易性・回帰防止であり、UARTログ文言・タイミング・ADVペイロード・ATTハンドル・鍵管理方針は一切変えない。

## 2. 非目的・制約（Global Constraints）

- UARTログテキスト（`BOOT`/`S `/`ATT-READ`/`ATT-CMD`/`LE-CONN`/`CAP-*`/`CMD *`/`SM *`等）の書式・順序を変更しない。`snprintf`バッファサイズも維持する。
- ADVペイロード31byte配置・`type 0x00/0x81`・peer逆順格納を変更しない。
- ATT DB 30段のhandle期待値（`0x0001`〜`0x0038`）と権限（U09決着: 独自DescriptorはREAD|WRITE|DYNAMIC）を変更しない。
- SMPを能動開始しない方針（`sm_set_authentication_requirements(0)`、鍵交換は0x0014の0x15コマンド経由）を維持する。
- 秘密値（LTK/IRK/AES鍵）をログに出さない方針（V12）を維持する。`wake_secure_zero`後は構造体ゼロ化順序を変えない。
- `main()`初期化順序（ATT build→`att_server_init`→SM→HCI→timer→dump→`hci_power_control(ON)`）を変更しない。
- Picoビルド構成（board `pico2_w`、stdio USB=1/UART=0、link: `pico_btstack_ble`等）を変更しない。
- C11、C17（CMakeの既定値）を維持する。`-Wall -Wextra -Werror`の新規導入はしない（既存警告の有無が未確認のため）。

## 3. アーキテクチャ

現状の層構造を保ち、`pico_wake.c`内の暗黙グルーをファイル分割で顕在化する。新規依存は `switch2_wake_util`（重複共通化）のみ。BTstack依存は `app_*` 側に閉じ込め、`switch2_wake_*` は引き続きホストコンパイル可能に保つ。

```
pico_wake.c (main + グローバル所有)
├── app_config (make_config, meta_state_name, 定数)
├── app_ports_adv/store/cmd/att (BTstack橋渡しテーブル)
├── app_att (ATT DB値・desc表・read/write callback)
├── app_ui (show_status, cap_show_list, handle_command, apply_actions, reset_core)
└── app_loop (poll_uart/button/poll_handler, att/sm/packet_handler)

switch2_wake_core/cmd/store/adv/input/capture/att_db (変更なし＋util呼出置換のみ)
switch2_wake_util (新規: secure_zero, is_zero, reverse16, hex)
tests/host (新規: MSVC cl + CTest、各モジュールのport mock)
```

データフロー・イベント経路は変更しない。`switch2_wake_*` の公開APIシグネチャも変更しない。

## 4. コンポーネント詳細

### 4.1 衛生

- 削除: `CMakeLists copy.txt`（旧3ファイル版）、`__init__.py`（0byte、Pythonパッケージ intentionなし）。
- `pico-wake.c`（0byte空テンプレ）は削除しない。VSCode Pico拡張が生成した可能性があり、削除で再生成される恐れがあるため。`CMakeLists.txt`コメント「intentionally not compiled」を維持し、ビルド対象に加えないことを明示する。
- `.gitignore` に `build/`、`*.elf`、`*.uf2`、`*.hex`、`*.bin`、`.cache/` を追加する。既存 `!.vscode/*` 行は触らない。
- `build/`・`.cache/clangd` の既存成果物は削除しない（再ビルド時間を避ける）が、git追跡対象にしない。

### 4.2 `switch2_wake_util` 新設

新規 `switch2_wake_util.h` / `switch2_wake_util.c`:

```c
void switch2_wake_secure_zero(void *ptr, size_t len);
bool switch2_wake_is_all_zero(const uint8_t *data, size_t len);
void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16]);
int switch2_wake_hex_value(char ch);
bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size);
```

置換対象（セマンティクス同一を確認済み）:
- `switch2_wake_core.c: secure_zero` → `switch2_wake_secure_zero`
- `switch2_wake_input.c: secure_zero`、`hex_value`、`parse_hex` → utilへ（`parse_hex`は`strlen(text) != size*2`チェックを維持）
- `switch2_wake_store.c: wake_secure_zero`、`wake_is_all_zero` → utilへ
- `switch2_wake_capture.c: all_zero` → utilへ
- `switch2_wake_cmd.c: reverse16` → utilへ（`for (i=0u;i<16u;i++) out[i]=in[15u-i]`と同一）
- `pico_wake.c` 側の `memset(...,0,...)` による秘密値消去は順序維持のため当面触らない（将来タスク）。

### 4.3 `pico_wake.c` 分割（逐語移動）

新設ファイルと移動内容（関数本体・ static変数・コメント・`★`注釈を一字も変えず移動、`static`公開調整のみ）:

- `app_config.h/.c`: `make_config()`（60000/20000/30000/20000/5000/10000）、`meta_state_name()`、`UART_ID/TX/RX/BAUD`、`POLL_MS=1`、`BUTTON_POLL_MS=10`、`FORGET_CONFIRM_MS=5000`。
- `app_ports_adv.c`: `port_set_params`/`port_set_data`/`port_enable`。
- `app_ports_store.c`: `tlv_impl/tlv_context`、`store_tlv_get/store/delete`、`store_db_max_count/info/has_ltk/add/set_ltk/remove`、`store_port`テーブル。
- `app_ports_cmd.c`: `cmd_get_local_addr`、`cmd_aes_complete`、`cmd_start_aes`、`cmd_store_bond`、`cmd_read_memory`、`cmd_emit_response`、`cmd_on_registered`、`cmd_observe`、`cmd_selftest_done/start`、`cmd_port`テーブル、`get_ltk_callback`。
- `app_ports_att.c`: `att_port_init/svc128/svc16/char128/char16/desc128/size`、`att_port`テーブル。
- `app_att.h/.c`: `value_unknown_read1/2`、`wake_desc_handles[7]`/`wake_desc_values`、`wake_desc_index()`、`att_read_callback()`、`att_write_callback()`。handle定数 `0x0003/0x0007/0x000f/0x0014/0x0016/0x0018/0x001e` は値変更なしで `app_att.h` へ集約し出所コメントを付ける。
- `app_ui.h/.c`: `apply_actions()`、`show_status()`、`reset_core()`、`cap_restore_adv()`、`bdaddr_spoof()`、`cap_show_list()`、`handle_command()`。
- `app_loop.h/.c`: `poll_uart()`、`handle_button()`、`poll_handler()`、`att_packet_handler()`、`sm_packet_handler()`、`packet_handler()`。
- `pico_wake.c`: グローバル定義と `main()` のみ残す。`#include "app_*.h"`追加以外のロジック変更なし。

グローバル共有のため `app_state.h` を新設し、既存の file-static 変数群（`adv`、`uart_parser`、`store`、`local_identity`、`att_report`、`le_connection`、ATTカウンタ群、cap/cmd状態群等）を `extern` 宣言で共有する。型・名前・初期値は変更しない。`WAKE_BUTTON_GPIO` の `#ifdef` 分岐も維持する。

### 4.4 定数集約

- `WAKE_DESC_TABLE_SIZE=7` と `wake_desc_handles` の7要素は `app_att.h` に集約。要素順序変更なし。
- `WAKE_CMD_HANDLE_*` は既存 `switch2_wake_cmd.h` を正とし、`pico_wake.c` 側の重複リテラルを置換しない（動作維持優先、将来タスクに記録）。

### 4.5 エラーハンドリング

既存方針を維持する。新規エラー経路を作らない:
- 初期化失敗（adv init、cyw43、ATT build）は `FATAL` 表示後 `tight_loop_contents()` 無限ループ。
- コマンド解釈失敗は `ERR commands: ...` 一行。`K CONFIRM` の期限切れは `K ERR NOT_ARMED`。
- TLV/DB失敗は `I ERR <理由>`。理由文字列は `switch2_wake_store_result_name()` の既存値をそのまま使う。

## 5. テスト戦略

ホストテストはMSVC `cl` + CTestで実行する（環境にhost gccなし、MSVC 19.35ありを確認済み）。ネットワーク不要の自前アサート形式（Unity等のFetchContentなし）。Pico SDK不要の `switch2_wake_*` のみ対象。

- `tests/host/CMakeLists.txt`: `project(pico_wake_host_tests C)`、各 `test_*.c` を実行ファイル化し `add_test()` 登録。`include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)` で `switch2_wake*.h` と `btstack_config.h` を参照。
- `test_core`: BOOT→UNBONDED/READY、`PAIR_REQUEST`→`PAIR_ADVERTISING`、`WAKE_REQUEST`受付/拒否、`FORGET`消去、`tick`タイムアウト遷移、stale generation無視。
- `test_cmd`: 0x15/01応答、0x15/04のLTK導出（device_public_key XOR）、LTK未設定時の0x15/02拒否、peer不一致時のget_ltk拒否、fingerprint非ゼロ、selftest値。
- `test_store`: meta encode→decode往復、CRC破損検出、schema不一致検出、`store_verify`のlocal不一致/DB不在/LTK不在、`import`全ゼロLTK拒否、`commit`のPENDING→VERIFIED、`forget`消去。
- `test_adv`: peer未設定時のWAKE要求=`NO_PEER`、peer設定後のWAKEペイロード（type 0x81＋peer逆順）、STOP、conflict検出（STANDARD|WAKE同時）。
- `test_input`: `W/P/S/C/L/B/D/X`、`C <1-60>`範囲外拒否、`T <12hex>`、`V 0/1`、`K ARM/CONFIRM`、`I ...`正常＋hex不正拒否、UART分割投入・overflow・CRLF処理、ボタンdebounce（50ms/2000ms/10000ms境界）。
- `test_capture`: 任天堂MFG parse（PID/flag/switch_mac反転）、offer重複集約・RSSI最大保持、`best_wake`選択、encode/decode往復・全ゼロspoof拒否・flag 0x81検査。
- `test_att_db`: mock portで30段build成功、`expected_handle(1..30)` spot check、先頭 `0x0001`・末尾 `0x0038`、失敗段のreport特定（mockで segment 13 をずらして `WAKE_ATT_ERR_STEP_HANDLE` になること）。

Pico側は既存 `CMakeLists.txt` のままクリーンビルド（`build-verify/` 新規）で回帰確認する。実機接続確認は対象外。

## 6. 実行順序と完了条件

1. 衛生＋`.gitignore`（Picoビルド影響なしを確認）
2. `switch2_wake_util`新設＋各モジュール置換（ホストテストで同一性確認）
3. `pico_wake.c`分割（逐語移動、Picoビルドで確認）
4. ホストテスト追加＋CTapest全緑＋Picoクリーンビルド成功
5. `git diff`でUARTログ文字列差分ゼロを目視確認

完了条件: `ctest`全PASS、Pico `pico-wake.elf` 生成成功、`git status`に意図外ファイルなし。
