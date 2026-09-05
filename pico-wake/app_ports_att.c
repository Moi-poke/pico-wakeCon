#include "btstack.h"
#include "switch2_wake_att_db.h"
#include "app_ports_att.h"

/* --- W5: att_db_util を builder の port へ橋渡しする --- */
static void att_port_init(void *ctx) { (void)ctx; att_db_util_init(); }

static uint16_t att_port_svc128(void *ctx, const uint8_t *uuid128) {
    (void)ctx;
    return att_db_util_add_service_uuid128(uuid128);
}

static uint16_t att_port_svc16(void *ctx, uint16_t uuid16) {
    (void)ctx;
    return att_db_util_add_service_uuid16(uuid16);
}

static uint16_t att_port_char128(void *ctx, const uint8_t *uuid128,
                                 uint16_t properties, uint8_t read_perm,
                                 uint8_t write_perm, uint8_t *data,
                                 uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_characteristic_uuid128(uuid128, properties,
                                                  read_perm, write_perm,
                                                  data, data_len);
}

static uint16_t att_port_char16(void *ctx, uint16_t uuid16,
                                uint16_t properties, uint8_t read_perm,
                                uint8_t write_perm, uint8_t *data,
                                uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_characteristic_uuid16(uuid16, properties,
                                                 read_perm, write_perm,
                                                 data, data_len);
}

static uint16_t att_port_desc128(void *ctx, const uint8_t *uuid128,
                                 uint16_t properties, uint8_t read_perm,
                                 uint8_t write_perm, uint8_t *data,
                                 uint16_t data_len) {
    (void)ctx;
    return att_db_util_add_descriptor_uuid128(uuid128, properties,
                                              read_perm, write_perm,
                                              data, data_len);
}

static uint16_t att_port_size(void *ctx) { (void)ctx; return att_db_util_get_size(); }

const switch2_wake_att_port_t att_port = {
    att_port_init, att_port_svc128, att_port_svc16,
    att_port_char128, att_port_char16, att_port_desc128,
    att_port_size, NULL
};
