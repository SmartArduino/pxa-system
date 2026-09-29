#ifndef PXA_ASSET_CACHE_H
#define PXA_ASSET_CACHE_H

#include "pxa/asset_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_ASSET_MEMORY_INTERNAL 0u
#define PXA_ASSET_MEMORY_EXTERNAL 1u
#define PXA_ASSET_MEMORY_CLASSES 2u
/* Keep a single 256-color RGB565 palette close to the CPU. Expanded lighting
 * tables belong with pixels in external memory: an 8 KiB table must not
 * require a contiguous SRAM block on an otherwise healthy device. The same
 * policy is used by LOAD, PREFETCH and path inspection. */
static inline uint8_t pxa_asset_default_memory_class(const pxa_asset_info_t *info) {
    return info->kind == PXA_ASSET_PALETTE && info->decoded_bytes <= 512u
        ? PXA_ASSET_MEMORY_INTERNAL : PXA_ASSET_MEMORY_EXTERNAL;
}
#define PXA_ASSET_REQUEST_QUEUED 1u
#define PXA_ASSET_REQUEST_ABSENT 0u
#define PXA_ASSET_REQUEST_LOADING 2u
#define PXA_ASSET_REQUEST_READY 3u
#define PXA_ASSET_REQUEST_FAILED 4u
#define PXA_ASSET_REQUEST_CANCELLED 5u
#define PXA_ASSET_JOB_LOAD 1u
#define PXA_ASSET_JOB_RELEASE 2u
#define PXA_ASSET_JOB_NOTIFY 3u

typedef uint64_t pxa_asset_ticket_t;
typedef struct pxa_asset_cache pxa_asset_cache_t;
typedef struct {
    uint16_t max_entries;
    uint16_t max_requests;
    uint16_t max_requests_per_owner;
    uint16_t max_pending;
    size_t resident_limit[PXA_ASSET_MEMORY_CLASSES];
    size_t owner_limit[PXA_ASSET_MEMORY_CLASSES];
} pxa_asset_cache_config_t;

typedef struct {
    /* Includes final allocations reserved by a loader and pending real frees. */
    size_t charged[PXA_ASSET_MEMORY_CLASSES];
    size_t peak_charged[PXA_ASSET_MEMORY_CLASSES];
    size_t loading[PXA_ASSET_MEMORY_CLASSES];
    uint64_t cache_hits;
    uint64_t coalesced_requests;
    uint64_t evictions;
    uint64_t load_failures;
    uint64_t prefetch_requests;
    uint64_t prefetch_failures; /* Subset of load_failures with no foreground waiter. */
    uint64_t pressure_retries;
    uint32_t requests;
    uint32_t queued;
    uint32_t ready;
    uint8_t worker_busy;
} pxa_asset_cache_stats_t;

typedef struct {
    uint8_t state;
    pxa_status_t status;
} pxa_asset_request_state_t;

typedef struct {
    uint8_t kind;
    uint8_t memory_class;
    uint64_t token; /* Internal entry generation, never a Guest handle. */
    uint64_t owner;
    size_t reserved_bytes;
    pxa_asset_info_t info; /* Borrowed until this job finishes. */
    pxa_raster_asset_t *asset; /* RELEASE only; transferred to the worker. */
} pxa_asset_job_t;

/* All cache calls are serialized by a Host mutex. They perform no I/O,
 * allocations, frees, or waits. Run LOAD/RELEASE jobs outside that mutex on a
 * single worker. Renderers only retain immutable pixel objects; they never
 * acquire this mutex in a pixel loop. The fixed workspace is budgeted by Host. */
size_t pxa_asset_cache_workspace_size(const pxa_asset_cache_config_t *config);
pxa_status_t pxa_asset_cache_init(void *workspace, size_t bytes,
                                  const pxa_asset_cache_config_t *config,
                                  pxa_asset_cache_t **output);
/* package_key identifies the installed package namespace and is copied. Hosts
 * with an activation-private cache can use a constant; never rehash at runtime.
 * info scalars are copied; its path, file record and digest BORROW immutable
 * installed catalog/manifest storage. That backing storage must remain alive
 * until shutdown has drained the cache and its worker has finished. A caller's
 * temporary lookup-info struct may be discarded as soon as this call returns.
 * Each caller receives an independent ticket/pin; duplicate pending requests
 * share one job, not their cancellation state. No request/entry limits shrink
 * with catalog size: retries and different owners can need separate entries. */
pxa_status_t pxa_asset_cache_request(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_ticket_t *ticket);
/* Same independent ticket contract, but queued work runs after foreground
 * requests. Release the ticket at completion to leave an evictable object. */
pxa_status_t pxa_asset_cache_prefetch(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_ticket_t *ticket);
/* A snapshot only: no ticket, pin, LRU touch or file read. ABSENT is a valid
 * result. READY can be evicted immediately; acquire through a new request. */
pxa_status_t pxa_asset_cache_inspect(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_request_state_t *state);
pxa_status_t pxa_asset_cache_query(pxa_asset_cache_t *cache, uint64_t owner,
    pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state);
/* Returns a retained pixel object. Caller releases it outside its lock. */
pxa_status_t pxa_asset_cache_acquire(pxa_asset_cache_t *cache, uint64_t owner,
    pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset);
pxa_status_t pxa_asset_cache_cancel(pxa_asset_cache_t *cache, uint64_t owner,
                                    pxa_asset_ticket_t ticket);
pxa_status_t pxa_asset_cache_release(pxa_asset_cache_t *cache, uint64_t owner,
                                     pxa_asset_ticket_t ticket);
void pxa_asset_cache_cancel_owner(pxa_asset_cache_t *cache, uint64_t owner);
/* Marks unpinned, consumer-free LRU objects for worker reclamation, by class.
 * Includes already scheduled frees in the returned conservative byte total;
 * no allocation/free/wait occurs here. References/tickets always win over
 * pressure. Host must wake the worker, then retry its failed operation. */
size_t pxa_asset_cache_trim(pxa_asset_cache_t *cache, uint8_t memory_class,
                            size_t needed_bytes);

/* NOT_FOUND means no work, BUSY means a worker already owns a job. NOTIFY has
 * no I/O action: a queued request reached an error state, so wake the runtime.
 * RELEASE transfers the last cache reference, but its bytes remain charged
 * until finish_release is called after the real free. */
pxa_status_t pxa_asset_cache_next_job(pxa_asset_cache_t *cache, pxa_asset_job_t *job);
int pxa_asset_cache_job_cancelled(pxa_asset_cache_t *cache, uint64_t token);
/* A successful load transfers one reference. On success, discard is either
 * empty or a RELEASE job that must run before taking the next job. A rejected
 * stale completion transfers nothing. A failed loader must pass NULL after
 * freeing its partial allocation; its reservation is then returned. */
pxa_status_t pxa_asset_cache_finish_load(pxa_asset_cache_t *cache,
    uint64_t token, pxa_status_t status, pxa_raster_asset_t *asset,
    pxa_asset_job_t *discard);
pxa_status_t pxa_asset_cache_finish_release(pxa_asset_cache_t *cache, uint64_t token);
void pxa_asset_cache_stats(const pxa_asset_cache_t *cache, pxa_asset_cache_stats_t *stats);
/* Cancels and releases all request tickets and schedules unreferenced assets
 * for reclamation. Frames/bindings must release their own references normally.
 * Free workspace only once drained() is true and the worker has been joined. */
void pxa_asset_cache_shutdown(pxa_asset_cache_t *cache);
int pxa_asset_cache_drained(const pxa_asset_cache_t *cache);

#ifdef __cplusplus
}
#endif
#endif
