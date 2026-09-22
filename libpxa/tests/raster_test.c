#include "pxa/raster.h"
#include "pxa/wire.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void put_u64(uint8_t *bytes, uint64_t value) {
    uint8_t index;
    for (index = 0; index < 8; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8u));
}

static void put_vertex_depth(uint8_t *bytes, int16_t x, int16_t y, int16_t u,
                             int16_t v, uint8_t light, uint16_t depth) {
    put_u16(bytes, (uint16_t)x);
    put_u16(bytes + 2, (uint16_t)y);
    put_u16(bytes + 4, (uint16_t)u);
    put_u16(bytes + 6, (uint16_t)v);
    bytes[8] = light;
    bytes[9] = 0;
    put_u16(bytes + 10, depth);
}

static void put_vertex(uint8_t *bytes, int16_t x, int16_t y, int16_t u,
                       int16_t v, uint8_t light) {
    put_vertex_depth(bytes, x, y, u, v, light, 256);
}

static uint32_t begin_list(uint8_t *bytes, uint32_t required,
                           uint32_t commands, uint64_t frame_id,
                           uint32_t total) {
    memset(bytes, 0, total);
    put_u32(bytes, PXA_RASTER_DRAW_MAGIC);
    put_u16(bytes + 4, PXA_RASTER_ABI_MAJOR);
    put_u16(bytes + 6, PXA_RASTER_ABI_MINOR);
    put_u32(bytes + 8, total);
    put_u32(bytes + 12, required);
    put_u32(bytes + 16, commands);
    put_u64(bytes + 20, frame_id);
    return PXA_RASTER_DRAW_HEADER_BYTES;
}

static void test_upload_validation(void) {
    uint8_t upload[PXA_RASTER_UPLOAD_HEADER_BYTES + 4] = {0};
    pxa_raster_upload_view_t view;
    put_u32(upload, PXA_RASTER_UPLOAD_MAGIC);
    put_u16(upload + 4, PXA_RASTER_ABI_MAJOR);
    upload[8] = PXA_RASTER_UPLOAD_TEXTURE_INDEX8;
    upload[9] = 3;
    put_u16(upload + 12, 2);
    put_u16(upload + 14, 2);
    put_u32(upload + 16, 4);
    assert(pxa_raster_decode_upload(upload, sizeof(upload), &view) ==
           PXA_STATUS_OK);
    assert(view.kind == PXA_RASTER_UPLOAD_TEXTURE_INDEX8);
    assert(view.slot == 3 && view.width == 2 && view.height == 2);
    upload[10] = 1;
    assert(pxa_raster_decode_upload(upload, sizeof(upload), &view) ==
           PXA_STATUS_PROTOCOL_ERROR);
    {
        uint8_t lit[PXA_RASTER_UPLOAD_HEADER_BYTES + 2u * 256u * 2u] = {0};
        put_u32(lit, PXA_RASTER_UPLOAD_MAGIC);
        put_u16(lit + 4, PXA_RASTER_ABI_MAJOR);
        put_u16(lit + 6, PXA_RASTER_ABI_MINOR);
        lit[8] = PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565;
        put_u16(lit + 12, 256);
        put_u16(lit + 14, 2);
        put_u32(lit + 16, 2u * 256u * 2u);
        assert(pxa_raster_decode_upload(lit, sizeof(lit), &view) ==
               PXA_STATUS_OK);
        assert(view.height == 2 && view.payload_bytes == 1024);
    }
}

static void put_painter_quad(uint8_t *record, uint8_t flags,
                             uint8_t texture_slot, uint8_t palette_index,
                             uint8_t light) {
    static const int16_t xy[8] = {0, 0, 128, 0, 128, 128, 0, 128};
    uint8_t vertex;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    record[1] = flags;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    record[4] = texture_slot;
    put_u16(record + 6, palette_index);
    for (vertex = 0; vertex < 4; ++vertex)
        put_vertex_depth(record + 8 + vertex * PXA_RASTER_VERTEX_BYTES,
                         xy[vertex * 2], xy[vertex * 2 + 1], 0, 0, light, 0);
}

