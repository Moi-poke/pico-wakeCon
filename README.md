# pico-wakecon

Raspberry Pi Pico 2 W 1台で動作する、Nintendo Switch 2 のウェイク装置兼
Pro Controller エミュレータ（無線・有線）です。

動作の流れは次のとおりです。

1. `C` コマンドで、ペアリング済みコントローラの wake ビーコンを取り込む
2. `B` コマンドで送信元 MAC アドレスを偽装して再生し、スリープ中の
   Switch 2 を起動する
3. Switch 1 には Classic Bluetooth で Pro Controller として接続する
4. `W 1` で有線モードに切り替え、USB ケーブルでドックに直結して
   Pro Controller として使う（Switch 2 のドックで動作確認済み）。
   `W 0` で無線に戻る

## 必要なもの

- Raspberry Pi Pico 2 W
- Nintendo Switch 2（スリープ復帰の対象）
- Switch 2 にペアリング済みのコントローラ（Joy-Con 2 など。取込元）
- Nintendo Switch 1（プロコン操作の対象）
- PC との接続：USB-シリアル変換アダプタ（GP0/GP1、115200bps。必須）
- 有線接続用：Switch ドック、データ通信対応の USB ケーブル

## 初回セットアップ

1. Pico の GP0（TX）/GP1（RX）/GND を USB-シリアル変換アダプタに接続します。
   シリアル端末を 115200bps で開きます（コマンド入出力は UART のみです）
2. BOOTSEL ボタンを押しながら Pico を PC に接続し、出てきたドライブに
   `build/pico-wakecon.uf2` を書き込みます（ビルド手順は後述）
3. 起動すると次の行が出ます。出なければ配線・端末設定を見直してください

```
=== wakecon ===
BT READY 7c:bb:8a:xx:xx:xx
addr ok (7C:BB:8A)
ready. C capture / B wake / S input / ? status
```

4. `?` を入力し、`st ...` 行が返ることを確認します（書式は後述）

## 使い方

### 1. wake パケットの取込

シリアル端末から次のコマンドを実行します（`C` のみで 15 秒スキャン。
`C 30` のように秒数を指定できます。範囲は 1〜60 秒です）。

```
C
```

実行中に Switch 2 をスリープさせ、右 Joy-Con の HOME ボタンを押して
ください。`flag=81` のパケットを捕捉すると、自動的に保存します。

```
CAP mac=98e255b1285b type=0 pid=2066 flag=81 rssi=-45 idx=0
CAP-DONE saved=1 spoof=98e255b1285b
```

補足事項：

- 取り込んだ内容は Flash に保存されます。再起動後も `B` が使えます
- `L` コマンドで取込一覧と保存内容を確認できます
- **1 回で捕捉できない場合は、スリープ→スリープ解除を数回繰り返して
  ください。** 繰り返すうちに `flag=81` のパケットが出やすくなり、
  確実に取り込めます

### 2. wake 再生

取込元のコントローラの電源を OFF にしてから（同一 MAC アドレスの衝突を
防ぐため）、次のコマンドを実行します。

```
B
```

約 1.5 秒間 wake ビーコンを送信します。終了後は自動的に Pico 自身の
アドレスに復帰します。

### 3. Switch 1 の Pro Controller として使う

Switch 1 の「持ちかた／順番を変える」画面を開くと接続します。初回は
ペアリング、2 回目以降は Pico の起動時に自動で再接続します。

姿勢の入力形式は次のとおりです（いずれも 16 進数）。

```
S 8 8 80 80 80 80   ← 姿勢入力（btn hat lx ly rx ry の順）
N                   ← ニュートラル（全解放）
```

各項目の値（ボタンとビットの対応・十字キーの向き・スティックの範囲）
は [`BUTTONS.md`](BUTTONS.md) を参照してください。

本体色を変更する場合は `O` コマンドを使います（各色 6 桁 16 進数）。

```
O 313131 0f0f0f 0ab9e6 ff3c28   ← 本体・ボタン・左・右の順
```

