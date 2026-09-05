# pico-wakecon リファクタリング Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** src全体を可読性重視で分割・重複除去し、Picoビルド回帰＋ホストテストで固定する

**Architecture:** 逐語移動＋util抽出のみ。新規依存は `util` だけ。BTstack依存は `link/hid/main` に閉じ込め、`cap/util` はホストコンパイル可能を維持する。

**Tech Stack:** C11, CMake + Ninja, Pico SDK 2.3.0, BTstack (Classic+BLE), Host tests: gcc/cl + CTest（自前assert、Unity不使用）

**Spec:** `docs/superpowers/specs/2026-09-05-pico-wakecon-refactor-design.md`

## Global Constraints

- UARTログ書式・`snprintf` バッファサイズを変更しない
- ADV31B配置・`type 0x00/0x81`・peer送り順を変更しない
- SPI応答値・SDP・HID記述子を変更しない
- `main()` 初期化順（GAP→L2CAP→SDP→HID→HCI→timer→power）を変更しない
- Picoビルド構成（board `pico2_w`, stdio USB=1/UART=0）を変更しない
- C11維持。新規 `-Werror` 導入なし
- 秘密（LTK/IRK/AES鍵）をログに出さない
- 全変更は `refactor/pico-wakecon-cleanup` ブランチにまとめる。mainへはマージしない

---

### Task 1: ベースライン確認＋util新設

**Files:**
- Create: `src/util.h`
- Create: `src/util.c`
- Modify: `CMakeLists.txt`（`src/util.c` を追加）
- Modify: `src/cap.c`（`all_zero` を util へ置換）
- Test: Picoビルド

**Interfaces:**
- Consumes: なし
- Produces:
  - `bool util_is_zero(const uint8_t *d, size_t n)`
  - `int util_hex_val(char c)`
  - `bool util_parse_hex(const char *s, int len, uint32_t *out)`
  - `int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want)`
  - `void util_format_color(char *m, size_t n)`

- [ ] **Step 1: `src/util.h` を作成**

