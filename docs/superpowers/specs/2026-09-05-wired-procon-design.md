# 有線Pro Controller (USB) 設計メモ

承認済み案A。`dekuNukem/Nintendo_Switch_Reverse_Engineering` の `USB-HID-Notes.md` を根拠にする。

## 目標

- Pico 2 W の USB ポートを Switch ドックの USB に直結し、Switch1 Pro Controller として有線接続する (Switch2 でも同プロトコルで認識される想定)。
- PC ↔ Pico は UART0 (GP0/GP1, 115200bps) のみに一本化。USB CDC (stdio_usb) は廃止。
- 既存機能 (C 取込 / B 再生 / Classic 無線) を壊さない。最終形は wake + 有線/無線の排他切替。

## USB プロトコル要点 (dekuNukem より)

- Device: VID `0x057E` / PID `0x2009`, USB 2.00, EP0 64B, 文字列 `Nintendo Co., Ltd` / `Pro Controller` / `000000000001` (ToadKing 記述子ダンプと一致)。
- Pro Controller は USB HID インタフェースを1つのみ出す (Joy-Con 充電グリップは2つ。本件は1つ)。
- HID 要求は `01 ..` / `10 ..` / `80 ..` で始まる。`80` 系が任天堂独自プロトコル:
  - `80 01` → `81 01 00 ...` 接続状態 + 機種別 + MAC を返す。セッション中の状態変化時にも送られる。
  - `80 02` → `81 02`。セッション毎に1回のみ。以降 Broadcom へのハンドシェイクを UART 転送する。
  - `80 03` → `81 03`。`80 02` の直後にのみ可。ボーレートを 3Mbit に上げる。後に再度 `80 02` が来る。
  - `80 04` → 応答後、USB HID のみで会話しタイムアウトしなくなる。**BT に戻さないために必須。**
  - `80 05` → タイムアウトと BT 復帰を許可する。
  - `80 06` → リセット相当の可能性。
  - `80 91` / `80 92` → プリハンドシェイク / 任意 UART コマンドの転送。初期実装では対象外。
- ハンドシェイクの正確な順序・Input レポート ID/周期は Spike で実機特定する (資料が古く FW 差分があり得るため)。

## 構成方針

- 依存方向 `main → link/ui/hid → cap/spi/store → util` を維持。新規 `usb_wired` + `usb_descriptors` に USB 依存を閉じ込める。
- `cap` / `util` は Pico・BTstack 非依存のままにする (ホストテスト可能条件)。`80 → 81` の応答組立は純粋関数に切り出し、ホストテスト対象にする。
- `hid.c` のボタン/ハット/スティック変換 (`pc_buttons_to_bt`, `hat_to_bt`, `probe_pack_stick`) と `spi.c` の色情報を再利用する。バイト値は実機写しなので変えない。
- TinyUSB device を使用。`pico_enable_stdio_usb` は OFF (stdio_usb は USB device を専有し `tinyusb_device` と共存できないため)。参考実装は `raspberrypi/pico-examples` の `usb/device/dev_hid_composite` (source + `tinyusb_device` + `tinyusb_board` リンク、`CFG_TUSB_OS=OPT_OS_PICO`)。
- `W` コマンドで有線/無線を排他切替。既存 `st` 行の書式は変えず、新規 `usb ...` 行で有線状態を出す。`main()` の初期化順は変えず末尾に分岐追加のみ。
- `80 04` 完了前は入力レポートを送らない (純正は BT にフォールバックするため)。未知の `80 xx` にはでたらめ値を返さない。
- 200ms watchdog (`WD` 中立化) は有線側にも適用する。
- 秘密情報 (LTK/IRK/AES 鍵) をログに出さない。

## 検証