static void test_painter_lit_palette_and_transparency(void) {
    enum {
        COMMANDS = 4,
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES +
                      COMMANDS * PXA_RASTER_TEXTURED_QUAD_BYTES,
    };
    uint8_t bytes[TOTAL_BYTES];
    uint8_t transparent_texel = 0;
    uint8_t blend_texel = 7;
    uint16_t palette[2 * 256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    palette[7] = UINT16_C(0xf800);
    palette[256 + 7] = UINT16_C(0x07e0);
    memset(pixels, 0, sizeof(pixels));
    memset(depth, 0x5a, sizeof(depth));
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.palette_light_levels = 2;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD |
                             PXA_RASTER_CAP_PAINTER_POLYGON |
                             PXA_RASTER_CAP_FIXED_ALPHA_BLEND;
    resources.textures[0].pixels = &transparent_texel;
    resources.textures[0].width = 1;
    resources.textures[0].height = 1;
    resources.textures[1].pixels = &blend_texel;
    resources.textures[1].width = 1;
    resources.textures[1].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes, resources.capabilities, COMMANDS, 11,
                        sizeof(bytes));
    put_painter_quad(bytes + offset,
                     PXA_RASTER_QUAD_SOLID_COLOR |
                         PXA_RASTER_QUAD_PAINTER,
                     0, 7, 1);
    offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    put_painter_quad(bytes + offset,
                     PXA_RASTER_QUAD_SOLID_COLOR |
                         PXA_RASTER_QUAD_PAINTER,
                     0, 7, 0);
    offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    put_painter_quad(bytes + offset,
                     PXA_RASTER_QUAD_PAINTER |
                         PXA_RASTER_QUAD_TRANSPARENT_INDEX0,
                     0, 0, 1);
    offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    put_painter_quad(bytes + offset,
                     PXA_RASTER_QUAD_PAINTER |
                         PXA_RASTER_QUAD_BLEND_75,
                     1, 0, 1);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    for (offset = 0; offset < 8u * 8u; ++offset) {
        assert(pixels[offset] == UINT16_C(0x3dc0));
        assert(depth[offset] == UINT16_C(0x5a5a));
    }
}

static void test_painter_triangle(void) {
    enum {
        RECORD_BYTES = PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                       3 * PXA_RASTER_VERTEX_BYTES,
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES + RECORD_BYTES,
    };
    uint8_t bytes[TOTAL_BYTES];
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8] = {0};
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint8_t *record;
    palette[3] = UINT16_C(0x1234);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.palette_light_levels = 1;
    resources.capabilities = PXA_RASTER_CAP_TRIANGLE_BATCH |
                             PXA_RASTER_CAP_PAINTER_POLYGON;
    target.pixels = pixels;
    target.stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    record = bytes + begin_list(bytes, resources.capabilities, 1, 12,
                                sizeof(bytes));
    record[0] = PXA_RASTER_RECORD_TRIANGLE_BATCH;
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR | PXA_RASTER_QUAD_PAINTER;
    put_u16(record + 2, RECORD_BYTES);
    put_u16(record + 6, 3);
    put_u16(record + 8, 1);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES,
                     0, 0, 0, 0, 0, 0);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES + 12,
                     128, 0, 0, 0, 0, 0);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES + 24,
                     0, 128, 0, 0, 0, 0);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    assert(pixels[0] == UINT16_C(0x1234));
    assert(pixels[7 * 8 + 7] == 0);
}

