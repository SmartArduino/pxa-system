#include <assert.h>
#include <stdint.h>

#include "pxa_lease.h"

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    assert(length == 26);
    assert(pxa_read_u16(data) == PXA_SERVICE_CORE);
    assert(pxa_read_u16(data + 2) == PXA_CORE_ACQUIRE_LEASE);
    assert(pxa_read_u32(data + 4) == 9);
    assert(pxa_read_u16(data + 12) == PXA_LEASE_KIND);
    assert(pxa_read_u16(data + 18) == PXA_LEASE_DURATION_MS);
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data, uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    uint8_t payload[32];
    uint8_t packet[64];
    assert(pxa_lease_acquire(9, PXA_LEASE_FOREGROUND, 1000, payload,
                             sizeof(payload), packet, sizeof(packet)));
    assert(!pxa_lease_acquire(0, PXA_LEASE_FOREGROUND, 1000, payload,
                              sizeof(payload), packet, sizeof(packet)));

    const uint8_t result_bytes[] = {
        1, 0, 3, 0, 9, 0, 0, 0, 12, 0, 0, 0,
        0, 0, 0, 0, 4, 0, 4, 0, 3, 0, 1, 0,
    };
    pxa_event_t event;
    pxa_lease_result_t result;
    assert(pxa_parse_event(result_bytes, sizeof(result_bytes), &event));
    assert(pxa_lease_parse_result(&event, &result));
    assert(result.status == PXA_STATUS_OK && result.handle == 0x00010003u);

    const uint8_t revoked_bytes[] = {
        1, 0, 3, 0x80, 0, 0, 0, 0, 8, 0, 0, 0,
        3, 0, 1, 0, 0xf6, 0xff, 0xff, 0xff,
    };
    pxa_lease_revoked_t revoked;
    assert(pxa_parse_event(revoked_bytes, sizeof(revoked_bytes), &event));
    assert(pxa_lease_parse_revoked(&event, &revoked));
    assert(revoked.handle == 0x00010003u && revoked.reason == PXA_STATUS_CANCELLED);
    return 0;
}
