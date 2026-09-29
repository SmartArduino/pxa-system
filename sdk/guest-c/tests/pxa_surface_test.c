#include <assert.h>

#include "pxa_surface.h"

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
    uint8_t packet[128] = {0};
    static uint8_t aligned_buffers[128]
        __attribute__((aligned(PXA_SURFACE_BUFFER_ALIGNMENT)));
    uint8_t payload[64] = {0};
    uint32_t size = 0;
    pxa_event_t event;
    pxa_surface_desc_t desc = {32, 16, PXA_SURFACE_RGB565, 2, 0};
    pxa_surface_rect_t damage = {1, 2, 3, 4};
    pxa_surface_create_result_t created;
    pxa_surface_state_t state;
    pxa_surface_released_t released;
    int32_t status = -1;
    const uint64_t handle = UINT64_C(0x1234567800000001);

    assert(pxa_surface_fit_scale(148, 120, 296, 240) == 2);
    assert(pxa_surface_fit_scale(100, 100, 800, 800) == 4);
    assert(pxa_surface_fit_scale(UINT32_MAX, 1, UINT32_MAX, 10) == 1);
    assert(pxa_surface_fit_scale(UINT32_MAX, 1, 100, 10) == 0);

    assert(pxa_surface_build_create(packet, sizeof(packet), 91, desc,
                                        &size) && size == 28);
    assert(pxa_parse_event(packet, size, &event) && event.token == 91 &&
           event.service == PXA_SURFACE_SERVICE &&
           event.payload_size == 8);
    desc.flags = PXA_SURFACE_PREMULTIPLIED_ALPHA;
    assert(!pxa_surface_build_create(packet, sizeof(packet), 91, desc,
                                         &size));
    assert(pxa_surface_build_configure_layer(
        packet, sizeof(packet), 92, handle, -1, 2, 32, 16, 3, 1, &size) &&
        size == 44 && pxa_load_u64(packet + 20) == handle);
    assert(pxa_surface_build_queue_frame(
        packet, sizeof(packet), handle, 44, &damage, 1, &size) &&
        size == 48 && pxa_load_u16(packet + 40) == 1);
    assert(pxa_parse_event(packet, size, &event) && event.token == 0 &&
           event.payload[16] == 1 && event.payload_size == 28);
    assert(pxa_surface_build_opaque_ui_regions(
        packet, sizeof(packet), 93, handle, &damage, 1, &size) &&
        size == 40 && pxa_load_u16(packet + 32) == 1);
    assert(pxa_surface_build_query_state(packet, sizeof(packet), 94,
                                             handle, &size) && size == 28);
    assert(pxa_surface_write_frame(0, packet, 8) == -1);
    assert(pxa_surface_register_buffers(handle, aligned_buffers + 1,
                                            8, 2) == -1);
    assert(pxa_surface_acquire_buffer(0, packet) == -1);
    assert(pxa_surface_present_buffer(handle, 0, 0) == -1);

    pxa_store_u32(payload, 0);
    pxa_store_u64(payload + 4, handle);
    pxa_store_u32(payload + 12, 64);
    pxa_store_u32(payload + 16, 1024);
    payload[20] = 2;
    event.service = PXA_SURFACE_SERVICE;
    event.opcode = PXA_SURFACE_CREATE;
    event.token = 91;
    event.payload = payload;
    event.payload_size = 24;
    assert(pxa_surface_parse_create(&event, 91, &created) &&
           created.handle == handle && created.frame_bytes == 1024);
    event.payload_size = 23;
    assert(!pxa_surface_parse_create(&event, 91, &created));
    event.opcode = PXA_SURFACE_CONFIGURE_LAYER;
    event.token = 92;
    event.payload_size = 4;
    assert(pxa_surface_parse_status(&event, 92,
                                        PXA_SURFACE_CONFIGURE_LAYER,
                                        &status) && status == 0);

    pxa_store_u64(payload + 4, 11);
    pxa_store_u64(payload + 12, 10);
    pxa_store_u32(payload + 44, 2);
    event.opcode = PXA_SURFACE_QUERY_STATE;
    event.token = 94;
    event.payload_size = 52;
    assert(pxa_surface_parse_state(&event, 94, &state) &&
           state.submitted_frames == 11 && state.presented_frames == 10 &&
           state.free_buffers == 2);

    pxa_store_u64(payload, handle);
    payload[8] = 1;
    payload[9] = payload[10] = payload[11] = 0;
    pxa_store_u64(payload + 12, 44);
    event.opcode = PXA_SURFACE_RELEASED;
    event.token = 0;
    event.payload_size = 20;
    assert(pxa_surface_parse_released(&event, &released) &&
           released.handle == handle && released.buffer_index == 1 &&
           released.frame_id == 44);
    payload[9] = 1;
    assert(!pxa_surface_parse_released(&event, &released));
    return 0;
}
