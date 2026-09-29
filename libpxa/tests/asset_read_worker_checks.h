/* Shared behavioral checks against the actual POSIX and ESP file workers.
 * Fixture: signed metadata for assets/zmap.bin, bytes i%251, size 8193. */
#ifndef PXA_ASSET_READ_WORKER_CHECKS_H
#define PXA_ASSET_READ_WORKER_CHECKS_H
#include "pxa/assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static pxa_bytes_t blob_path_bytes(void) {
    static const char path[] = "assets/zmap.bin";
    return (pxa_bytes_t){(const uint8_t *)path,sizeof(path)-1};
}
static pxa_status_t await_blob(pxa_assets_backend_t *b, uint64_t ticket,
    pxa_bytes_t *out, void (*delay)(void)) {
    for (unsigned i=0;i<15000;++i) {
        pxa_status_t status = b->read_result(b->context,1,ticket,out);
        if (status != PXA_STATUS_WOULD_BLOCK) return status;
        delay();
    }
    assert(!"blob worker timeout"); return PXA_STATUS_INTERNAL;
}
static uint64_t begin_blob(pxa_assets_backend_t *b,uint32_t offset,uint32_t count,void (*delay)(void)) {
    uint64_t ticket = 0;
    for (unsigned i=0;i<15000;++i) {
        pxa_status_t s = b->read(b->context,1,blob_path_bytes(),offset,count,&ticket);
        if (!s) return ticket;
        assert(s == PXA_STATUS_WOULD_BLOCK); delay();
    }
    assert(!"blob queue never released"); return 0;
}
static void check_blob_result(pxa_assets_backend_t *b,uint64_t ticket,uint32_t offset,uint32_t count,void (*delay)(void)) {
    pxa_bytes_t out;
    assert(!await_blob(b,ticket,&out,delay));
    uint32_t n=8193-offset;
    if (n>count) n=count;
    assert(out.size==8+n && pxa_read_u32(out.data)==offset && pxa_read_u32(out.data+4)==8193);
    for (uint32_t i=0;i<n;++i) assert(out.data[8+i] == (offset+i)%251);
    b->read_release(b->context,1,ticket);
}
static void check_worker_blob_reads(pxa_assets_backend_t *b,const char *root,
    void (*pause_io)(void),void (*wait_io)(void),void (*resume_io)(void),void (*delay)(void)) {
    uint64_t tickets[4], next; pxa_bytes_t out;
    pause_io(); tickets[0]=begin_blob(b,0,PXA_ASSET_READ_MAX_BYTES,delay); wait_io();
    for (unsigned i=1;i<4;++i) tickets[i]=begin_blob(b,i,128,delay);
    assert(b->read(b->context,1,blob_path_bytes(),0,1,&next)==PXA_STATUS_WOULD_BLOCK && !next);
    assert(b->read_result(b->context,2,tickets[0],&out)==PXA_STATUS_NOT_FOUND);
    b->read_release(b->context,2,tickets[0]); /* wrong owner cannot cancel */
    assert(b->read_result(b->context,1,tickets[0],&out)==PXA_STATUS_WOULD_BLOCK);
    b->read_release(b->context,1,tickets[0]); /* cancel during blocked I/O */
    b->read_release(b->context,1,tickets[1]); /* queued cancellation frees slot */
    next=begin_blob(b,4090,128,delay); assert(next != tickets[0] && next != tickets[1]);
    resume_io();
    for (unsigned i=2;i<4;++i) check_blob_result(b,tickets[i],i,128,delay);
    check_blob_result(b,next,4090,128,delay);
    assert(b->read_result(b->context,1,tickets[0],&out)==PXA_STATUS_NOT_FOUND);
    const uint32_t offsets[]={0,4097,8192,8193};
    for (unsigned i=0;i<4;++i) {
        next=begin_blob(b,offsets[i],PXA_ASSET_READ_MAX_BYTES,delay);
        check_blob_result(b,next,offsets[i],PXA_ASSET_READ_MAX_BYTES,delay);
    }
    assert(b->read(b->context,1,blob_path_bytes(),8194,1,&next)==PXA_STATUS_INVALID_ARGUMENT);
    (void)root;
    puts("blob worker: real files, queue full, ownership, blocked cancellation, full/short/EOF reads and exact-range reads passed");
}
#endif
