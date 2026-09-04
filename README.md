# pico-wakecon

Raspberry Pi Pico 2 W 1台で動作する、Nintendo Switch 2 のウェイク装置兼
Nintendo Switch 1 用 Pro Controller エミュレータです。

動作の流れは次のとおりです。

1. `C` コマンドで、ペアリング済みコントローラの wake ビーコンを取り込む
2. `B` コマンドで送信元 MAC アドレスを偽装して再生し、スリープ中の
   Switch 2 を起動する
3. Switch 1 には Classic Bluetooth で Pro Controller として接続する

## 必要なもの

- Raspberry Pi Pico 2 W
- Nintendo Switch 2（スリープ復帰の対象）
- Switch 2 にペアリング済みのコントローラ（Joy-Con 2 など。取込元）
- Nintendo Switch 1（プロコン操作の対象）
- PC との接続：USB-シリアル変換アダプタ（GP0/GP1、115200bps）または USB CDC

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

無線モードでは、Switch 1 の「持ちかた／順番を変える」画面を開くと
繋がります。初回はペアリング、2 回目以降は Pico の起動時に自動で
再接続します。

有線モードでは、Pico の USB 端子を Switch のドック（または本体）に
直接繋ぐと認識します。この場合、PC との接続は UART（GP0/GP1）に
してください。USB 端子が Switch で塞がるためです。

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
ください。

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
| `?` | 状態表示（接続状態・保存内容・本体色・モード） |
| `W` / `W <0/1>` | モード照会・切替（0＝無線、1＝有線。切替後は再起動） |
| `D` | HCI 生ログの on/off（既定 off） |
| `M` | 監視表示の on/off（既定 off） |
| `K` | Classic リンク鍵の全削除 |
| `P` | PONG（疎通確認） |

## 有線／無線モードの切替え

Switch 1 への接続は、無線（Classic Bluetooth）と有線（USB 直結）を
切り替えられます。wake（BLE）の部分はどちらのモードでも使えます。

- 現在のモードは Pico 本体の LED で分かります。**無線は点灯、有線は消灯**です
- 切替えはシリアルから行います（`W 0`＝無線、`W 1`＝有線、`W`＝照会）。
  切替え後は自動で再起動します
- モードは Flash に保存されます
- **UART 変換器を持たない人は `W 1` を送らないでください。** USB が
  Switch 用（HID）になり、操作口が無くなります。間違えた場合は、
  電源の入れ直しを 4 回繰り返してください（各回 60 秒以内に切る）。
  短い起動が続くと自動で無線に戻ります
- BOOTSEL ボタンでの切替えはありません。ボタンを読むにはフラッシュの
  信号線を触る必要があり、RP2350 では動作中に触ると止まるためです
- 有線モードでは PC との接続に UART を使ってください（USB 端子の
  空きが無いため）

## 注意事項

- 取込元は Switch 2 にペアリング済みのコントローラにしてください。
  未ペア機器の MAC アドレスでは起動できません
- 再生中は本物の Joy-Con の電源を OFF にしてください
- 完全電源断からの起動はできません。通常スリープ専用です
- 再生直後は Switch 1 への再接続に少し時間がかかります。終われば自動で
  繋ぎ直すので、そのままお待ちください
- リンク鍵の不整合時は Pico 側で `K` を実行し、Switch 側の登録も解除して
  ください。両側の情報を消去してから繋ぎ直します

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
| `store.c` | Flash 保存（相手番地・色・取込） |
| `link.c` | 自アドレス・再接続・取込・再生 |
| `ui.c` | UART/USB 入出力・コマンド・状態表示 |
| `switch_hid.h` | HID 記述子・VID/PID/COD |
| `btstack_config.h` | BTstack 設定 |

## 謝辞・参考

- [ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) — Switch 2 BLE プロトコル仕様
- [Bill-git1/Switch2-RPI-Wake](https://github.com/Bill-git1/Switch2-RPI-Wake) — 取込→再生方式
- [zhantss/ESP32-BLE5-NSController-Emulator](https://github.com/zhantss/ESP32-BLE5-NSController-Emulator) — 5ms 間隔の知見
- [mizuyoukanao/btstack](https://github.com/mizuyoukanao/btstack) — BTstack 版 Pro Controller 2 実装
- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) — Switch 1 プロコン仕様
- [DavidPagels/retro-pico-switch](https://github.com/DavidPagels/retro-pico-switch) (MIT) — HID 記述子等の参照元

## ライセンス

- 本リポジトリのコード: MIT License（`LICENSE` ファイル参照）
- `src/switch_hid.h` の HID 記述子: Copyright (c) 2023 David Pagels（MIT。同ファイル内に許諾文を同梱）
- 注意: ビルド時に Pico SDK 経由でリンクされる BTstack は、BlueKitchen の
  独自許諾（非商用に限り無償）である。バイナリ（`.uf2`）を配布・商用利用
  する場合は、別途 BlueKitchen への確認が必要。ソースの MIT とは範囲が
  異なるため混同しないこと。
