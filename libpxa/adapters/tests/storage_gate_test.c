#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/posix/pxa_posix_storage_gate.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void *allocate(void *ctx,size_t bytes) { (void)ctx; return malloc(bytes); }
static void release(void *ctx,void *p) { (void)ctx; free(p); }
static void tick(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
typedef struct {
    pxa_posix_storage_gate_t *gate;
    int fd, cancel;
    unsigned lane;
    pxa_status_t status;
    size_t bytes;
    uint8_t output[4096];
} request_t;
static int cancelled(void *ctx) { return __atomic_load_n(&((request_t*)ctx)->cancel,__ATOMIC_RELAXED); }
static void *read_task(void *ctx) {
    request_t *r=ctx;
    r->status=pxa_posix_storage_gate_read(r->gate,r->lane,r->fd,r->output,sizeof(r->output),&r->bytes,r,cancelled);
    return NULL;
}
static void await_state(pxa_posix_storage_gate_t *g,unsigned active,unsigned resources,unsigned music) {
    for(unsigned i=0;i<3000;++i) {
        pxa_posix_storage_stats_t s; pxa_posix_storage_gate_stats(g,&s);
        if(s.active==active && s.waiting[0]==resources && s.waiting[1]==music) return;
        tick();
    }
    assert(!"storage gate timed out");
}
int main(void) {
    pxa_memory_budget_t budget; pxa_memory_owner_t owner;
    pxa_memory_budget_config_t bc={.limit={0,8192},.temporary_limit={0,8192}};
    assert(!pxa_memory_budget_init(&budget,&bc)); assert(!pxa_memory_owner_open(&budget,bc.limit,&owner));
    pxa_memory_allocator_t a={&budget,owner,1,PXA_MEMORY_METADATA,NULL,allocate,release};
    pxa_posix_storage_gate_config_t config={0,0}; pxa_posix_storage_gate_t *gate;
    assert(!pxa_posix_storage_gate_create(&config,&a,&gate));
    FILE *f=tmpfile(); assert(f);
    uint8_t data[8192]; memset(data,0x37,sizeof(data)); assert(fwrite(data,1,sizeof(data),f)==sizeof(data)); fflush(f); rewind(f);
    request_t first={.gate=gate,.fd=fileno(f),.lane=PXA_STORAGE_RESOURCE};
    request_t music={.gate=gate,.fd=fileno(f),.lane=PXA_STORAGE_MUSIC};
    request_t queued={.gate=gate,.fd=fileno(f),.lane=PXA_STORAGE_RESOURCE};
    pthread_t t1,t2,t3;
    pxa_posix_storage_gate_stall(gate,2000000);
    assert(!pthread_create(&t1,NULL,read_task,&first)); await_state(gate,1,0,0);
    assert(!pthread_create(&t2,NULL,read_task,&queued)); await_state(gate,1,1,0);
    assert(!pthread_create(&t3,NULL,read_task,&music)); await_state(gate,1,1,1);
    pxa_posix_storage_gate_stall(gate,0);
    assert(!pthread_join(t1,NULL) && !pthread_join(t2,NULL) && !pthread_join(t3,NULL));
    assert(!first.status && first.bytes==4096 && !music.status && music.bytes==4096);
    assert(!queued.status && !queued.bytes); // Music consumed the second block first.
    pxa_posix_storage_gate_stall(gate,2000000);
    first.cancel=0;
    assert(!pthread_create(&t1,NULL,read_task,&first)); await_state(gate,1,0,0);
    __atomic_store_n(&first.cancel,1,__ATOMIC_RELAXED);
    assert(!pthread_join(t1,NULL)); assert(first.status==PXA_STATUS_CANCELLED && !first.bytes);
    pxa_posix_storage_stats_t stats; pxa_posix_storage_gate_stats(gate,&stats);
    assert(!stats.active && !stats.waiting[0] && !stats.waiting[1]);
    assert(stats.lanes[0].bytes==4096 && stats.lanes[1].bytes==4096 && stats.stalls==2);
    assert(stats.lanes[0].max_read_bytes==4096 && stats.lanes[0].errors==1);
    pxa_posix_storage_gate_destroy(gate); fclose(f);
    assert(!pxa_memory_owner_close(&budget,owner));
    puts("storage gate: shared bounded reads, music priority, cancellable outage, counters and budget reclamation passed");
    return 0;
}
