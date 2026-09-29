#ifndef PXA_ASSET_STREAM_H
#define PXA_ASSET_STREAM_H
#include "pxa/asset_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Decoder-worker-only bounded reads of an installed package. No verification
 * buffer, SHA, heap or intermediate memcpy. Installation verified contents.
 * The loader fills the caller's decoder buffer directly; on error bytes=0 and
 * the caller must discard any partial contents. Catalog outlives the stream. */
typedef struct {
    pxa_asset_block_map_t map;
    void *context;
    pxa_status_t (*load)(void *, const pxa_asset_block_t *, uint8_t *, size_t);
    int (*cancelled)(void *);
    uint32_t position;
    pxa_status_t status;
    uint64_t read_bytes, read_calls;
} pxa_asset_stream_t;
pxa_status_t pxa_asset_stream_init(pxa_asset_stream_t *, const pxa_asset_block_map_t *,
    void *context, pxa_status_t (*load)(void *, const pxa_asset_block_t *, uint8_t *, size_t), int (*cancelled)(void *));
pxa_status_t pxa_asset_stream_read(pxa_asset_stream_t *, uint8_t *, size_t, size_t *);
/* Absolute position including EOF. No I/O. Errors/cancellation are sticky. */
pxa_status_t pxa_asset_stream_seek(pxa_asset_stream_t *, uint32_t position);
#ifdef __cplusplus
}
#endif
#endif
