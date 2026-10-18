#include "platform_api_vmcore.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct { void *pointer; size_t capacity; } allocations[8];
static unsigned slow_calls,fail_next;
void *heap_caps_malloc(size_t bytes,unsigned caps) {
    (void)caps;
    if(fail_next) { fail_next=0; return NULL; }
    assert(bytes<1024*1024);
    for(unsigned i=0;i<8;++i) if(!allocations[i].pointer) {
        void *p=malloc(bytes); assert(p);
        allocations[i].pointer=p;allocations[i].capacity=bytes;return p;
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
        free(origin);allocations[i].pointer=NULL;return;
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
int main(void) {
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
    assert(os_mremap(p,65536,131072)==NULL && slow_calls==1);
    for(unsigned i=0;i<4096;++i)assert(p[i]==(uint8_t)(i*37+11));
    uint8_t *grown=os_mremap(p,65536,131072);
    assert(grown && grown!=p && slow_calls==2);
    for(unsigned i=0;i<4096;++i)assert(grown[i]==(uint8_t)(i*37+11));
    for(unsigned i=4096;i<131072;++i)assert(grown[i]==0);
    os_munmap(grown,131072);
    for(unsigned i=0;i<8;++i)assert(!allocations[i].pointer);
    puts("ESP WAMR mapping: stable reserved growth, zeroed tail, fallback and failure preservation passed");
}