成功すると `color ...` 行が返り、Flash に保存されます。Switch 側の表示に
反映されない場合は、Switch のコントローラー登録を解除して繋ぎ直して
ください。有線モード中は成功時に自動で再列挙し、Switch に読み直させます。
それでも反映されなければ USB ケーブルを挿し直してください。

### 4. 有線接続で使う

Switch の「設定」で「Proコントローラーの有線通信」を ON にしてから、
次の手順で接続します（Switch 2 のドックで動作確認済みです）。

```
W 1
```

Pico の USB ポートをドックの USB ポートに接続します。`?` の `usb ...` 行が
`cfg=1 hs=1` になれば接続完了です（読み方は後述）。あとは `S`・`N` で
無線と同じように操作できます。

補足事項：

- `W`（引数なし）で現在のモードを表示します。`W 0` で無線に、`W 1` で
  有線に戻ります。モードは Flash に保存され、起動時に復元されます
- 有線モード中は Classic Bluetooth の電波を止めます。`C`・`B` を使うときは
  自動で電波を起こし、終われば止め直します
- `W 0` に戻すと数秒で無線が再接続します。繋がらない場合は `K` と Switch 側の
  登録解除から繋ぎ直してください

### 5. PC の USB シリアルで操作する（無線時のみ）

無線モード（`W 0`）では、Pico の USB ポートを PC に接続すると COM ポートが
出ます（USB CDC）。UART アダプタなしで同じコマンドを使えます。`?` の `usb` 行
が `cdc=1` になれば接続完了です。出力は UART と USB の両方に送られます。

補足事項：

- `W` コマンドは UART からのみ受け付けます。USB CDC から送ると
  `W from UART only` が返り、モードは変わりません
- 無線モードのまま Switch ドックに接続しても、Pro Controller としては
  認識されません（想定内の動作です。有線で使うときは `W 1` にしてから
  接続してください）
- 有線モード（`W 1`）では COM ポートは出ません（純粋 HID のみ）

## コマンド一覧

| 入力 | 機能 |
| ---- | ---- |
| `S <btn> <hat> <lx> <ly> <rx> <ry>` | 姿勢入力（16 進数 6 項目） |
| `N` | ニュートラル（全解放） |
| `O <本体> <ボタン> <左> <右>` | 本体色設定（各 6 桁 16 進数、Flash 保存） |
| `C` / `C <1-60>` | wake ビーコン取込スキャン（秒数指定可） |
| `L` | 取込一覧と保存内容の表示 |
| `X` | 取込一覧と保存内容の破棄 |
| `B` | 保存内容で wake 再生（約 1.5 秒） |
| `?` | 状態表示（接続状態・保存内容・本体色） |
| `D` | HCI 生ログの on/off（既定 off） |
| `M` | 監視表示の on/off（既定 off） |
| `K` | Classic リンク鍵の全削除 |
| `P` | PONG（疎通確認） |
| `W` / `W 0` / `W 1` | 有線・無線の表示・切替（Flash 保存、起動時復元） |

### エラー行の意味

操作が受け付けられないときは、理由付きの行が返ります。

| 表示 | 意味・対処 |
| ---- | ---- |
| `C ERR BUSY` | 取込・再生の実行中です。終わってから再実行してください |
| `C ERR CONNECTED` | Switch 1 と接続中は取込できません。Switch 1 側で切断してから実行してください |
| `B ERR NO_SAVE. run C first` | 保存済みビーコンがありません。先に `C` で取り込んでください |
| `B ERR BUSY` / `B ERR CONNECTED` | `C ERR ...` と同様です |
| `X ERR BUSY` | 取込・再生の実行中は破棄できません。終わってから実行してください |
| `usage: O <body> <btn> <left> <right> (hex)` | `O` の書式が正しくありません。各色 6 桁 16 進数で指定してください |

### `?` 状態表示の読み方

`?` を入力すると、次の 4 種類の行が返ります。

```
st host=1 cid=1537 full=1 keys=1 saved=1 scan=0 bcn=0
color body=313131 btn=0f0f0f left=0ab9e6 right=ff3c28
saved spoof=aabbccddeeff sw=112233445566
usb en=1 cfg=1 hs=1 mnt=1 umnt=0 rx80=4 last=04 tx81=4 tx21=8 in30=391 sof=0 sus=0 rsm=0 ep=31 ct=0 h=05010204 h1=10101010 u=00/2 n=3 f1=0302081001000000 sp=6020/24*11 sh=0 sd=01
```

