#include "pxa_ipc.h"

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
    const uint8_t body[] = {'h', 'i'};
    assert(pxa_ipc_call(71, "example.echo", 12, body, sizeof(body), payload,
                        sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 34 && pxa_read_u16(captured) == PXA_SERVICE_IPC &&
           pxa_read_u16(captured + 2) == PXA_IPC_CALL &&
           pxa_read_u32(captured + 4) == 71);
    assert(!pxa_ipc_call(1, "1bad", 4, NULL, 0, payload, sizeof(payload), packet,
                         sizeof(packet)));
    {
        const uint8_t call_result[] = {7, 0, 1, 0, 71, 0, 0, 0, 8, 0, 0, 0,
                                       0, 0, 0, 0, 42, 0, 0, 0};
        pxa_event_t event;
        pxa_ipc_call_result_t result;
        assert(pxa_parse_event(call_result, sizeof(call_result), &event));
        assert(pxa_ipc_parse_call_result(&event, &result));
        assert(result.status == PXA_STATUS_OK && result.call_id == 42);
    }
    {
        const uint8_t result_event[] = {7, 0, 2, 128, 42, 0, 0, 0, 10, 0, 0, 0,
                                        0, 0, 0, 0, 3, 0, 2, 0, 'o', 'k'};
        pxa_event_t event;
        pxa_ipc_result_t result;
        assert(pxa_parse_event(result_event, sizeof(result_event), &event));
        assert(pxa_ipc_parse_result(&event, &result));
        assert(result.call_id == 42 && result.status == PXA_STATUS_OK &&
               result.payload_length == 2 && memcmp(result.payload, "ok", 2) == 0);
    }
    return 0;
}
