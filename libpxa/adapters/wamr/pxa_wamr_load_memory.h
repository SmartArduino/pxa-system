#ifndef PXA_WAMR_LOAD_MEMORY_H
#define PXA_WAMR_LOAD_MEMORY_H
#include <stddef.h>
/* ESP platform-private, owner-thread loading transaction. Reserve the pinned
 * linear mapping before AOT code; temporary section bytes may use that memory
 * until WAMR has cloned them. ready() requires all scratch leases released.
 * The next exact-sized non-executable mmap adopts and clears the reservation.
 * end() releases an unadopted reservation on every load/instantiate failure.
 * No Guest ABI or executable-memory validation is changed. */
int pxa_wamr_load_memory_begin(size_t bytes);
void *pxa_wamr_load_memory_scratch(size_t bytes);
int pxa_wamr_load_memory_release(void *pointer);
void pxa_wamr_load_memory_ready(void);
void pxa_wamr_load_memory_end(void);
/* Only temporary relocation tables use these optional loans. Permanent
 * module structures keep the normal WAMR allocator and lifetime. */
void *pxa_wamr_aot_temporary_allocate(size_t bytes);
int pxa_wamr_aot_temporary_release(void *pointer);
#endif
