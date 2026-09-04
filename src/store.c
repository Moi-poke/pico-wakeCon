#include <string.h>

#include "btstack_tlv.h"
#include "cap.h"
#include "spi.h"
#include "link.h"
#include "store.h"

#define TAG_HOST 0x4E58484Fu  /* 'NXHO' */
#define TAG_COLOR 0x4E58434Cu /* 'NXCL' */
#define TAG_CAP 0x57435031u   /* 'WCP1' */
#define TAG_MODE 0x4E584D44u  /* 'NXMD' */

void store_host(bd_addr_t addr)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return;
    }
    tlv->store_tag(ctx, TAG_HOST, addr, 6);
    memcpy(probe_host_addr, addr, 6);
    probe_host_known = true;
}

bool store_host_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return false;
    }
    if (tlv->get_tag(ctx, TAG_HOST, probe_host_addr, 6) != 6) {
        return false;
    }
    probe_host_known = true;
    return true;
}

void store_color(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return;
    }
    tlv->store_tag(ctx, TAG_COLOR, spi_color_6050, 13);
}

void store_color_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t buf[13];
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return;
    }
    if (tlv->get_tag(ctx, TAG_COLOR, buf, 13) != 13) {
        return;
    }
    memcpy(spi_color_6050, buf, 12);  /* 13B 目は仕様値のため戻さない */
}

bool store_cap_save(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t blob[CAP_BLOB_SIZE];
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return false;
    }
    if (!cap_encode(&probe_cap_saved, blob)) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    if (tlv->store_tag(ctx, TAG_CAP, blob, (uint32_t)sizeof(blob)) != 0) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    memset(blob, 0, sizeof(blob));
    return true;
}

bool store_cap_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t blob[CAP_BLOB_SIZE];
    uint32_t got;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return false;
    }
    memset(blob, 0, sizeof(blob));
    got = tlv->get_tag(ctx, TAG_CAP, blob, (uint32_t)sizeof(blob));
    if (!cap_decode(blob, got, &probe_cap_saved)) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    memset(blob, 0, sizeof(blob));
    probe_cap_valid = true;
    return true;
}

void store_cap_forget(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return;
    }
    tlv->delete_tag(ctx, TAG_CAP);
}

void store_mode_save(uint8_t mode)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return;
    }
    tlv->store_tag(ctx, TAG_MODE, &mode, 1);
}

bool store_mode_load(uint8_t *mode)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t value = 0u;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv == NULL) {
        return false;
    }
    if (tlv->get_tag(ctx, TAG_MODE, &value, 1) != 1) {
        return false;
    }
    *mode = value;
    return true;
}
