#ifndef PXA_ASSET_LOADER_H
#define PXA_ASSET_LOADER_H

#include "pxa/raster_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_ASSET_READ_CHUNK_BYTES 4096u

/* Worker-only input for an installed package. Installation owns SHA/signature
 * checks; runtime reads directly into the final allocation and checks bounds.
 * No callbacks run under the resource-manager lock. */
typedef struct {
    void *context;
    pxa_status_t (*read)(void *context, uint8_t *output, size_t capacity,
                         size_t *read_bytes);
    int (*cancelled)(void *context);
    /* Optional cooperative scheduling point after each bounded read. */
    void (*yield)(void *context);
} pxa_asset_input_t;

/* Header lives on the stack; all payload reads go directly into the final
 * allocation. No whole-file compressed/input copy or Guest buffer exists.
 * Caller reserves required_bytes up front, closes input on every outcome,
 * and publishes the result only on success. */
/* Shared resident loader: PXR1 textures/palettes or raw short PCM. One final
 * allocation, chunked reads and EOF before publication. */
pxa_status_t pxa_asset_load_resident(const pxa_asset_info_t *, const pxa_asset_input_t *,
    pxa_asset_object_alloc_fn, pxa_asset_object_free_fn, void *, pxa_asset_object_t **);
pxa_status_t pxa_asset_load_raster(
    const pxa_asset_info_t *info, const pxa_asset_input_t *input,
    pxa_raster_asset_alloc_fn allocate, pxa_raster_asset_free_fn deallocate,
    void *allocator_context, pxa_raster_asset_t **output);

/* Worker-only direct range read, at most 4096 B. Input is positioned at the
 * requested offset. No SHA, alignment requirement or intermediate copy.
 * On failure the caller discards the partial buffer. */
pxa_status_t pxa_asset_load_blob_block(const pxa_asset_blob_block_t *block,
    const pxa_asset_input_t *input, uint8_t *output, size_t capacity);
/* Shared range loader for blob or encoded Ogg data; same read/cancel contract. */
pxa_status_t pxa_asset_load_block(const pxa_asset_block_t *block,
    const pxa_asset_input_t *input, uint8_t *output, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
