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
    uint8_t draw[512];
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

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 10);
    assert(pxa_raster_solid_coverage_quad(&list, vertices,
                                          UINT16_C(0xf800)));
    assert(pxa_raster_textured_quad_flags(
        &list, vertices, 0, PXA_RASTER_QUAD_PAINTER |
                              PXA_RASTER_QUAD_COVERAGE_MASK));
    assert(pxa_raster_submit(9, &list) == (int32_t)list.length);
    assert((pxa_read_u32(captured + 12) &
            (PXA_RASTER_CAP_COVERAGE_MASK |
             PXA_RASTER_CAP_PAINTER_PERSPECTIVE)) ==
           (PXA_RASTER_CAP_COVERAGE_MASK |
            PXA_RASTER_CAP_PAINTER_PERSPECTIVE));

    {
        static const int16_t xy_q4[8] = {
            -17, 17, 33, 17, 33, 49, -17, 49,
        };
        pxa_raster_sprite_instance_t instance = {
            -3, 3, 5, 7, 9, 11, 13, 15,
        };
        uint32_t offset = PXA_RASTER_DRAW_HEADER_BYTES;
        pxa_raster_draw_list_begin_scaled(&list, draw, sizeof(draw), 10, 1);
        assert(list.status == PXA_STATUS_OK && list.coordinate_shift == 1);
        assert(pxa_raster_flat_quad(&list, xy_q4, 0));
        assert((int16_t)pxa_read_u16(draw + offset + 8) == -9);
        assert((int16_t)pxa_read_u16(draw + offset + 10) == 8);
        offset += PXA_RASTER_FLAT_QUAD_BYTES;
        assert(pxa_raster_textured_quad(&list, vertices, 0));
        assert(pxa_read_u16(draw + offset + 8 + PXA_RASTER_VERTEX_BYTES) ==
               8);
        assert(pxa_read_u16(draw + offset + 8 +
                            PXA_RASTER_VERTEX_BYTES + 4) == 16);
        offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
        assert(pxa_raster_sprite(&list, 0, 0, 0, -3, 3, 5, 7,
                                 9, 11, 13, 15, 0));
        assert((int16_t)pxa_read_u16(draw + offset + 8) == -2);
        assert(pxa_read_u16(draw + offset + 10) == 1);
        assert(pxa_read_u16(draw + offset + 12) == 3);
        assert(pxa_read_u16(draw + offset + 14) == 4);
        assert(pxa_read_u16(draw + offset + 16) == 9);
        offset += PXA_RASTER_SPRITE_BYTES;
        assert(pxa_raster_sprite_batch(
            &list, 0, 0, PXA_RASTER_CAP_SPRITE_BATCH, &instance, 1, 0));
        assert((int16_t)pxa_read_u16(
                   draw + offset + PXA_RASTER_SPRITE_BATCH_HEADER_BYTES) ==
               -2);
        assert(pxa_read_u16(draw + offset +
                            PXA_RASTER_SPRITE_BATCH_HEADER_BYTES + 4) == 3);
        assert(pxa_read_u16(draw + offset +
                            PXA_RASTER_SPRITE_BATCH_HEADER_BYTES + 8) == 9);
    }

    pxa_raster_draw_list_begin_scaled(
        &list, draw, sizeof(draw), 11,
        (uint8_t)(PXA_RASTER_MAX_COORDINATE_SHIFT + 1u));
    assert(list.status == PXA_STATUS_INVALID_ARGUMENT);

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 12);
    assert(pxa_raster_textured_quad_flags(
        &list, vertices, 0, PXA_RASTER_QUAD_TRANSPARENT_INDEX0));
    assert((list.required_capabilities & PXA_RASTER_CAP_DEPTH_CUTOUT) != 0);

    pxa_raster_draw_list_begin(&list, draw, sizeof(draw), 13);
    assert(pxa_raster_textured_quad_flags(
        &list, vertices, 0, PXA_RASTER_QUAD_BLEND_75));
    assert((list.required_capabilities &
            PXA_RASTER_CAP_FIXED_ALPHA_BLEND) != 0);

    pxa_raster_draw_list_begin(&list, draw, PXA_RASTER_DRAW_HEADER_BYTES, 14);
    assert(!pxa_raster_clear(&list, 0));
    assert(list.status == PXA_STATUS_LIMIT_EXCEEDED);
    return 0;
}
