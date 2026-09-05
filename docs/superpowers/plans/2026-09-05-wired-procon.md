# 有線Pro Controller (USB) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pico 2 W の USB を Switch ドック直結の有線 Pro Controller 化し、UART 入力のまま wake + 有線/無線排他切替を実現する。

**Architecture:** USB 依存は新規 `usb_hid` (純粋応答組立) + `usb_wired`/`usb_descriptors` (TinyUSB glue) に隔離し、入力状態 (`probe_btn/lx/ly/rx/ry`) と色 (`spi_color_6050`) は既存 `hid`/`spi` を再利用する。BT 初期化順・ログ書式・BT 記述子は不変。

**Tech Stack:** C11, Pico SDK 2.3.0 (RP2350, board `pico2_w` 固定), TinyUSB device, BTstack (既存), CTest ホストテスト (MSVC + `/utf-8`)。

**Spec:** `docs/superpowers/specs/2026-09-05-wired-procon-design.md`

## Global Constraints

- board は `pico2_w` 固定 (`CMakeLists.txt` 内で設定)。変えない。
- `main()` の初期化順 (GAP → L2CAP → SDP → HID → HCI → timer → power) を変えない。追加は末尾分岐のみ。
- UART ログ書式・`snprintf` バッファサイズを変えない。`st` 行の書式変更禁止。新規状態は `usb ...` 行に分離。
- ADV 31B 配置・`type 0x00/0x81`・peer 送り順・SPI 応答値・SDP/HID 記述子 (BT 側) を変えない。
- 秘密 (LTK/IRK/AES 鍵) をログに出さない。
- `cap`/`util` (+新規 `usb_hid`) は Pico・BTstack 非依存に保つ (`btstack.h` や `pico/*.h` を include しない)。
- ソースの日本語コメントは残す。改行コードの一括変換はしない。
- ビルドはフルパス cmake/ninja。ビルド前に VSCode saveAll。成果物 `build/`・`build-verify/` は git 管理外。
- 作業ブランチ `refactor/wired-procon`。main への push・PR は指示があるときのみ。

## File Structure

- Create: `src/usb_hid.h` — 純粋な `80 xx → 81 xx` 応答組立 (Pico/BTstack 非依存、ホストテスト対象)。
- Create: `src/usb_hid.c` — 上記の実装 (`80 01/02/03/04/05/06` のみ。`91/92` は未対応=0 返却)。
- Create: `src/usb_wired.h` — TinyUSB glue の公開 IF (有効化・状態・タスク)。
- Create: `src/usb_wired.c` — ハンドシェイク状態機・入力レポート送信・`tud_*` コールバック呼び出し側。
- Create: `src/usb_descriptors.c` — TinyUSB 記述子コールバック (VID `0x057E`/PID `0x2009` 等の純正写し)。
- Create: `src/tusb_config.h` — TinyUSB 設定 (`pico-examples/usb/device/dev_hid_composite` 相当を最小化)。
- Create: `tests/host/test_usb.c` — `test_cap.c` と同形式 (CHECK マクロ) のホストテスト。
- Modify: `tests/host/CMakeLists.txt` — `test_usb` 追加 (既存 2 行のパターン踏襲)。
- Modify: `CMakeLists.txt` — `pico_enable_stdio_usb(pico-wakecon 0)`、新規 3 ソース追加、`tinyusb_device` + `tinyusb_board` リンク、`CFG_TUSB_OS=OPT_OS_PICO` 定義。
- Modify: `src/main.c` — 初期化末尾に `usb_wired_init()` 追加のみ。既存順序不変。
- Modify: `src/ui.c` — `UI_CMDS` に `W` 1 行追加 + `cmd_w` + `usb ...` 状態行。既存コマンド・書式不変。
- Modify: `src/link_conn.c` (または `link.c`) — 有線有効中は Classic 自動再接続を抑止するガード 1 箇所。

---

### Task 1: Spike — 有線ハンドシェイク対応表の確定 (throwaway 調査、結論のみ残す)

**Files:**
- Modify: なし (コード変更なし)
- Test: なし (実機観測)
- Memo: 観測結果を `docs/superpowers/specs/2026-09-05-wired-procon-design.md` の末尾に追記する (対応表のみ)

