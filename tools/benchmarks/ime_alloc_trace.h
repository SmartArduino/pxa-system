/* Test-only linker wrappers count requested bytes in static-linked LVGL/Host,
 * excluding allocations made internally by shared libraries and malloc metadata. */
#include <pthread.h>
static pthread_mutex_t allocation_mutex=PTHREAD_MUTEX_INITIALIZER;
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t); void __real_free(void *);
static struct { void *ptr; size_t size; } allocations[16384];
static size_t live_bytes,peak_bytes,alloc_calls;
static void forget(void *ptr) {
    if (!ptr) return;
    for (unsigned i=0;i<16384;++i) if (allocations[i].ptr==ptr) {
        live_bytes-=allocations[i].size; allocations[i].ptr=NULL; return;
    }
}
static void remember(void *ptr,size_t size) {
    if (!ptr) return;
    for (unsigned i=0;i<16384;++i) if (!allocations[i].ptr) {
        allocations[i].ptr=ptr; allocations[i].size=size; live_bytes+=size;
        if (live_bytes>peak_bytes) peak_bytes=live_bytes;
        ++alloc_calls; return;
    }
    abort();
}
void *__wrap_malloc(size_t size) {
    pthread_mutex_lock(&allocation_mutex);
    void *p=__real_malloc(size); remember(p,size);
    pthread_mutex_unlock(&allocation_mutex);
    return p;
}
void *__wrap_calloc(size_t n,size_t size) {
    pthread_mutex_lock(&allocation_mutex);
    void *p=__real_calloc(n,size); remember(p,n*size);
    pthread_mutex_unlock(&allocation_mutex);
    return p;
}
void __wrap_free(void *p) {
    pthread_mutex_lock(&allocation_mutex);
    forget(p); __real_free(p);
    pthread_mutex_unlock(&allocation_mutex);
}
void *__wrap_realloc(void *p,size_t size) {
    /* Remove before calling realloc, so no freed pointer is read afterwards. */
    pthread_mutex_lock(&allocation_mutex);
    size_t old=0;
    for (unsigned i=0;i<16384;++i) if (allocations[i].ptr==p) { old=allocations[i].size; break; }
    forget(p); void *next=__real_realloc(p,size);
    if (next) remember(next,size); else if (size && p) remember(p,old);
    pthread_mutex_unlock(&allocation_mutex);
    return next;
}