* `st` 行：`host`＝相手番地の記憶有無、`cid`＝HID 接続 ID（0 は未接続）、
  `full`＝入力レポート送出中、`keys`＝Classic リンク鍵の数、
  `saved`＝ビーコン保存有無、`scan`/`bcn`＝取込・再生の実行中
* `color` 行：現在の本体色（`O` の応答と同じ書式）
* `saved` 行：再生に使う偽装元 MAC（`spoof`）と Switch 側 MAC（`sw`）。
  未保存のときは出ません
* `usb` 行：有線接続の状態と診断計数。`en`＝有線モード（`W 1` で 1）、
  `cfg`＝USB 列挙済み、`hs`＝ハンドシェイク完了。接続完了の目安は
  `cfg=1 hs=1` です。以降は診断用で、普段は見なくて構いません：
  `mnt`/`umnt`＝マウント回数、`rx80`/`last`＝`80 xx` 受信数・最終種別、
  `tx81`/`tx21`＝応答送出数、`in30`＝入力送出数、`ep`/`ct`＝受信経路別総数、
  `h`＝直近の `80` 系 4 件、`h1`＝直近の `01` 系 4 件、
  `u`＝未知 ID（最終値・長さ・回数）、`f1`＝`01` 系の到達順 8 件、
  `sp`＝直近 SPI 読出（番地・長さ・回数）、`sh`＝短い受信の回数、
  `sd`＝直近 `01` のデータバイト。`sof`/`sus`/`rsm` は環境により
  進まないことがある参考値です

## 注意事項

- 取込元は Switch 2 にペアリング済みのコントローラにしてください。
  未ペア機器の MAC アドレスでは起動できません
- 再生中は本物の Joy-Con の電源を OFF にしてください
- 完全電源断からの起動はできません。通常スリープ専用です
- 再生直後は Switch 1 への再接続に少し時間がかかります。終われば自動で
  繋ぎ直すので、そのままお待ちください
- リンク鍵の不整合時は Pico 側で `K` を実行し、Switch 側の登録も解除して
  ください。両側の情報を消去してから繋ぎ直します

## トラブルシューティング

基本の切分け手順は `?` で状態確認 → `D`・`M` で詳細表示 → `K` で修復です。

* **繋がらない・すぐ切れる**: `D` で HCI ログを on にして再現させます
  * `console stall (no SSP). power OFF Switch, or K + re-pair` が出たら、
    Switch 側が認証前に切断しています。Switch の電源を一度 OFF にし、
    解消しなければ Pico 側で `K`、Switch 側でコントローラー登録解除の
    両方を行ってから繋ぎ直してください
  * `hid open FAIL status=0x66`（`0x66=refused security`）も鍵不整合です。
    同じく両側の情報を消去して繋ぎ直してください
  * `conn status=0x04`（Page Timeout）は相手が無応答です。Switch 1 の
    「持ちかた／順番を変える」画面を開いているか確認してください
* **自アドレスがおかしい**: 起動時に `addr NG (SDK default remains)` が出たら、
  自アドレスの上書きに失敗しています。`BT READY` 行の MAC を確認してください
* **入力が止まらなくなる**: `WD` 行が出たら、PC からの入力途絶を検出して
  中立化した合図です。正常動作なので、そのまま次の `S` を送ってください
* **切分け用表示**: `D` は HCI 生ログの on/off、`M` は 1 秒ごとの
  監視表示（`mon ...` 行）の on/off です。どちらも既定 off で、問題が
  再現したら on にしてログを取ってください
* **有線で認識されない**（`usb` 行の `cfg=0` のまま）: ケーブルがデータ通信
  対応か、ドックの USB ポート・電源、Switch 側の「有線通信」設定、`W 1`
  での `en=1` を順に確認してください
* **有線のハンドシェイクが進まない**（`cfg=1` のまま `hs=0`）: USB ケーブルを
  挿し直してください。直らなければ `usb ...` 行全体を控えてください
