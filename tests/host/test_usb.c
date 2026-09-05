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
    uint8_t r01[] = {0x80, 0x01};
    uint8_t r02[] = {0x80, 0x02};
    uint8_t r03[] = {0x80, 0x03};
    uint8_t r04[] = {0x80, 0x04};
    uint8_t r05[] = {0x80, 0x05};
    uint8_t r06[] = {0x80, 0x06};

    /* 応答は常に 64B ゼロパディング (2wiCC の実働通り)。
     * 短縮応答では厳格なホストが先に進まない実測のため。 */
    {
        int i;
        /* 80 01 は 81 01 00 <type> <mac6> + 0 埋め。type は入力の写し
         * (Pro=0x03 は 2wiCC の kUsbDeviceTypeProController で確認)。 */
        n = usb_build_81_reply(r01, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x01u &&
              out[2] == 0x00u);
        CHECK(out[3] == 0x03u && memcmp(&out[4], mac, 6) == 0);
        for (i = 10; i < 64; i++) {
            CHECK(out[i] == 0u);
        }
        n = usb_build_81_reply(r02, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x02u);
        for (i = 2; i < 64; i++) {
            CHECK(out[i] == 0u);
        }
        n = usb_build_81_reply(r03, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x03u);
        n = usb_build_81_reply(r04, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x04u);
        n = usb_build_81_reply(r05, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x05u);
        n = usb_build_81_reply(r06, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x06u);
    }
    /* handshake 判定の真偽。 */
    CHECK(usb_req_is_handshake(r01, 2));
    {
        uint8_t noths[] = {0x81, 0x01};
        CHECK(!usb_req_is_handshake(noths, 2));
        CHECK(!usb_req_is_handshake(r01, 1));
        CHECK(!usb_req_is_handshake(NULL, 2));
    }
    /* NULL 引数は 0 を返す。 */
    CHECK(usb_build_81_reply(NULL, 2, out, sizeof(out), mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r01, 2, NULL, sizeof(out), mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r01, 2, out, sizeof(out), NULL, 0x03u) == 0);
    /* out が 64B 未満の場合は 0 を返す。 */
    CHECK(usb_build_81_reply(r01, 2, out, 63, mac, 0x03u) == 0);
    CHECK(usb_build_81_reply(r02, 2, out, 10, mac, 0x03u) == 0);
    /* 91/92・未知サブコマンドも 81 <sub> の 64B で返す (2wiCC 通り)。
     * 無応答にするとホストが止まる実測のため。 */
    {
        uint8_t r91[] = {0x80, 0x91, 0x00};
        uint8_t r92[] = {0x80, 0x92};
        uint8_t rff[] = {0x80, 0xFF};
        int i;
        n = usb_build_81_reply(r91, 3, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x91u);
        for (i = 2; i < 64; i++) {
            CHECK(out[i] == 0u);
        }
        n = usb_build_81_reply(r92, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x92u);
        n = usb_build_81_reply(rff, 2, out, sizeof(out), mac, 0x03u);
        CHECK(n == 64 && out[0] == 0x81u && out[1] == 0xFFu);
    }
    /* 0x21 応答。文脈は固定値で捏ねる。 */
    {
        usb_sub_ctx_t ctx;
        uint8_t q02[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
        uint8_t q03[12] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03, 0x30};
        uint8_t q10[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
                           0x50, 0x60, 0, 0, 13};
        uint8_t q30[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x30};
        uint8_t q40[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40};
        uint8_t q48[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x48};
        uint8_t q33[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x33};
        uint8_t q10unk[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
                              0x34, 0x12, 0, 0, 4};
        int i;
        ctx.btn[0] = 0x08u;
        ctx.btn[1] = 0x10u;
        ctx.btn[2] = 0xF2u;
        ctx.lx = 0x80u; ctx.ly = 0x80u;
        ctx.rx = 0x80u; ctx.ry = 0x80u;
        ctx.timer = 0x41u;
        memcpy(ctx.mac, mac, 6);
        ctx.player = 0u;
        /* 0x02 機器情報: ID+12B 接頭 + 82 02 03 48 03 02 + MAC + 01 01。 */
        n = usb_build_21_reply(q02, 11, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[0] == 0x21u);
        CHECK(out[1] == 0x41u && out[2] == 0x91u);
        CHECK(out[3] == 0x08u && out[4] == 0x90u && out[5] == 0xC2u);
        CHECK(out[13] == 0x82u && out[14] == 0x02u);
        CHECK(out[15] == 0x03u && out[16] == 0x48u);
        CHECK(out[17] == 0x03u && out[18] == 0x02u);
        CHECK(memcmp(&out[19], mac, 6) == 0);
        CHECK(out[25] == 0x01u && out[26] == 0x02u);
        for (i = 27; i < 64; i++) {
            CHECK(out[i] == 0u);
        }
        /* 中立スティックは 12bit 0x800 (0x00,0x08,0x80)。 */
        CHECK(out[6] == 0x00u && out[7] == 0x08u && out[8] == 0x80u);
        n = usb_build_21_reply(q03, 12, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[0] == 0x21u && out[13] == 0x80u &&
              out[14] == 0x03u);
        /* 0x10 SPI 読出: 0x6050 を 13B。addr(2B)+00 00+13+中身。 */
        n = usb_build_21_reply(q10, 16, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u);
        CHECK(out[15] == 0x50u && out[16] == 0x60u);
        CHECK(out[17] == 0x00u && out[18] == 0x00u && out[19] == 13u);
        n = usb_build_21_reply(q30, 11, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[13] == 0x80u && out[14] == 0x30u);
        n = usb_build_21_reply(q40, 11, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[13] == 0x80u && out[14] == 0x40u);
        n = usb_build_21_reply(q48, 11, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[13] == 0x80u && out[14] == 0x48u);
        /* 未知サブコマンドは 80 + sub の既定応答。 */
        n = usb_build_21_reply(q33, 11, out, sizeof(out), &ctx);
        CHECK(n == 64 && out[13] == 0x80u && out[14] == 0x33u);
        /* 表にない 0x60xx (例: 機器種別 0x6012) は 0xFF 埋めで答える。 */
        {
            uint8_t q12[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
                               0x12, 0x60, 0, 0, 4};
            n = usb_build_21_reply(q12, 16, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u);
            CHECK(out[15] == 0x12u && out[16] == 0x60u && out[19] == 4u);
            CHECK(out[20] == 0xFFu && out[21] == 0xFFu);
            CHECK(out[22] == 0xFFu && out[23] == 0xFFu);
        }
        /* 0x6000 は空応答 (Switch 2 の 2162-0002 対策)。 */
        {
            uint8_t q00[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
                               0x00, 0x60, 0, 0, 16};
            n = usb_build_21_reply(q00, 16, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u);
            CHECK(out[15] == 0x00u && out[16] == 0x60u && out[19] == 16u);
            for (i = 20; i < 36; i++) {
                CHECK(out[i] == 0xFFu);
            }
        }
        /* 0x6020 は妥当な校正値 (全 0 ではない)。 */
        {
            uint8_t q20[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
                               0x20, 0x60, 0, 0, 24};
            n = usb_build_21_reply(q20, 16, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[19] == 24u);
            CHECK(out[25] == 0x01u && out[27] == 0x40u);
        }
        /* 0x01 BT ペアリング: ack 81 + 種別雛形。type 1 は自 MAC ASCII。 */
        {
            uint8_t p1[12] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x01};
            uint8_t p2[12] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02};
            uint8_t p3[12] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x03};
            uint8_t p0[11] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
            n = usb_build_21_reply(p1, 12, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[13] == 0x81u && out[14] == 0x01u);
            CHECK(out[15] == 0x01u);
            CHECK(memcmp(&out[16], "7CBB8A112233", 12) == 0);
            for (i = 46; i < 64; i++) {
                CHECK(out[i] == 0u);
            }
            n = usb_build_21_reply(p2, 12, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[15] == 0x02u && out[16] == 0xe5u);
            n = usb_build_21_reply(p3, 12, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[15] == 0x03u && out[16] == 0x00u);
            /* 種別バイトなしは type 3 扱い。 */
            n = usb_build_21_reply(p0, 11, out, sizeof(out), &ctx);
            CHECK(n == 64 && out[15] == 0x03u);
        }
        /* 不正は 0。範囲外 SPI・短い要求・短い out・NULL・非 0x01。 */
        CHECK(usb_build_21_reply(q10unk, 16, out, sizeof(out), &ctx) == 0);
        CHECK(usb_build_21_reply(q10, 15, out, sizeof(out), &ctx) == 0);
        CHECK(usb_build_21_reply(q02, 10, out, sizeof(out), &ctx) == 0);
        CHECK(usb_build_21_reply(q02, 11, out, 63, &ctx) == 0);
        CHECK(usb_build_21_reply(NULL, 11, out, sizeof(out), &ctx) == 0);
        CHECK(usb_build_21_reply(q02, 11, NULL, sizeof(out), &ctx) == 0);
        CHECK(usb_build_21_reply(q02, 11, out, sizeof(out), NULL) == 0);
        {
            uint8_t q30x[11] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x30};
            CHECK(usb_build_21_reply(q30x, 11, out, sizeof(out), &ctx) == 0);
        }
    }
    /* 0x30 入力レポート: ID + 12B 状態 + 36B IMU(0) + 15B 埋め。 */
    {
        usb_sub_ctx_t ctx30;
        uint8_t r30[64];
        int i;
        memset(&ctx30, 0, sizeof(ctx30));
        ctx30.btn[0] = 0x08u;
        ctx30.btn[1] = 0x10u;
        ctx30.btn[2] = 0xF2u;
        ctx30.lx = 0x80u; ctx30.ly = 0x80u;
        ctx30.rx = 0x80u; ctx30.ry = 0x80u;
        ctx30.timer = 0x55u;
        n = usb_build_30_report(&ctx30, r30);
        CHECK(n == 64 && r30[0] == 0x30u);
        CHECK(r30[1] == 0x55u && r30[2] == 0x91u);
        CHECK(r30[3] == 0x08u && r30[4] == 0x90u && r30[5] == 0xC2u);
        CHECK(r30[6] == 0x00u && r30[7] == 0x08u && r30[8] == 0x80u);
        CHECK(r30[12] == 0x09u);
        for (i = 13; i < 64; i++) {
            CHECK(r30[i] == 0u);
        }
        CHECK(usb_build_30_report(NULL, r30) == 0);
        CHECK(usb_build_30_report(&ctx30, NULL) == 0);
    }
    if (fails == 0) { printf("OK usb\n"); }
    return fails != 0;
}