**Interfaces:**
- Consumes: `USB-HID-Notes.md` の `80 01〜06/91/92` 定義、ToadKing デバイス記述子ダンプ
- Produces: 対応表 `Switch要求バイト列 → 期待応答バイト列` と Input レポート ID/サイズ/周期 (Task 2〜4 の入力)

- [ ] **Step 1: 純正記述子ダンプを PC で取得する**

```powershell
# Pico ではなく手持ちの純正 Pro Controller を PC の USB に挿す
# OS の USB 記述子ビューアで次を確認する
# bcdUSB=2.00, bMaxPacketSize0=64, idVendor=0x057E, idProduct=0x2009
# iManufacturer="Nintendo Co., Ltd", iProduct="Pro Controller", iSerial="000000000001"
# HID Report Descriptor 全バイトと Endpoint (IN/OUT アドレス・サイズ・間隔)
```

- [ ] **Step 2: Switch ドック接続時の要求順序を観測する**

```text
観測手段 (あるものから選ぶ。なければ Task 2 の最小 HID で Switch に挿して UART ログに要求を出す):
1. 純正コントローラと Switch ドック間に USB プロトコルアナライザ/Beagle を挟む
2. なければ Pico 側に素通しロガーを書いて順序だけ記録する (本実装とは別ファイル、使い捨て)
記録項目: SETUP/GET_DESCRIPTOR 後の最初の HID OUT (80 02 か?)、80 03 の有無、
80 04 到達までの往復、Input IN の Report ID/サイズ/周期、80 05 が来る条件
```

- [ ] **Step 3: 対応表を設計メモに追記する**

```markdown
例 (実測値で置き換えること。推測値を書かない):
- `80 02` (len=2) → `81 02` (len=2), セッション先頭で1回
- `80 03` (len=2) → `81 03` (len=2), 80 02 直後のみ
- `80 04` (len=2) → `81 04` (len=2), 以降 USB のみ・BT復帰なし
- `80 01` → `81 01 00 <type> <mac6> ...` (全バイト記録)
- Input: Report ID=0x__, サイズ=__B, 周期=__ms
```

- [ ] **Step 4: Go/No-Go 判定**

```text
Go 条件: 80 02/03/04 (+01) の往復が再現性あり、Input ID/サイズ特定
No-Go 時: 案B (HORI 型標準 HID) へ切替をユーザに相談。推測実装はしない。
```

---

### Task 2: 純粋応答ロジック `usb_hid` + ホストテスト (TDD)

**Files:**
- Create: `src/usb_hid.h`
- Create: `src/usb_hid.c`
- Create: `tests/host/test_usb.c`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 の対応表 (要求→応答バイト列)
- Produces: `int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out, int out_max, const uint8_t mac[6], uint8_t dev_type)` — 成功時 応答長 (>0)、未対応 (`80 91/92` 等)・不正時 0。`bool usb_req_is_handshake(const uint8_t *req, int req_len)`

- [ ] **Step 1: Write the failing test**

```c
/* tests/host/test_usb.c: test_cap.c と同形式。BTstack/Pico不要。 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "usb_hid.h"

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } \
} while (0)

int main(void)
{
    uint8_t out[64];
    const uint8_t mac[6] = {0x7c, 0xbb, 0x8a, 0x11, 0x22, 0x33};
    int n;
    uint8_t r02[] = {0x80, 0x02};
    uint8_t r04[] = {0x80, 0x04};

    n = usb_build_81_reply(r02, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x02u);
    n = usb_build_81_reply(r04, 2, out, sizeof(out), mac, 0x03u);
    CHECK(n == 2 && out[0] == 0x81u && out[1] == 0x04u);
    /* 91/92 は未対応=0 を返す (でたらめ値を返さない)。 */
    {
        uint8_t r91[] = {0x80, 0x91, 0x00};
        CHECK(usb_build_81_reply(r91, 3, out, sizeof(out), mac, 0x03u) == 0);
    }
    if (fails == 0) { printf("OK usb\n"); }
    return fails != 0;
}
```

- [ ] **Step 2: CMake に test_usb を追加する (既存パターン踏襲)**

```cmake
add_executable(test_usb test_usb.c ../../src/usb_hid.c)
target_include_directories(test_usb PRIVATE ../../src)
add_test(NAME usb COMMAND test_usb)
```

