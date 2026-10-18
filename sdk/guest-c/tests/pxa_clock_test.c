#include "../../testing/pxa_control_capture.h"
#include <assert.h>

#include "pxa_clock.h"
static unsigned controls;

int32_t pxa_test_control(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    ++controls;
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
    uint8_t payload[24];
    uint32_t size = 0;
    uint64_t tick = 0;
    pxa_event_t event;
    pxa_clock_now_result_t result;
    assert(pxa_clock_set_period(8)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_clock_set_period(1001)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_clock_now(0)==PXA_STATUS_INVALID_ARGUMENT);
    assert(controls==0);
    assert(!pxa_clock_set_period(0) && !pxa_clock_set_period(16) &&
           !pxa_clock_set_period(1000) && controls==3);
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
    {
        pxa_clock_limits_result_t limits;
        pxa_clock_configure_result_t configured;
        assert(!pxa_clock_build_limits(packet,sizeof(packet),0,&size));
        assert(pxa_clock_build_limits(packet,sizeof(packet),51,&size) && size==20);
        assert(pxa_clock_build_configure(packet,sizeof(packet),52,8000,&size) && size==24);
        assert(pxa_parse_event(packet,size,&event) && event.token==52 &&
            event.opcode==PXA_CLOCK_CONFIGURE && pxa_load_u32(event.payload)==8000);
        pxa_zero(payload,sizeof(payload));
        pxa_store_u32(payload+4,1000); pxa_store_u32(payload+8,60000000);
        pxa_store_u32(payload+12,1); pxa_store_u32(payload+16,1); pxa_store_u32(payload+20,3);
        event=(pxa_event_t){.service=4,.opcode=PXA_CLOCK_LIMITS,.token=51,
            .payload=payload,.payload_size=24};
        assert(pxa_clock_parse_limits(&event,51,&limits) && !limits.status);
        assert(pxa_clock_period_supported(&limits.limits,8000));
        assert(!pxa_clock_period_supported(&limits.limits,999));
        event.payload_size=23; assert(!pxa_clock_parse_limits(&event,51,&limits));
        event.payload_size=24; pxa_store_u32(payload+12,0);
        assert(!pxa_clock_parse_limits(&event,51,&limits));
        event.opcode=PXA_CLOCK_CONFIGURE;event.token=52;event.payload_size=8;
        pxa_store_u32(payload+4,8000);
        assert(pxa_clock_parse_configure(&event,52,8000,&configured) && configured.period_us==8000);
        assert(!pxa_clock_parse_configure(&event,52,8001,&configured));
        pxa_store_u32(payload,(uint32_t)PXA_STATUS_UNSUPPORTED); event.payload_size=4;
        assert(pxa_clock_parse_configure(&event,52,8000,&configured) && configured.status==PXA_STATUS_UNSUPPORTED);
    }
    return 0;
}