static void test_quads_clipping_uv_and_telemetry(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                  PXA_RASTER_TEXTURED_QUAD_BYTES +
                  PXA_RASTER_FLAT_QUAD_BYTES];
    uint8_t texture[4] = {1, 2, 3, 4};
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    pxa_raster_telemetry_t telemetry;
    uint32_t offset;
    uint8_t *record;
    palette[1] = UINT16_C(0xf800);
    palette[2] = UINT16_C(0x07e0);
    palette[3] = UINT16_C(0x001f);
    palette[4] = UINT16_C(0xffff);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_FLAT_QUAD |
                             PXA_RASTER_CAP_TEXTURED_QUAD;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 2;
    resources.textures[0].height = 2;
    target.pixels = pixels;
    target.stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes,
                        PXA_RASTER_CAP_FLAT_QUAD |
                            PXA_RASTER_CAP_TEXTURED_QUAD,
                        3, 7, sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    put_u16(record + 4, UINT16_C(0x1234));
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    record[4] = 0;
    put_vertex(record + 8, 0, 0, 0, 0, 255);
    put_vertex(record + 20, 64, 0, 32, 0, 255);
    put_vertex(record + 32, 64, 64, 32, 32, 255);
    put_vertex(record + 44, 0, 64, 0, 32, 255);
    offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_FLAT_QUAD;
    put_u16(record + 2, PXA_RASTER_FLAT_QUAD_BYTES);
    put_u16(record + 4, UINT16_C(0xabcd));
    put_u16(record + 8, (uint16_t)-32);
    put_u16(record + 10, 64);
    put_u16(record + 12, 32);
    put_u16(record + 14, 64);
    put_u16(record + 16, 32);
    put_u16(record + 18, 160);
    put_u16(record + 20, (uint16_t)-32);
    put_u16(record + 22, 160);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    memset(&telemetry, 0, sizeof(telemetry));
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources,
                                 &telemetry);
    assert(pixels[0] == UINT16_C(0xf800));
    assert(pixels[3] == UINT16_C(0x07e0));
    assert(pixels[3 * 8] == UINT16_C(0x001f));
    assert(pixels[2 * 8 + 6] == UINT16_C(0x1234));
    assert(pixels[5 * 8] == UINT16_C(0xabcd));
    assert(telemetry.clear_commands == 1);
    assert(telemetry.textured_quad_commands == 1);
    assert(telemetry.flat_quad_commands == 1);
    assert(telemetry.last_draw_list_bytes == sizeof(bytes));
    assert(telemetry.last_covered_pixels >= 64);
}

static void test_additive_capability_fallback(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_SPRITE_BYTES];
    uint8_t texture = 1;
    uint16_t palette[256] = {0};
    uint16_t pixel = UINT16_C(0x7bef);
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint8_t *record;
    palette[1] = UINT16_C(0x8410);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.textures[0].pixels = &texture;
    resources.textures[0].width = 1;
    resources.textures[0].height = 1;
    target.pixels = &pixel;
    target.stride_pixels = 1;
    target.width = 1;
    target.height = 1;
    begin_list(bytes, PXA_RASTER_CAP_ADDITIVE_SPRITE, 1, 1,
               sizeof(bytes));
    record = bytes + PXA_RASTER_DRAW_HEADER_BYTES;
    record[0] = PXA_RASTER_RECORD_SPRITE;
    record[1] = PXA_RASTER_SPRITE_ADDITIVE |
                PXA_RASTER_SPRITE_SOLID_COLOR;
    put_u16(record + 2, PXA_RASTER_SPRITE_BYTES);
    put_u16(record + 6, UINT16_C(0x8410));
    put_u16(record + 12, 1);
    put_u16(record + 14, 1);
    put_u16(record + 20, 1);
    put_u16(record + 22, 1);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
    resources.capabilities = PXA_RASTER_CAP_ADDITIVE_SPRITE;
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    assert(pixel == UINT16_C(0xffff));
}