- PC 上で記述子が純正一致 (VID/PID/文字列/レポート記述子)。
- Switch1 ドック有線で接続・操作・抜挿再接続。`80 04` 後にタイムアウト復帰しないこと。
- Switch2 でも認識されること。
- `W 0` で従来 BT + C/B が従来通り動くこと (増分 + クリーン `build-verify`、ホストテスト、ログ書式 diff)。

## Spike T1 対応表 (desk)

机上調査分 (Task 1)。実機観測 (Step 1–2) は未実施のため、UNKNOWN 行はすべて `実測待ち` とする。推測バイトは書かない。

### 出典

- ToadKing `pro.c` (純正 Pro Controller 記述子ダンプ): https://gist.github.com/ToadKing/b883a8ccfa26adcc6ba9905e75aeb4f2 (raw: https://gist.githubusercontent.com/ToadKing/b883a8ccfa26adcc6ba9905e75aeb4f2/raw/64f8144a195f6ce0f567aa3c8e3f672ebfc4d8cd/pro.c )
- dekuNukem `USB-HID-Notes.md`: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/USB-HID-Notes.md
- dekuNukem `bluetooth_hid_notes.md` (BT 側参考。有線事実ではない): https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_notes.md
- DavidPagels/retro-pico-switch `include/SwitchConsts.h` (MIT。記述子・文字列・ボタン定数の参照元): https://github.com/DavidPagels/retro-pico-switch/blob/master/include/SwitchConsts.h
- mizuyoukanao/btstack (README 参照): https://github.com/mizuyoukanao/btstack — BTstack 本体のフォーク。トップレベルに有線/USB 定数はなし。本タスクでは dry (収穫なし) とする。
- 補助参考 StonedModder `ProControllerResearch.md` (8BitDo Ultimate 2 の Switch モード観測。純正実測ではない): https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher/blob/main/ProControllerResearch.md

### 記述子バイト表 (ToadKing 原文の写し)

Device descriptor (18B):

```text
0x12,        // bLength
0x01,        // bDescriptorType (Device)
0x00, 0x02,  // bcdUSB 2.00
0x00,        // bDeviceClass (Use class information in the Interface Descriptors)
0x00,        // bDeviceSubClass
0x00,        // bDeviceProtocol
0x40,        // bMaxPacketSize0 64
0x7E, 0x05,  // idVendor 0x057E
0x09, 0x20,  // idProduct 0x2009
0x00, 0x02,  // bcdDevice 4.00
0x01,        // iManufacturer (String Index)
0x02,        // iProduct (String Index)
0x03,        // iSerialNumber (String Index)
0x01,        // bNumConfigurations 1
```

注意: `bcdDevice` のコメントは `4.00` とあるが、バイト `0x00, 0x02` (LE) = `0x0200` は bcd `2.00` を示す。コメント誤記の疑い。retro-pico-switch は `0x10, 0x02` (= `0x0210`) を使う。不一致のため Task 3 採用値は実機で確定する (実測待ち)。

文字列記述子:

```text
Manufacturer String: Nintendo Co., Ltd
Product String: Pro Controller
SerialNumber String: 000000000001
```

Configuration 一式 (wTotalLength 41):

```text
0x09,        //   bLength
0x02,        //   bDescriptorType (Configuration)
0x29, 0x00,  //   wTotalLength 41
0x01,        //   bNumInterfaces 1
0x01,        //   bConfigurationValue
0x00,        //   iConfiguration (String Index)
0xA0,        //   bmAttributes Remote Wakeup
0xFA,        //   bMaxPower 500mA

0x09,        //   bLength
0x04,        //   bDescriptorType (Interface)
0x00,        //   bInterfaceNumber 0
0x00,        //   bAlternateSetting
0x02,        //   bNumEndpoints 2
0x03,        //   bInterfaceClass
0x00,        //   bInterfaceSubClass
0x00,        //   bInterfaceProtocol
0x00,        //   iInterface (String Index)

0x09,        //   bLength
0x21,        //   bDescriptorType (HID)
0x11, 0x01,  //   bcdHID 1.11
0x00,        //   bCountryCode
0x01,        //   bNumDescriptors
0x22,        //   bDescriptorType[0] (HID)
0xCB, 0x00,  //   wDescriptorLength[0] 203
```