* **一覧に表示されない**: 上記を確認し、解消しなければ Switch を再起動して
  から挿し直してください（中途半端な登録情報が残ることがあります）
* **有線で 2162-0002 が出た**: 既知の問題として報告されています。まず Switch
  を再起動し、Pico を挿し直してください。繰り返す場合は発生時の `usb ...` 行
  を控えてください
* **`W` 切替後に無線が繋がらない**: `W 0` のあと数秒待ってください（自動で
  再接続します）。駄目な場合は `K` と Switch 側の登録解除から繋ぎ直して
  ください
* **有線で色が反映されない**: `O` 成功で自動再列挙します。反映されなければ
  USB ケーブルを挿し直してください

## ビルド

Pico SDK 2.3.0 と Ninja が必要です（Windows の例）。

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build --target pico-wakecon
```

`build/pico-wakecon.uf2` を BOOTSEL モードで書き込んでください。

## 構成（`src/` 配下）

| ファイル | 内容 |
| ---- | ---- |
| `main.c` | 初期化・イベント振分け・タイマ |
| `cap.c` | wake ビーコン解釈・表・保存形式 |
| `hid.c` | 入力状態・サブコマンド応答・送信 |
| `spi.c` | Pro Controller SPI フラッシュ値 |
| `store.c` | Flash 保存（相手番地・色・取込・W モード） |
| `link.c` | 取込・再生期限の振分け（薄層） |
| `link_conn.c` | 自アドレス・再接続・リンク鍵数 |
| `link_cap.c` | wake ビーコン取込・表・保存判定 |
| `link_beacon.c` | wake 再生・MAC 偽装・LE 追跡 |
| `ui.c` | UART 入出力・コマンド・状態表示 |
| `util.c` | 純粋ヘルパー（16進・色表示・スティック配置等） |
| `usb_hid.c` | 有線応答組立（Pico・BTstack 非依存、ホストテスト可） |
| `usb_wired.c` | 有線状態機・入出力・診断計数（TinyUSB） |
| `usb_descriptors.c` | USB 記述子（VID/PID/文字列/HID、純正値の写し） |
| `usb_cdc.c` | USB CDC コンソールの TinyUSB 接着部（`ui.c` は `usb_cdc.h` 経由でのみ使う） |
| `ui_line.c` | 発信元タグ付き行組立（UART・CDC 共用、Pico・BTstack 非依存） |
| `tusb_config.h` | TinyUSB 設定（HID のみ） |
| `switch_hid.h` | HID 記述子・VID/PID/COD |
| `btstack_config.h` | BTstack 設定 |

ホスト単体テストは `tests/host`（`test_util`・`test_cap`・`test_usb`、CTest）。

## 謝辞・参考

- [ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) — Switch 2 BLE プロトコル仕様
- [Bill-git1/Switch2-RPI-Wake](https://github.com/Bill-git1/Switch2-RPI-Wake) — 取込→再生方式
- [zhantss/ESP32-BLE5-NSController-Emulator](https://github.com/zhantss/ESP32-BLE5-NSController-Emulator) — 5ms 間隔の知見
- [mizuyoukanao/btstack](https://github.com/mizuyoukanao/btstack) — BTstack 版 Pro Controller 2 実装
- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) — Switch 1 プロコン仕様
- [DavidPagels/retro-pico-switch](https://github.com/DavidPagels/retro-pico-switch) (MIT) — HID 記述子等の参照元
- [knflrpn/2wiCC](https://github.com/knflrpn/2wiCC) (MIT) — 有線 Pro Controller 実装（USB 記述子・ハンドシェイク応答の参考）

## ライセンス

- 本リポジトリのコード: MIT License（`LICENSE` ファイル参照）
- `src/switch_hid.h` の HID 記述子: Copyright (c) 2023 David Pagels（MIT。同ファイル内に許諾文を同梱）
- 注意: ビルド時に Pico SDK 経由でリンクされる BTstack は、BlueKitchen の
  独自許諾（非商用に限り無償）である。バイナリ（`.uf2`）を配布・商用利用
  する場合は、別途 BlueKitchen への確認が必要。ソースの MIT とは範囲が
  異なるため混同しないこと。
