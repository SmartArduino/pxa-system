#ifndef PXA_ASSET_OBJECT_H
#define PXA_ASSET_OBJECT_H
#include "pxa/asset_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Immutable resident resource. Cache, Guest handles and consumers own
 * independent references to one allocation. No native pointer crosses ABI. */
typedef struct pxa_asset_object pxa_asset_object_t;
/* Prepared UI pixels can feed cache-line-aligned hardware draw buffers.
 * Padding is part of the single resource allocation and its budget charge. */
#define PXA_ASSET_IMAGE_ALIGNMENT 64u
typedef void *(*pxa_asset_object_alloc_fn)(void *, size_t);
typedef void (*pxa_asset_object_free_fn)(void *, void *);
typedef struct {
    const uint8_t *data;
    uint32_t bytes;
    uint16_t width, height;
    uint8_t kind, encoding;
} pxa_asset_object_view_t;
size_t pxa_asset_object_required_bytes(const pxa_asset_info_t *);
pxa_status_t pxa_asset_object_create(const pxa_asset_info_t *,
    pxa_asset_object_alloc_fn, pxa_asset_object_free_fn, void *,
    pxa_asset_object_t **, uint8_t **payload);
void pxa_asset_object_finish_loading(pxa_asset_object_t *);
void pxa_asset_object_view(const pxa_asset_object_t *, pxa_asset_object_view_t *);
void pxa_asset_object_retain(pxa_asset_object_t *);
void pxa_asset_object_release(pxa_asset_object_t *);
/* Consumer of a cache-pinned resource: drops only the consumer reference and
 * cannot free. The cache must retain its own reference until consumers stop.
 * Suitable for a mixer critical section; reclamation stays on the worker. */
void pxa_asset_object_release_pinned(pxa_asset_object_t *);
uint32_t pxa_asset_object_reference_count(const pxa_asset_object_t *);
size_t pxa_asset_object_allocation_bytes(const pxa_asset_object_t *);
#ifdef __cplusplus
}
#endif
#endif
