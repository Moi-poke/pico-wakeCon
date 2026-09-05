# pico-wakecon リファクタリング設計書

日付: 2026-09-05
対象: `src/` 全体（main.c 329行, hid.c 602行, link.c 318行, ui.c 300行, cap.c 169行, store.c 118行, spi.c 70行, 他ヘッダ）
目的: 可読性重視（B方針: 分割+テスト）。動作・ログ・タイミングは維持し、軽微な整理は許容。

## 1. 背景

- `probe_*` グローバルが hid/link/ui に分散し相互書き換え（例: `link_note_disconnected()` が `probe_hid_cid` をクリア、`hid.c` が `probe_addr` を読む）。
- `main.c:packet_handler` 約180行が HCIログ・STATE・接続・HID・広告を一括処理。マジック `0x31-0x36/0x05/0x18/0x0e` あり。
- `ui.c:handle_line` 約145行の if 連鎖。各コマンド inline。色フォーマット2重複（O応答と `probe_show_status`）、C秒数パース手書き、L一覧 inline、先頭文字ホワイトリスト重複。
- `hid.c:answer_subcmd` 約160行 switch。`reply_buf/reply_len` グローバル。S/O パースのトークン分割重複。50B パディング知識が `probe_report_handler` に分散。
- `link.c` が addr初期化・再接続・取込・ビーコン・LE追跡の4責務。`link_cap_poll` 宣言（link.h:43）が未定義（dead）。
- `store.c` が TLV 定型7回重複。`spi.c` の `SPI_TABLE_N` が非const可変グローバル。
- `cap.c` は最清潔で BTstack 非依存。`all_zero` が store 側と重複。

## 2. 非目的・制約

- UARTログ書式・`snprintf` バッファサイズ・ADV31B配置・`type 0x00/0x81`・peer送り順・ATT/SDP・SPI応答値・`main()` 初期化順・Picoビルド構成（board `pico2_w`, stdio USB=1/UART=0, link `pico_btstack_*`）・C11/C17は変更しない。
- 軽微な変更は許容：静的関数分割、コマンド表化、名前付き定数、重複除去、dead宣言削除、`const` 化。新規 `-Wall -Wextra -Werror` 導入なし。
- 秘密（LTK/IRK/AES鍵）をログに出さない方針維持。

## 3. アーキテクチャ

依存方向を `main -> link/ui/hid -> cap/spi/store -> util` に整理。BTstack依存は `link/hid/main` に閉じ込め、`cap/util/input_parse` はホストコンパイル可能に保つ。新規依存は `util` のみ。

```
src/
  util.h/c      新設: is_zero/hex/色フォーマット/トークン分割
  main.c        packet_handler を静的ヘルパーへ分割のみ
  ui.c          handle_line をコマンド表 + ヘルパーへ
  hid.c         answer_subcmd をヘルパー群へ、S/Oパースを util へ
  link_conn.c   新設: 再接続・link_key_count（link.c から逐語移動）
  link_cap.c    新設: 取込・表（link.c から逐語移動）
  link_beacon.c 新設: 偽装・再生・LE追跡（link.c から逐語移動）
  link.c        互換薄層 or 削除（CMakeとlink.h更新）
  store.c       get_tlv() ヘルパー化
  spi.c         SPI_TABLE_N const化
  tests/host/   新設: cap/util/input_parse の CTest
```

公開APIシグネチャ（`cap_*`, `probe_*`, `link_*`, `store_*`）は変更しない。`static` の公開調整のみ。

## 4. コンポーネント詳細

### 4.1 util 新設

```c
bool util_is_zero(const uint8_t *d, size_t n);
int util_hex_val(char c);
bool util_parse_hex(const char *s, int len, uint32_t *out);
bool util_parse_u8_hex(const char *s, int len, uint8_t *out);
int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want);
void util_format_color(char *m, size_t n);
```

置換対象：`cap.c:all_zero`、`hid.c:parse_hex_one`、S/O行トークンループ、`ui.c` C秒数パースは範囲チェック付きで残し数字変換のみ利用、色 `snprintf` 2箇所。

### 4.2 main.c 分割

`packet_handler` から `log_hci_packet()`、`handle_bt_ready()`、`handle_conn_complete()`、`handle_disc()`、`handle_hid_meta()` を静的抽出。本文は switch のみ残す。SSP `0x31-0x36`、切断 `0x05`、BDADDR応答 `0x0e/0x01-0x09-0x10` に `enum`/`define`。

### 4.3 ui.c 表化

```c
typedef void (*ui_cmd_fn)(void);
typedef struct { char c; ui_cmd_fn fn; } ui_cmd_t;
```

`handle_line` の各 `if (c0 == ...)` 本体を `cmd_s/cmd_n/cmd_o/...` 静的関数へ。`line_buf` 走査は `util_split_tokens` へ。色表示は `util_format_color` へ。`probe_uart_task` の先頭文字ホワイトリストを表走査に置換。

### 4.4 hid.c 分割

`answer_subcmd` の各 case 本体を `reply_device_info()`, `reply_trigger_blank()`, `reply_power()`, `reply_spi()` 等へ。`pc_buttons_to_bt/hat_to_bt/probe_pack_stick` は純粋関数のまま `input_parse` 的位置付けで残し、将来のホストテスト対象に。50Bパディング定数 `REPLY_WANT = 2+48` を命名。

### 4.5 link 分割

`link.c` を逐語移動で3分割。`probe_addr/probe_host_*` は `link_conn`、`probe_cap_*` 表は `link_cap`、`bdaddr_spoof/beacon/le_handle` は `link_beacon`。`link.h` は3ヘッダの再export薄層にし、既存 include を壊さない。`link_cap_poll` dead宣言を削除。

### 4.6 store/spi 衛生

`store.c` 先頭に `static bool get_tlv(const btstack_tlv_t **t, void **c)`。`spi.c: SPI_TABLE_N` を `const uint8_t` 化（`spi.h` も `extern const uint8_t` へ）。

## 5. テスト戦略

- Pico回帰：既存 `CMakeLists.txt` のまま `cmake -S . -B build -G Ninja`＋`cmake --build build`。`build/pico-wakecon.elf/.uf2` 生成確認。
- ホスト回帰：`tests/host/CMakeLists.txt`＋`test_util.c/test_cap.c/test_input_parse.c` を `ctest` で実行。Pico SDK・BTstack不要、純粋関数のみ。
- 差分目視：`git diff` でUART文字列（`CAP/BNC/hid open/0x06 power/SUB=`等）の差分ゼロを確認。

## 6. 完了条件

- Picoクリーンビルド成功、ホスト `ctest` 全PASS、`git status` に意図外ファイルなし、UARTログ差分なし。
