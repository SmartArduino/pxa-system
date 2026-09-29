#ifndef PXA_POSIX_ASSET_WORKER_H
#define PXA_POSIX_ASSET_WORKER_H

#include "pxa/asset_cache.h"
#include "pxa/posix/pxa_posix_storage_gate.h"
#include "pxa/resource_budget.h"
#include "pxa/asset_read.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pxa_posix_asset_worker pxa_posix_asset_worker_t;
typedef struct {
    const char *package_root;
    /* Already authenticated; must outlive the worker. */
    const pxa_package_manifest_t *manifest;
    pxa_asset_cache_config_t cache;
    size_t max_catalog_bytes;
    /* Optional shared-budget allocator, immutable through destroy(). */
    const pxa_memory_allocator_t *metadata_allocator;
    size_t stack_bytes; /* Zero selects 128 KiB for the native worker. */
    void *allocator_context;
    void *(*allocate)(void *context, size_t bytes, uint8_t memory_class, uint8_t asset_kind);
    void (*release)(void *context, void *memory);
    void *notify_context;
    /* May only signal the runtime; never invoke Guest code on this thread. */
    void (*notify)(void *context);
    pxa_posix_storage_gate_t *storage_gate; /* Borrowed through destroy(). */
    void *io_context;
    /* Optional I/O scheduling/fault-injection hook, outside all cache locks. */
    void (*before_read)(void *context);
    uint32_t read_delay_us; /* Simulator/test storage latency per read. */
    const pxa_memory_allocator_t *temporary_allocator; /* Required for blob reads. */
} pxa_posix_asset_worker_config_t;

/* Activation-only: verifies/loads the bounded catalog. The single worker starts
 * lazily before the first async request is accepted; metadata-only consumers
 * do not need its stack. Pixel I/O happens on that worker, never in request. */
pxa_status_t pxa_posix_asset_worker_create(const pxa_posix_asset_worker_config_t *config,
                                          pxa_posix_asset_worker_t **output);
pxa_status_t pxa_posix_asset_worker_request(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_bytes_t path, uint8_t memory_class, pxa_asset_ticket_t *ticket);
pxa_status_t pxa_posix_asset_worker_prefetch(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_bytes_t path, uint8_t memory_class, pxa_asset_ticket_t *ticket);
pxa_status_t pxa_posix_asset_worker_inspect(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_bytes_t path, pxa_asset_request_state_t *state);
/* Immutable verified catalog lookup, no file I/O. Borrows worker storage. */
pxa_status_t pxa_posix_asset_worker_find(pxa_posix_asset_worker_t *worker,
    pxa_bytes_t path, pxa_asset_info_t *info);
/* Same borrowed catalog lifetime as find(). The Host must stop/join every
 * decoder using this map before destroying the worker or its manifest. */
pxa_status_t pxa_posix_asset_worker_block_map(pxa_posix_asset_worker_t *,
    pxa_bytes_t path, pxa_asset_block_map_t *);
/* Diagnostic snapshot: a raw data read callback is active (including injected
 * latency), after its private verification buffer has been allocated. */
int pxa_posix_asset_worker_reading_blob(pxa_posix_asset_worker_t *);
/* Configured native stack allocation, zero before first async request.
 * Excludes pthread library/TCB overhead; not a stack high-water measurement. */
size_t pxa_posix_asset_worker_stack_bytes(pxa_posix_asset_worker_t *);
pxa_status_t pxa_posix_asset_worker_read(pxa_posix_asset_worker_t *, uint64_t owner,
    pxa_bytes_t path, uint32_t offset, uint32_t bytes, uint64_t *ticket);
pxa_status_t pxa_posix_asset_worker_read_result(pxa_posix_asset_worker_t *, uint64_t owner,
    uint64_t ticket, pxa_bytes_t *result);
pxa_status_t pxa_posix_asset_worker_read_release(pxa_posix_asset_worker_t *, uint64_t owner, uint64_t ticket);
pxa_status_t pxa_posix_asset_worker_query(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state);
pxa_status_t pxa_posix_asset_worker_acquire(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset);
pxa_status_t pxa_posix_asset_worker_cancel(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_asset_ticket_t ticket);
pxa_status_t pxa_posix_asset_worker_release(pxa_posix_asset_worker_t *worker,
    uint64_t owner, pxa_asset_ticket_t ticket);
/* Nonblocking pressure signal. Returns planned object bytes, not freed RAM. */
size_t pxa_posix_asset_worker_trim(pxa_posix_asset_worker_t *worker,
                                  uint8_t memory_class, size_t needed_bytes);
void pxa_posix_asset_worker_cancel_owner(pxa_posix_asset_worker_t *worker, uint64_t owner);
void pxa_posix_asset_worker_stats(pxa_posix_asset_worker_t *worker,
    pxa_asset_cache_stats_t *stats, size_t *metadata_bytes);
/* Nonblocking shutdown: invalidates requests and wakes reclamation. Returns
 * WOULD_BLOCK while I/O or frame-held references still exist. Retry after
 * releasing bindings/frames; only OK joins the worker and frees its storage. */
pxa_status_t pxa_posix_asset_worker_destroy(pxa_posix_asset_worker_t *worker);

#ifdef __cplusplus
}
#endif
#endif
