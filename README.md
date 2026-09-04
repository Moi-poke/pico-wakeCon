# pico-wakecon

Pico 2 W 1台で完結する Nintendo Switch 2 ウェイク＋ Switch 1 プロコンエミュレーション。

1. `C` でペア済み Joy-Con / Pro Controller 2 の wake ビーコンを取り込む
2. `B` で送信元 MAC を偽装して再生し、スリープ中の Switch 2 を起こす
3. 起きた Switch 1 には Classic Bluetooth で Pro Controller として繋がる

## 必要なもの

- Raspberry Pi Pico 2 W
- Nintendo Switch 2（スリープ復帰用）
- Switch 2 にペア済みのコントローラ（Joy-Con 2 など。取込元）
- Nintendo Switch 1（プロコン操作用）
- USB-シリアル変換（GP0/GP1、115200bps）または USB CDC

## 使い方

### 1. wake パケットの取込

```
C        ← 15 秒スキャン（C 30 で秒数指定、最大 60）
```

実行中に Switch 2 をスリープさせ、右 Joy-Con の HOME を押す。
`flag=81` の実包を捕捉すると保存する。

```
CAP mac=98e255b1285b type=0 pid=2066 flag=81 rssi=-45 idx=0
CAP-DONE saved=1 spoof=98e255b1285b
```

- 保存は Flash に残る。再起動後も `B` が使える
- `L` で取込一覧と保存内容を確認できる

### 2. wake 再生

Joy-Con の電源を OFF にして（同一 MAC 衝突防止）：

```
B
```

1.5 秒だけ wake ビーコンを出す。終了後は自アドレスに自動復帰する。

### 3. Switch 1 プロコンとして使う

Switch 1 の「持ちかた／順番を変える」を開くと繋がる。
初回はペアリング、2 回目以降は Pico 起動で自動再接続する。

```
S 8 8 80 80 80 80   ← 姿勢入力 (btn hat lx ly rx ry、16進)
N                   ← ニュートラル（全解放）
```

## コマンド一覧

| 入力 | 機能 |
| ---- | ---- |
| `S <btn> <hat> <lx> <ly> <rx> <ry>` | 姿勢入力（16進6項目） |
| `N` | ニュートラル |
| `O <本体> <ボタン> <左> <右>` | 本体色設定（各6桁16進、Flash 保存） |
| `C` / `C <1-60>` | wake ビーコン取込スキャン |
| `L` | 取込一覧＋保存内容 |
| `X` | 取込一覧と保存を破棄 |
| `B` | 保存内容で wake 再生（1.5 秒） |
| `?` | 状態表示 |
| `D` | HCI 生ログ on/off（既定 off） |
| `M` | 監視表示 on/off（既定は landmark のみ） |
| `K` | Classic リンク鍵全削除 |
| `P` | PONG（疎通確認） |

## 注意

- 取込元は Switch 2 にペア済みのコントローラであること。未ペア機器の MAC では起きない
- 再生中は本物の Joy-Con の電源を OFF にすること
- フルスリープ（電源断）からの起動はできない。通常スリープ専用
- 再生中（1.5 秒）は Classic 再接続を抑止する。偽装 MAC で名乗らないため

## ビルド

Pico SDK 2.3.0＋Ninja 前提（Windows 例）。

```powershell
$cmake = Join-Path $env:USERPROFILE '.pico-sdk/cmake/v4.3.4/bin/cmake.exe'
$ninja = Join-Path $env:USERPROFILE '.pico-sdk/ninja/v1.13.2/ninja.exe'
& $cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja
& $cmake --build build --target pico-wakecon
```

`build/pico-wakecon.uf2` を BOOTSEL で書き込む。

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
