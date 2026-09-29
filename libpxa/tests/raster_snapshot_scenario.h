/* Shared behavior test for the actual ESP and desktop GameRender backends. */
#include "pxa/game_render.h"
#include "pxa/wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef void (*snapshot_present_fn)(void *context, uint64_t id, uint16_t color);

static void snapshot_upload_header(uint8_t *bytes, uint8_t kind,
                                    uint16_t width, uint16_t height) {
    memset(bytes, 0, PXA_RASTER_UPLOAD_HEADER_BYTES);
    pxa_write_u32(bytes, PXA_RASTER_UPLOAD_MAGIC);
    pxa_write_u16(bytes + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(bytes + 6, PXA_RASTER_ABI_MINOR);
    bytes[8] = kind;
    pxa_write_u16(bytes + 12, width);
    pxa_write_u16(bytes + 14, height);
    pxa_write_u32(bytes + 16, (uint32_t)width * height *
        (kind == PXA_RASTER_UPLOAD_TEXTURE_INDEX8 ? 1u : 2u));
}

static void *snapshot_allocate(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}
static void snapshot_free(void *context, void *memory) {
    (void)context;
    free(memory);
}

static void raster_used_resources_scenario(
    const pxa_game_render_backend_t *backend, snapshot_present_fn present,
    void *present_context) {
    pxa_game_render_desc_t desc = {4, 4, 3, 0, PXA_GAME_RENDER_SCRATCH_NONE, 4096};
    pxa_raster_bindings_t replacement = {0}, empty = {0};
    pxa_raster_asset_t *texture, *unused, *palette;
    uint64_t surface;
    uint32_t capabilities;
    const uint64_t mask = UINT64_C(1) | (UINT64_C(1) << 47);
    uint8_t texture_bytes[PXA_RASTER_UPLOAD_HEADER_BYTES + 16];
    uint8_t palette_bytes[PXA_RASTER_UPLOAD_HEADER_BYTES + 512] = {0};
    uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_SPRITE_BYTES] = {0};
    uint8_t *record = draw + PXA_RASTER_DRAW_HEADER_BYTES;
    assert(backend->create(backend->context, &desc, &surface, &capabilities) == 0);
    snapshot_upload_header(texture_bytes, PXA_RASTER_UPLOAD_TEXTURE_INDEX8, 4, 4);
    memset(texture_bytes + PXA_RASTER_UPLOAD_HEADER_BYTES, 1, 16);
    snapshot_upload_header(palette_bytes, PXA_RASTER_UPLOAD_PALETTE_RGB565, 256, 1);
    assert(pxa_raster_asset_from_upload(texture_bytes, sizeof(texture_bytes),
        snapshot_allocate, snapshot_free, NULL, &texture) == 0);
    assert(pxa_raster_asset_from_upload(texture_bytes, sizeof(texture_bytes),
        snapshot_allocate, snapshot_free, NULL, &unused) == 0);
    assert(pxa_raster_asset_from_upload(palette_bytes, sizeof(palette_bytes),
        snapshot_allocate, snapshot_free, NULL, &palette) == 0);
    replacement.textures[0] = texture;
    replacement.textures[47] = unused;
    replacement.palette = palette;
    assert(backend->bind_assets(backend->context, surface, &replacement, mask, 1) == 0);
    pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC);
    pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR);
    pxa_write_u32(draw + 8, PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES);
    pxa_write_u32(draw + 16, 1);
    pxa_write_u64(draw + 20, 1);
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    pxa_write_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    pxa_write_u16(record + 4, 0x001f);
    assert(backend->submit(backend->context, surface, draw,
        PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES) == 0);
    /* An in-flight loading screen must not prevent eviction of old assets. */
    assert(pxa_raster_asset_reference_count(texture) == 2);
    assert(pxa_raster_asset_reference_count(unused) == 2);
    assert(pxa_raster_asset_reference_count(palette) == 2);
    assert(backend->bind_assets(backend->context, surface, &empty, mask, 1) == 0);
    assert(pxa_raster_asset_reference_count(texture) == 1);
    assert(pxa_raster_asset_reference_count(unused) == 1);
    assert(pxa_raster_asset_reference_count(palette) == 1);
    present(present_context, 1, 0x001f);
    assert(backend->bind_assets(backend->context, surface, &replacement, mask, 1) == 0);
    memset(record, 0, PXA_RASTER_SPRITE_BYTES);
    pxa_write_u32(draw + 8, sizeof(draw));
    pxa_write_u64(draw + 20, 2);
    record[0] = PXA_RASTER_RECORD_SPRITE;
    record[1] = PXA_RASTER_SPRITE_SOLID_COLOR;
    pxa_write_u16(record + 2, PXA_RASTER_SPRITE_BYTES);
    pxa_write_u16(record + 6, 0xf800);
    pxa_write_u16(record + 12, 4);
    pxa_write_u16(record + 14, 4);
    pxa_write_u16(record + 20, 4);
    pxa_write_u16(record + 22, 4);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    assert(pxa_raster_asset_reference_count(texture) == 3);
    assert(pxa_raster_asset_reference_count(unused) == 2);
    assert(pxa_raster_asset_reference_count(palette) == 2);
    assert(backend->bind_assets(backend->context, surface, &empty, mask, 1) == 0);
    pxa_raster_asset_release(unused);
    pxa_raster_asset_release(palette);
    present(present_context, 2, 0xf800);
    assert(pxa_raster_asset_reference_count(texture) == 1);
    pxa_raster_asset_release(texture);
    backend->close(backend->context, surface);
}

