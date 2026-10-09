#ifndef PXA_ASSET_CATALOG_H
#define PXA_ASSET_CATALOG_H

#include "pxa/package.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_ASSET_INDEX_PATH "assets/resources.pxi"
#define PXA_ASSET_INDEX_MAGIC UINT32_C(0x49525850) /* PXRI */
#define PXA_ASSET_FILE_MAGIC UINT32_C(0x31525850) /* PXR1 */
#define PXA_ASSET_FORMAT_MAJOR UINT16_C(1)
#define PXA_ASSET_FORMAT_MINOR UINT16_C(0)
#define PXA_ASSET_INDEX_MINOR UINT16_C(3)
#define PXA_ASSET_BLOB_BLOCK_BYTES 4096u
#define PXA_ASSET_INDEX_HEADER_BYTES 16u
#define PXA_ASSET_INDEX_RECORD_BYTES 24u
#define PXA_ASSET_FILE_HEADER_BYTES 32u
#define PXA_ASSET_PATH_MAX 255u

#define PXA_ASSET_TEXTURE 1u
#define PXA_ASSET_PALETTE 2u
#define PXA_ASSET_AUDIO 3u
#define PXA_ASSET_IMAGE 4u
#define PXA_ASSET_BLOB 5u

#define PXA_ASSET_ENCODING_RAW 0u
#define PXA_ASSET_ENCODING_INDEX8 1u
#define PXA_ASSET_ENCODING_RGB565 2u
#define PXA_ASSET_ENCODING_OGG_OPUS 3u
#define PXA_ASSET_ENCODING_OGG_VORBIS 4u
#define PXA_ASSET_ENCODING_PCM_U8_16K_MONO 5u
#define PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO 9u
#define PXA_ASSET_AUDIO_MAX_PCM_BYTES (16000u * 2u * 30u)
#define PXA_ASSET_ENCODING_PNG 6u
/* Offline UI pixels: B,G,R,A bytes, straight alpha; no runtime decoder. */
#define PXA_ASSET_ENCODING_BGRA8888 7u
/* B,G,R channels already multiplied by A; LVGL consumes them without a
 * per-frame conversion or a second alpha multiplication. */
#define PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED 8u
#define PXA_ASSET_IMAGE_MAX_DIMENSION 4096u

typedef struct {
    pxa_bytes_t path;
    const pxa_package_file_t *file;
    uint32_t stored_bytes;
    /* Zero for streams (no full decode allocation is permitted). */
    uint32_t decoded_bytes;
    uint32_t payload_offset;
    uint16_t width;
    uint16_t height;
    uint16_t format_version;
    uint8_t kind;
    uint8_t encoding;
} pxa_asset_info_t;

/* Borrows an installed package manifest/index. Installation owns integrity
 * verification; runtime validates only structure, paths, sizes and membership. */
typedef struct {
    pxa_bytes_t bytes;
    const pxa_package_manifest_t *manifest;
    uint32_t count;
} pxa_asset_catalog_t;

/* Bounded range beginning at the requested offset; bytes=0 at EOF.
 * Metadata borrows the installed catalog and must outlive the read. */
typedef struct {
    pxa_asset_info_t info;
    uint32_t offset;
    uint32_t bytes;
} pxa_asset_blob_block_t;

/* Same bounded block representation for raw Guest data and encoded music. */
typedef pxa_asset_blob_block_t pxa_asset_block_t;
/* Borrows the installed catalog, including its manifest/path.
 * Capture once per stream; range lookup is O(1), without rescanning the index.
 * The owner must retain the catalog for the complete stream lifetime. */
typedef struct {
    pxa_asset_info_t info;
} pxa_asset_block_map_t;

int pxa_asset_path_valid(pxa_bytes_t path);
pxa_status_t pxa_asset_catalog_init(pxa_asset_catalog_t *catalog,
                                    pxa_bytes_t index,
                                    const pxa_package_manifest_t *manifest);
pxa_status_t pxa_asset_catalog_find(const pxa_asset_catalog_t *catalog,
                                    pxa_bytes_t path, pxa_asset_info_t *info);
/* Returns a range starting at offset, bounded to 4096 B and file length. */
pxa_status_t pxa_asset_catalog_blob_block(const pxa_asset_catalog_t *catalog,
    pxa_bytes_t path, uint32_t offset, pxa_asset_blob_block_t *block);
/* Discover blob/Ogg stream metadata without I/O, SHA or block tables. */
pxa_status_t pxa_asset_catalog_block_map(const pxa_asset_catalog_t *,
    pxa_bytes_t path, pxa_asset_block_map_t *);
pxa_status_t pxa_asset_block_map_get(const pxa_asset_block_map_t *,
    uint32_t offset, pxa_asset_block_t *);
/* Validates a PXR1 header before allocating. `file_size` is the exact signed
 * file length; output has no path/file until associated with the catalog. */
pxa_status_t pxa_asset_file_header(const uint8_t *header, size_t header_size,
                                   uint64_t file_size, pxa_asset_info_t *info);
int pxa_asset_info_equal(const pxa_asset_info_t *a, const pxa_asset_info_t *b);

#ifdef __cplusplus
}
#endif
#endif
