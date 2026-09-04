#ifndef _PICO_BTSTACK_BTSTACK_CONFIG_H
#define _PICO_BTSTACK_BTSTACK_CONFIG_H

// ==========================================================================
// 段8-a（bt_probe）の BTstack 設定
//
// ★★★2026/08/30 全面的に書き直した。
//   ★私が最初に書いた版は不足が多く、ビルドが通らなかった。
//     HCI_ACL_CHUNK_SIZE_ALIGNMENT が無く、cyw43 の転送層が弾いた。
//   ★★そこで pico-examples の bluetooth/config/btstack_config_common.h
//     を土台にした。★これは Raspberry Pi 公式が実際に使っている設定である。
//   ★★★推測で書かず、動いている実物を土台にする（W-1）。
//
// ★段8-a では Classic だけを使う（CMakeLists で pico_btstack_ble を
//   リンクしていないので ENABLE_BLE は定義されない）。
//   ★★#ifdef ENABLE_BLE のブロックはそのまま残す。段8-c で ble を
//     リンクしたとき、この設定が自動で効くようにするためである。
// ==========================================================================

// --- 使う機能 -------------------------------------------------------------
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

#ifdef ENABLE_BLE
// ★段8-c（Switch 2 の BLE ペアリング）で効く。いまは定義されない。
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_LE_ENHANCED_CONNECTION_COMPLETE_EVENT
#endif

#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP
#endif

#if defined (ENABLE_CLASSIC) && defined (ENABLE_BLE)
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// --- 大きさの設定 ---------------------------------------------------------
// ★★★HCI_ACL_CHUNK_SIZE_ALIGNMENT が今回の要点である。
//   ★cyw43 の転送層は 4 の倍数でないと #error で止まる。
//   ★★これが無いのが最初のビルド失敗の原因だった。
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

// ★★cyw43 の共有バスが溢れないよう、使う buffer の数を抑える。
//   ★公式の設定をそのまま採る。減らす理由が無い。
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// ★★★同じ理由で、Controller → Host の流量制御を入れる。
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// --- 保存先 ---------------------------------------------------------------
// ★リンクキーとボンド情報をフラッシュへ置く（TLV）。
//   ★★段8-c（Switch 2 の LTK 保存）でこれを使う。
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

// ★malloc を渡さないので、ATT DB は固定長にする。
//   ★★段8-c で GATT サーバを作るときに要る。
#define MAX_ATT_DB_SIZE 512

// --- 動かし方 -------------------------------------------------------------
#define HAVE_EMBEDDED_TIME_MS

// ★btstack_assert を Pico SDK の assert へ回す。
#define HAVE_ASSERT

// ★HCI reset の応答が遅い場合の待ち時間。公式の値をそのまま採る。
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

// ★★ENABLE_SCO_OVER_HCI は入れない。
//   ★公式は音声（ヘッドセット）向けに入れているが、
//     コントローラのエミュレートには要らない。
//   ★★入れると SCO 用の buffer が確保され、RAM を無駄に使う。

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ★★★HAVE_BTSTACK_STDIN は定義しない。
//   ★公式の例は標準入力から操作するので定義しているが、
//     ★★bt_probe は UART を自分で読むので要らない。
//   ★★★定義すると stdio を要求され、pico_enable_stdio_* を 0 に
//     している本プロジェクトでは食い違う。

#endif // _PICO_BTSTACK_BTSTACK_CONFIG_H
