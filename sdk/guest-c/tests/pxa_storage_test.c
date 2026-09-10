#include "pxa_storage.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    uint8_t payload[128];
    uint8_t packet[160];
    const uint8_t value[] = {'v', '1'};
    assert(pxa_storage_set(71, "counter.total", 13, value, sizeof(value), payload,
                           sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 35);
    assert(pxa_read_u16(captured) == PXA_SERVICE_STORAGE);
    assert(pxa_read_u16(captured + 2) == PXA_STORAGE_SET);
    assert(pxa_read_u32(captured + 4) == 71);
    assert(!pxa_storage_get(1, "1bad", 4, payload, sizeof(payload), packet,
                            sizeof(packet)));
    {
        const uint8_t event_bytes[] = {
            6, 0, 1, 0, 7, 0, 0, 0, 10, 0, 0, 0,
            0, 0, 0, 0, 2, 0, 2, 0, 'v', '1'};
        pxa_event_t event;
        pxa_storage_get_result_t result;
        assert(pxa_parse_event(event_bytes, sizeof(event_bytes), &event));
        assert(pxa_storage_parse_get(&event, &result));
        assert(result.status == PXA_STATUS_OK && result.value_length == 2 &&
               memcmp(result.value, value, sizeof(value)) == 0);
    }
    return 0;
}