注意: retro-pico-switch の同等箇所はバイト `0xCB, 0x00` (= 203、ToadKing と一致) だがコメントが `86` になっている。バイトを採用しコメントは誤記扱いとする。

Endpoints:

```text
0x07,        //   bLength
0x05,        //   bDescriptorType (Endpoint)
0x81,        //   bEndpointAddress (IN/D2H)
0x03,        //   bmAttributes (Interrupt)
0x40, 0x00,  //   wMaxPacketSize 64
0x08,        //   bInterval 8 (unit depends on device speed)

0x07,        //   bLength
0x05,        //   bDescriptorType (Endpoint)
0x01,        //   bEndpointAddress (OUT/H2D)
0x03,        //   bmAttributes (Interrupt)
0x40, 0x00,  //   wMaxPacketSize 64
0x08,        //   bInterval 8 (unit depends on device speed)
```

注意: 純正 OUT は `0x01`。StonedModder 観測の 8BitDo は OUT `0x02` のため、写しは `0x01` とする。

HID Report Descriptor (wDescriptorLength 203、ToadKing 原文の写し):

```text
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x15, 0x00,        // Logical Minimum (0)
0x09, 0x04,        // Usage (Joystick)
0xA1, 0x01,        // Collection (Application)
0x85, 0x30,        //   Report ID (48)
0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x01,        //   Usage Minimum (0x01)
0x29, 0x0A,        //   Usage Maximum (0x0A)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x0A,        //   Report Count (10)
0x55, 0x00,        //   Unit Exponent (0)
0x65, 0x00,        //   Unit (None)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x0B,        //   Usage Minimum (0x0B)
0x29, 0x0E,        //   Usage Maximum (0x0E)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x04,        //   Report Count (4)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //   Report Size (1)
0x95, 0x02,        //   Report Count (2)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x0B, 0x01, 0x00, 0x01, 0x00,  //   Usage (0x010001)
0xA1, 0x00,        //   Collection (Physical)
0x0B, 0x30, 0x00, 0x01, 0x00,  //     Usage (0x010030)
0x0B, 0x31, 0x00, 0x01, 0x00,  //     Usage (0x010031)
0x0B, 0x32, 0x00, 0x01, 0x00,  //     Usage (0x010032)
0x0B, 0x35, 0x00, 0x01, 0x00,  //     Usage (0x010035)
0x15, 0x00,        //     Logical Minimum (0)
0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
0x75, 0x10,        //     Report Size (16)
0x95, 0x04,        //     Report Count (4)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x0B, 0x39, 0x00, 0x01, 0x00,  //   Usage (0x010039)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x07,        //   Logical Maximum (7)
0x35, 0x00,        //   Physical Minimum (0)
0x46, 0x3B, 0x01,  //   Physical Maximum (315)
0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
0x75, 0x04,        //   Report Size (4)
0x95, 0x01,        //   Report Count (1)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x0F,        //   Usage Minimum (0x0F)
0x29, 0x12,        //   Usage Maximum (0x12)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x04,        //   Report Count (4)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //   Report Size (8)
0x95, 0x34,        //   Report Count (52)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
0x85, 0x21,        //   Report ID (33)
0x09, 0x01,        //   Usage (0x01)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x85, 0x81,        //   Report ID (-127)
0x09, 0x02,        //   Usage (0x02)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x85, 0x01,        //   Report ID (1)
0x09, 0x03,        //   Usage (0x03)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x91, 0x83,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
0x85, 0x10,        //   Report ID (16)
0x09, 0x04,        //   Usage (0x04)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x91, 0x83,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
0x85, 0x80,        //   Report ID (-128)
0x09, 0x05,        //   Usage (0x05)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x91, 0x83,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
0x85, 0x82,        //   Report ID (-126)
0x09, 0x06,        //   Usage (0x06)
0x75, 0x08,        //   Report Size (8)
0x95, 0x3F,        //   Report Count (63)
0x91, 0x83,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
0xC0,              // End Collection
```

