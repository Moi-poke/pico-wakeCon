# DEVELOPMENT — pico-wakecon 開発者向け

利用者向けの操作手順は [`README.md`](../README.md) を参照。
ここでは構成・ビルド・テスト・不変条件のみを扱う。

## アーキテクチャ

依存方向：`main → link/ui/hid → cap/spi/store → util`。
BTstack・Pico SDK 依存は `main`・`link_*`・`hid` に閉じ込める。
TinyUSB 依存は `usb_wired`・`usb_descriptors` に閉じ込める。
`cap`・`util`・`usb_hid` は純粋ロジックのみに保つ（ホストテスト可能条件。
この3つに `btstack.h` や `pico/*.h` を include しないこと）。

| ファイル | 責務 |
| ---- | ---- |
| `main.c` | 初期化・`packet_handler`（振分けのみ。実処理は `log_hci_packet`/`handle_*` 静的関数）・タイマ |
| `link.c` | 薄層（`link_poll` → `link_cap_tick`/`link_beacon_tick` 振分けのみ） |
| `link_conn.c` | 自アドレス生成・再接続・リンク鍵数・有線時の電波制御 |
| `link_cap.c` | wake ビーコン取込・表・保存判定 |
| `link_beacon.c` | wake 再生・MAC 偽装・LE 接続追跡 |
| `ui.c` | UART 入出力・1文字コマンド（`UI_CMDS` 表引き）・状態表示 |
| `hid.c` | 入力状態・サブコマンド応答（`reply_*` ヘルパー）・レポート送信 |
| `cap.c` | wake ビーコン解釈・表・保存形式（BTstack 非依存） |
| `spi.c` | Pro Controller SPI フラッシュ値（実機仕様の写し） |
| `store.c` | Flash 保存（相手番地・色・取込・W モード。Classic 鍵は SDK が自動保存） |
| `util.c` | 純粋ヘルパー（16進変換・トークン分割・色書式・スティック配置） |
| `usb_hid.c` | 有線応答組立（`80 xx`→`81`・`0x01`→`0x21`・`0x30`。BTstack 非依存） |
| `usb_wired.c` | 有線状態機・TinyUSB 送受信・診断計数 |
| `usb_descriptors.c` | USB 記述子（VID/PID/文字列/HID、純正値の写し） |
| `usb_cdc.c` | USB CDC コンソールの TinyUSB 接着部（`tusb.h` を外に出さない） |
| `ui_line.c` | 発信元タグ付き行組立（`ui_src_t`・`ui_line_acc_t`・`W` ゲート述語） |
| `tusb_config.h` | TinyUSB 設定（HID のみ） |

`probe_*` グローバルは所有モジュールが分散している（`hid`＝入力・送信状態、
`link_*`＝接続・取込状態、`ui`＝表示・カウンタ、`usb_wired`＝USB 状態・
診断計数）。変更時は読み書き箇所を grep して影響を確認すること。
安易な改名・集約はしない。

## ビルド

Pico SDK・cmake・ninja は PATH にない。フルパスで呼ぶ：

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build --target pico-wakecon
```

* board は `pico2_w` 固定（`CMakeLists.txt` 内で設定）。変えない。
* `build/` は増分用、`build-verify/` はクリーン確認用（削除して作り直す）。
  どちらも git 管理外。
* `main()` の初期化順（GAP → L2CAP → SDP → HID → HCI → timer → power）を変えない。
  USB 初期化（`usb_wired_init`）は BT より先に行う（列挙前の消費電流制限に
  かからないようにするため）。BT 内部の順序自体は変えない。

## ホストテスト

`tests/host`（CTest、`test_util`・`test_cap`・`test_usb`）。対象は BTstack
非依存の純粋関数のみ。素の PowerShell にはコンパイラがないため、`vcvars64.bat`
経由の cmd で実行する。`ctest.exe` もフルパス（cmake と同ディレクトリ）。
ソースの日本語コメントに由来する MSVC の C4819 警告は
`tests/host/CMakeLists.txt` の `/utf-8` で抑止済み。コメントを削らないこと。

## 不変条件

* UART ログ書式・`snprintf` バッファサイズを変えない。
  検証は main との文字列リテラル集合 diff。
* ADV 31B 配置・`type 0x00/0x81`・peer 送り順・SPI 応答値・SDP/HID 記述子を変えない。
  `hid.c`・`spi.c` のバイト値は実機仕様の写し。
* USB 記述子（VID/PID/文字列/HID）は純正値の写しで変えない。`81`・`0x21` 応答は
  64B 固定。`usb ...` 行は付加のみで、`st` 行の書式は変えない。
* CDC の Endpoint は通知 `0x83`・OUT `0x02`・IN `0x82`（HID の `0x81`/`0x01` と衝突させない）。新規リテラルは `W from UART only` のみ。
* 秘密（LTK/IRK/AES 鍵）をログに出さない。

## Flash 保存内容

| 内容 | 取得・更新 | 参照 |
| ---- | ---- | ---- |
| 相手番地（Switch 1） | HID 接続確立時に保存（`store_host`）、起動時に読込 | `K` で Pico 側を削除（Switch 側の登録解除も必要） |
| 本体色 | `O` コマンドで保存、起動時に読込 | `?` の `color` 行で確認 |
| 取込ビーコン | `C` 完了時に保存 | `L` の `saved` 行・`B` で再生、`X` で破棄 |
| 有線モード | `W 0`・`W 1` で保存 | 起動時に復元（有線起動は電波を上げず USB 先行で列挙） |

## デバッグ

* `?`：状態表示（`st`＝接続・保存状態、`color`＝本体色、`saved`＝保存ビーコン、
  `usb`＝有線状態・診断計数）。各行の読み方は `README.md` の
  「`?` 状態表示の読み方」を参照。
* `D`：HCI 生ログの on/off（既定 off）。`M`：1秒ごとの監視表示
  （`mon ...` 2行）の on/off。問題再現時に on にしてログを取る。
* `P`：PONG（疎通確認）。

## エージェント向け補足

`AGENTS.md`（リポジトリ直下）に作業上の gotcha をまとめている。
そちらも参照すること。
