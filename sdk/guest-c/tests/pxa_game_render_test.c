#include "pxa_raster.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[64];
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

int main(void) {
    uint8_t packet[64];
    uint8_t event_bytes[32] = {0};
    pxa_event_t event;
    pxa_game_render_create_result_t created;
    assert(pxa_game_render_create(7, 148, 120, 3, 1, packet,
                                  sizeof(packet)));
    assert(captured_length == 20 &&
           pxa_read_u16(captured) == PXA_SERVICE_GAME_RENDER &&
           pxa_read_u16(captured + 2) == PXA_GAME_RENDER_CREATE_CONTEXT &&
           pxa_read_u16(captured + 12) == 148 &&
           pxa_read_u16(captured + 14) == 120 && captured[16] == 3 &&
           captured[17] == PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT);

    pxa_game_render_store_u16(event_bytes, PXA_SERVICE_GAME_RENDER);
    pxa_game_render_store_u16(event_bytes + 2,
                              PXA_GAME_RENDER_CREATE_CONTEXT);
    pxa_game_render_store_u32(event_bytes + 4, 7);
    pxa_game_render_store_u32(event_bytes + 8, 20);
    pxa_game_render_store_u32(event_bytes + 12, PXA_STATUS_OK);
    pxa_game_render_store_u32(event_bytes + 16, 9);
    pxa_game_render_store_u32(event_bytes + 20,
                          PXA_RASTER_CAP_SPRITE_BATCH |
                              PXA_RASTER_CAP_TRIANGLE_BATCH);
    pxa_game_render_store_u32(event_bytes + 24, PXA_RASTER_MAX_DRAW_BYTES);
    pxa_game_render_store_u16(event_bytes + 28, 256);
    event_bytes[30] = 16;
    assert(pxa_parse_event(event_bytes, sizeof(event_bytes), &event));
    assert(pxa_game_render_parse_create(&event, &created));
    assert(created.status == PXA_STATUS_OK && created.context_handle == 9 &&
           created.max_draw_bytes == PXA_RASTER_MAX_DRAW_BYTES &&
           created.max_texture_dimension == 256 && created.max_textures == 16);
    return 0;
}