retro-pico-switch の `switch_usb_report_descriptor` は同内容 (バイト一致。コメントの折返しのみ異なる)。同ファイルのボタン/ハット/スティック定数 (`SWITCH_MASK_*`、`SWITCH_HAT_*`、`SWITCH_JOYSTICK_MID 0x7FF` / `MAX 0xFFF`)、USB 文字列 3 種も ToadKing と一致する。

### `80 xx → 81 xx` 対応表 (dekuNukem `USB-HID-Notes.md` 基準)

CERTAIN = dekuNukem が応答バイトを明示。UNKNOWN = 資料に応答例なし (`実測待ち`)。推測で埋めない。

| 要求 | 応答 (確定分のみ) | 状態 | 備考 (dekuNukem の記述) |
| ---- | ----------------- | ---- | ----------------------- |
| `80 01` | `81 01 00 <type> <mac6>` (計10B。形式確定) | 部分確定 | 例 `81 01 00 02 57 30 ea 8a bb 7c` (type `02` = 右Joy-Con、MAC `7c:bb:8a:ea:30:57` が逆順格納)。接続状態 + 機種別 + MAC。セッション中の状態変化時にも送られる。Pro Controller の type 値・MAC バイトは実測待ち |
| `80 02` | `81 02` (2B) | CERTAIN | Broadcom への UART ハンドシェイク転送。セッション毎に1回のみ |
| `80 03` | `81 03` (2B) | CERTAIN | `80 02` の直後にのみ可。ボーレート 3Mbit 化。後に再度 `80 02` が来る (2回目のハンドシェイクが必須) |
| `80 04` | 実測待ち | UNKNOWN | 応答例の記載なし。効果のみ: USB HID のみで会話しタイムアウトしなくなる。BT に戻さないために必須 |
| `80 05` | 実測待ち | UNKNOWN | 応答例の記載なし。効果のみ: タイムアウトと BT 復帰を許可する |
| `80 06` | 実測待ち | UNKNOWN | 応答例の記載なし。リセット相当の可能性。UART 送信内容のみ記載 (`19 01 03 07 00 00 92 ... 06 01`) |
| `80 91` | (対象外) | — | 初期実装では対象外 (plan 通り)。プリハンドシェイク転送。可変長の UART post-header 長の記録のみ |
| `80 92` | (対象外) | — | 初期実装では対象外 (plan 通り)。任意 UART 転送。長さは `80 92` に続く 16bit 指定 |
| `01 ..` / `10 ..` | (参考) | — | それぞれ `19 01 03 07 00 00 92 ... 01 ..` / `... 10 ..` を UART 送信する旨の記録のみ |

警告: plan Task 2 の例示コードは `80 04/05/06 → 81 xx (2B)` を仮定しているが、dekuNukem に根拠はない。`80 04` に応答自体があるかも不明。Task 2 のテスト期待値は実測確定まで仮置きとし、最低限 `80 02/03` (+ `01` 形式) の確定分で組むこと。

### Input レポート配置: 既知 vs 実測待ち

