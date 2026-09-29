#ifndef PXA_RASTER_ASSETS_H
#define PXA_RASTER_ASSETS_H

#include "pxa/raster.h"
#include "pxa/asset_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-only immutable storage. A Guest handle, a binding and a submitted frame
 * each own a reference; none of those references owns another pixel copy.
 * Allocators must return naturally aligned memory. Alloc/free may block and
 * must therefore never be called from a critical section or pixel loop. */
/* Raster-facing names share the single resident object implementation. */
typedef pxa_asset_object_alloc_fn pxa_raster_asset_alloc_fn;
typedef pxa_asset_object_free_fn pxa_raster_asset_free_fn;
typedef pxa_asset_object_t pxa_raster_asset_t;
#define pxa_raster_asset_finish_loading pxa_asset_object_finish_loading
#define pxa_raster_asset_reference_count pxa_asset_object_reference_count
#define pxa_raster_asset_retain pxa_asset_object_retain
#define pxa_raster_asset_release pxa_asset_object_release
#define pxa_raster_asset_allocation_bytes pxa_asset_object_allocation_bytes

typedef struct {
    pxa_raster_asset_t *textures[PXA_RASTER_MAX_TEXTURES];
    pxa_raster_asset_t *palette;
} pxa_raster_bindings_t;

/* Reserve this exact number of bytes in the Host budget before allocation.
 * Returns zero for unsupported/malformed metadata. */
size_t pxa_raster_asset_required_bytes(const pxa_asset_info_t *info);
/* Creates private, uninitialized storage for a loader. Do not publish until
 * the payload has been filled, verified and finish_loading has been called. */
pxa_status_t pxa_raster_asset_create(
    const pxa_asset_info_t *info, pxa_raster_asset_alloc_fn allocate,
    pxa_raster_asset_free_fn deallocate, void *context,
    pxa_raster_asset_t **output, uint8_t **payload);
void pxa_raster_asset_finish_loading(pxa_raster_asset_t *asset);
uint32_t pxa_raster_asset_reference_count(const pxa_raster_asset_t *asset);

pxa_status_t pxa_raster_asset_from_upload(
    const uint8_t *bytes, size_t size, pxa_raster_asset_alloc_fn allocate,
    pxa_raster_asset_free_fn deallocate, void *context,
    pxa_raster_asset_t **output);
void pxa_raster_asset_retain(pxa_raster_asset_t *asset);
void pxa_raster_asset_release(pxa_raster_asset_t *asset);
size_t pxa_raster_asset_allocation_bytes(const pxa_raster_asset_t *asset);

/* Caller serializes changes to bindings. Retain a snapshot while holding the
 * binding lock, then resolve/read it without that lock. Destination must be
 * empty. Releases may deallocate: detach under the lock and release outside.
 * Only copy an owning set to transfer ownership: clear the source, or stop
 * using it entirely (for example a local returned from its owning scope). */
void pxa_raster_bindings_snapshot(pxa_raster_bindings_t *destination,
                                  const pxa_raster_bindings_t *source);
/* Same locking/empty-destination contract, retaining only validated execution
 * dependencies. Use prune after validation if validation needed a full snapshot
 * outside the binding lock. prune can free: never call it under that lock. */
void pxa_raster_bindings_snapshot_for_draw(pxa_raster_bindings_t *destination,
    const pxa_raster_bindings_t *source, const pxa_raster_draw_list_view_t *list);
void pxa_raster_bindings_prune_for_draw(pxa_raster_bindings_t *bindings,
    const pxa_raster_draw_list_view_t *list);
void pxa_raster_bindings_release(pxa_raster_bindings_t *bindings);
/* Apply a validated batch under the binding lock. A set mask bit with NULL
 * clears that slot. retired must be empty and is released outside the lock.
 * replacement is borrowed; the destination retains each new object. */
void pxa_raster_bindings_update(pxa_raster_bindings_t *destination,
    const pxa_raster_bindings_t *replacement, uint64_t texture_mask,
    uint8_t update_palette, pxa_raster_bindings_t *retired);
void pxa_raster_bindings_view(const pxa_raster_bindings_t *bindings,
                              uint32_t capabilities,
                              pxa_raster_resources_t *view);
/* Takes a reference to asset; returns the previous owned reference for release
 * outside the caller's lock. slot is ignored for palettes. */
pxa_raster_asset_t *pxa_raster_bindings_replace(
    pxa_raster_bindings_t *bindings, uint8_t slot, pxa_raster_asset_t *asset);

#ifdef __cplusplus
}
#endif
#endif
