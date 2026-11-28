#include "platform_api_vmcore.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../../../../libpxa/adapters/wamr/pxa_wamr_load_memory.h"
#if defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE) && CONFIG_CACHE_L2_CACHE_LINE_SIZE > 64
#define EXPECTED_ALIGNMENT CONFIG_CACHE_L2_CACHE_LINE_SIZE
#else
#define EXPECTED_ALIGNMENT 64u
#endif
static struct { void *pointer; void *origin; size_t capacity, reserved; } allocations[8];
static unsigned slow_calls,fail_next,raw_residue;
static unsigned realloc_calls;
static size_t adjacent_bytes;
void *heap_caps_malloc(size_t bytes,unsigned caps) {
    (void)caps;
    if(fail_next) { fail_next=0; return NULL; }
    assert(bytes<1024*1024);
    for(unsigned i=0;i<8;++i) if(!allocations[i].pointer) {
        void *origin=malloc(bytes+adjacent_bytes+2*EXPECTED_ALIGNMENT); assert(origin);
        void *p=(void *)((((uintptr_t)origin+EXPECTED_ALIGNMENT-1)&~(uintptr_t)(EXPECTED_ALIGNMENT-1))+raw_residue);
        allocations[i].origin=origin;allocations[i].pointer=p;allocations[i].capacity=bytes;
        allocations[i].reserved=bytes+adjacent_bytes;return p;
    }
    assert(0);return NULL;
}
void *heap_caps_realloc(void *p,size_t bytes,unsigned caps) {
    ++realloc_calls;
    if(fail_next) {fail_next=0; return NULL;}
    for(unsigned i=0;i<8;++i) if(allocations[i].pointer==p) {
        assert(caps==MALLOC_CAP_8BIT && bytes>allocations[i].capacity);
        if(bytes<=allocations[i].reserved) {
            allocations[i].capacity=bytes;return p;
        }
        void *next=heap_caps_malloc(bytes,caps);
        if(next) {
            memcpy(next,p,allocations[i].capacity);
            free(allocations[i].origin);allocations[i].pointer=NULL;
        }
        return next;
    }
    assert(0);return NULL;
}
size_t heap_caps_get_allocated_size(void *p) {
    for(unsigned i=0;i<8;++i) if(allocations[i].pointer==p) return allocations[i].capacity;
    assert(0);return 0;
}
void os_free(void *p) {
    if(!p) return;
    void *origin=(void *)*((uintptr_t *)p-1);
    for(unsigned i=0;i<8;++i) if(allocations[i].pointer==origin) {
        free(allocations[i].origin);allocations[i].pointer=NULL;return;
    }
    assert(0);
}
void *os_mremap_slow(void *old,size_t before,size_t after) {
    ++slow_calls;
    void *p=os_mmap(NULL,after,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    if(!p) return NULL;
    memcpy(p,old,before<after?before:after);
    os_munmap(old,before);return p;
}
static void *other_owner(void *original) {
    assert(pxa_wamr_load_memory_begin(8192));
    void *p=pxa_wamr_load_memory_scratch(4096);
    assert(p && p!=original);memset(p,0xcd,4096);
    assert(pxa_wamr_load_memory_release(p));
    pxa_wamr_load_memory_end();return NULL;
}
int main(void) {
    /* ESP heap allocations may have any four-byte residue. Mapping alignment
     * must survive growth, shrink and failure without losing old payload. */
    for(raw_residue=0;raw_residue<EXPECTED_ALIGNMENT;raw_residue+=4) {
        static const unsigned sizes[]={1,7,8,63,64,65,4095,4096};
        for(unsigned trial=0;trial<sizeof(sizes)/sizeof(sizes[0]);++trial) {
            unsigned n=sizes[trial];
            uint8_t *a=os_mmap(NULL,n,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
            assert(a && (uintptr_t)a%EXPECTED_ALIGNMENT==0);
            for(unsigned i=0;i<n;++i){assert(a[i]==0);a[i]=(uint8_t)(i*23+91);}
            fail_next=1;assert(!os_mremap(a,n,n+128));
            for(unsigned i=0;i<n;++i)assert(a[i]==(uint8_t)(i*23+91));
            unsigned previous_residue=raw_residue;
            raw_residue=(raw_residue+EXPECTED_ALIGNMENT/2)%EXPECTED_ALIGNMENT;
            uint8_t *grown=os_mremap(a,n,n+128);assert(grown && (uintptr_t)grown%EXPECTED_ALIGNMENT==0);
            raw_residue=previous_residue;
            for(unsigned i=0;i<n;++i)assert(grown[i]==(uint8_t)(i*23+91));
            for(unsigned i=n;i<n+128;++i)assert(grown[i]==0);
            uint8_t *small=os_mremap(grown,n+128,1);assert(small && (uintptr_t)small%EXPECTED_ALIGNMENT==0 && small[0]==91);
            os_munmap(small,1);
        }
    }
    slow_calls=raw_residue=realloc_calls=0;

    assert(os_mmap(NULL, SIZE_MAX-64, MMAP_PROT_NONE, MMAP_MAP_NONE, 0)==NULL);
    assert(os_mmap(NULL, SIZE_MAX, MMAP_PROT_NONE, MMAP_MAP_NONE, 0)==NULL);
    assert(os_mmap(NULL, 0, MMAP_PROT_NONE, MMAP_MAP_NONE, 0)==NULL);
    uint8_t *p=os_mmap(NULL,65536,MMAP_PROT_NONE,MMAP_MAP_NONE,0);assert(p);
    for(unsigned i=0;i<4096;++i)p[i]=(uint8_t)(i*37+11);
    memset(p+4096,0xab,12288);
    uint8_t *same=os_mremap(p,4096,8192);
    assert(same==p && !slow_calls);
    for(unsigned i=0;i<4096;++i)assert(p[i]==(uint8_t)(i*37+11));
    for(unsigned i=4096;i<8192;++i)assert(p[i]==0);
    for(unsigned i=8192;i<16384;++i)assert(p[i]==0xab);
    same=os_mremap(p,8192,65536);assert(same==p && !slow_calls);
    for(unsigned i=8192;i<65536;++i)assert(p[i]==0);
    fail_next=1;
    assert(!os_mremap(p,65536,SIZE_MAX) && !realloc_calls);
    assert(os_mremap(p,65536,131072)==NULL && realloc_calls==1 && !slow_calls);
    for(unsigned i=0;i<4096;++i)assert(p[i]==(uint8_t)(i*37+11));
    uint8_t *grown=os_mremap(p,65536,131072);
    assert(grown && grown!=p && realloc_calls==2 && !slow_calls);
    for(unsigned i=0;i<4096;++i)assert(grown[i]==(uint8_t)(i*37+11));
    for(unsigned i=4096;i<131072;++i)assert(grown[i]==0);
    os_munmap(grown,131072);
    /* Only the adjacent free block is available. The next full allocation
     * would fail, but extending this one preserves native addresses. */
    adjacent_bytes=65536;
    p=os_mmap(NULL,65536,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    adjacent_bytes=0;
    assert(p);memset(p,0xc7,65536);
    assert(os_mremap(p,65536,131072)==p && realloc_calls==3 && !slow_calls);
    for(unsigned i=0;i<65536;++i)assert(p[i]==0xc7);
    for(unsigned i=65536;i<131072;++i)assert(!p[i]);
    os_munmap(p,131072);
    assert(!pxa_wamr_load_memory_begin(0));
    assert(!pxa_wamr_load_memory_begin(SIZE_MAX));
    fail_next=1;assert(!pxa_wamr_load_memory_begin(65536));
    assert(pxa_wamr_load_memory_begin(65536));
    assert(!pxa_wamr_load_memory_begin(65536));
    assert(!pxa_wamr_load_memory_scratch(0));
    assert(!pxa_wamr_load_memory_scratch(65537));
    uint8_t *a=pxa_wamr_load_memory_scratch(30001);
    uint8_t *b=pxa_wamr_load_memory_scratch(30000);
    assert(a && b && (uintptr_t)b%16==0 && b>=a+30001);
    memset(a,0xa5,30001);memset(b,0x5a,30000);
    pthread_t thread;assert(!pthread_create(&thread,NULL,other_owner,a));
    assert(!pthread_join(thread,NULL));assert(a[0]==0xa5 && b[0]==0x5a);
    pxa_wamr_load_memory_ready(); // Live scratch cannot be adopted.
    uint8_t *separate=os_mmap(NULL,65536,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    assert(separate && separate!=a);os_munmap(separate,65536);
    assert(!pxa_wamr_load_memory_release(&thread));
    assert(pxa_wamr_load_memory_release(a) && pxa_wamr_load_memory_release(b));
    pxa_wamr_load_memory_ready();assert(!pxa_wamr_load_memory_scratch(1));
    uint8_t *adopted=os_mmap(NULL,65536,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    assert(adopted<=a && (size_t)(a-adopted)<16 && !((uintptr_t)adopted&63));
    for(unsigned i=0;i<65536;++i)assert(adopted[i]==0);
    pxa_wamr_load_memory_end();adopted[0]=47;
    assert(adopted[0]==47);os_munmap(adopted,65536);
    /* A fixed AOT memory may export malloc/free, omitting the optional Host
     * heap. Adopt the smaller mapping without freeing/reallocating its block,
     * and clear all temporary loader data in the retained capacity. */
    assert(pxa_wamr_load_memory_begin(65536+16384));
    uint8_t *scratch=pxa_wamr_load_memory_scratch(65536+16384);
    assert(scratch);memset(scratch,0xa5,65536+16384);
    assert(pxa_wamr_load_memory_release(scratch));pxa_wamr_load_memory_ready();
    adopted=os_mmap(NULL,65536,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    assert(adopted==scratch);
    for(unsigned i=0;i<65536+16384;++i)assert(adopted[i]==0);
    pxa_wamr_load_memory_end();os_munmap(adopted,65536);
    assert(pxa_wamr_load_memory_begin(4096));
    assert(pxa_wamr_load_memory_scratch(32));
    pxa_wamr_load_memory_end(); // Failed loader abandons scratch and mapping.
    assert(pxa_wamr_load_memory_begin(4096));
    pxa_wamr_load_memory_ready();
    uint8_t *different=os_mmap(NULL,8192,MMAP_PROT_NONE,MMAP_MAP_NONE,0);
    assert(different);pxa_wamr_load_memory_end();os_munmap(different,8192);
    for(unsigned i=0;i<8;++i)assert(!allocations[i].pointer);
    puts("ESP WAMR: stable growth, reservation scratch/adoption, zeroing, owner isolation and failures passed");
}