- 有線 USB の Input Report ID / サイズ / 周期は、dekuNukem `USB-HID-Notes.md` に記載なし。すべて実測待ち。
- 既知 (記述子由来): Report ID `0x30` の Input 定義あり。ボタン 10bit + 4bit + パディング 2bit = 2B、スティック 4×16bit = 8B、ハット 4bit + ボタン 4bit = 1B、固定パディング 52B → ペイロード 63B + ID 1B = 64B。EP `wMaxPacketSize` 64 と一致する。OUT 方向の Report ID `0x01` / `0x10` / `0x80` / `0x82` (各 63B) は Switch→コントローラ方向の候補だが用途は実測待ち。
- BT 側参考 (`bluetooth_hid_notes.md`。有線事実ではない): Input `0x30` は IMU 付き full mode で @60Hz、Pro Controller は @120Hz。標準入力レポート配置は timer 1B、battery/conn 1B、ボタン 3B、スティック 6B (12bit packed)、vibrator 1B、IMU 3フレーム×12B。INPUT `0x3F` は OS 向け通常コントローラ IF。
- retro-pico-switch 参考 (USB 実装例。MIT): `SwitchReport` 構造体 = `batteryConnection` + `buttons[3]` + `l[3]` + `r[3]`。スティック中央 `SWITCH_JOYSTICK_MID 0x7FF`。
- StonedModder 参考 (8BitDo 観測。純正未確認): ストリーミング 64B、純正は先頭 `0x30`、約60Hz、battery `0x70` = USB 給電・満充電。コントローラ発 `0x81 0x01` → ホスト `80 02`、`0x81 0x02` → ホスト `80 04` の応答は約200ms 以内に返すこと。subcmd 初期化順は `0x40` (IMU) → `0x48` (vibration) → `0x30` (player LED) → `0x03` (mode `0x30`) を最後に。subcmd パケット 12B (Report ID `0x01` + timer + ニュートラルランブル 8B + subcmd + data)。

### 実機チェックリスト (plan Task 1 Step 1–2、人手用)

Step 1 (純正 Pro Controller を PC の USB に挿す。OS の USB 記述子ビューア):

- `bcdUSB=2.00`、`bMaxPacketSize0=64`、`idVendor=0x057E`、`idProduct=0x2009` を確認する。
- `bcdDevice` を記録する (`0x0200` / `0x0210` 不一致の解消。ToadKing の `4.00` コメントも含め確認)。
- `iManufacturer=Nintendo Co., Ltd`、`iProduct=Pro Controller`、`iSerial=000000000001` を確認する。
- HID Report Descriptor 全バイト (203B) と Endpoint (IN `0x81` / OUT `0x01`、サイズ 64、間隔) を確認する。

Step 2 (Switch ドック接続時の要求順序の観測。アナライザ / Beagle / Pico 素通しロガー):

- SETUP/GET_DESCRIPTOR 後の最初の HID OUT (`80 02` か?) を記録する。
- `80 03` の有無、`80 04` 到達までの往復を記録する。特に `80 04/05/06` に対する応答の有無・全バイト・長さを記録する。
- `80 01` 応答の全10B (Pro Controller の type 値 + MAC 格納順) を記録する。
- Input IN の Report ID / サイズ / 周期を記録する。
- `80 05` が来る条件、`80 91/92` の有無を記録する。

Go 条件: `80 02/03/04` (+ `01`) の往復が再現性あり、Input ID/サイズ特定。No-Go 時は案B (HORI 型標準 HID) へ切替をユーザに相談。推測実装はしない。

## Task 6 ハード申送り

本タスク (Task 6) はビルドのみで、実機への書き込み・通電確認は未実施である。以下は人手による実機確認の申送りチェックリストである。測定値はすべて記録し、推測で埋めないこと。

### 0. 前提 (機械確認済み)

- ホストテスト全 PASS (`util` / `cap` / `usb`)。増分 `build` + クリーン `build-verify` 成功 (`pico-wakecon.elf` / `pico-wakecon.uf2` 生成)。
- ログ書式 diff: 既存 `snprintf` / `probe_line` の変更なし。新規リテラルは `usb en=%u cfg=%u hs=%u` と `usage: W [0|1]` のみ。
- `src/hid.c` は `main` からの diff なし。`main()` 初期化順不変。

### 1. 書き込み + PC 列挙 (R-T1-2 の解消)