static void test_sprite_scaling_and_clipping(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_SPRITE_BYTES];
    uint8_t texture[3 * 2] = {1, 2, 3, 4, 5, 6};
    uint16_t palette[256] = {0};
    uint16_t pixels[5 * 4] = {0};
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint8_t *record;
    uint32_t x;
    uint32_t y;
    for (x = 1; x <= 6; ++x) palette[x] = (uint16_t)(0x1000u + x);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 3;
    resources.textures[0].height = 2;
    target.pixels = pixels;
    target.stride_pixels = 5;
    target.width = 5;
    target.height = 4;
    begin_list(bytes, 0, 1, 2, sizeof(bytes));
    record = bytes + PXA_RASTER_DRAW_HEADER_BYTES;
    record[0] = PXA_RASTER_RECORD_SPRITE;
    put_u16(record + 2, PXA_RASTER_SPRITE_BYTES);
    put_u16(record + 8, (uint16_t)-2);
    put_u16(record + 10, (uint16_t)-1);
    put_u16(record + 12, 9);
    put_u16(record + 14, 6);
    put_u16(record + 20, 3);
    put_u16(record + 22, 2);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    for (y = 0; y < target.height; ++y) {
        const uint32_t source_y = (y + 1u) * 2u / 6u;
        for (x = 0; x < target.width; ++x) {
            const uint32_t source_x = (x + 2u) * 3u / 9u;
            const uint8_t texel = texture[source_y * 3u + source_x];
            assert(pixels[y * target.stride_pixels + x] == palette[texel]);
        }
    }
}

static void test_sprite_scaling_ratios(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_SPRITE_BYTES];
    uint8_t texture[16];
    uint16_t palette[256] = {0};
    uint16_t pixels[31];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint8_t *record;
    uint32_t output_width;
    uint32_t x;
    for (x = 0; x < 16; ++x) {
        texture[x] = (uint8_t)(x + 1u);
        palette[x + 1u] = (uint16_t)(0x2000u + x);
    }
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 16;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.stride_pixels = 31;
    target.width = 31;
    target.height = 1;
    begin_list(bytes, 0, 1, 3, sizeof(bytes));
    record = bytes + PXA_RASTER_DRAW_HEADER_BYTES;
    record[0] = PXA_RASTER_RECORD_SPRITE;
    put_u16(record + 2, PXA_RASTER_SPRITE_BYTES);
    put_u16(record + 14, 1);
    put_u16(record + 20, 16);
    put_u16(record + 22, 1);
    for (output_width = 1; output_width <= 31; ++output_width) {
        memset(pixels, 0, sizeof(pixels));
        put_u16(record + 12, (uint16_t)output_width);
        assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                             &resources, &list) ==
               PXA_STATUS_OK);
        pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
        for (x = 0; x < output_width; ++x) {
            const uint32_t source_x = x * 16u / output_width;
            assert(pixels[x] == palette[texture[source_x]]);
        }
        for (; x < target.width; ++x) assert(pixels[x] == 0);
    }
}

static void test_perspective_uv_and_solid_depth(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                  PXA_RASTER_TEXTURED_QUAD_BYTES * 2u];
    uint8_t texture[4] = {1, 2, 3, 4};
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    uint8_t *record;
    palette[1] = UINT16_C(0x1111);
    palette[2] = UINT16_C(0x2222);
    palette[3] = UINT16_C(0x3333);
    palette[4] = UINT16_C(0x4444);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 1;
    resources.textures[0].height = 4;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes, PXA_RASTER_CAP_TEXTURED_QUAD, 3, 9,
                        sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    put_vertex_depth(record + 8, 0, 0, 0, 0, 255, 256);
    put_vertex_depth(record + 20, 128, 0, 0, 0, 255, 256);
    put_vertex_depth(record + 32, 128, 128, 0, 64, 255, 1024);
    put_vertex_depth(record + 44, 0, 128, 0, 64, 255, 1024);
    offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    put_u16(record + 6, UINT16_C(0xbeef));
    put_vertex_depth(record + 8, 0, 0, 0, 0, 255, 768);
    put_vertex_depth(record + 20, 128, 0, 0, 0, 255, 768);
    put_vertex_depth(record + 32, 128, 128, 0, 0, 255, 768);
    put_vertex_depth(record + 44, 0, 128, 0, 0, 255, 768);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    assert(pixels[4 * 8 + 4] == palette[1]);
    assert(pixels[7 * 8 + 4] == UINT16_C(0xbeef));
    assert(depth[1 * 8 + 4] > depth[7 * 8 + 4]);
}

