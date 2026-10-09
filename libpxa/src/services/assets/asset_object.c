#include "asset_object_internal.h"
#include "pxa/wire.h"
#include <assert.h>
#include <string.h>

static size_t payload_padding(uint8_t kind) {
    return kind == PXA_ASSET_IMAGE ? PXA_ASSET_IMAGE_ALIGNMENT - 1u : 0u;
}
static uint8_t *object_payload(const pxa_asset_object_t *asset) {
    uintptr_t pointer = (uintptr_t)(asset + 1);
    if (asset->kind == PXA_ASSET_IMAGE)
        pointer = (pointer + PXA_ASSET_IMAGE_ALIGNMENT - 1u) &
                  ~(uintptr_t)(PXA_ASSET_IMAGE_ALIGNMENT - 1u);
    return (uint8_t *)pointer;
}

size_t pxa_asset_object_required_bytes(const pxa_asset_info_t *info) {
    uint32_t expected;
    if (!info) return 0;
    if (info->kind == PXA_ASSET_AUDIO) {
        if ((info->encoding != PXA_ASSET_ENCODING_PCM_U8_16K_MONO &&
             info->encoding != PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO) ||
            info->format_version || info->payload_offset || info->width || info->height ||
            !info->decoded_bytes ||
            (info->encoding == PXA_ASSET_ENCODING_PCM_U8_16K_MONO ? info->decoded_bytes > 16000 :
                info->decoded_bytes > PXA_ASSET_AUDIO_MAX_PCM_BYTES || info->decoded_bytes % 2) ||
            info->stored_bytes != info->decoded_bytes) return 0;
        return sizeof(pxa_asset_object_t) + info->decoded_bytes;
    }
    if (info->kind == PXA_ASSET_IMAGE) {
        if (info->format_version != 1 || info->payload_offset != PXA_ASSET_FILE_HEADER_BYTES ||
            !info->width || !info->height || info->width > PXA_ASSET_IMAGE_MAX_DIMENSION ||
            info->height > PXA_ASSET_IMAGE_MAX_DIMENSION ||
            (info->encoding != PXA_ASSET_ENCODING_RGB565 &&
             info->encoding != PXA_ASSET_ENCODING_BGRA8888 &&
             info->encoding != PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED)) return 0;
        expected = (uint32_t)info->width * info->height *
            (info->encoding == PXA_ASSET_ENCODING_RGB565 ? 2u : 4u);
        if (info->decoded_bytes != expected || info->stored_bytes != expected + PXA_ASSET_FILE_HEADER_BYTES) return 0;
        return sizeof(pxa_asset_object_t) + payload_padding(info->kind) + expected;
    }
    if (info->format_version != 1 || !info->width || !info->height ||
        info->width > 256 || info->height > 256 ||
        info->payload_offset != PXA_ASSET_FILE_HEADER_BYTES) return 0;
    if (info->kind == PXA_ASSET_TEXTURE && info->encoding == PXA_ASSET_ENCODING_INDEX8)
        expected = (uint32_t)info->width * info->height;
    else if (info->kind == PXA_ASSET_PALETTE && info->width == 256 && info->encoding == PXA_ASSET_ENCODING_RGB565)
        expected = (uint32_t)info->width * info->height * 2;
    else return 0;
    if (info->decoded_bytes != expected || info->stored_bytes != expected + PXA_ASSET_FILE_HEADER_BYTES) return 0;
    return sizeof(pxa_asset_object_t) + expected;
}
pxa_status_t pxa_asset_object_create(const pxa_asset_info_t *info,
    pxa_asset_object_alloc_fn allocate, pxa_asset_object_free_fn deallocate,
    void *context, pxa_asset_object_t **out, uint8_t **payload) {
    if (!out || !payload) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL; *payload = NULL;
    size_t bytes = pxa_asset_object_required_bytes(info);
    if (!bytes || !allocate || !deallocate) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_asset_object_t *asset = allocate(context,bytes);
    if (!asset) return PXA_STATUS_RESOURCE_LIMIT;
    *asset = (pxa_asset_object_t){1,info->decoded_bytes,deallocate,context,info->width,info->height,info->kind,info->encoding};
    *out = asset; *payload = object_payload(asset);
    return PXA_STATUS_OK;
}
void pxa_asset_object_finish_loading(pxa_asset_object_t *asset) {
    if (asset->encoding != PXA_ASSET_ENCODING_RGB565) return;
    uint8_t *payload = object_payload(asset);
    for (uint32_t i=0;i<asset->payload_bytes/2;++i)
        ((uint16_t *)payload)[i] = pxa_read_u16(payload+2*i);
}
void pxa_asset_object_view(const pxa_asset_object_t *asset, pxa_asset_object_view_t *view) {
    memset(view,0,sizeof(*view));
    if (asset) *view = (pxa_asset_object_view_t){object_payload(asset),asset->payload_bytes,asset->width,asset->height,asset->kind,asset->encoding};
}
void pxa_asset_object_retain(pxa_asset_object_t *asset) {
    if (asset) {
        uint32_t before = __atomic_fetch_add(&asset->references,1,__ATOMIC_RELAXED);
        assert(before && before != UINT32_MAX); (void)before;
    }
}
void pxa_asset_object_release(pxa_asset_object_t *asset) {
    if (asset && __atomic_fetch_sub(&asset->references,1,__ATOMIC_ACQ_REL) == 1)
        asset->deallocate(asset->context,asset);
}
void pxa_asset_object_release_pinned(pxa_asset_object_t *asset) {
    if (asset) {
        uint32_t before = __atomic_fetch_sub(&asset->references,1,__ATOMIC_ACQ_REL);
        assert(before > 1); (void)before;
    }
}
uint32_t pxa_asset_object_reference_count(const pxa_asset_object_t *asset) {
    return asset ? __atomic_load_n(&asset->references,__ATOMIC_ACQUIRE) : 0;
}
size_t pxa_asset_object_allocation_bytes(const pxa_asset_object_t *asset) {
    return asset ? sizeof(*asset)+payload_padding(asset->kind)+asset->payload_bytes : 0;
}