- [ ] **Step 3: Run test to verify it fails**

```powershell
# vcvars64.bat 経由の cmd で実行する (素の PowerShell にコンパイラはない)。
# build-host/ は設定済み。ctest.exe も cmake と同ディレクトリのフルパスを使う。
cmd /c "<vcvars経由のbat> で cmake --build build-host --target test_usb && build-host\test_usb.exe"
# Expected: ビルド失敗 ("usb_hid.h がない") または FAIL。成功したらおかしい。
```

- [ ] **Step 4: Write minimal implementation**

```c
/* src/usb_hid.h */
#ifndef USB_HID_H
#define USB_HID_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 80 xx → 81 xx 応答組立。Pico/BTstack 非依存。 */
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type);
bool usb_req_is_handshake(const uint8_t *req, int req_len);
#ifdef __cplusplus
}
#endif
#endif
```

```c
/* src/usb_hid.c: 日本語コメントは残す。でたらめ値は返さない。 */
#include <string.h>
#include "usb_hid.h"

bool usb_req_is_handshake(const uint8_t *req, int req_len)
{
    return req != NULL && req_len >= 2 && req[0] == 0x80u;
}

/* 80 01/02/03/04/05/06 のみ応答。91/92 等は 0 (未対応)。 */
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type)
{
    uint8_t sub;
    if (req == NULL || out == NULL || mac == NULL) {
        return 0;
    }
    if (!usb_req_is_handshake(req, req_len)) {
        return 0;
    }
    sub = req[1];
    /* Task 1 の対応表で各分岐の応答バイト列を確定させる。 */
    switch (sub) {
        case 0x01u: {
            /* 例: 81 01 00 <type> <mac6>。実測で全バイト確定後に固定化。 */
            if (out_max < 10) {
                return 0;
            }
            out[0] = 0x81u; out[1] = 0x01u; out[2] = 0x00u;
            out[3] = dev_type;
            memcpy(&out[4], mac, 6);
            return 10;
        }
        case 0x02u:
        case 0x03u:
        case 0x04u:
        case 0x05u:
        case 0x06u:
            if (out_max < 2) {
                return 0;
            }
            out[0] = 0x81u; out[1] = sub;
            return 2;
        default:
            return 0;
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

```powershell
cmd /c "<vcvars経由のbat> で cmake --build build-host --target test_usb && build-host\test_usb.exe && ctest --test-dir build-host -R \"util|cap|usb\" --output-on-failure"
# Expected: PASS (OK usb)。既存 test_util/test_cap も PASS のまま。
```

- [ ] **Step 6: Commit**

```bash
git add src/usb_hid.h src/usb_hid.c tests/host/test_usb.c tests/host/CMakeLists.txt
git commit -m "feat: add USB 80xx handshake reply builder with host test"
```

---

### Task 3: TinyUSB 記述子 + ビルド切替 (UART 一本化)

**Files:**
- Create: `src/usb_descriptors.c`
- Create: `src/tusb_config.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 の記述子ダンプ (VID/PID/文字列/HID レポート/EP)
- Produces: TinyUSB device として列挙成功。`build/pico-wakecon.uf2` 生成。

- [ ] **Step 1: 記述子ファイルを作る (純正値の写し。書換余地なし)**

```c
/* src/usb_descriptors.c (抜粋。実測で全バイト確定させる) */
#include "tusb.h"

/* Device: USB 2.00, EP0 64B, VID 0x057E, PID 0x2009, bcdDevice は実測値。 */
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x057E,
    .idProduct = 0x2009,
    .bcdDevice = 0x0200, /* 実測で上書き */
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

/* 文字列: "Nintendo Co., Ltd" / "Pro Controller" / "000000000001" (実測一致)。 */
/* HID Report / Config / EP は Task 1 の実測で埋める。推測で埋めない。 */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}
```

- [ ] **Step 2: CMake を切替える (stdio_usb 無効 + TinyUSB リンク)**