1. `build/pico-wakecon.uf2` (または `build-verify` のもの) を Pico 2 W に書き込む。
2. PC の USB に挿し、OS の USB 記述子ビューアで確認する:
   - `idVendor=0x057E` / `idProduct=0x2009`、`bcdUSB=2.00`、`bMaxPacketSize0=64`。
   - 文字列 `Nintendo Co., Ltd` / `Pro Controller` / `000000000001`。
   - Configuration 一式 `wTotalLength=41`、HID 記述子 `wDescriptorLength=203` (Report Descriptor 全 203B を Spike T1 表と照合)。
   - Endpoint IN `0x81` / OUT `0x01`、各 `wMaxPacketSize=64`、`bInterval=8`。
3. `bcdDevice` を記録する (ToadKing `0x0200` / retro-pico-switch `0x0210` の不一致を実機値で解消。R-T1-2)。

### 2. UART と W コマンド (Task 5 report §5 の通り)

- PC ↔ Pico は UART0 (GP0/GP1、115200bps) で見る。USB CDC は出ない (正常)。
1. 起動後に既存の起動行が出ること。
2. `?` → 既存 3 行 (`st ...` / `color ...` / `saved ...`) がバイト一致 + 末尾に `usb en=0 cfg=0 hs=0` (未接続・無線既定)。
3. `W` → `usb en=0 cfg=0 hs=0`、状態変化なし。
4. `W 2` / `W x` / `W 1x` → `usage: W [0|1]`、状態変化なし (前後で `W` を打ち再確認)。
5. `W 1` → `usb en=1 ...`。未接続中は `reconnect try` が出なくなること。
6. `W 0` → `usb en=0 ...`、5 秒以内に再接続ループ復帰 (`reconnect try` 再開)。

### 3. Switch ドック有線 (R-T1-1・Input 暫定値の解消)

1. `W 1` の状態で Switch ドックの USB に接続する。
2. Pro Controller として認識されること。`cfg=1`、ハンドシェイク完了で `hs=1` を確認する。
3. 以下を全バイト記録する (推測禁止):
   - `80 01` への応答全 10B (Pro Controller の type 値 + MAC 格納順)。
   - `80 04` / `80 05` / `80 06` への応答の有無・全バイト・長さ (R-T1-1 の解消)。
   - Input レポートの Report ID / サイズ / 周期 / 配置 (Input 暫定値の解消。暫定実装は周期 8ms 要求・実効 10ms 量子化のため、実測周期を記録)。
   - GET_REPORT の有無。
4. `80 04` 前は入力が流れないこと。`80 04` 後に操作が反映されること。抜挿で再接続すること。`80 04` 後に BT へ復帰しないこと。

### 4. W-while-connected の決定 (実装しない・観測のみ)

- Classic 接続中に `W 1` した場合、現実装は切断しない (Touch Nothing Else)。Switch 側が二重認識 (dual-transport) するか、片方を優先するかを観測して記録する。
- 自動切断の要否はこの観測をもって別途判断する。本チェックでは実装変更しない。

### 5. Switch 2・WD・W 0 復帰

1. Switch 2 でも認識されること (有線プロトコル同一の想定。実測で確認)。
2. 有線中の `WD` 中立化: 200ms 放置で入力が中立化すること。
3. `W 0` 復帰ケイデンス: 長時間有線の後の `W 0` で `reconnect try` が 5 秒周期で再開するか観測する (giveup-decay 凍結のため 1 周期遅延の可能性あり。異常ではなく記録対象)。

### 6. T1 Go/No-Go 判定枠

- Go (次タスクで暫定値を確定): `80 02/03/04` (+ `01`) の往復が再現性あり、Input ID/サイズ特定。上記 1・3 の記録が埋まったら、暫定値 (周期・`bcdDevice`・`81 04/05/06` 応答) を実測値で固定化するフォローアップを行う。
- No-Go (案B 協議): 往復が再現しない・Input 特定不可の場合は HORI 型標準 HID (案B) への切替を相談する。推測実装はしない。
