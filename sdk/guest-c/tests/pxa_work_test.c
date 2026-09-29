#include <assert.h>

#include "pxa_work.h"

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
    uint8_t packet[160];
    uint8_t payload[64] = {0};
    uint8_t value[8];
    uint32_t written = 0;
    size_t offset = 0;
    size_t size = 0;
    pxa_event_t event;
    pxa_work_enqueue_result_t queued;
    pxa_work_context_t context;
    pxa_work_stop_t stop;
    pxa_work_request_t request = {0};
    request.worker = "sync.worker";
    request.worker_size = 11;
    request.initial_delay_ms = 1000;
    request.execution_hint_ms = 10000;
    request.retry_delay_ms = 1000;
    request.max_attempts = 2;
    assert(pxa_work_build_enqueue(packet, sizeof(packet), 77,
                                     &request, &written));
    assert(pxa_parse_event(packet, written, &event) &&
           event.service == PXA_WORK_SERVICE &&
           event.opcode == PXA_WORK_ENQUEUE && event.token == 77);
    request.worker = "Sync.worker";
    assert(!pxa_work_build_enqueue(packet, sizeof(packet), 77,
                                      &request, &written));
    assert(pxa_work_build_id_request(packet, sizeof(packet),
                                        PXA_WORK_CANCEL, 78, 4, 0,
                                        &written) && written == 28);
    assert(pxa_work_build_id_request(packet, sizeof(packet),
                                        PXA_WORK_COMPLETE, 79, 4,
                                        PXA_WORK_SUCCESS,
                                        &written) && written == 33);

    pxa_store_u32(payload, 0);
    pxa_store_u32(value, 4);
    assert(pxa_wire_record_encode(payload + 4, sizeof(payload) - 4,
                                  4, value, 4, &size));
    pxa_store_u32(value, 10000);
    assert(pxa_wire_record_encode(payload + 4 + size,
                                  sizeof(payload) - 4 - size,
                                  8, value, 4, &offset));
    event.service = PXA_WORK_SERVICE;
    event.opcode = PXA_WORK_ENQUEUE;
    event.token = 77;
    event.payload = payload;
    event.payload_size = (uint32_t)(4 + size + offset);
    assert(pxa_work_parse_enqueue(&event, 77, &queued) &&
           queued.id == 4 && queued.granted_execution_ms == 10000);
    payload[6] = 8;
    assert(!pxa_work_parse_enqueue(&event, 77, &queued));

    offset = 0;
    pxa_store_u32(value, 4);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  7, value, 4, &size));
    offset += size;
    value[0] = 1;
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  9, value, 1, &size));
    offset += size;
    pxa_store_u64(value, 1234);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  10, value, 8, &size));
    offset += size;
    assert(pxa_work_parse_context(payload, offset, &context) &&
           context.id == 4 && context.attempt == 1 &&
           context.deadline_ms == 1234);
    pxa_store_u32(payload, 4);
    pxa_store_u64(payload + 4, 5678);
    event.opcode = PXA_WORK_STOP_REQUESTED;
    event.token = 0;
    event.payload_size = 12;
    assert(pxa_work_parse_stop(&event, &stop) &&
           stop.id == 4 && stop.deadline_ms == 5678);
    return 0;
}
