#include <assert.h>

#include "pxa_lease.h"

int32_t pxa_submit(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t size) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)size;
    return -3;
}

int main(void) {
    const uint64_t handle = UINT64_C(0x1234567800000001);
    uint8_t packet[40];
    uint8_t payload[16] = {0};
    uint32_t written = 0;
    pxa_event_t event;
    pxa_lease_result_t result;
    pxa_lease_revoked_t revoked;
    assert(pxa_lease_build_acquire(packet, sizeof(packet), 77,
                                      1, 100, &written) && written == 34);
    assert(pxa_parse_event(packet, written, &event) &&
           event.service == PXA_CORE_SERVICE &&
           event.opcode == PXA_LEASE_ACQUIRE && event.token == 77 &&
           event.payload_size == 14);
    assert(!pxa_lease_build_acquire(packet, sizeof(packet), 77,
                                       7, 100, &written));
    pxa_store_u32(payload, 0);
    pxa_store_u16(payload + 4, 4);
    pxa_store_u16(payload + 6, 8);
    pxa_store_u64(payload + 8, handle);
    event.payload = payload;
    event.payload_size = 16;
    assert(pxa_lease_parse_result(&event, 77, &result) &&
           result.status == 0 && result.handle == handle);
    payload[6] = 4;
    assert(!pxa_lease_parse_result(&event, 77, &result));
    pxa_store_u64(payload, handle);
    pxa_store_u32(payload + 8, (uint32_t)-10);
    event.opcode = PXA_LEASE_REVOKED;
    event.token = 0;
    event.payload_size = 12;
    assert(pxa_lease_parse_revoked(&event, &revoked) &&
           revoked.handle == handle && revoked.reason == -10);
    return 0;
}
