#undef NDEBUG
#include "pxa/asset_read.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pxa_memory_budget_t budget;
static unsigned position, cancelled;
static pxa_status_t read_status;
static void *allocate(void *ctx, size_t n) { (void)ctx; return malloc(n); }
static void release(void *ctx, void *p) { (void)ctx; free(p); }
static pxa_status_t read_input(void *ctx, uint8_t *out, size_t capacity, size_t *n) {
    (void)ctx; if (read_status) return read_status; *n = capacity > 17 ? 17 : capacity;
    for (size_t i = 0; i < *n; ++i) out[i] = (uint8_t)(position++ % 251);
    return 0;
}
static int cancel(void *ctx) { (void)ctx; return (int)cancelled; }
static size_t charged(void) {
    pxa_memory_stats_t stats; assert(!pxa_memory_budget_stats(&budget,0,&stats));
    return stats.charged[1];
}
static void free_next(pxa_asset_read_queue_t *q, uint64_t token) {
    pxa_asset_read_job_t job;
    assert(!pxa_asset_read_next(q,&job) && job.kind == PXA_ASSET_READ_JOB_RELEASE && job.token == token);
    assert(charged());
    pxa_memory_release(job.buffer); assert(!charged());
    assert(!pxa_asset_read_drained(q)); /* slot lives through actual free */
    pxa_asset_read_freed(q,token);
}
int main(void) {
    const size_t capacity = pxa_memory_allocation_bytes(136);
    pxa_memory_budget_config_t config = {.limit={0,8192},.temporary_limit={0,capacity}};
    pxa_memory_owner_t owner; const size_t limits[2] = {0,8192};
    assert(!pxa_memory_budget_init(&budget,&config));
    assert(!pxa_memory_owner_open(&budget,limits,&owner));
    pxa_memory_allocator_t allocator = {&budget,owner,1,PXA_MEMORY_TEMPORARY,NULL,allocate,release};
    pxa_asset_read_queue_t q;
    pxa_asset_read_job_t job, ignored;
    pxa_asset_blob_block_t block = {0};
    block.info.kind = PXA_ASSET_BLOB; block.info.stored_bytes = 8193;
    block.offset = 4090; block.bytes = 4096;
    pxa_asset_input_t input = {NULL,read_input,cancel,NULL};
    uint64_t tickets[4], next;
    pxa_bytes_t result; uint8_t *buffer; size_t size;
    pxa_asset_read_init(&q,NULL);
    assert(pxa_asset_read_begin(&q,1,&block,4090,1,&next) == PXA_STATUS_UNSUPPORTED);
    pxa_asset_read_init(&q,&allocator);
    assert(pxa_asset_read_begin(&q,1,&block,0,1,&next) == PXA_STATUS_INVALID_ARGUMENT);
    for (unsigned i = 0; i < 4; ++i) assert(!pxa_asset_read_begin(&q,i==3?2:1,&block,4090,128,&tickets[i]));
    assert(pxa_asset_read_begin(&q,1,&block,4090,1,&next) == PXA_STATUS_WOULD_BLOCK && !next);
    assert(pxa_asset_read_result(&q,2,tickets[0],&result) == PXA_STATUS_NOT_FOUND);
    assert(pxa_asset_read_release(&q,2,tickets[0]) == PXA_STATUS_NOT_FOUND);
    assert(!pxa_asset_read_next(&q,&job) && job.token == tickets[0]);
    assert(pxa_asset_read_next(&q,&ignored) == PXA_STATUS_BUSY);
    position = job.block.offset;
    assert(!pxa_asset_read_load(&job,&input,&allocator,&buffer,&size));
    assert(position == 4218 && size == 136 && charged() == capacity);
    assert(pxa_read_u32(buffer) == 4090 && pxa_read_u32(buffer+4) == 8193);
    for (unsigned i=0;i<128;++i) assert(buffer[8+i] == (4090+i)%251);
    pxa_asset_read_finish(&q,job.token,0,buffer,size);
    assert(!pxa_asset_read_result(&q,1,tickets[0],&result) && result.data == buffer && result.size == 136);
    assert(!pxa_asset_read_next(&q,&job) && job.token == tickets[1]);
    assert(pxa_asset_read_load(&job,&input,&allocator,&buffer,&size) == PXA_STATUS_RESOURCE_LIMIT);
    assert(!buffer && !size && position == 4218);
    pxa_asset_read_finish(&q,job.token,PXA_STATUS_RESOURCE_LIMIT,NULL,0);
    assert(pxa_asset_read_result(&q,1,tickets[1],&result) == PXA_STATUS_RESOURCE_LIMIT);
    assert(!pxa_asset_read_release(&q,1,tickets[1]));
    assert(!pxa_asset_read_release(&q,1,tickets[0])); free_next(&q,tickets[0]);
    assert(!pxa_asset_read_next(&q,&job) && job.token == tickets[2]);
    pxa_asset_read_cancel_owner(&q,1);
    assert(pxa_asset_read_cancelled(&q,job.token));
    cancelled = 1;
    assert(pxa_asset_read_load(&job,&input,&allocator,&buffer,&size) == PXA_STATUS_CANCELLED);
    assert(!charged()); pxa_asset_read_finish(&q,job.token,PXA_STATUS_CANCELLED,NULL,0);
    cancelled = 0;
    assert(!pxa_asset_read_next(&q,&job) && job.token == tickets[3]);
    position = 0; read_status = PXA_STATUS_IO_ERROR;
    assert(pxa_asset_read_load(&job,&input,&allocator,&buffer,&size) == PXA_STATUS_IO_ERROR && !charged());
    pxa_asset_read_finish(&q,job.token,PXA_STATUS_IO_ERROR,NULL,0);
    assert(!pxa_asset_read_release(&q,2,tickets[3]));
    assert(pxa_asset_read_drained(&q));
    read_status = 0; block.offset = 0;
    assert(!pxa_asset_read_begin(&q,1,&block,3,1,&next) && next > tickets[3]);
    assert(!pxa_asset_read_next(&q,&job)); position = 0;
    assert(!pxa_asset_read_load(&job,&input,&allocator,&buffer,&size));
    pxa_asset_read_shutdown(&q); /* shutdown races successful I/O */
    assert(pxa_asset_read_begin(&q,1,&block,0,1,&tickets[0]) == PXA_STATUS_BAD_STATE);
    assert(!pxa_asset_read_drained(&q));
    pxa_asset_read_finish(&q,job.token,0,buffer,size); free_next(&q,next);
    assert(pxa_asset_read_drained(&q));
    pxa_asset_read_init(&q,&allocator);
    block.offset = 8193; block.bytes = 0;
    assert(!pxa_asset_read_begin(&q,1,&block,8193,PXA_ASSET_READ_MAX_BYTES,&next));
    assert(!pxa_asset_read_next(&q,&job));
    unsigned before = position;
    assert(!pxa_asset_read_load(&job,&input,&allocator,&buffer,&size) && size == 8 && position == before);
    pxa_asset_read_finish(&q,job.token,0,buffer,size);
    assert(!pxa_asset_read_release(&q,1,next)); free_next(&q,next);
    q.sequence = UINT64_MAX;
    assert(pxa_asset_read_begin(&q,1,&block,8193,1,&next) == PXA_STATUS_RESOURCE_LIMIT);
    assert(!pxa_memory_owner_close(&budget,owner));
    printf("asset read: FIFO, ownership, retained budget=%zu B, pressure recovery, cancellation, read failure and EOF passed\n",capacity);
}