static void test_affine_uv_flag_and_depth(void) {
    uint8_t bytes[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                  PXA_RASTER_TEXTURED_QUAD_BYTES];
    uint8_t texture[4] = {1, 2, 3, 4};
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    uint8_t *record;
    uint32_t x;
    palette[1] = UINT16_C(0x1001);
    palette[2] = UINT16_C(0x2002);
    palette[3] = UINT16_C(0x3003);
    palette[4] = UINT16_C(0x4004);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 4;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes, PXA_RASTER_CAP_TEXTURED_QUAD, 2, 11,
                        sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    record[1] = PXA_RASTER_QUAD_AFFINE_UV;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    put_vertex_depth(record + 8, 0, 0, 0, 0, 255, 256);
    put_vertex_depth(record + 20, 128, 0, 64, 0, 255, 1024);
    put_vertex_depth(record + 32, 128, 128, 64, 0, 255, 1024);
    put_vertex_depth(record + 44, 0, 128, 0, 0, 255, 256);
    /* The Host rejects the flag unless it advertised the capability. */
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD |
                             PXA_RASTER_CAP_AFFINE_UV;
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    /* Screen-linear UV: the 4 texel row is stretched to 8 pixels, so the
     * far end of the depth ramp does not compress the texels. */
    for (x = 0; x < 8; ++x)
        assert(pixels[4 * 8 + x] == palette[texture[x / 2u]]);
    assert(depth[4 * 8] > depth[4 * 8 + 7]);
    /* A solid depth-only quad still validates and rejects affine. */
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR | PXA_RASTER_QUAD_AFFINE_UV;
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_PROTOCOL_ERROR);
}

