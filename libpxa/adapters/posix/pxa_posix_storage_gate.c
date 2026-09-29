#define _POSIX_C_SOURCE 200809L
#include "pxa/posix/pxa_posix_storage_gate.h"
#include "pxa/asset_loader.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct pxa_posix_storage_gate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pxa_posix_storage_gate_config_t config;
    pxa_posix_storage_stats_t stats;
    uint64_t started, stall_until;
};
static uint64_t clock_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
static void wait_tick(pxa_posix_storage_gate_t *g, uint64_t until) {
    uint64_t tick=clock_us()+10000u;
    if (until && tick>until) tick=until;
    struct timespec t={(time_t)(tick/1000000u),(long)(tick%1000000u)*1000};
    (void)pthread_cond_timedwait(&g->condition,&g->mutex,&t);
}
/* Cancellation callbacks may take other short locks; never call under ours. */
static int is_cancelled(pxa_posix_storage_gate_t *g,void *ctx,int (*cancelled)(void *)) {
    if (!cancelled) return 0;
    pthread_mutex_unlock(&g->mutex); int result=cancelled(ctx); pthread_mutex_lock(&g->mutex);
    return result;
}
pxa_status_t pxa_posix_storage_gate_create(const pxa_posix_storage_gate_config_t *config,
    const pxa_memory_allocator_t *allocator,pxa_posix_storage_gate_t **out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out=NULL;
    if (!config || !allocator) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_posix_storage_gate_t *g=pxa_memory_allocate(allocator,sizeof(*g));
    if (!g) return PXA_STATUS_RESOURCE_LIMIT;
    memset(g,0,sizeof(*g)); g->config=*config; g->started=clock_us();
    if (pthread_mutex_init(&g->mutex,NULL)) { pxa_memory_release(g); return PXA_STATUS_RESOURCE_LIMIT; }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr)) { pthread_mutex_destroy(&g->mutex); pxa_memory_release(g); return PXA_STATUS_RESOURCE_LIMIT; }
    int error=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
    if (!error) error=pthread_cond_init(&g->condition,&attr);
    pthread_condattr_destroy(&attr);
    if (error) { pthread_mutex_destroy(&g->mutex); pxa_memory_release(g); return PXA_STATUS_RESOURCE_LIMIT; }
    *out=g; return PXA_STATUS_OK;
}
void pxa_posix_storage_gate_destroy(pxa_posix_storage_gate_t *g) {
    if (!g) return;
    pthread_cond_destroy(&g->condition); pthread_mutex_destroy(&g->mutex); pxa_memory_release(g);
}
pxa_status_t pxa_posix_storage_gate_read(pxa_posix_storage_gate_t *g,unsigned lane,
    int fd,uint8_t *buffer,size_t capacity,size_t *bytes,void *ctx,int (*cancelled)(void *)) {
    if (!g || lane>1 || !buffer || !bytes || capacity>PXA_ASSET_READ_CHUNK_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
    *bytes=0;
    uint64_t started=clock_us();
    pthread_mutex_lock(&g->mutex);
    if (g->stats.waiting[0]+g->stats.waiting[1]>=8) { pthread_mutex_unlock(&g->mutex); return PXA_STATUS_WOULD_BLOCK; }
    ++g->stats.waiting[lane];
    pxa_status_t status=PXA_STATUS_OK;
    for (;;) {
        if (is_cancelled(g,ctx,cancelled)) { status=PXA_STATUS_CANCELLED; break; }
        if (!g->stats.active && (lane==PXA_STORAGE_MUSIC || !g->stats.waiting[PXA_STORAGE_MUSIC])) break;
        wait_tick(g,0);
    }
    --g->stats.waiting[lane];
    if (status) { pthread_cond_broadcast(&g->condition); pthread_mutex_unlock(&g->mutex); return status; }
    g->stats.active=1;
    uint64_t granted=clock_us();
    uint64_t cost=g->config.latency_us;
    if (g->config.bytes_per_second) cost+=(uint64_t)capacity*1000000u/g->config.bytes_per_second+
        ((uint64_t)capacity*1000000u%g->config.bytes_per_second!=0);
    uint64_t until=granted+cost;
    for (;;) {
        if (is_cancelled(g,ctx,cancelled)) { status=PXA_STATUS_CANCELLED; break; }
        uint64_t deadline=until>g->stall_until ? until : g->stall_until;
        if (clock_us()>=deadline) break;
        wait_tick(g,deadline);
    }
    pthread_mutex_unlock(&g->mutex);
    if (!status) {
        ssize_t n;
        do { n=read(fd,buffer,capacity); } while(n<0 && errno==EINTR);
        if(n<0) status=PXA_STATUS_IO_ERROR; else *bytes=(size_t)n;
    }
    uint64_t ended=clock_us();
    pthread_mutex_lock(&g->mutex);
    pxa_posix_storage_lane_t *s=&g->stats.lanes[lane];
    ++s->reads; s->bytes+=*bytes; s->errors+=status!=0;
    s->wait_us+=granted-started; s->service_us+=ended-granted;
    if (s->max_wait_us<granted-started) s->max_wait_us=granted-started;
    if (s->max_service_us<ended-granted) s->max_service_us=ended-granted;
    if (s->max_read_bytes<capacity) s->max_read_bytes=(uint32_t)capacity;
    g->stats.active=0; pthread_cond_broadcast(&g->condition); pthread_mutex_unlock(&g->mutex);
    return status;
}
void pxa_posix_storage_gate_stall(pxa_posix_storage_gate_t *g,uint32_t us) {
    if (!g) return;
    pthread_mutex_lock(&g->mutex); g->stall_until=clock_us()+us;
    if(us) { ++g->stats.stalls; g->stats.stall_requested_us+=us; }
    pthread_cond_broadcast(&g->condition); pthread_mutex_unlock(&g->mutex);
}
void pxa_posix_storage_gate_stats(pxa_posix_storage_gate_t *g,pxa_posix_storage_stats_t *out) {
    if (!out) return;
    memset(out,0,sizeof(*out)); if(!g) return;
    pthread_mutex_lock(&g->mutex); *out=g->stats; out->elapsed_us=clock_us()-g->started; pthread_mutex_unlock(&g->mutex);
}
