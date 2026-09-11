#include "pxa_surface.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[128];
static uint32_t captured_length;
static uint32_t io_handle;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(operation == PXA_IO_WRITE && data != NULL);
    io_handle = handle;
    return (int32_t)length;
}

int main(void) {
    uint8_t packet[128];
    uint8_t pixels[32] = {0};
    uint8_t event_bytes[48] = {0};
    pxa_event_t event;
    pxa_surface_create_result_t created;
    pxa_surface_damage_rect_t damage = {0, 0, 4, 4};

    assert(pxa_surface_create_rgb565(7, 4, 4, 2, packet,
                                      sizeof(packet)));
    assert(captured_length == 20 &&
           pxa_read_u16(captured) == PXA_SERVICE_SURFACE &&
           pxa_read_u16(captured + 2) == PXA_SURFACE_CREATE);
    assert(pxa_surface_create_rgb565_direct(9, 4, 4, 2, packet,
                                             sizeof(packet)));
    assert(captured[19] == PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT);
    assert(pxa_surface_create_argb8888_premultiplied(
        8, 4, 4, 2, packet, sizeof(packet)));
    assert(captured_length == 20 && pxa_read_u16(captured + 12) == 4 &&
           pxa_read_u16(captured + 14) == 4 &&
           pxa_read_u16(captured + 16) ==
               PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED &&
           captured[19] == PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA);
    assert(pxa_surface_configure_layer(8, 9, 2, 3, 4, 4, 0, 1, packet,
                                        sizeof(packet)));
    assert(captured_length == 32 && pxa_read_u32(captured + 12) == 9);
    assert(pxa_surface_write_frame(9, pixels, sizeof(pixels)) ==
           (int32_t)sizeof(pixels) && io_handle == 9);
    assert(pxa_surface_queue_frame(9, 42, &damage, 1, packet,
                                    sizeof(packet)) == PXA_STATUS_OK);
    assert(captured_length == 36 &&
           pxa_read_u16(captured + 2) == PXA_SURFACE_QUEUE_FRAME &&
           pxa_read_u64(captured + 16) == 42);

    pxa_surface_store_u16(event_bytes, PXA_SERVICE_SURFACE);
    pxa_surface_store_u16(event_bytes + 2, PXA_SURFACE_CREATE);
    pxa_surface_store_u32(event_bytes + 4, 7);
    pxa_surface_store_u32(event_bytes + 8, 20);
    pxa_surface_store_u32(event_bytes + 16, 9);
    pxa_surface_store_u32(event_bytes + 20, 8);
    pxa_surface_store_u32(event_bytes + 24, 32);
    event_bytes[28] = 2;
    assert(pxa_parse_event(event_bytes, 32, &event));
    assert(pxa_surface_parse_create(&event, &created));
    assert(created.surface_handle == 9 && created.stride_bytes == 8 &&
           created.frame_bytes == 32 && created.buffer_count == 2);
    return 0;
}
