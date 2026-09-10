#include "pxa_work.h"

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

static void test_enqueue_and_complete(void) {
    static const uint8_t input[] = {1, 2, 3};
    pxa_work_request_t work = {
        "sync.worker", 11, 2000, 5000, input, sizeof(input), 3000, 3};
    uint8_t payload[96];
    uint8_t packet[128];
    assert(pxa_work_enqueue(7, &work, payload, sizeof(payload), packet,
                            sizeof(packet)));
    assert(pxa_read_u16(captured) == PXA_SERVICE_WORK);
    assert(pxa_read_u16(captured + 2) == PXA_WORK_ENQUEUE);
    assert(pxa_read_u32(captured + 4) == 7);
    assert(pxa_read_u16(captured + 12) == PXA_WORK_WORKER);
    assert(pxa_work_complete(8, 42, PXA_WORK_RETRY, payload,
                             sizeof(payload), packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_WORK_COMPLETE);
    assert(pxa_read_u16(captured + 12) == PXA_WORK_ID);
    assert(pxa_read_u32(captured + 16) == 42);
    assert(pxa_work_cancel(9, 42, payload, sizeof(payload), packet,
                           sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_WORK_CANCEL);
}

static void test_results(void) {
    const uint8_t completed[] = {
        13, 0, 1, 0, 7, 0, 0, 0, 20, 0, 0, 0,
        0, 0, 0, 0,
        4, 0, 4, 0, 42, 0, 0, 0,
        8, 0, 4, 0, 0x88, 0x13, 0, 0,
    };
    pxa_event_t event;
    pxa_work_enqueue_result_t result;
    assert(pxa_parse_event(completed, sizeof(completed), &event));
    assert(pxa_work_parse_enqueue(&event, &result));
    assert(result.status == PXA_STATUS_OK && result.id == 42 &&
           result.granted_execution_ms == 5000);
}

static void test_context_and_stop(void) {
    const uint8_t config[] = {
        7, 0, 4, 0, 42, 0, 0, 0,
        9, 0, 1, 0, 2,
        10, 0, 8, 0, 0x88, 0x13, 0, 0, 0, 0, 0, 0,
        11, 0, 3, 0, 'a', 'b', 'c',
    };
    const uint8_t stop[] = {
        13, 0, 1, 0x80, 0, 0, 0, 0, 12, 0, 0, 0,
        42, 0, 0, 0, 0x88, 0x13, 0, 0, 0, 0, 0, 0,
    };
    pxa_work_context_t context;
    pxa_work_stop_t stopping;
    pxa_event_t event;
    assert(pxa_work_parse_context(config, sizeof(config), &context));
    assert(context.id == 42 && context.attempt == 2 &&
           context.deadline_ms == 5000 && context.input_length == 3 &&
           memcmp(context.input, "abc", 3) == 0);
    assert(pxa_parse_event(stop, sizeof(stop), &event));
    assert(pxa_work_parse_stop(&event, &stopping));
    assert(stopping.id == 42 && stopping.deadline_ms == 5000);
}

int main(void) {
    test_enqueue_and_complete();
    test_results();
    test_context_and_stop();
    return 0;
}