static void raster_snapshot_scenario(const pxa_game_render_backend_t *backend,
                                      snapshot_present_fn present,
                                      void *present_context) {
    pxa_game_render_desc_t desc = {4, 4, 3, 0, PXA_GAME_RENDER_SCRATCH_NONE, 4096};
    uint64_t surface;
    uint32_t capabilities;
    uint8_t texture[PXA_RASTER_UPLOAD_HEADER_BYTES + 16] = {0};
    uint8_t palette[PXA_RASTER_UPLOAD_HEADER_BYTES + 512] = {0};
    uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                 PXA_RASTER_SPRITE_BYTES] = {0};
    uint8_t *record = draw + PXA_RASTER_DRAW_HEADER_BYTES;
    assert(backend->create(backend->context, &desc, &surface, &capabilities) == 0);
    snapshot_upload_header(texture, PXA_RASTER_UPLOAD_TEXTURE_INDEX8, 4, 4);
    memset(texture + PXA_RASTER_UPLOAD_HEADER_BYTES, 1, 16);
    snapshot_upload_header(palette, PXA_RASTER_UPLOAD_PALETTE_RGB565, 256, 1);
    pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 2, 0xf800);
    assert(backend->upload(backend->context, surface, texture, sizeof(texture)) == 0);
    assert(backend->upload(backend->context, surface, palette, sizeof(palette)) == 0);
    pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC);
    pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR);
    pxa_write_u32(draw + 8, sizeof(draw));
    pxa_write_u32(draw + 16, 2);
    pxa_write_u64(draw + 20, 1);
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    pxa_write_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    record += PXA_RASTER_CLEAR_BYTES;
    record[0] = PXA_RASTER_RECORD_SPRITE;
    pxa_write_u16(record + 2, PXA_RASTER_SPRITE_BYTES);
    pxa_write_u16(record + 12, 4);
    pxa_write_u16(record + 14, 4);
    pxa_write_u16(record + 20, 4);
    pxa_write_u16(record + 22, 4);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    /* Old pending frame must retain the old palette and texture. */
    pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 2, 0x07e0);
    pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 4, 0x001f);
    assert(backend->upload(backend->context, surface, palette, sizeof(palette)) == 0);
    memset(texture + PXA_RASTER_UPLOAD_HEADER_BYTES, 2, 16);
    assert(backend->upload(backend->context, surface, texture, sizeof(texture)) == 0);
    present(present_context, 1, 0xf800);
    pxa_write_u64(draw + 20, 2);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    present(present_context, 2, 0x001f);
    /* Dropping a pending frame releases its old asset references. */
    pxa_write_u64(draw + 20, 3);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    memset(texture + PXA_RASTER_UPLOAD_HEADER_BYTES, 1, 16);
    assert(backend->upload(backend->context, surface, texture, sizeof(texture)) == 0);
    pxa_write_u64(draw + 20, 4);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    present(present_context, 4, 0x07e0);
    /* Protocol failure leaves bindings intact. */
    texture[10] = 1;
    assert(backend->upload(backend->context, surface, texture, sizeof(texture)) ==
           PXA_STATUS_PROTOCOL_ERROR);
    pxa_write_u64(draw + 20, 5);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    present(present_context, 5, 0x07e0);
    pxa_write_u64(draw + 20, 6);
    assert(backend->submit(backend->context, surface, draw, sizeof(draw)) == 0);
    backend->close(backend->context, surface); /* Exiting with a queued frame. */
    raster_used_resources_scenario(backend, present, present_context);
}
