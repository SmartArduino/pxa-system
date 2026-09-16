#include "pxa_raster.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[1024];
static uint32_t captured_handle;
static uint32_t captured_operation;
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    (void)data;
    (void)length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(data != NULL && length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_handle = handle;
    captured_operation = operation;
    captured_length = length;
    return (int32_t)length;
}

int main(void) {
    uint16_t palette[256];
    uint8_t upload[532];
    uint8_t draw[128];
    pxa_raster_draw_list_t list;
    pxa_raster_vertex_t vertices[4] = {
        {0, 0, 0, 0, 255, 256},
        {16, 0, 16, 0, 255, 256},
        {16, 16, 16, 16, 255, 512},
        {0, 16, 0, 16, 255, 512},
    };
    unsigned index;

    for (index = 0; index < 256; ++index) palette[index] = (uint16_t)index;
    assert(pxa_raster_upload_palette_rgb565(9, palette, upload,
                                             sizeof(upload)) ==
           (int32_t)sizeof(upload));
    assert(captured_handle == 9 &&
           captured_operation == PXA_GAME_RENDER_IO_UPLOAD &&
           captured_length == sizeof(upload) &&
           pxa_read_u32(captured) == PXA_RASTER_UPLOAD_MAGIC &&
           pxa_read_u16(captured + 20 + 510) == 255);

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 7);
    assert(pxa_raster_clear(&list, 0x1234));
    assert(pxa_raster_sprite(
        &list, 1,
        PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
            PXA_RASTER_SPRITE_ADDITIVE,
        0, 2, 3, 4, 5, 6, 7, 8, 9, 0));
    assert((draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES + 1] &
            PXA_RASTER_SPRITE_ADDITIVE) == 0);
    assert(pxa_raster_submit(9, &list) == (int32_t)list.length);
    assert(captured_operation == PXA_GAME_RENDER_IO_SUBMIT &&
           pxa_read_u32(captured + 12) == 0 &&
           pxa_read_u64(captured + 20) == 7);

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 8);
    assert(pxa_raster_sprite(&list, 1, PXA_RASTER_SPRITE_ADDITIVE,
                             PXA_RASTER_CAP_ADDITIVE_SPRITE,
                             0, 0, 1, 1, 0, 0, 1, 1, 0));
    assert(pxa_raster_submit(9, &list) == (int32_t)list.length);
    assert((pxa_read_u32(captured + 12) &
            PXA_RASTER_CAP_ADDITIVE_SPRITE) != 0 &&
           (captured[PXA_RASTER_DRAW_HEADER_BYTES + 1] &
            PXA_RASTER_SPRITE_ADDITIVE) != 0);

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 9);
    assert(pxa_raster_solid_depth_quad(&list, vertices, UINT16_C(0xbeef)));
    assert(pxa_raster_submit(9, &list) == (int32_t)list.length);
    assert((captured[PXA_RASTER_DRAW_HEADER_BYTES + 1] &
            PXA_RASTER_QUAD_SOLID_COLOR) != 0);
    assert(pxa_read_u16(captured + PXA_RASTER_DRAW_HEADER_BYTES + 6) ==
           UINT16_C(0xbeef));
    assert(pxa_read_u16(captured + PXA_RASTER_DRAW_HEADER_BYTES + 8 + 10) ==
           256);

    pxa_raster_draw_list_begin(&list, draw, PXA_RASTER_DRAW_HEADER_BYTES, 10);
    assert(!pxa_raster_clear(&list, 0));
    assert(list.status == PXA_STATUS_LIMIT_EXCEEDED);
    return 0;
}
