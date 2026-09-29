#include "pxa/raster_assets.h"
#include "pxa/wire.h"

#include <assert.h>
#include <string.h>

#include "services/assets/asset_object_internal.h"

size_t pxa_raster_asset_required_bytes(const pxa_asset_info_t *info) {
    return info && (info->kind == PXA_ASSET_TEXTURE || info->kind == PXA_ASSET_PALETTE)
        ? pxa_asset_object_required_bytes(info) : 0;
}
pxa_status_t pxa_raster_asset_create(const pxa_asset_info_t *info,
    pxa_raster_asset_alloc_fn allocate, pxa_raster_asset_free_fn deallocate,
    void *context, pxa_raster_asset_t **out, uint8_t **payload) {
    if (!out || !payload) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL; *payload = NULL;
    if (!pxa_raster_asset_required_bytes(info)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_asset_object_create(info,allocate,deallocate,context,out,payload);
}

pxa_status_t pxa_raster_asset_from_upload(
    const uint8_t *bytes, size_t size, pxa_raster_asset_alloc_fn allocate,
    pxa_raster_asset_free_fn deallocate, void *context,
    pxa_raster_asset_t **output) {
    pxa_raster_upload_view_t upload;
    pxa_raster_asset_t *asset;
    uint8_t *payload;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (allocate == NULL || deallocate == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_raster_decode_upload(bytes, size, &upload);
    if (status != PXA_STATUS_OK) return status;
    {
        pxa_asset_info_t info;
        memset(&info, 0, sizeof(info));
        info.kind = upload.kind == PXA_RASTER_UPLOAD_TEXTURE_INDEX8
                        ? PXA_ASSET_TEXTURE : PXA_ASSET_PALETTE;
        info.encoding = info.kind == PXA_ASSET_TEXTURE
                            ? PXA_ASSET_ENCODING_INDEX8 : PXA_ASSET_ENCODING_RGB565;
        info.width = upload.width;
        info.height = upload.height;
        info.decoded_bytes = upload.payload_bytes;
        info.stored_bytes = upload.payload_bytes + PXA_ASSET_FILE_HEADER_BYTES;
        info.payload_offset = PXA_ASSET_FILE_HEADER_BYTES;
        info.format_version = 1;
        status = pxa_raster_asset_create(&info, allocate, deallocate, context,
                                          &asset, &payload);
        if (status != PXA_STATUS_OK) return status;
    }
    memcpy(payload, upload.payload, upload.payload_bytes);
    pxa_raster_asset_finish_loading(asset);
    *output = asset;
    return PXA_STATUS_OK;
}

void pxa_raster_bindings_snapshot(pxa_raster_bindings_t *destination,
                                  const pxa_raster_bindings_t *source) {
    uint8_t i;
    *destination = *source;
    for (i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i)
        pxa_raster_asset_retain(destination->textures[i]);
    pxa_raster_asset_retain(destination->palette);
}

void pxa_raster_bindings_snapshot_for_draw(pxa_raster_bindings_t *destination,
    const pxa_raster_bindings_t *source, const pxa_raster_draw_list_view_t *list) {
    memset(destination, 0, sizeof(*destination));
    for (uint8_t i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i) {
        if (!(list->texture_mask & (UINT64_C(1) << i))) continue;
        destination->textures[i] = source->textures[i];
        pxa_raster_asset_retain(destination->textures[i]);
    }
    if (list->uses_palette) {
        destination->palette = source->palette;
        pxa_raster_asset_retain(destination->palette);
    }
}

void pxa_raster_bindings_prune_for_draw(pxa_raster_bindings_t *bindings,
    const pxa_raster_draw_list_view_t *list) {
    for (uint8_t i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i) {
        if (list->texture_mask & (UINT64_C(1) << i)) continue;
        pxa_raster_asset_release(bindings->textures[i]);
        bindings->textures[i] = NULL;
    }
    if (!list->uses_palette) {
        pxa_raster_asset_release(bindings->palette);
        bindings->palette = NULL;
    }
}

void pxa_raster_bindings_release(pxa_raster_bindings_t *bindings) {
    uint8_t i;
    for (i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i)
        pxa_raster_asset_release(bindings->textures[i]);
    pxa_raster_asset_release(bindings->palette);
    memset(bindings, 0, sizeof(*bindings));
}

void pxa_raster_bindings_update(pxa_raster_bindings_t *destination,
    const pxa_raster_bindings_t *replacement, uint64_t texture_mask,
    uint8_t update_palette, pxa_raster_bindings_t *retired) {
    for (uint8_t i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i) {
        if (!(texture_mask & (UINT64_C(1) << i))) continue;
        pxa_raster_asset_retain(replacement->textures[i]);
        retired->textures[i] = destination->textures[i];
        destination->textures[i] = replacement->textures[i];
    }
    if (update_palette) {
        pxa_raster_asset_retain(replacement->palette);
        retired->palette = destination->palette;
        destination->palette = replacement->palette;
    }
}

void pxa_raster_bindings_view(const pxa_raster_bindings_t *bindings,
                              uint32_t capabilities,
                              pxa_raster_resources_t *view) {
    uint8_t i;
    memset(view, 0, sizeof(*view));
    view->capabilities = capabilities;
    if (bindings->palette != NULL) {
        view->palette = (const uint16_t *)(bindings->palette + 1);
        view->palette_light_levels = bindings->palette->height;
    }
    for (i = 0; i < PXA_RASTER_MAX_TEXTURES; ++i) {
        const pxa_raster_asset_t *asset = bindings->textures[i];
        if (asset == NULL) continue;
        view->textures[i].pixels = (const uint8_t *)(asset + 1);
        view->textures[i].width = asset->width;
        view->textures[i].height = asset->height;
    }
}

pxa_raster_asset_t *pxa_raster_bindings_replace(
    pxa_raster_bindings_t *bindings, uint8_t slot, pxa_raster_asset_t *asset) {
    pxa_raster_asset_t **binding;
    pxa_raster_asset_t *previous;
    assert(asset != NULL && (asset->kind == PXA_ASSET_TEXTURE || asset->kind == PXA_ASSET_PALETTE));
    assert(asset->kind != PXA_ASSET_TEXTURE ||
           slot < PXA_RASTER_MAX_TEXTURES);
    binding = asset->kind == PXA_ASSET_TEXTURE
                  ? &bindings->textures[slot] : &bindings->palette;
    previous = *binding;
    pxa_raster_asset_retain(asset);
    *binding = asset;
    return previous;
}
