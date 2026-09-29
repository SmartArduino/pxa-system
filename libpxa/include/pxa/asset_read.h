#ifndef PXA_ASSET_READ_H
#define PXA_ASSET_READ_H
#include "pxa/asset_loader.h"
#include "pxa/resource_budget.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Host profile capacity, not a Guest wire limit. Uses the existing file worker
 * and its mutex; no thread, I/O, allocation or free in the queue operations. */
#define PXA_ASSET_READ_SLOTS 4u
#define PXA_ASSET_READ_MAX_BYTES (PXA_WIRE_MAX_CONTROL_MESSAGE - PXA_WIRE_V1_SIZE - 12u)
#define PXA_ASSET_READ_RESULT_HEADER_BYTES 8u
#define PXA_ASSET_READ_JOB_LOAD 1u
#define PXA_ASSET_READ_JOB_RELEASE 2u
typedef struct {
    pxa_asset_blob_block_t block;
    uint64_t token;
    uint32_t offset, bytes;
    uint8_t kind;
    uint8_t *buffer; /* RELEASE only, freed outside the queue lock. */
} pxa_asset_read_job_t;
typedef struct {
    const pxa_memory_allocator_t *allocator;
    uint64_t sequence;
    uint8_t closing;
    struct {
        pxa_asset_read_job_t job;
        uint64_t owner;
        size_t result_bytes;
        pxa_status_t status;
        uint8_t state, drop;
    } slots[PXA_ASSET_READ_SLOTS];
} pxa_asset_read_queue_t;

/* Zero-initialized queue. Allocator is immutable through drain; NULL disables
 * reads. Catalog/block views are borrowed through job completion/release. */
void pxa_asset_read_init(pxa_asset_read_queue_t *, const pxa_memory_allocator_t *);
pxa_status_t pxa_asset_read_begin(pxa_asset_read_queue_t *, uint64_t owner,
    const pxa_asset_blob_block_t *, uint32_t offset, uint32_t bytes, uint64_t *ticket);
/* OK returns offset:u32 | total:u32 | bytes, borrowed until release. Runtime
 * serialization must prevent concurrent release/shutdown while copying it. */
pxa_status_t pxa_asset_read_result(pxa_asset_read_queue_t *, uint64_t owner,
    uint64_t ticket, pxa_bytes_t *result);
pxa_status_t pxa_asset_read_release(pxa_asset_read_queue_t *, uint64_t owner, uint64_t ticket);
void pxa_asset_read_cancel_owner(pxa_asset_read_queue_t *, uint64_t owner);
void pxa_asset_read_shutdown(pxa_asset_read_queue_t *);
int pxa_asset_read_drained(const pxa_asset_read_queue_t *);
pxa_status_t pxa_asset_read_next(pxa_asset_read_queue_t *, pxa_asset_read_job_t *);
int pxa_asset_read_cancelled(const pxa_asset_read_queue_t *, uint64_t token);
/* Called once by the single worker. LOAD transfers ownership of its result;
 * RELEASE is acknowledged only after the worker actually frees that buffer. */
void pxa_asset_read_finish(pxa_asset_read_queue_t *, uint64_t token,
    pxa_status_t status, uint8_t *result, size_t bytes);
void pxa_asset_read_freed(pxa_asset_read_queue_t *, uint64_t token);
/* Worker-only helper: budget the requested range+header, read directly, then retain only
 * the requested slice in the same allocation. Failure frees every byte. */
pxa_status_t pxa_asset_read_load(const pxa_asset_read_job_t *, const pxa_asset_input_t *,
    const pxa_memory_allocator_t *, uint8_t **result, size_t *bytes);
#ifdef __cplusplus
}
#endif
#endif