```cmake
# 変更前: pico_enable_stdio_usb(pico-wakecon 1)
pico_enable_stdio_uart(pico-wakecon 0)
pico_enable_stdio_usb(pico-wakecon 0)

# SOURCES に追加: src/usb_hid.c src/usb_wired.c src/usb_descriptors.c
# 参考 pico-examples/dev_hid_composite:
target_link_libraries(pico-wakecon
    # ...既存...
    tinyusb_device
    tinyusb_board
)
target_compile_definitions(pico-wakecon PRIVATE CFG_TUSB_OS=OPT_OS_PICO)
target_include_directories(pico-wakecon PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
```

- [ ] **Step 3: ビルドして列挙確認する**

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build --target pico-wakecon
# PC に挿して VID 057E / PID 2009 で列挙されること。UART ログは GP0/GP1 側で見る。
# Expected: ビルド成功 + 記述子一致。失敗時は Task 1 に戻る。
```

- [ ] **Step 4: Commit**

```bash
git add src/usb_descriptors.c src/tusb_config.h CMakeLists.txt
git commit -m "feat: add wired Pro Controller USB descriptors, stdio over UART only"
```

---

### Task 4: `usb_wired` 状態機 + 入力レポート送信

**Files:**
- Create: `src/usb_wired.h`
- Create: `src/usb_wired.c`
- Modify: `src/main.c` (末尾に init 追加のみ)
- Modify: `src/hid.c` or `src/hid.h` (必要なら共有ヘルパ公開。なければ変更なし)

**Interfaces:**
- Consumes: `usb_build_81_reply()` (Task 2)、`probe_btn/probe_lx/ly/rx/ry` (hid)、`probe_pack_stick()`、`spi_color_6050`
- Produces: `void usb_wired_init(void)`、`void usb_wired_task(uint32_t now_ms)`、`void usb_wired_set_enabled(bool)`、`bool usb_wired_is_enabled(void)`、`bool usb_wired_is_configured(void)`、`bool usb_wired_handshake_done(void)`

- [ ] **Step 1: 公開 IF を定義する**

```c
/* src/usb_wired.h */
#ifndef USB_WIRED_H
#define USB_WIRED_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void usb_wired_init(void);
void usb_wired_task(uint32_t now_ms);
void usb_wired_set_enabled(bool en);
bool usb_wired_is_enabled(void);
bool usb_wired_is_configured(void);
bool usb_wired_handshake_done(void);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: 状態機を実装する (80 04 完了前は入力を送らない)**

```c
/* src/usb_wired.c (骨子) */
#include "tusb.h"
#include "usb_wired.h"
#include "usb_hid.h"
#include "hid.h"

static bool wired_enabled;
static bool handshake_done; /* 80 04 応答後に true。80 05 で false に戻す。 */

/* HID OUT 受信 → usb_build_81_reply() で応答組立 → tud_hid_report() で返送。 */
/* 80 04 で handshake_done=true、80 05 で false。91/92 は無応答。 */
/* tud_hid_set_report_cb() / tud_mount_cb() / tud_umount_cb() で状態更新。 */

void usb_wired_task(uint32_t now_ms)
{
    (void)now_ms;
    tud_task();
    /* enabled && configured && handshake_done のときのみ、
       probe_btn/lx.. のスナップショットを Task 1 確定の Input 形式で送信。
       周期は実測値 (BT 側 full 相当の約7ms を暫定上限にしない。実測優先)。
       80 04 前は送らない。 */
}
```

- [ ] **Step 3: main.c の末尾に init を足す (順序不変)**

```c
/* 既存の hci_power_control / timer 登録の後に追加。GAP→..→power の順は触らない。 */
usb_wired_init();
```

- [ ] **Step 4: ビルドする**

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake --build build --target pico-wakecon
# Expected: 成功。UART に既存の起動行が出ること。
```

- [ ] **Step 5: Commit**

```bash
git add src/usb_wired.h src/usb_wired.c src/main.c
git commit -m "feat: add wired USB state machine and input sender"
```

---

### Task 5: `W` モード切替 + Classic 抑止 + `usb` 状態行

**Files:**
- Modify: `src/ui.c`
- Modify: `src/link_conn.c` (または `src/link.c`)
- Modify: `src/usb_wired.c` (状態表示用 getter が足りなければ)

**Interfaces:**
- Consumes: `usb_wired_set_enabled/is_enabled/is_configured/handshake_done`
- Produces: `W` コマンド (`W`/`W 0`/`W 1`)、`usb en=.. cfg=.. hs=..` 行。有線中有効中は Classic 再接続抑止。

- [ ] **Step 1: `W` コマンドを追加する (表に1行。既存行不変)**

```c
/* src/ui.c */
static void cmd_w(void)
{
    /* W[ 0|1]: 0=無線/BT、1=有線/USB。引数なしは現在値表示。 */
    /* パースは既存 cmd_c の数字解釈パターンを踏襲。範囲外は usage 行。 */
    /* 切替時は usb_wired_set_enabled() を呼ぶ。書式は新規 "usb ..." のみ。 */
}

