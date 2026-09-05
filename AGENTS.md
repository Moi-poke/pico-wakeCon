# AGENTS.md — pico-wakecon

Raspberry Pi Pico 2 W 用 C11 プロジェクト。Switch 2 ウェイク（BLE ビーコン取込→再生）と Switch 1 Pro Controller エミュレーション（Classic Bluetooth/BTstack）を1台で動かす。

## Build / test

Pico SDK・cmake・ninja は PATH にない。フルパスで呼ぶ：

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build --target pico-wakecon
```

* board は `pico2_w` 固定（CMakeLists 内で設定）。変えない。
* 成果物は `build/`（増分用）と `build-verify/`（クリーン確認用）。どちらも git 管理外。

ホスト単体テスト（`tests/host`、CTest、`test_util`/`test_cap`）は MSVC が要るが、素の PowerShell にはコンパイラがない。`vcvars64.bat` 経由の cmd で実行する：

```powershell
cmd /c <script>.bat   # bat 内で vcvars64.bat を call してから cmake/ctest を呼ぶ
```

`ctest.exe` もフルパス（cmake と同ディレクトリ）。詳細は `tests/host/CMakeLists.txt`。

## Gotchas

* VSCode 経由の編集はバッファが dirty のまま残る。**ビルド前に必ず saveAll** すること。保存忘れで古いソースがビルドされる。
* PowerShell 5.1：`&&` 不可（`cmd1; if ($?) { cmd2 }` を使う）。`cd` せず workdir 指定。
* ソースの日本語コメントは残す。MSVC の C4819 警告対策は `tests/host/CMakeLists.txt` の `/utf-8` で済ませてある。
* `src/cap.c` 等の LF/CRLF 警告は既知。改行コードの一括変換はしない。

## Architecture

依存方向：`main → link/ui/hid → cap/spi/store → util`。BTstack 依存は `main/link/hid` に閉じ込める。`cap`/`util` は Pico・BTstack 非依存に保つ（ホストテスト可能条件）。

* `main.c`：`packet_handler` は振分けのみ。実処理は `log_hci_packet/handle_*` 静的関数。
* `link.c` は薄層（`link_poll`→`link_cap_tick`/`link_beacon_tick` 振分け）。実体は `link_conn.c`（再接続）/`link_cap.c`（取込）/`link_beacon.c`（再生・LE追跡）。
* `ui.c`：1文字コマンドは `UI_CMDS` 表引き。新規コマンドは表に1行追加＋`cmd_*` 関数。
* `hid.c`：サブコマンド応答は `reply_*` ヘルパー。バイト値は実機仕様の写しなので変えない。
* `probe_*` グローバルは所有モジュールが分散している。変更時は読み書き箇所を grep して影響を確認する。

## Behavior constraints（ refactoring 時の不変条件）

* UART ログ書式・`snprintf` バッファサイズを変えない。検証は main との文字列リテラル集合 diff。
* ADV 31B 配置・`type 0x00/0x81`・peer 送り順・SPI 応答値・SDP/HID 記述子・`main()` 初期化順を変えない。
* 秘密（LTK/IRK/AES 鍵）をログに出さない。

## Git

* 作業は `refactor/*` 等の別ブランチで。main への直接作業・push・PR は指示があるときのみ。
* `build*/`、`*.elf/.uf2/.hex/.bin`、`.cache/` は git 管理外（`.gitignore` 済み）。
