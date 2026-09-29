#include <assert.h>

#include "pxa_clock.h"

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
    uint8_t packet[32];
    uint8_t payload[12];
    uint32_t size = 0;
    uint64_t tick = 0;
    pxa_event_t event;
    pxa_clock_now_result_t result;
    assert(!pxa_clock_build_set_period(packet, sizeof(packet), 15,
                                           &size));
    assert(pxa_clock_build_set_period(packet, sizeof(packet), 16,
                                         &size) && size == 22);
    assert(pxa_parse_event(packet, size, &event) && event.token == 0 &&
           event.payload_size == 2 && pxa_load_u16(event.payload) == 16);
    assert(pxa_clock_build_now(packet, sizeof(packet), 99, &size) &&
           size == 20);
    assert(pxa_parse_event(packet, size, &event) && event.token == 99);
    pxa_store_u64(payload, 1234);
    event.service = PXA_CLOCK_SERVICE;
    event.opcode = PXA_CLOCK_TICK;
    event.token = 0;
    event.payload = payload;
    event.payload_size = 8;
    assert(pxa_clock_parse_tick(&event, &tick) && tick == 1234);
    pxa_store_u32(payload, 0);
    pxa_store_u64(payload + 4, 5678);
    event.opcode = PXA_CLOCK_NOW;
    event.token = 99;
    event.payload_size = 12;
    assert(pxa_clock_parse_now(&event, 99, &result) &&
           result.timestamp_us == 5678);
    event.payload_size = 11;
    assert(!pxa_clock_parse_now(&event, 99, &result));
    tick = 0;
    assert(pxa_clock_tick_steps(&tick, 1000000, 20, 2) == 1);
    assert(pxa_clock_tick_steps(&tick, 1020000, 20, 2) == 1);
    assert(pxa_clock_tick_steps(&tick, 1020000, 20, 2) == 0);
    assert(pxa_clock_tick_steps(&tick, 1200000, 20, 2) == 2);
    assert(pxa_clock_tick_steps(&tick, 1200001, 20, 2) == 0);
    return 0;
}
