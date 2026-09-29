#include "pxa/asset_read.h"
#include <assert.h>
#include <string.h>

#define QUEUED 1u
#define LOADING 2u
#define READY 3u
#define RELEASING 4u
static int slot(const pxa_asset_read_queue_t *q, uint64_t token) {
    if (q && token) for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i)
        if (q->slots[i].state && q->slots[i].job.token == token) return (int)i;
    return -1;
}
void pxa_asset_read_init(pxa_asset_read_queue_t *q, const pxa_memory_allocator_t *a) {
    memset(q, 0, sizeof(*q)); q->allocator = a;
}
pxa_status_t pxa_asset_read_begin(pxa_asset_read_queue_t *q, uint64_t owner,
    const pxa_asset_blob_block_t *block, uint32_t offset, uint32_t bytes, uint64_t *ticket) {
    if (!ticket) return PXA_STATUS_INVALID_ARGUMENT;
    *ticket = 0;
    if (!q || !owner || !block || !bytes || bytes > PXA_ASSET_READ_MAX_BYTES ||
        offset < block->offset || offset > block->info.stored_bytes ||
        block->bytes > PXA_ASSET_BLOB_BLOCK_BYTES ||
        (block->bytes ? offset - block->offset >= block->bytes : offset != block->info.stored_bytes))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!q->allocator || q->allocator->kind != PXA_MEMORY_TEMPORARY) return PXA_STATUS_UNSUPPORTED;
    if (q->closing) return PXA_STATUS_BAD_STATE;
    if (q->sequence == UINT64_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i) if (!q->slots[i].state) {
        q->slots[i].job = (pxa_asset_read_job_t){*block, ++q->sequence, offset, bytes, PXA_ASSET_READ_JOB_LOAD, NULL};
        q->slots[i].job.block.offset = offset;
        q->slots[i].job.block.bytes = block->info.stored_bytes - offset;
        if (q->slots[i].job.block.bytes > bytes) q->slots[i].job.block.bytes = bytes;
        q->slots[i].owner = owner; q->slots[i].state = QUEUED;
        *ticket = q->sequence; return PXA_STATUS_OK;
    }
    return PXA_STATUS_WOULD_BLOCK;
}
pxa_status_t pxa_asset_read_result(pxa_asset_read_queue_t *q, uint64_t owner,
    uint64_t token, pxa_bytes_t *out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = (pxa_bytes_t){NULL,0};
    int i = slot(q,token);
    if (i < 0 || q->slots[i].owner != owner || q->slots[i].drop) return PXA_STATUS_NOT_FOUND;
    if (q->slots[i].state != READY) return PXA_STATUS_WOULD_BLOCK;
    if (!q->slots[i].status) *out = (pxa_bytes_t){q->slots[i].job.buffer,q->slots[i].result_bytes};
    return q->slots[i].status;
}
pxa_status_t pxa_asset_read_release(pxa_asset_read_queue_t *q, uint64_t owner, uint64_t token) {
    int i = slot(q,token);
    if (i < 0 || q->slots[i].owner != owner) return PXA_STATUS_NOT_FOUND;
    if (q->slots[i].state == QUEUED || (q->slots[i].state == READY && !q->slots[i].job.buffer))
        memset(&q->slots[i],0,sizeof(q->slots[i]));
    else q->slots[i].drop = 1;
    return PXA_STATUS_OK;
}
void pxa_asset_read_cancel_owner(pxa_asset_read_queue_t *q, uint64_t owner) {
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i)
        if (q->slots[i].state && q->slots[i].owner == owner)
            (void)pxa_asset_read_release(q,owner,q->slots[i].job.token);
}
void pxa_asset_read_shutdown(pxa_asset_read_queue_t *q) {
    q->closing = 1;
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i)
        if (q->slots[i].state) (void)pxa_asset_read_release(q,q->slots[i].owner,q->slots[i].job.token);
}
int pxa_asset_read_drained(const pxa_asset_read_queue_t *q) {
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i) if (q->slots[i].state) return 0;
    return 1;
}
pxa_status_t pxa_asset_read_next(pxa_asset_read_queue_t *q, pxa_asset_read_job_t *job) {
    int oldest = -1;
    memset(job,0,sizeof(*job));
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i)
        if (q->slots[i].state == LOADING || q->slots[i].state == RELEASING) return PXA_STATUS_BUSY;
    for (unsigned i = 0; i < PXA_ASSET_READ_SLOTS; ++i) {
        if (q->slots[i].state == READY && q->slots[i].drop) {
            *job = q->slots[i].job; job->kind = PXA_ASSET_READ_JOB_RELEASE;
            q->slots[i].state = RELEASING; return PXA_STATUS_OK;
        }
        if (q->slots[i].state == QUEUED &&
            (oldest < 0 || q->slots[i].job.token < q->slots[oldest].job.token)) oldest = (int)i;
    }
    if (oldest < 0) return PXA_STATUS_NOT_FOUND;
    *job = q->slots[oldest].job; q->slots[oldest].state = LOADING;
    return PXA_STATUS_OK;
}
int pxa_asset_read_cancelled(const pxa_asset_read_queue_t *q, uint64_t token) {
    int i = slot(q,token);
    return i < 0 || q->closing || q->slots[i].drop;
}
void pxa_asset_read_finish(pxa_asset_read_queue_t *q, uint64_t token,
    pxa_status_t status, uint8_t *result, size_t bytes) {
    int i = slot(q,token);
    assert(i >= 0 && q->slots[i].state == LOADING);
    assert(status ? !result && !bytes : result && bytes >= 8 && bytes <= 8+PXA_ASSET_READ_MAX_BYTES);
    q->slots[i].job.buffer = result; q->slots[i].result_bytes = bytes;
    q->slots[i].status = status; q->slots[i].state = READY;
    if (q->slots[i].drop && !result) memset(&q->slots[i],0,sizeof(q->slots[i]));
}
void pxa_asset_read_freed(pxa_asset_read_queue_t *q, uint64_t token) {
    int i = slot(q,token); assert(i >= 0 && q->slots[i].state == RELEASING);
    memset(&q->slots[i],0,sizeof(q->slots[i]));
}
pxa_status_t pxa_asset_read_load(const pxa_asset_read_job_t *job, const pxa_asset_input_t *input,
    const pxa_memory_allocator_t *a, uint8_t **result, size_t *bytes) {
    *result = NULL; *bytes = 0;
    uint8_t *buffer = pxa_memory_allocate(a,8u+job->block.bytes);
    if (!buffer) return PXA_STATUS_RESOURCE_LIMIT;
    pxa_status_t status = pxa_asset_load_blob_block(&job->block,input,buffer+8,job->block.bytes);
    if (status) { pxa_memory_release(buffer); return status; }
    uint32_t count = job->block.bytes;
    pxa_write_u32(buffer,job->offset); pxa_write_u32(buffer+4,job->block.info.stored_bytes);
    *result = buffer; *bytes = 8u+count; return PXA_STATUS_OK;
}
