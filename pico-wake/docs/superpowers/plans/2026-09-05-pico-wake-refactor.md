# pico-wake リファクタリング Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** pico_wake.cを動作不変で分割し重複をutil化、ホスト単体テストで回帰固定する。

**Architecture:** 逐語移動＋util抽出のみ。新規依存はswitch2_wake_utilだけ。BTstack依存はapp_*に閉じ込め、switch2_wake_*はホストコンパイル可能を維持する。

**Tech Stack:** C11/C17, CMake 4.3.4 + Ninja 1.13.2, Pico SDK 2.3.0 (arm-none-eabi-gcc 15.2.1), Host tests: MSVC 19.35 cl + CTest (Unity不使用・自前assert)。

**Spec:** `docs/superpowers/specs/2026-09-05-pico-wake-refactor-design.md`

## Global Constraints

- UARTログ書式・順序・snprintfバッファサイズを変更しない。
- ADV 31byte配置・type 0x00/0x81・peer逆順格納を変更しない。
- ATT DB 30段handle期待値 (0x0001〜0x0038) と権限を変更しない。
- SMP能動開始なし方針を維持する (sm_set_authentication_requirements(0))。
- 秘密値 (LTK/IRK/AES鍵) をログに出さない。
- main()初期化順序を変更しない。
- Picoビルド構成 (board pico2_w, link pico_btstack_ble等) を変更しない。
- C11/C17維持。新規-Werror導入なし。

---

### Task 1: 衛生と.gitignore

**Files:**
- Delete: `C:\Users\moilo\pico-wake\CMakeLists copy.txt`
- Delete: `C:\Users\moilo\pico-wake\__init__.py`
- Modify: `C:\Users\moilo\pico-wake\.gitignore`
- Test: `C:\Users\moilo\pico-wake\CMakeLists.txt` (read-only確認)

**Interfaces:**
- Consumes: なし
- Produces: クリーンな作業ツリー (pico-wake.cは温存し対象外と明示)

- [ ] **Step 1: 放置ファイルの存在確認**

```powershell
Get-Item "C:\Users\moilo\pico-wake\CMakeLists copy.txt", "C:\Users\moilo\pico-wake\__init__.py", "C:\Users\moilo\pico-wake\pico-wake.c" | Select-Object Name,Length
```

Run: 上記を `C:\Users\moilo\pico-wake` で実行
Expected: 3行表示 (710, 0, 0 byte)

- [ ] **Step 2: 放置ファイル2件を削除**

```powershell
Remove-Item -LiteralPath "C:\Users\moilo\pico-wake\CMakeLists copy.txt"
Remove-Item -LiteralPath "C:\Users\moilo\pico-wake\__init__.py"
```

Run: 実行後 `Test-Path` で両方Falseを確認
Expected: 両方存在しない。pico-wake.cは残す。

- [ ] **Step 3: .gitignoreを拡張**

`.gitignore` の現内容は2行 (`build`, `!.vscode/*`)。以下へ書換え:

```
build
build-verify/
*.elf
*.uf2
*.hex
*.bin
.cache/
!.vscode/*
```

Run: ファイル保存後 `Get-Content .gitignore`
Expected: 上記7行と一致

- [ ] **Step 4: Commit**

```bash
git add pico-wake/.gitignore
git rm --cached -r --quiet pico-wake/"CMakeLists copy.txt" pico-wake/__init__.py 2>/dev/null; true
git status --short -- pico-wake/docs pico-wake/.gitignore
git commit -m "chore: remove stray files and ignore build artifacts"
```

Expected: commit成功。`git status`にbuild成果物が出ないこと。

---

### Task 2: switch2_wake_util新設

**Files:**
- Create: `C:\Users\moilo\pico-wake\switch2_wake_util.h`
- Create: `C:\Users\moilo\pico-wake\switch2_wake_util.c`
- Modify: `C:\Users\moilo\pico-wake\CMakeLists.txt` (add_executableにswitch2_wake_util.c追加)
- Test: ホストコンパイル確認 (Task 3で使用)

**Interfaces:**
- Consumes: なし
- Produces:
  - `void switch2_wake_secure_zero(void *ptr, size_t len)`
  - `bool switch2_wake_is_all_zero(const uint8_t *data, size_t len)`
  - `void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16])`
  - `int switch2_wake_hex_value(char ch)`
  - `bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size)`

