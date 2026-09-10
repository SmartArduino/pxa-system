#include "pxa_permission.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[256];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

static void test_request(void) {
    uint8_t payload[64];
    uint8_t packet[96];
    const uint8_t scope[] = {'a', 'p', 'i'};
    assert(pxa_permission_acquire(19, "net.client", 10, scope, sizeof(scope),
                                  payload, sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 33);
    assert(pxa_read_u16(captured) == PXA_SERVICE_PERMISSION);
    assert(pxa_read_u16(captured + 2) == PXA_PERMISSION_ACQUIRE);
    assert(pxa_read_u32(captured + 4) == 19);
    assert(pxa_read_u32(captured + 8) == 21);
    assert(pxa_read_u16(captured + 12) == PXA_PERMISSION_NAME);
    assert(pxa_read_u16(captured + 14) == 10);
    assert(memcmp(captured + 16, "net.client", 10) == 0);
    assert(pxa_read_u16(captured + 26) == PXA_PERMISSION_SCOPE);
    assert(pxa_read_u16(captured + 28) == 3);
    assert(memcmp(captured + 30, scope, sizeof(scope)) == 0);
    assert(!pxa_permission_check(0, "net.client", 10, NULL, 0, payload,
                                 sizeof(payload), packet, sizeof(packet)));
}

static void test_results(void) {
    const uint8_t checked[] = {11, 0, 1, 0, 8, 0, 0, 0, 5, 0, 0, 0,
                               0, 0, 0, 0, 1};
    const uint8_t acquired[] = {11, 0, 2, 0, 9, 0, 0, 0, 8, 0, 0, 0,
                                0, 0, 0, 0, 4, 3, 2, 1};
    pxa_event_t event;
    pxa_permission_check_result_t check;
    pxa_permission_acquire_result_t acquire;
    assert(pxa_parse_event(checked, sizeof(checked), &event));
    assert(pxa_permission_parse_check(&event, &check));
    assert(check.status == PXA_STATUS_OK && check.decision == PXA_PERMISSION_ALLOW);
    assert(pxa_parse_event(acquired, sizeof(acquired), &event));
    assert(pxa_permission_parse_acquire(&event, &acquire));
    assert(acquire.status == PXA_STATUS_OK && acquire.handle == 0x01020304);
}

int main(void) {
    test_request();
    test_results();
    return 0;
}
