#ifndef PXA_RESOURCE_BUDGET_H
#define PXA_RESOURCE_BUDGET_H
#include <stddef.h>
#include "pxa/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_MEMORY_INTERNAL 0u
#define PXA_MEMORY_EXTERNAL 1u
#define PXA_MEMORY_CLASSES 2u
#define PXA_MEMORY_RASTER 0u
#define PXA_MEMORY_IMAGE 1u
#define PXA_MEMORY_AUDIO 2u
#define PXA_MEMORY_TEMPORARY 3u
#define PXA_MEMORY_METADATA 4u
#define PXA_MEMORY_FRAME 5u
#define PXA_MEMORY_KINDS 6u
#define PXA_MEMORY_MAX_OWNERS 8u

typedef uint64_t pxa_memory_owner_t;
typedef struct {
    /* Charged includes pending allocation reservations and allocations whose
     * actual free has not returned. Prefix bytes are included. */
    size_t charged[PXA_MEMORY_CLASSES];
    size_t reserved[PXA_MEMORY_CLASSES];
    size_t peak[PXA_MEMORY_CLASSES];
    size_t by_kind[PXA_MEMORY_KINDS][PXA_MEMORY_CLASSES];
    /* TEMPORARY includes reservations and old+new during resize, just like
     * charged. Report its peak separately from resident resources. */
    size_t temporary_peak[PXA_MEMORY_CLASSES];
    uint64_t denied;
    uint64_t allocation_failures;
} pxa_memory_stats_t;
typedef struct {
    size_t limit[PXA_MEMORY_CLASSES];
    void *lock_context;
    void (*lock)(void *);
    void (*unlock)(void *);
    /* Optional nonblocking pressure notification, outside the budget lock.
     * Schedule reclamation; do not wait, allocate recursively or perform I/O.
     * The failed allocation still returns NULL; callers decide when to retry. */
    void *reclaim_context;
    void (*reclaim)(void *, pxa_memory_owner_t owner, uint8_t memory_class,
                    size_t needed_bytes);
    /* Global TEMPORARY ceiling in each class, in addition to the total/global
     * and per-owner ceilings. Zero forbids temporary allocation. Not a memory
     * reservation or guarantee; other kinds may consume the total budget.
     * Includes allocator prefixes, concurrent jobs and retiring allocations. */
    size_t temporary_limit[PXA_MEMORY_CLASSES];
} pxa_memory_budget_config_t;

/* Fixed Host-owned storage; fields are private to this implementation.
 * Initialize once before use. The configured short lock serializes counters;
 * allocator callbacks never run inside it. No I/O or pixel-loop work here. */
typedef struct {
    uint32_t magic;
    pxa_memory_budget_config_t config;
    pxa_memory_stats_t stats;
    struct {
        pxa_memory_stats_t stats;
        size_t limit[PXA_MEMORY_CLASSES];
        uint32_t generation;
        uint8_t active, closing;
    } owners[PXA_MEMORY_MAX_OWNERS];
} pxa_memory_budget_t;

typedef struct {
    pxa_memory_budget_t *budget;
    pxa_memory_owner_t owner;
    uint8_t memory_class, kind;
    void *context;
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
} pxa_memory_allocator_t;

pxa_status_t pxa_memory_budget_init(pxa_memory_budget_t *budget,
    const pxa_memory_budget_config_t *config);
pxa_status_t pxa_memory_owner_open(pxa_memory_budget_t *budget,
    const size_t limit[PXA_MEMORY_CLASSES], pxa_memory_owner_t *owner);
/* Prevents new allocations immediately. WOULD_BLOCK means live allocations
 * still belong to this owner; retry after consumers release them. Never reset
 * or recycle an owner/allocator while its old allocations are alive. */
pxa_status_t pxa_memory_owner_close(pxa_memory_budget_t *budget,
    pxa_memory_owner_t owner);
pxa_status_t pxa_memory_budget_stats(pxa_memory_budget_t *budget,
    pxa_memory_owner_t owner, pxa_memory_stats_t *stats); /* owner 0 = global */

/* Allocator descriptors must remain immutable and allocator/budget storage
 * alive until every block allocated through them has been freed. Maximum fundamental C alignment is
 * preserved if the raw allocator supplies malloc-compatible alignment.
 * Returns NULL on quota, overflow, stale owner or allocator failure.
 * Resize deliberately budgets old + new; a failed resize preserves old data.
 * Allocate/resize outside real-time render/audio callbacks. Release only after
 * the final consumer completes. None of these functions are ISR-safe; do not
 * call them while holding a spinlock or an interrupt-disabled critical section. */
size_t pxa_memory_allocation_bytes(size_t payload_bytes);
void *pxa_memory_allocate(const pxa_memory_allocator_t *allocator, size_t bytes);
void *pxa_memory_resize(const pxa_memory_allocator_t *allocator, void *memory,
                        size_t bytes);
void pxa_memory_release(void *memory);

#ifdef __cplusplus
}
#endif
#endif