- [ ] **Step 1: ヘッダ作成**

```c
#ifndef SWITCH2_WAKE_UTIL_H
#define SWITCH2_WAKE_UTIL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void switch2_wake_secure_zero(void *ptr, size_t len);
bool switch2_wake_is_all_zero(const uint8_t *data, size_t len);
void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16]);
int switch2_wake_hex_value(char ch);
bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: 実装作成 (既存コードと同一セマンティクス)**

```c
#include "switch2_wake_util.h"
#include <string.h>
void switch2_wake_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0u) { *p++ = 0u; }
}
bool switch2_wake_is_all_zero(const uint8_t *data, size_t len) {
    uint8_t acc = 0u; size_t i;
    for (i = 0u; i < len; i++) { acc |= data[i]; }
    return acc == 0u;
}
void switch2_wake_reverse16(const uint8_t in[16], uint8_t out[16]) {
    unsigned int i;
    for (i = 0u; i < 16u; i++) { out[i] = in[15u - i]; }
}
int switch2_wake_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}
bool switch2_wake_parse_hex(const char *text, uint8_t *out, size_t size) {
    size_t i;
    if (text == NULL || out == NULL) { return false; }
    if (strlen(text) != size * 2u) { return false; }
    for (i = 0u; i < size; ++i) {
        int hi = switch2_wake_hex_value(text[i * 2u]);
        int lo = switch2_wake_hex_value(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) { return false; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}
```

- [ ] **Step 3: CMakeListsへ1行追加**

`switch2_wake_capture.c` の次に `switch2_wake_util.c` を追加する。`pico-wake.c`は加えない。

- [ ] **Step 4: Commit**

```bash
git add pico-wake/switch2_wake_util.h pico-wake/switch2_wake_util.c pico-wake/CMakeLists.txt
git commit -m "refactor: add switch2_wake_util for shared helpers"
```

---

### Task 3: 既存モジュールの重複をutil呼出へ置換

**Files:**
- Modify: `switch2_wake_core.c` (secure_zero→util, 26-31行削除しinclude追加)
- Modify: `switch2_wake_input.c` (secure_zero/hex_value/parse_hex→util)
- Modify: `switch2_wake_store.c` (wake_secure_zero/wake_is_all_zero→util)
- Modify: `switch2_wake_capture.c` (all_zero→util)
- Modify: `switch2_wake_cmd.c` (reverse16→util)
- Test: `tests/host/test_util_regression.c` (Task 9で作成、ここではPicoビルドで確認)

**Interfaces:**
- Consumes: Task 2の5関数
- Produces: 同一振る舞いの各モジュール (API変更なし)

- [ ] **Step 1: core置換**

`switch2_wake_core.c` 先頭に `#include "switch2_wake_util.h"` を追加し、`static void secure_zero` 定義 (26-31行) を削除。`clear_secrets()` 内の `secure_zero(` 3箇所+response箇所を `switch2_wake_secure_zero(` へ置換。`switch2_wake_init` 内の `secure_zero(&ctx` も置換。

- [ ] **Step 2: input置換**

`static void secure_zero`、`static int hex_value`、`static bool parse_hex` の3定義を削除し `#include "switch2_wake_util.h"` 追加。呼出側 `switch2_wake_input_command_clear` の `secure_zero(command` → `switch2_wake_secure_zero(command`、`parse_hex(` → `switch2_wake_parse_hex(` (5箇所)、`hex_value(` 間接呼出はutil内で解決されるため追加対応不要。

- [ ] **Step 3: store/capture/cmd置換**

store: `wake_secure_zero`→`switch2_wake_secure_zero` (全置換)、`wake_is_all_zero(ltk, 16)`→`switch2_wake_is_all_zero(ltk, 16)`。capture: `all_zero(`→`switch2_wake_is_all_zero(`。cmd: `reverse16(`→`switch2_wake_reverse16(` (4箇所: aes準備2、get_ltk 1、selftest 2)。各ファイルのstatic定義を削除。

- [ ] **Step 4: Picoビルドで回帰確認**

```powershell
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.3.0"
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake" -S . -B build-verify -G Ninja "-DPICO_BOARD=pico2_w"
& "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja" -C build-verify pico-wake
```

Expected: `pico-wake.elf` 生成成功、警告の新規増加なし。

- [ ] **Step 5: Commit**

```bash
git add pico-wake/switch2_wake_core.c pico-wake/switch2_wake_input.c pico-wake/switch2_wake_store.c pico-wake/switch2_wake_capture.c pico-wake/switch2_wake_cmd.c
git commit -m "refactor: dedupe zero/hex/reverse helpers via switch2_wake_util"
```

---

### Task 4: app_state/app_config抽出

**Files:**
- Create: `C:\Users\moilo\pico-wake\app_state.h` (extern共有)
- Create: `C:\Users\moilo\pico-wake\app_config.h`
- Create: `C:\Users\moilo\pico-wake\app_config.c`
- Modify: `C:\Users\moilo\pico-wake\pico_wake.c` (make_config/meta_state_nameを削除しincludeへ)
- Modify: `C:\Users\moilo\pico-wake\CMakeLists.txt` (app_config.c追加)
- Test: Picoビルド

**Interfaces:**
- Consumes: なし
- Produces:
  - `switch2_wake_config_t make_config(void)` →改め `switch2_wake_config_t app_make_config(void)`
  - `const char *app_meta_state_name(uint8_t state)`
  - `app_state.h` のextern群 (adv, uart_parser, store, local_identity, att_report, le_connection, カウンタ群, cap/cmd状態群)

注意: 関数名変更は動作に影響しないがログ不変のため、旧`make_config`/`meta_state_name`は残さず新名へ一括置換する。呼出は `packet_handler` BOOT経路、`reset_core`、`show_status` の3箇所。

- [ ] **Step 1: app_state.h作成 (変数extern列挙、型・名前不変)**

`pico_wake.c` 30-88行のstatic変数群をextern宣言化。`WAKE_BUTTON_GPIO` ifdef維持。`null_addr`、`stack_ready`含む。

- [ ] **Step 2: app_config作成・pico_wake.cから削除・呼出置換・ビルド・Commit**

```bash
git add pico-wake/app_state.h pico-wake/app_config.h pico-wake/app_config.c pico-wake/pico_wake.c pico-wake/CMakeLists.txt
git commit -m "refactor: extract app_state and app_config from pico_wake"
```

---

### Task 5: app_ports_*抽出 (BTstack橋渡し)

**Files:**
- Create: `app_ports_adv.c`, `app_ports_store.c`, `app_ports_cmd.c`, `app_ports_att.c` (+対応.hは最小限: テーブルexternのみ)
- Modify: `pico_wake.c` (該当関数削除)
- Modify: `CMakeLists.txt` (4ファイル追加)
- Test: Picoビルド

**Interfaces:**
- Consumes: Task 4のapp_state.h
- Produces: `extern const switch2_wake_store_port_t app_store_port;` 等4テーブル (旧`store_port`/`cmd_port`/`att_port`を改名せず維持する場合はextern名維持。改名する場合は全参照置換。推奨: 名前維持で移動のみ)

- [ ] **Step 1: 4ファイルを逐語移動 (コメント・★注釈維持)**

adv: port_set_params/data/enable。store: tlv_impl/context＋8関数＋store_port。cmd: aes系＋store_bond＋read_memory＋emit＋observe＋selftest＋cmd_port＋get_ltk_callback。att: att_port_init/svc128/svc16/char128/char16/desc128/size＋att_port。

- [ ] **Step 2: ビルド・Commit**

```bash
git add pico-wake/app_ports_adv.c pico-wake/app_ports_store.c pico-wake/app_ports_cmd.c pico-wake/app_ports_att.c pico-wake/pico_wake.c pico-wake/CMakeLists.txt
git commit -m "refactor: extract BTstack port bridges from pico_wake"
```

---

### Task 6: app_att抽出

**Files:**
- Create: `app_att.h`, `app_att.c`
- Modify: `pico_wake.c`
- Modify: `CMakeLists.txt`
- Test: Picoビルド

**Interfaces:**
- Consumes: app_state.h
- Produces:
  - `uint16_t att_read_callback(hci_con_handle_t, uint16_t, uint16_t, uint8_t *, uint16_t)`
  - `int att_write_callback(hci_con_handle_t, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t)`
  - `#define APP_ATT_HANDLE_UNKNOWN_READ1 0x0003u` 等 (値は既存リテラルと同一)

- [ ] **Step 1: value/desc表＋callbackを逐語移動**

`value_unknown_read1/2`、`WAKE_DESC_TABLE_SIZE/wake_desc_handles/wake_desc_values/wake_desc_index`、`att_read_callback`、`att_write_callback` を移動。`0x0003/0x0007/0x000f` リテラルは同値defineへ置換 (動作同一)。

- [ ] **Step 2: ビルド・Commit**

```bash
git add pico-wake/app_att.h pico-wake/app_att.c pico-wake/pico_wake.c pico-wake/CMakeLists.txt
git commit -m "refactor: extract ATT callbacks into app_att"
```

---

### Task 7: app_ui抽出

**Files:**
- Create: `app_ui.h`, `app_ui.c`
- Modify: `pico_wake.c`
- Modify: `CMakeLists.txt`
- Test: Picoビルド

**Interfaces:**
- Consumes: app_state.h, app_config, app_att (payload参照), switch2_wake_* API
- Produces:
  - `void app_apply_actions(const char *name, wake_result_t result, uint32_t actions)`
  - `void app_show_status(void)`
  - `void app_reset_core(bool verified, uint32_t *actions, wake_result_t *result)`
  - `void app_handle_command(switch2_wake_input_command_t *command)`

旧`apply_actions`/`show_status`/`reset_core`/`cap_restore_adv`/`bdaddr_spoof`/`cap_show_list`/`handle_command`を移動。`handle_command`内の巨大switchは分割せず移動のみ (ログ文言維持)。

- [ ] **Step 1: 逐語移動・ビルド・Commit**

```bash
git add pico-wake/app_ui.h pico-wake/app_ui.c pico-wake/pico_wake.c pico-wake/CMakeLists.txt
git commit -m "refactor: extract status and command UI into app_ui"
```

---

### Task 8: app_loop抽出・mainスリム化

**Files:**
- Create: `app_loop.h`, `app_loop.c`
- Modify: `pico_wake.c` (グローバル定義＋mainのみ残す)
- Modify: `CMakeLists.txt`
- Test: Picoビルド

**Interfaces:**
- Consumes: Task 4-7の全モジュール
- Produces:
  - `void app_poll_handler(btstack_timer_source_t *timer)`
  - `void app_packet_handler(uint8_t, uint16_t, uint8_t *, uint16_t)`

`poll_uart`/`handle_button`/`poll_handler`/`att_packet_handler`/`sm_packet_handler`/`packet_handler`を移動。`main()`の初期化順序は一切変更しない。

- [ ] **Step 1: 逐語移動・ビルド・Commit**

```bash
git add pico-wake/app_loop.h pico-wake/app_loop.c pico-wake/pico_wake.c pico-wake/CMakeLists.txt
git commit -m "refactor: extract event loop into app_loop, slim main"
```

---

### Task 9: ホストテスト基盤＋core/adv/input/capture

**Files:**
- Create: `tests/host/CMakeLists.txt`
- Create: `tests/host/test_core.c`, `test_adv.c`, `test_input.c`, `test_capture.c`, `test_util.c`
- Modify: トップCMakeLists.txtに `if(BUILD_HOST_TESTS) add_subdirectory(tests/host) endif()` 追加 (Picoビルド無影響)
- Test: `ctest`

**Interfaces:**
- Consumes: Task 2-3のutil＋各switch2_wake API
- Produces: 5つの実行ファイル＋CTest登録

tests/host/CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.13)
project(pico_wake_host_tests C)
set(CMAKE_C_STANDARD 11)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(test_util test_util.c ../../switch2_wake_util.c)
add_executable(test_core test_core.c ../../switch2_wake_core.c ../../switch2_wake_util.c)
add_executable(test_adv test_adv.c ../../switch2_wake_adv.c)
add_executable(test_input test_input.c ../../switch2_wake_input.c ../../switch2_wake_util.c)
add_executable(test_capture test_capture.c ../../switch2_wake_capture.c ../../switch2_wake_util.c)
add_test(NAME util COMMAND test_util)
add_test(NAME core COMMAND test_core)
add_test(NAME adv COMMAND test_adv)
add_test(NAME input COMMAND test_input)
add_test(NAME capture COMMAND test_capture)
```

自前assert形式 (例 test_adv.c抜粋):

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "switch2_wake_adv.h"
static uint8_t last_data[31]; static int enable_calls;
static int fake_set_params(void *c, uint16_t a, uint16_t b) { (void)c; (void)a; (void)b; return 0; }
static int fake_set_data(void *c, const uint8_t *d, uint8_t n) { (void)c; memcpy(last_data, d, n); return 0; }
static int fake_enable(void *c, bool e) { (void)c; (void)e; enable_calls++; return 0; }
int main(void) {
    switch2_wake_adv_t adv; uint8_t peer[6] = {1,2,3,4,5,6};
    switch2_wake_adv_port_t port = {NULL, fake_set_params, fake_set_data, fake_enable};
    assert(switch2_wake_adv_init(&adv, &port) == WAKE_ADV_RESULT_OK);
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_WAKE) == WAKE_ADV_RESULT_NO_PEER);
    assert(switch2_wake_adv_set_peer(&adv, peer) == WAKE_ADV_RESULT_OK);
    assert(switch2_wake_adv_apply(&adv, WAKE_ACTION_ADV_WAKE) == WAKE_ADV_RESULT_OK);
    assert(last_data[16] == 0x81u);
    assert(last_data[17] == 6u && last_data[22] == 1u);
    assert(switch2_wake_adv_is_wake(&adv));
    printf("adv ok\n"); return 0;
}
```

- [ ] **Step 1: 基盤＋5テスト作成**

Run:

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 >NUL && C:\Users\moilo\.pico-sdk\cmake\v4.3.4\bin\cmake -S tests/host -B build-host -G Ninja && C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja -C build-host && .\build-host\test_adv.exe && .\build-host\test_input.exe'
```

Expected: 全実行ファイルが `ok` 表示で終了コード0。

- [ ] **Step 2: Commit**

```bash
git add pico-wake/tests/host pico-wake/CMakeLists.txt
git commit -m "test: add host unit tests for util/core/adv/input/capture"
```

---

### Task 10: cmd/store/att_dbテスト

**Files:**
- Create: `tests/host/test_cmd.c`, `test_store.c`, `test_att_db.c`
- Modify: `tests/host/CMakeLists.txt` (3ターゲット＋add_test追加)
- Test: `ctest`

**Interfaces:**
- Consumes: Task 9基盤
- Produces: 3実行ファイル

要点:
- test_cmd: port mock (get_local_addr/store_bond/emit_response/start_aes即時done) で0x15/01→9byte応答、0x15/04→LTK導出、未設定時0x15/02拒否、peer不一致get_ltk拒否。
- test_store: TLVメモリmock＋DB配列mockでencode/decode往復、CRC破損→CRC、schema不一致→SCHEMA、全ゼロLTK import拒否、commit昇格、forget消去。
- test_att_db: att_port mock (呼出毎にhandle+1を返すカウンタ) でbuild成功＋`expected_handle(13)==0x0018`確認、mockを13段目で+1ずらして`WAKE_ATT_ERR_STEP_HANDLE`＋`failed_step==13`を確認。

- [ ] **Step 1: 3テスト作成・ctest全緑**

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 >NUL && C:\Users\moilo\.pico-sdk\ninja\v1.13.2\ninja -C build-host && cd build-host && ctest --output-on-failure'
```

Expected: `8/8 tests passed` (util/core/adv/input/capture/cmd/store/att_db)。

- [ ] **Step 2: Commit**

```bash
git add pico-wake/tests/host
git commit -m "test: add host unit tests for cmd/store/att_db"
```

---

### Task 11: 最終検証とPR準備

**Files:**
- 変更なし (検証のみ)

- [ ] **Step 1: ホスト全テスト**

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 >NUL && cd build-host && ctest --output-on-failure'
```

Expected: 全PASS。

- [ ] **Step 2: Picoクリーンビルド**

```powershell
Remove-Item -Recurse -Force build-verify -ErrorAction SilentlyContinue
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.3.0"
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake" -S . -B build-verify -G Ninja "-DPICO_BOARD=pico2_w"
& "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja" -C build-verify pico-wake
```

Expected: `pico-wake.elf/.uf2` 生成。

- [ ] **Step 3: ログ差分ゼロ確認**

```powershell
git diff main...HEAD -- pico_wake.c app_*.c | Select-String "uart_put_line|snprintf" | Measure-Object
git status --short -- pico-wake | Select-Object -First 20
```

Expected: ログ書式の実質差分なし (移動のみ)、意図外ファイルなし。

- [ ] **Step 4: Push (PRはWebで作成)**

```bash
git push -u origin refactor/pico-wake-split
```

Expected: push成功。PR URLを報告。マージはしない。