static void test_lit_palette_depth(void) {
    enum {
        COMMANDS = 3,
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                      2 * PXA_RASTER_TEXTURED_QUAD_BYTES,
    };
    static const int16_t xy[8] = {0, 0, 128, 0, 128, 128, 0, 128};
    uint8_t bytes[TOTAL_BYTES];
    uint8_t texture = 7;
    uint16_t palette[2 * 256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    uint8_t *record;
    uint8_t vertex;
    palette[7] = UINT16_C(0xf800);
    palette[256 + 7] = UINT16_C(0x07e0);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.palette_light_levels = 2;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD |
                             PXA_RASTER_CAP_AFFINE_UV |
                             PXA_RASTER_CAP_LIT_PALETTE_DEPTH;
    resources.textures[0].pixels = &texture;
    resources.textures[0].width = 1;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes, resources.capabilities, COMMANDS, 13,
                        sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    offset += PXA_RASTER_CLEAR_BYTES;
    /* Draw the near green quad first, then a far red quad. Depth must retain
     * the green result even though list order is deliberately wrong. */
    for (uint8_t quad = 0; quad < 2; ++quad) {
        record = bytes + offset;
        record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
        record[1] = PXA_RASTER_QUAD_AFFINE_UV |
                    PXA_RASTER_QUAD_LIT_PALETTE;
        put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
        for (vertex = 0; vertex < 4; ++vertex) {
            put_vertex_depth(record + 8 + vertex * PXA_RASTER_VERTEX_BYTES,
                             xy[vertex * 2], xy[vertex * 2 + 1], 0, 0,
                             quad == 0 ? 1 : 0, quad == 0 ? 256 : 512);
        }
        offset += PXA_RASTER_TEXTURED_QUAD_BYTES;
    }
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    for (offset = 0; offset < 8u * 8u; ++offset)
        assert(pixels[offset] == UINT16_C(0x07e0));
    put_u16(bytes + 6, 3);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
}

static void test_depth_cutout(void) {
    enum {
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                      PXA_RASTER_TEXTURED_QUAD_BYTES,
    };
    uint8_t bytes[TOTAL_BYTES];
    uint8_t texture[2] = {0, 1};
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    uint8_t *record;
    static const int16_t xy[8] = {0, 0, 128, 0, 128, 128, 0, 128};
    static const int16_t uv[8] = {0, 0, 32, 0, 32, 0, 0, 0};
    palette[1] = UINT16_C(0x07e0);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD;
    resources.textures[0].pixels = texture;
    resources.textures[0].width = 2;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes,
                        PXA_RASTER_CAP_TEXTURED_QUAD |
                            PXA_RASTER_CAP_DEPTH_CUTOUT,
                        2, 14, sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    put_u16(record + 4, UINT16_C(0x1234));
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    record[1] = PXA_RASTER_QUAD_TRANSPARENT_INDEX0;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    for (uint8_t vertex = 0; vertex < 4; ++vertex)
        put_vertex_depth(record + 8 + vertex * PXA_RASTER_VERTEX_BYTES,
                         xy[vertex * 2], xy[vertex * 2 + 1],
                         uv[vertex * 2], uv[vertex * 2 + 1], 255, 256);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
    resources.capabilities |= PXA_RASTER_CAP_DEPTH_CUTOUT;
    put_u16(bytes + 6, 4);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
    put_u16(bytes + 6, PXA_RASTER_ABI_MINOR);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    assert(pixels[4 * 8 + 1] == UINT16_C(0x1234));
    assert(depth[4 * 8 + 1] == 0);
    assert(pixels[4 * 8 + 6] == UINT16_C(0x07e0));
    assert(depth[4 * 8 + 6] != 0);
}

static void test_fixed_alpha_blend(void) {
    enum {
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                      PXA_RASTER_TEXTURED_QUAD_BYTES,
    };
    uint8_t bytes[TOTAL_BYTES];
    uint8_t texture = 1;
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    uint32_t offset;
    uint8_t *record;
    palette[1] = UINT16_C(0x001f);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD;
    resources.textures[0].pixels = &texture;
    resources.textures[0].width = 1;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes,
                        PXA_RASTER_CAP_TEXTURED_QUAD |
                            PXA_RASTER_CAP_FIXED_ALPHA_BLEND,
                        2, 15, sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    put_u16(record + 4, UINT16_C(0xf800));
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TEXTURED_QUAD;
    record[1] = PXA_RASTER_QUAD_BLEND_75;
    put_u16(record + 2, PXA_RASTER_TEXTURED_QUAD_BYTES);
    put_vertex_depth(record + 8, 0, 0, 0, 0, 255, 256);
    put_vertex_depth(record + 20, 128, 0, 0, 0, 255, 256);
    put_vertex_depth(record + 32, 128, 128, 0, 0, 255, 256);
    put_vertex_depth(record + 44, 0, 128, 0, 0, 255, 256);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) ==
           PXA_STATUS_UNSUPPORTED);
    resources.capabilities |= PXA_RASTER_CAP_FIXED_ALPHA_BLEND;
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources, NULL);
    for (offset = 0; offset < 8u * 8u; ++offset) {
        assert(pixels[offset] == UINT16_C(0x3816));
        assert(depth[offset] == 0);
    }
}

