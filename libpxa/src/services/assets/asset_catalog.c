#include "pxa/asset_catalog.h"
#include "pxa/raster.h"

#include <string.h>

int pxa_asset_path_valid(pxa_bytes_t path) {
    size_t start = 0, i;
    if (path.data == NULL || path.size <= 7 || path.size > PXA_ASSET_PATH_MAX ||
        memcmp(path.data, "assets/", 7) != 0)
        return 0;
    for (i = 0; i <= path.size; ++i) {
        if (i == path.size || path.data[i] == '/') {
            size_t length = i - start;
            if (length == 0 ||
                (length == 1 && path.data[start] == '.') ||
                (length == 2 && path.data[start] == '.' && path.data[start + 1] == '.'))
                return 0;
            start = i + 1;
        } else {
            uint8_t c = path.data[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                return 0;
        }
    }
    return 1;
}

static int compare(pxa_bytes_t a, pxa_bytes_t b) {
    size_t n = a.size < b.size ? a.size : b.size;
    int c = memcmp(a.data, b.data, n);
    return c != 0 ? c : a.size < b.size ? -1 : a.size > b.size;
}

static pxa_status_t validate_info(const pxa_asset_info_t *info) {
    uint64_t expected;
    if (info->kind == PXA_ASSET_TEXTURE || info->kind == PXA_ASSET_PALETTE) {
        if (info->format_version != 1 ||
            info->payload_offset != PXA_ASSET_FILE_HEADER_BYTES)
            return PXA_STATUS_UNSUPPORTED;
        if (info->kind == PXA_ASSET_TEXTURE) {
            if (info->encoding != PXA_ASSET_ENCODING_INDEX8 ||
                info->width == 0 || info->height == 0 ||
                info->width > PXA_RASTER_MAX_TEXTURE_DIMENSION ||
                info->height > PXA_RASTER_MAX_TEXTURE_DIMENSION)
                return PXA_STATUS_PROTOCOL_ERROR;
            expected = (uint64_t)info->width * info->height;
        } else {
            if (info->encoding != PXA_ASSET_ENCODING_RGB565 ||
                info->width != 256 || info->height == 0 || info->height > 256)
                return PXA_STATUS_PROTOCOL_ERROR;
            expected = (uint64_t)info->width * info->height * 2;
        }
        if (info->decoded_bytes != expected ||
            info->stored_bytes != expected + PXA_ASSET_FILE_HEADER_BYTES)
            return PXA_STATUS_PROTOCOL_ERROR;
    } else if (info->kind == PXA_ASSET_AUDIO) {
        if (info->format_version != 0 || info->payload_offset != 0 ||
            info->width != 0 || info->height != 0 || info->stored_bytes == 0)
            return PXA_STATUS_PROTOCOL_ERROR;
        if (info->encoding == PXA_ASSET_ENCODING_PCM_U8_16K_MONO) {
            if (info->decoded_bytes != info->stored_bytes || info->stored_bytes > 16000)
                return PXA_STATUS_LIMIT_EXCEEDED;
        } else if (info->encoding == PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO) {
            if (info->decoded_bytes != info->stored_bytes || info->stored_bytes % 2 ||
                info->stored_bytes > PXA_ASSET_AUDIO_MAX_PCM_BYTES)
                return PXA_STATUS_LIMIT_EXCEEDED;
        } else if ((info->encoding != PXA_ASSET_ENCODING_OGG_OPUS &&
                    info->encoding != PXA_ASSET_ENCODING_OGG_VORBIS) ||
                   info->decoded_bytes != 0) return PXA_STATUS_UNSUPPORTED;
    } else if (info->kind == PXA_ASSET_IMAGE) {
        if (!info->width || !info->height) return PXA_STATUS_PROTOCOL_ERROR;
        if (info->encoding == PXA_ASSET_ENCODING_PNG) {
            /* Metadata discovery only. PNG is not a resident LOAD format. */
            expected = (uint64_t)info->width * info->height * 4;
            if (info->format_version || info->payload_offset || info->stored_bytes < 33 ||
                expected > UINT32_MAX || info->decoded_bytes != expected)
                return PXA_STATUS_PROTOCOL_ERROR;
        } else {
            if (info->format_version != 1 || info->payload_offset != PXA_ASSET_FILE_HEADER_BYTES ||
                info->width > PXA_ASSET_IMAGE_MAX_DIMENSION || info->height > PXA_ASSET_IMAGE_MAX_DIMENSION ||
                (info->encoding != PXA_ASSET_ENCODING_RGB565 &&
                 info->encoding != PXA_ASSET_ENCODING_BGRA8888 &&
                 info->encoding != PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED))
                return PXA_STATUS_UNSUPPORTED;
            expected = (uint64_t)info->width * info->height *
                (info->encoding == PXA_ASSET_ENCODING_RGB565 ? 2u : 4u);
            if (info->decoded_bytes != expected || info->stored_bytes != expected + PXA_ASSET_FILE_HEADER_BYTES)
                return PXA_STATUS_PROTOCOL_ERROR;
        }
    } else if (info->kind == PXA_ASSET_BLOB) {
        if (info->format_version != 0 || info->encoding != PXA_ASSET_ENCODING_RAW ||
            info->payload_offset != 0 || info->width != 0 || info->height != 0 ||
            info->stored_bytes != info->decoded_bytes)
            return PXA_STATUS_PROTOCOL_ERROR;
    } else return PXA_STATUS_UNSUPPORTED;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_file_header(const uint8_t *header, size_t size,
                                   uint64_t file_size, pxa_asset_info_t *info) {
    if (info == NULL || header == NULL || size < PXA_ASSET_FILE_HEADER_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    if (pxa_read_u32(header) != PXA_ASSET_FILE_MAGIC)
        return PXA_STATUS_PROTOCOL_ERROR;
    if (pxa_read_u16(header + 4) != PXA_ASSET_FORMAT_MAJOR ||
        pxa_read_u16(header + 6) != PXA_ASSET_FORMAT_MINOR)
        return PXA_STATUS_UNSUPPORTED;
    if (file_size > UINT32_MAX || pxa_read_u16(header + 8) > UINT8_MAX ||
        pxa_read_u16(header + 10) > UINT8_MAX || pxa_read_u64(header + 24) != 0)
        return PXA_STATUS_PROTOCOL_ERROR;
    info->kind = (uint8_t)pxa_read_u16(header + 8);
    info->encoding = (uint8_t)pxa_read_u16(header + 10);
    info->width = pxa_read_u16(header + 12);
    info->height = pxa_read_u16(header + 14);
    info->decoded_bytes = pxa_read_u32(header + 16);
    info->payload_offset = pxa_read_u32(header + 20);
    info->stored_bytes = (uint32_t)file_size;
    info->format_version = 1;
    if (info->kind != PXA_ASSET_TEXTURE && info->kind != PXA_ASSET_PALETTE && info->kind != PXA_ASSET_IMAGE)
        return PXA_STATUS_UNSUPPORTED;
    return validate_info(info);
}

static pxa_status_t read_entry(const pxa_asset_catalog_t *catalog,
    size_t *offset, pxa_asset_info_t *info, const uint8_t **block_hashes) {
    const uint8_t *p;
    size_t length, padded, i;
    pxa_status_t status;
    if (block_hashes) *block_hashes = NULL;
    if (*offset > catalog->bytes.size ||
        catalog->bytes.size - *offset < PXA_ASSET_INDEX_RECORD_BYTES)
        return PXA_STATUS_PROTOCOL_ERROR;
    p = catalog->bytes.data + *offset;
    length = pxa_read_u16(p);
    padded = (length + 3) & ~(size_t)3;
    *offset += PXA_ASSET_INDEX_RECORD_BYTES;
    if (padded > catalog->bytes.size - *offset || pxa_read_u16(p + 18) != 0)
        return PXA_STATUS_PROTOCOL_ERROR;
    memset(info, 0, sizeof(*info));
    info->path = (pxa_bytes_t){catalog->bytes.data + *offset, length};
    if (!pxa_asset_path_valid(info->path)) return PXA_STATUS_DENIED;
    for (i = length; i < padded; ++i)
        if (info->path.data[i] != 0) return PXA_STATUS_PROTOCOL_ERROR;
    *offset += padded;
    info->kind = p[2];
    info->encoding = p[3];
    info->width = pxa_read_u16(p + 4);
    info->height = pxa_read_u16(p + 6);
    info->stored_bytes = pxa_read_u32(p + 8);
    info->decoded_bytes = pxa_read_u32(p + 12);
    info->format_version = pxa_read_u16(p + 16);
    info->payload_offset = pxa_read_u32(p + 20);
    status = validate_info(info);
    if (status != PXA_STATUS_OK) return status;
    info->file = pxa_package_file_find(catalog->manifest, info->path);
    if (info->file == NULL ||
        info->file->size != info->stored_bytes)
        return PXA_STATUS_DENIED;
    uint16_t minor = pxa_read_u16(catalog->bytes.data + 6);
    if (minor < 3 && ((info->kind == PXA_ASSET_BLOB && minor >= 1) ||
        (info->kind == PXA_ASSET_AUDIO && !info->decoded_bytes && minor >= 2))) {
        size_t blocks = info->stored_bytes / PXA_ASSET_BLOB_BLOCK_BYTES +
            (info->stored_bytes % PXA_ASSET_BLOB_BLOCK_BYTES != 0);
        /* Division first avoids overflow on 32-bit Hosts and huge declarations. */
        if (blocks > (catalog->bytes.size - *offset) / 32u)
            return PXA_STATUS_PROTOCOL_ERROR;
        if (block_hashes && blocks) *block_hashes = catalog->bytes.data + *offset;
        *offset += blocks * 32u;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_catalog_init(pxa_asset_catalog_t *catalog,
                                    pxa_bytes_t index,
                                    const pxa_package_manifest_t *manifest) {
    pxa_asset_catalog_t candidate;
    pxa_bytes_t previous = {NULL, 0};
    uint32_t i;
    size_t offset = PXA_ASSET_INDEX_HEADER_BYTES;
    if (catalog == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(catalog, 0, sizeof(*catalog));
    if (index.data == NULL || manifest == NULL ||
        (manifest->file_count != 0 && manifest->files == NULL) ||
        index.size < PXA_ASSET_INDEX_HEADER_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (pxa_read_u32(index.data) != PXA_ASSET_INDEX_MAGIC ||
        index.size != pxa_read_u32(index.data + 12))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (pxa_read_u16(index.data + 4) != PXA_ASSET_FORMAT_MAJOR ||
        pxa_read_u16(index.data + 6) > PXA_ASSET_INDEX_MINOR)
        return PXA_STATUS_UNSUPPORTED;
    candidate.bytes = index;
    candidate.manifest = manifest;
    candidate.count = pxa_read_u32(index.data + 8);
    if (candidate.count > manifest->file_count ||
        candidate.count > (index.size - offset) / PXA_ASSET_INDEX_RECORD_BYTES)
        return PXA_STATUS_LIMIT_EXCEEDED;
    for (i = 0; i < candidate.count; ++i) {
        pxa_asset_info_t info;
        pxa_status_t status = read_entry(&candidate, &offset, &info, NULL);
        if (status != PXA_STATUS_OK) return status;
        if (previous.data != NULL && compare(previous, info.path) >= 0)
            return PXA_STATUS_PROTOCOL_ERROR;
        previous = info.path;
    }
    if (offset != index.size) return PXA_STATUS_PROTOCOL_ERROR;
    *catalog = candidate;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_catalog_find(const pxa_asset_catalog_t *catalog,
                                    pxa_bytes_t path, pxa_asset_info_t *info) {
    size_t offset = PXA_ASSET_INDEX_HEADER_BYTES;
    uint32_t i;
    if (catalog == NULL || catalog->manifest == NULL || info == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    if (!pxa_asset_path_valid(path)) return PXA_STATUS_DENIED;
    for (i = 0; i < catalog->count; ++i) {
        pxa_asset_info_t candidate;
        pxa_status_t status = read_entry(catalog, &offset, &candidate, NULL);
        int order;
        if (status != PXA_STATUS_OK) return status;
        order = compare(candidate.path, path);
        if (order == 0) { *info = candidate; return PXA_STATUS_OK; }
        if (order > 0) break;
    }
    return PXA_STATUS_NOT_FOUND;
}

pxa_status_t pxa_asset_catalog_blob_block(const pxa_asset_catalog_t *catalog,
    pxa_bytes_t path, uint32_t position, pxa_asset_blob_block_t *block) {
    if (!block) return PXA_STATUS_INVALID_ARGUMENT;
    memset(block, 0, sizeof(*block));
    pxa_asset_block_map_t map;
    pxa_status_t status = pxa_asset_catalog_block_map(catalog, path, &map);
    if (status) return status;
    if (map.info.kind != PXA_ASSET_BLOB) return PXA_STATUS_UNSUPPORTED;
    return pxa_asset_block_map_get(&map, position, block);
}

pxa_status_t pxa_asset_catalog_block_map(const pxa_asset_catalog_t *catalog,
    pxa_bytes_t path, pxa_asset_block_map_t *map) {
    size_t offset = PXA_ASSET_INDEX_HEADER_BYTES;
    if (!map) return PXA_STATUS_INVALID_ARGUMENT;
    memset(map, 0, sizeof(*map));
    if (!catalog || !catalog->manifest) return PXA_STATUS_INVALID_ARGUMENT;
    if (!pxa_asset_path_valid(path)) return PXA_STATUS_DENIED;
    for (uint32_t i = 0; i < catalog->count; ++i) {
        pxa_asset_info_t info;
        const uint8_t *hashes;
        pxa_status_t status = read_entry(catalog, &offset, &info, &hashes);
        if (status != PXA_STATUS_OK) return status;
        int order = compare(info.path, path);
        if (order > 0) break;
        if (order) continue;
        if (!(info.kind == PXA_ASSET_BLOB ||
              (info.kind == PXA_ASSET_AUDIO && !info.decoded_bytes))) return PXA_STATUS_UNSUPPORTED;
        map->info = info;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_NOT_FOUND;
}

pxa_status_t pxa_asset_block_map_get(const pxa_asset_block_map_t *map,
    uint32_t position, pxa_asset_block_t *block) {
    if (!block) return PXA_STATUS_INVALID_ARGUMENT;
    memset(block, 0, sizeof(*block));
    if (!map || position > map->info.stored_bytes) return PXA_STATUS_INVALID_ARGUMENT;
    block->info = map->info;
    if (position == map->info.stored_bytes) { block->offset = position; return PXA_STATUS_OK; }
    block->offset = position;
    block->bytes = map->info.stored_bytes - block->offset;
    if (block->bytes > PXA_ASSET_BLOB_BLOCK_BYTES) block->bytes = PXA_ASSET_BLOB_BLOCK_BYTES;
    return PXA_STATUS_OK;
}

int pxa_asset_info_equal(const pxa_asset_info_t *a, const pxa_asset_info_t *b) {
    return a->kind == b->kind && a->encoding == b->encoding &&
           a->width == b->width && a->height == b->height &&
           a->format_version == b->format_version &&
           a->stored_bytes == b->stored_bytes &&
           a->decoded_bytes == b->decoded_bytes &&
           a->payload_offset == b->payload_offset;
}
