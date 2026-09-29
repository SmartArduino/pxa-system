#undef NDEBUG
#include "pxa/raster_assets.h"
#include "pxa/wire.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations;
static int fail_allocate;
static void *allocate(void *context, size_t bytes) {
    void *memory;
    assert(context == &allocations);
    if (fail_allocate) return NULL;
    memory = malloc(bytes);
    assert(memory != NULL);
    ++allocations;
    return memory;
}
static void deallocate(void *context, void *memory) {
    assert(context == &allocations && allocations != 0);
    --allocations;
    free(memory);
}
static void upload_header(uint8_t *bytes, uint8_t kind,
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
int main(void) {
    pxa_raster_bindings_t bindings = {0}, old_frame = {0}, new_frame = {0};
    pxa_raster_asset_t *asset = NULL;
    pxa_raster_resources_t old_view, new_view;
    uint8_t texture[PXA_RASTER_UPLOAD_HEADER_BYTES + 4] = {0};
    uint8_t palette[PXA_RASTER_UPLOAD_HEADER_BYTES + 512] = {0};
    upload_header(texture, PXA_RASTER_UPLOAD_TEXTURE_INDEX8, 2, 2);
    memset(texture + PXA_RASTER_UPLOAD_HEADER_BYTES, 1, 4);
    assert(pxa_raster_asset_from_upload(texture, sizeof(texture), allocate,
        deallocate, &allocations, &asset) == PXA_STATUS_OK);
    assert(pxa_raster_asset_allocation_bytes(asset) > 4);
    assert(pxa_raster_bindings_replace(&bindings, 0, asset) == NULL);
    pxa_raster_asset_release(asset); /* Guest ownership ends before rendering. */
    upload_header(palette, PXA_RASTER_UPLOAD_PALETTE_RGB565, 256, 1);
    pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 2, 0xf800);
    assert(pxa_raster_asset_from_upload(palette, sizeof(palette), allocate,
        deallocate, &allocations, &asset) == PXA_STATUS_OK);
    assert(pxa_raster_bindings_replace(&bindings, 0, asset) == NULL);
    pxa_raster_asset_release(asset);
    pxa_raster_bindings_snapshot(&old_frame, &bindings);
    assert(allocations == 2); /* Snapshot owns references, no pixel copy. */
    pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 2, 0x07e0);
    assert(pxa_raster_asset_from_upload(palette, sizeof(palette), allocate,
        deallocate, &allocations, &asset) == PXA_STATUS_OK);
    pxa_raster_asset_release(pxa_raster_bindings_replace(&bindings, 0, asset));
    pxa_raster_asset_release(asset);
    pxa_raster_bindings_snapshot(&new_frame, &bindings);
    pxa_raster_bindings_release(&bindings); /* App/context exits. */
    pxa_raster_bindings_view(&old_frame, 0, &old_view);
    pxa_raster_bindings_view(&new_frame, 0, &new_view);
    assert(old_view.palette[1] == 0xf800 && new_view.palette[1] == 0x07e0);
    assert(old_view.textures[0].pixels == new_view.textures[0].pixels);
    assert(old_view.textures[0].pixels[3] == 1 && allocations == 3);
    pxa_raster_bindings_release(&old_frame);
    assert(allocations == 2 && new_view.palette[1] == 0x07e0);
    pxa_raster_bindings_release(&new_frame);
    assert(allocations == 0);
    /* A high slot is retained while unrelated bindings are pruned; after
     * unbind it stays alive until the actual consuming frame completes. */
    {
        pxa_raster_draw_list_view_t list = {0};
        assert(pxa_raster_asset_from_upload(texture, sizeof(texture), allocate,
            deallocate, &allocations, &asset) == PXA_STATUS_OK);
        assert(pxa_raster_bindings_replace(&bindings, 47, asset) == NULL);
        assert(pxa_raster_bindings_replace(&bindings, 0, asset) == NULL);
        pxa_raster_asset_release(asset);
        list.texture_mask = UINT64_C(1) << 47;
        pxa_raster_bindings_snapshot_for_draw(&old_frame, &bindings, &list);
        assert(old_frame.textures[0] == NULL && old_frame.textures[47] == asset);
        assert(pxa_raster_asset_reference_count(asset) == 3);
        pxa_raster_bindings_snapshot(&new_frame, &bindings);
        pxa_raster_bindings_prune_for_draw(&new_frame, &list);
        assert(new_frame.textures[0] == NULL && new_frame.textures[47] == asset);
        assert(pxa_raster_asset_reference_count(asset) == 4);
        pxa_raster_bindings_release(&bindings);
        pxa_raster_bindings_release(&new_frame);
        assert(allocations == 1 && pxa_raster_asset_reference_count(asset) == 1);
        list.texture_mask = 0; /* Replaced by a clear-only frame. */
        pxa_raster_bindings_prune_for_draw(&old_frame, &list);
        assert(allocations == 0 && old_frame.textures[47] == NULL);
    }
    fail_allocate = 1;
    assert(pxa_raster_asset_from_upload(texture, sizeof(texture), allocate,
        deallocate, &allocations, &asset) == PXA_STATUS_RESOURCE_LIMIT);
    assert(asset == NULL && allocations == 0);
    fail_allocate = 0;
    texture[10] = 1;
    assert(pxa_raster_asset_from_upload(texture, sizeof(texture), allocate,
        deallocate, &allocations, &asset) == PXA_STATUS_PROTOCOL_ERROR);
    assert(asset == NULL && allocations == 0);
    return 0;
}