static const ui_cmd_t UI_CMDS[] = {
    /* ...既存... */
    { 'W', cmd_w },
    /* ...既存... */
};
```

- [ ] **Step 2: Classic 再接続を抑止する (1 箇所ガード)**

```c
/* src/link_conn.c の link_reconnect_handler() 先頭付近 */
if (usb_wired_is_enabled()) {
    return; /* 有線モード中は Classic に出ていかない。二重認識防止。 */
}
```

- [ ] **Step 3: 状態行を足す (`st` は触らない)**

```c
/* probe_show_status() の末尾に追加 (既存 3 行の後に 1 行)。 */
snprintf(m, sizeof(m), "usb en=%u cfg=%u hs=%u",
         usb_wired_is_enabled() ? 1u : 0u,
         usb_wired_is_configured() ? 1u : 0u,
         usb_wired_handshake_done() ? 1u : 0u);
probe_line(m);
```

- [ ] **Step 4: ビルド + 手動確認**

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake --build build --target pico-wakecon
# W / W 1 / W 0 / ? で usb 行が出ること。有線中有効中は "reconnect try" が出ないこと。
```

- [ ] **Step 5: Commit**

```bash
git add src/ui.c src/link_conn.c src/usb_wired.c
git commit -m "feat: add W wired/wireless mode switch with Classic suppression"
```

---

### Task 6: 回帰確認 (無線・wake・書式不変の証跡)

**Files:**
- Modify: なし (確認のみ。失敗があれば該当 Task に戻る)

**Interfaces:**
- Consumes: Task 1〜5 の成果
- Produces: 増分 + クリーンビルド成功、ホストテスト全 PASS、ログ書式 diff 空の証跡

- [ ] **Step 1: ホストテスト全 PASS**

```powershell
cmd /c "<vcvars経由のbat> で ctest --test-dir build-host --output-on-failure"
# Expected: util / cap / usb すべて PASS。
```

- [ ] **Step 2: 増分 + クリーンビルド**

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake --build build --target pico-wakecon
& $cmake -S . -B build-verify -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build-verify --target pico-wakecon
# Expected: 両方成功。
```

- [ ] **Step 3: ログ書式 diff (不変条件の証跡)**

```powershell
git diff main...HEAD -- src/main.c src/ui.c src/hid.c | Select-String "snprintf|probe_line"
# 既存の書式文字列に変更がないこと。新規は "usb ..." と "W ..." のみ。
```

- [ ] **Step 4: 実機確認**

```text
1. W 0: 従来 BT 接続 + S/N/O + C/B が従来通り (無線回帰)。
2. W 1 + Switch ドック USB: 有線で認識・操作反映・抜挿再接続。80 04 後に BT 復帰しない。
3. Switch2 でも認識されること。
4. 200ms 放置で WD 中立化すること。
```

- [ ] **Step 5: Commit (証跡メモがあれば)**

```bash
git add docs/superpowers/specs/2026-09-05-wired-procon-design.md
git commit -m "docs: record wired handshake table from Spike" || echo "nothing to commit"
```

---

## Self-Review (checked at plan time)

1. **Spec coverage:** 記述子・`80 01〜06`・`80 04` 必須・`91/92` 対象外・排他切替・UART 一本化・watchdog・秘密非開示 → Task 1〜6 に割当済み。
2. **Placeholder scan:** 推測バイト列の固定化はすべて「Task 1 実測で確定」に紐付け済み。`bcdDevice` 等の可変値は実測上書き指定。
3. **Type consistency:** `usb_build_81_reply(req, req_len, out, out_max, mac, dev_type)` の署名は Task 2 定義→Task 4 使用で一致。`usb_wired_*` 6 関数は Task 4 定義→Task 5 使用で一致。