```c
#ifndef WAKECON_UTIL_H
#define WAKECON_UTIL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool util_is_zero(const uint8_t *d, size_t n);
int util_hex_val(char c);
bool util_parse_hex(const char *s, int len, uint32_t *out);
int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want);
void util_format_color(char *m, size_t n);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: `src/util.c` を作成**

```c
#include <string.h>
#include <stdio.h>
#include "util.h"
#include "spi.h"
bool util_is_zero(const uint8_t *d, size_t n) {
    size_t i;
    uint8_t acc = 0u;
    if (d == NULL) return true;
    for (i = 0u; i < n; i++) acc |= d[i];
    return acc == 0u;
}
int util_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool util_parse_hex(const char *s, int len, uint32_t *out) {
    uint32_t v = 0u;
    int k;
    if (s == NULL || out == NULL) return false;
    if (len <= 0 || len > 8) return false;
    for (k = 0; k < len; k++) {
        int d = util_hex_val(s[k]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}
int util_split_tokens(const char *s, int len, int start, uint32_t *v, int want) {
    int idx = 0;
    int p = start;
    while (idx < want) {
        int st;
        while (p < len && s[p] == ' ') p++;
        st = p;
        while (p < len && s[p] != ' ') p++;
        if (p == st) return 0;
        if (!util_parse_hex(&s[st], p - st, &v[idx])) return 0;
        idx++;
    }
    return 1;
}
void util_format_color(char *m, size_t n) {
    if (m == NULL || n < 64u) return;
    snprintf(m, n,
        "color body=%02x%02x%02x btn=%02x%02x%02x"
        " left=%02x%02x%02x right=%02x%02x%02x",
        spi_color_6050[0], spi_color_6050[1], spi_color_6050[2],
        spi_color_6050[3], spi_color_6050[4], spi_color_6050[5],
        spi_color_6050[6], spi_color_6050[7], spi_color_6050[8],
        spi_color_6050[9], spi_color_6050[10], spi_color_6050[11]);
}
```

- [ ] **Step 3: `CMakeLists.txt` に `src/util.c` を追加**

`src/store.c` の次に `src/util.c` を1行追加する。既存7行の並びを崩さない。

- [ ] **Step 4: `src/cap.c` の `all_zero` を置換**

`static bool all_zero(...)` 定義（14-23行）を削除し、`#include "util.h"` を追加。呼出2箇所を置換する：
- `cap_best_wake` 内：`all_zero(table->slot[i].wake.switch_mac, 6)` → `util_is_zero(table->slot[i].wake.switch_mac, 6)`
- `cap_encode` 内：`all_zero(saved->spoof, 6)` → `util_is_zero(saved->spoof, 6)`
- `cap_decode` 内：`all_zero(saved->spoof, 6)` → `util_is_zero(saved->spoof, 6)`

- [ ] **Step 5: Picoビルドで回帰確認**

Run: `$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'; $ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'; & $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja; & $cmake --build build --target pico-wakecon`
Expected: `build/pico-wakecon.elf` 生成成功。警告の新規増加なし

---

### Task 2: main.c packet_handler 分割

**Files:**
- Modify: `src/main.c:52-233`
- Test: Picoビルド＋`git diff` でログ文字列差分ゼロ確認

**Interfaces:**
- Consumes: Task 1なし（独立可だが順序はTask 1の後）
- Produces: 静的ヘルパー `log_hci_packet()`, `handle_bt_ready()`, `handle_conn_complete()`, `handle_disc()`, `handle_hid_meta()`（いずれも `static`、`main.c` 内のみ）

- [ ] **Step 1: マジック定数に名前を付ける（`main.c` 先頭、include直後）**

```c
#define HCI_EV_SSP_BEGIN 0x31u
#define HCI_EV_SSP_END 0x36u
#define HCI_EV_CONN_REQUEST 0x05u
#define HCI_EV_CONN_COMPLETE 0x03u
```

既存の `0x31u/0x32u/0x33u/0x36u` 判定、`0x05u/0x18u` 判定をこの定数で置換する。値は変更しない。

- [ ] **Step 2: `log_hci_packet()` を静的抽出**

`packet_handler` 内の `want` 判定〜`probe_line(msg)` まで（75-101行）をそのまま静的関数へ移動する。シグネチャ：`static void log_hci_packet(uint8_t ev, uint8_t *packet, uint16_t size)`。BDADDR応答表示も含む。ロジック・書式は一字も変えない。

- [ ] **Step 3: `handle_bt_ready/handle_disc/handle_hid_meta` を静的抽出**

各 `case` 本体を逐語移動する。`handle_bt_ready` は `BT READY/link keys/reconnect/cap` 表示まで、`handle_disc` は stall診断＋`link_note_disconnected` まで、`handle_hid_meta` は HID open/close/can_send まで。`packet_handler` 本文は `switch (ev)` の振分けのみ残す。

- [ ] **Step 4: ビルド＋ログ差分確認**

Run: `cmake --build build --target pico-wakecon` および `git diff -- src/main.c | Select-String 'probe_line\("'`
Expected: ビルド成功。`probe_line` 文字列リテラルに差分なし（移動のみ）

---

### Task 3: ui.c コマンド表化

**Files:**
- Modify: `src/ui.c:55-230`
- Modify: `src/util.c`（変更なし、利用のみ）
- Test: Picoビルド

**Interfaces:**
- Consumes: Task 1の `util_split_tokens`, `util_format_color`
- Produces: 静的 `cmd_s/cmd_n/cmd_o/cmd_c/cmd_l/cmd_b/cmd_x/cmd_status/cmd_d/cmd_m/cmd_k/cmd_p`＋`ui_cmd_t` 表（`ui.c` 内のみ）

- [ ] **Step 1: 各 `if (c0 == ...)` 本体を静的関数へ移動**

例（S行、その他も同様に逐語移動）：

```c
static void cmd_s(void) {
    if (probe_parse_s_line(line_buf, line_len)) {
        probe_s_line_ok++;
        probe_request_send();
    } else {
        probe_s_line_ng++;
    }
}
```

`line_buf/line_len` は既存 static のまま。`handle_line` は `c0` で表引きするのみにする。

- [ ] **Step 2: 色フォーマット2重複を `util_format_color` へ**

O応答（87-93行）と `probe_show_status`（244-250行）の `snprintf("color body=...")` を `util_format_color(m, sizeof(m))` に置換する。書式文字列は `util.c` 側が同一のため出力不変。

- [ ] **Step 3: C秒数パースの数字変換を `util_parse_hex` 系へ寄せる**

範囲チェック（1-60）は残し、手書き16進ループは触らない（10進のため）。ここでは変更なしとし、S/O行のトークン分割のみ `util_split_tokens` 利用へ寄せる場合は `hid.c` 側と合わせて行う。`ui.c` 単独では表化＋色共通化までとする。

- [ ] **Step 4: 先頭文字ホワイトリストを表走査へ**

`probe_uart_task` 内の `c != 'S' && c != 'N' && ...` 長鎖を `ui_cmd_is_known(c)` 静的関数（表走査、`?` 含む）に置換する。許可集合は変更しない。

- [ ] **Step 5: ビルド**

Run: `cmake --build build --target pico-wakecon`
Expected: 成功。`usage: O`, `cap cleared`, `PONG` 等の文字列不変

---

### Task 4: hid.c answer_subcmd 分割＋パース共通化

**Files:**
- Modify: `src/hid.c:208-368`（パース部）
- Modify: `src/hid.c:389-550`（応答部）
- Test: Picoビルド＋ホストテスト準備

**Interfaces:**
- Consumes: Task 1の `util_parse_hex`, `util_split_tokens`
- Produces: 静的 `reply_device_info()`, `reply_trigger_blank()`, `reply_power()`, `reply_spi()` 等（`hid.c` 内のみ）。公開API変更なし

- [ ] **Step 1: `parse_hex_one` を `util_parse_hex` へ置換**

`static int parse_hex_one(...)`（208-231行）を削除し、`#include "util.h"` を追加。`probe_parse_s_line` 内の `parse_hex_one(&s[start], p-start, &v[idx])` を `util_parse_hex(&s[start], p-start, &v[idx])` に置換する。戻り値が `int/bool` で異なるため `if (!util_parse_hex(...)) return 0;` とする。意味は同一（0<len<=8、16進のみ）。

- [ ] **Step 2: S/O行トークンループを `util_split_tokens` へ**

`probe_parse_s_line`（299-335行）の `while (idx < 6)` 手書き分割を `if (!util_split_tokens(s, len, 1, v, 6)) return 0;` に置換する。`probe_parse_color_line`（338-368行）も `util_split_tokens(s, len, 1, v, 4)` へ。以降の `pc_buttons_to_bt/hat_to_bt` 代入部は残す。

- [ ] **Step 3: `answer_subcmd` の case 本体を静的ヘルパーへ**

例（0x02機器情報、0x06電源、0x10 SPIのみ抽出、他caseは移動のみ）：

```c
static void reply_device_info(uint16_t *p) {
    *p = probe_build_reply(0x82u, 0x02u);
    reply_buf[(*p)++] = 0x03u;
    reply_buf[(*p)++] = 0x8Bu;
    reply_buf[(*p)++] = 0x03u;
    reply_buf[(*p)++] = 0x02u;
    for (int k = 0; k < 6; k++) reply_buf[(*p)++] = probe_addr[k];
    reply_buf[(*p)++] = 0x01u;
    reply_buf[(*p)++] = 0x02u;
}
```

バイト値・順序は変更しない。`answer_subcmd` 本文は `switch (sub)` の振分けのみ残す。

- [ ] **Step 4: 50Bパディング定数を命名**

`probe_report_handler` 内の `const uint16_t want = 2u + 48u;` をファイル先頭 `#define HID_REPLY_WANT (2u + 48u)` に置換する。値は同一。

- [ ] **Step 5: ビルド**

Run: `cmake --build build --target pico-wakecon`
Expected: 成功。`0x30 mode start` 等の文字列不変

---

### Task 5: link/store/spi 整理＋link分割

**Files:**
- Modify: `src/link.h`（`link_cap_poll` dead宣言削除）
- Modify: `src/store.c`（`get_tlv` ヘルパー）
- Modify: `src/spi.c`, `src/spi.h`（`SPI_TABLE_N` const化）
- Create: `src/link_conn.c`, `src/link_cap.c`, `src/link_beacon.c`
- Modify: `src/link.c`（薄層化 or 削除）、`CMakeLists.txt`
- Test: Picoビルド

**Interfaces:**
- Consumes: Task 1-4なし（独立可）
- Produces: 公開API不変（`link_*`, `store_*`, `spi_*` シグネチャ同一）。`link.h` は再export維持

- [ ] **Step 1: dead宣言削除＋定数命名**

`src/link.h:43` の `void link_cap_poll(uint32_t now_ms);` を削除する（定義が存在しない）。`src/link.c` 先頭に `#define BEACON_MS 1500u`、`#define SCAN_INT 0x0030u` を追加し、`1500u/0x0030u` リテラルを置換する。値は同一。

- [ ] **Step 2: `store.c` のTLV定型を共通化**

先頭に追加：

```c
static bool get_tlv(const btstack_tlv_t **t, void **c) {
    btstack_tlv_get_instance(t, c);
    return *t != NULL;
}
```

7箇所の `btstack_tlv_get_instance(&tlv, &ctx); if (tlv == NULL) return ...;` を `if (!get_tlv(&tlv, &ctx)) return ...;` に置換する。TAG・サイズ・戻り値は不変。

- [ ] **Step 3: `SPI_TABLE_N` を const化**

`src/spi.c:58` の `uint8_t SPI_TABLE_N = ...` を `const uint8_t SPI_TABLE_N = ...` に、`src/spi.h:21` の `extern uint8_t SPI_TABLE_N;` を `extern const uint8_t SPI_TABLE_N;` に変更する。

- [ ] **Step 4: `link.c` を3ファイルへ逐語移動**

`link_init/reconnect/key_count` → `link_conn.c`、`cap_start/cap_report/cap_used/cap_entry/cap_clear/取込期限` → `link_cap.c`、`beacon_start/spoof/再生期限/le_packet/is_le` → `link_beacon.c`。コメント・`☁`注釈も移動。`probe_*` 変数定義は使用側ファイルへ。`link.c` は削除し、`CMakeLists.txt` の `src/link.c` を3ファイルに置換、`link.h` は3分割せず既存シグネチャ維持（include互換）。

- [ ] **Step 5: ビルド**

Run: `cmake --build build --target pico-wakecon`
Expected: 成功。`CAP-START/BCN-START/BCN-DONE/cap cleared` 文字列不変

---

### Task 6: ホストテスト新設

**Files:**
- Create: `tests/host/CMakeLists.txt`
- Create: `tests/host/test_util.c`
- Create: `tests/host/test_cap.c`
- Create: `tests/host/test_input_parse.c`
- Test: `ctest`

**Interfaces:**
- Consumes: Task 1, 4の純粋関数（`util_*`, `cap_*`, `probe_pack_stick/hat/pc_buttons` はホスト用に `hid_parse` 分離が望ましいが、今回は `util/cap` のみ）
- Produces: `ctest` 全PASS

- [ ] **Step 1: `tests/host/CMakeLists.txt` を作成**

```cmake
cmake_minimum_required(VERSION 3.13)
project(pico_wakecon_host_tests C)
set(CMAKE_C_STANDARD 11)
add_executable(test_util test_util.c ../../src/util.c ../../src/spi.c)
add_executable(test_cap test_cap.c ../../src/cap.c ../../src/util.c)
add_test(NAME util COMMAND test_util)
add_test(NAME cap COMMAND test_cap)
```

`spi.c` は `util_format_color` のためリンクする。BTstack不要。

- [ ] **Step 2: `test_util/test_cap` を作成（自前assert）**

`util_hex_val('0'/'a'/'F'/'g')`、`util_parse_hex` 正常・不正、`util_is_zero` 全ゼロ・非ゼロ、`cap_parse` 任天堂Company `0x53,0x05`/VID `0x7e,0x05` の受理・拒否、`cap_offer/best/encode/decode` 往復を検証する。`printf("PASS\n")`＋非ゼロ復帰で失敗とする最小形式。

- [ ] **Step 3: 実行**

Run: `cmake -S tests/host -B build-host -G Ninja; cmake --build build-host; ctest --test-dir build-host -V`
Expected: 全PASS

---

### Task 7: 最終検証＋ブランチまとめ

**Files:**
- Test: Picoクリーンビルド、ホストctest、ログ差分目視
- Commit: `refactor/pico-wakecon-cleanup` へ

**Interfaces:**
- Consumes: Task 1-6すべて
- Produces: レビュー可能な単一ブランチ

- [ ] **Step 1: Picoクリーンビルド**

Run: `Remove-Item -Recurse -Force build-verify -ErrorAction SilentlyContinue; $cmake ... -S . -B build-verify -G Ninja ...; $cmake --build build-verify --target pico-wakecon`
Expected: `pico-wakecon.elf` 生成成功

- [ ] **Step 2: ログ差分目視**

Run: `git diff main...HEAD -- src | Select-String 'probe_line\("|CAP-|BCN-|color |hid open'`
Expected: 文字列リテラル差分なし（移動・共通化のみ）

- [ ] **Step 3: コミット**

```bash
git add src/util.h src/util.c src/main.c src/ui.c src/hid.c src/link.c src/link_conn.c src/link_cap.c src/link_beacon.c src/store.c src/spi.c src/spi.h src/link.h CMakeLists.txt tests/host docs/superpowers
git commit -m "refactor: split wakecon for readability (util/link/ui/hid) with host tests"
```

Expected: `git status --short` に意図外ファイル（`build/`, `build-verify/`, `*.elf`）なし。`git log --oneline -3` でブランチ先頭確認
