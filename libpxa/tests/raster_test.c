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
}

int main(void) {
    test_upload_validation();
    test_quads_clipping_uv_and_telemetry();
    test_additive_capability_fallback();
    test_perspective_uv_and_solid_depth();
    test_sprite_and_triangle_batches();
    return 0;
}