static void test_sprite_and_triangle_batches(void) {
    enum {
        SPRITE_RECORD_BYTES = PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
                              2 * PXA_RASTER_SPRITE_INSTANCE_BYTES,
        TRIANGLE_RECORD_BYTES = PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                                3 * PXA_RASTER_VERTEX_BYTES,
        TOTAL_BYTES = PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                      SPRITE_RECORD_BYTES + TRIANGLE_RECORD_BYTES,
    };
    uint8_t bytes[TOTAL_BYTES];
    uint8_t texture = 1;
    uint16_t palette[256] = {0};
    uint16_t pixels[8 * 8];
    uint16_t depth[8 * 8];
    uint16_t split_pixels[8 * 8];
    uint16_t split_depth[8 * 8];
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    pxa_raster_telemetry_t telemetry = {0};
    uint32_t offset;
    uint8_t *record;
    uint8_t *instance;
    palette[1] = UINT16_C(0x07e0);
    memset(&resources, 0, sizeof(resources));
    memset(&target, 0, sizeof(target));
    resources.palette = palette;
    resources.capabilities = PXA_RASTER_CAP_SPRITE_BATCH |
                             PXA_RASTER_CAP_TRIANGLE_BATCH;
    resources.textures[0].pixels = &texture;
    resources.textures[0].width = 1;
    resources.textures[0].height = 1;
    target.pixels = pixels;
    target.depth_pixels = depth;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.width = 8;
    target.height = 8;
    offset = begin_list(bytes, resources.capabilities, 3, 10, sizeof(bytes));
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    put_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    offset += PXA_RASTER_CLEAR_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_SPRITE_BATCH;
    put_u16(record + 2, SPRITE_RECORD_BYTES);
    put_u16(record + 8, 2);
    instance = record + PXA_RASTER_SPRITE_BATCH_HEADER_BYTES;
    put_u16(instance + 4, 2);
    put_u16(instance + 6, 2);
    put_u16(instance + 12, 1);
    put_u16(instance + 14, 1);
    instance += PXA_RASTER_SPRITE_INSTANCE_BYTES;
    put_u16(instance, 32);
    put_u16(instance + 4, 2);
    put_u16(instance + 6, 2);
    put_u16(instance + 12, 1);
    put_u16(instance + 14, 1);
    offset += SPRITE_RECORD_BYTES;
    record = bytes + offset;
    record[0] = PXA_RASTER_RECORD_TRIANGLE_BATCH;
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR;
    put_u16(record + 2, TRIANGLE_RECORD_BYTES);
    put_u16(record + 6, UINT16_C(0xf800));
    put_u16(record + 8, 1);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES,
                     0, 64, 0, 0, 255, 256);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES + 12,
                     64, 64, 0, 0, 255, 256);
    put_vertex_depth(record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES + 24,
                     64, 0, 0, 0, 255, 256);
    assert(pxa_raster_validate_draw_list(bytes, sizeof(bytes), &target,
                                         &resources, &list) == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(bytes, &list, &target, &resources,
                                 &telemetry);
    assert(telemetry.sprite_commands == 2 &&
           telemetry.textured_quad_commands == 1 &&
           telemetry.last_draw_list_bytes == sizeof(bytes));
    assert(pixels[0] == palette[1] && pixels[3 * 8 + 3] == UINT16_C(0xf800));
    memset(split_pixels, 0xff, sizeof(split_pixels));
    memset(split_depth, 0xff, sizeof(split_depth));
    target.pixels = split_pixels;
    target.depth_pixels = split_depth;
    pxa_raster_execute_draw_list_rows(bytes, &list, &target, &resources, 0, 3,
                                      NULL);
    pxa_raster_execute_draw_list_rows(bytes, &list, &target, &resources, 3, 8,
                                      NULL);
    assert(memcmp(split_pixels, pixels, sizeof(pixels)) == 0);
    assert(memcmp(split_depth, depth, sizeof(depth)) == 0);
}

int main(void) {
    test_upload_validation();
    test_painter_lit_palette_and_transparency();
    test_painter_triangle();
    test_quads_clipping_uv_and_telemetry();
    test_additive_capability_fallback();
    test_sprite_scaling_and_clipping();
    test_sprite_scaling_ratios();
    test_perspective_uv_and_solid_depth();
    test_affine_uv_flag_and_depth();
    test_lit_palette_depth();
    test_depth_cutout();
    test_fixed_alpha_blend();
    test_sprite_and_triangle_batches();
    return 0;
}
