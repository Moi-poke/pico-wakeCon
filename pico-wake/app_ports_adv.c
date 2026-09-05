#include "btstack.h"
#include "app_state.h"
#include "app_ports_adv.h"

int port_set_params(void *context, uint16_t min, uint16_t max) {
    (void)context;
    gap_advertisements_set_params(min, max, 0x00, 0x00, null_addr, 0x07, 0x00);
    return 0;
}

int port_set_data(void *context, const uint8_t *data, uint8_t length) {
    (void)context;
    gap_advertisements_set_data(length, (uint8_t *)data);
    return 0;
}

int port_enable(void *context, bool enabled) {
    (void)context;
    gap_advertisements_enable(enabled ? 1 : 0);
    return 0;
}
