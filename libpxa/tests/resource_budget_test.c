#undef NDEBUG
#include "pxa/resource_budget.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pxa_memory_budget_t budget;
static pxa_memory_allocator_t allocators[2];
static int fail_raw, close_during_allocate, inspect_callbacks;
static size_t release_bytes, last_needed;
static unsigned pressure_calls;
static void budget_lock(void *context) { assert(pthread_mutex_lock(context) == 0); }
static void budget_unlock(void *context) { assert(pthread_mutex_unlock(context) == 0); }
static pxa_memory_stats_t stats(pxa_memory_owner_t owner) {
    pxa_memory_stats_t s;
    assert(pxa_memory_budget_stats(&budget, owner, &s) == PXA_STATUS_OK);
    return s;
}
static void reclaim(void *context, pxa_memory_owner_t owner, uint8_t cls, size_t needed) {
    (void)context;
    if (!inspect_callbacks) return;
    assert(stats(owner).charged[cls] <= budget.config.limit[cls]);
    assert(needed > 0);
    ++pressure_calls; last_needed = needed;
}
static void *raw_allocate(void *context, size_t bytes) {
    const pxa_memory_allocator_t *a = context;
    if (inspect_callbacks) {
        /* Reentrant stats/close would deadlock if raw allocation ran locked. */
        pxa_memory_stats_t s = stats(a->owner);
        assert(s.reserved[a->memory_class] >= bytes);
        assert(s.charged[a->memory_class] >= bytes);
        if (close_during_allocate)
            assert(pxa_memory_owner_close(&budget, a->owner) == PXA_STATUS_WOULD_BLOCK);
    }
    return fail_raw ? NULL : malloc(bytes);
}
static void raw_release(void *context, void *memory) {
    const pxa_memory_allocator_t *a = context;
    if (inspect_callbacks) {
        pxa_memory_stats_t s = stats(a->owner);
        assert(s.charged[a->memory_class] >= release_bytes);
        assert(!s.reserved[a->memory_class]);
    }
    free(memory);
}
static void init_allocator(unsigned i, pxa_memory_owner_t owner, unsigned kind) {
    allocators[i] = (pxa_memory_allocator_t){&budget, owner, PXA_MEMORY_EXTERNAL,
        (uint8_t)kind, &allocators[i], raw_allocate, raw_release};
}
static void *parallel_allocate(void *context) {
    pxa_memory_allocator_t *a = context;
    for (unsigned i = 0; i < 20000; ++i) {
        unsigned char *p = pxa_memory_allocate(a, 97);
        if (p) {
            memset(p, 0xa7, 97);
            pxa_memory_stats_t s = stats(0);
            assert(s.charged[1] <= budget.config.limit[1]);
            assert(s.reserved[1] <= s.charged[1]);
            if (a->kind == PXA_MEMORY_TEMPORARY) {
                assert(s.by_kind[PXA_MEMORY_TEMPORARY][1] <= budget.config.temporary_limit[1]);
                assert(s.temporary_peak[1] <= budget.config.temporary_limit[1]);
            }
            assert(p[0] == 0xa7 && p[96] == 0xa7);
            pxa_memory_release(p);
        }
    }
    return NULL;
}
static void test_temporary_limit(void) {
    const size_t small = pxa_memory_allocation_bytes(97);
    const size_t limits[2] = {1024, 10 * small};
    pxa_memory_budget_config_t config = {
        .limit = {limits[0], limits[1]}, .lock_context = &mutex,
        .lock = budget_lock, .unlock = budget_unlock, .reclaim = reclaim,
        .temporary_limit = {0, 2 * small}
    };
    pxa_memory_owner_t first, second;
    assert(!pxa_memory_budget_init(&budget, &config));
    assert(!pxa_memory_owner_open(&budget, limits, &first));
    assert(!pxa_memory_owner_open(&budget, limits, &second));
    init_allocator(0, first, PXA_MEMORY_TEMPORARY);
    init_allocator(1, second, PXA_MEMORY_TEMPORARY);
    inspect_callbacks = 1; release_bytes = small;
    unsigned char *p = pxa_memory_allocate(&allocators[0], 97);
    void *q = pxa_memory_allocate(&allocators[1], 97);
    assert(p && q); memset(p, 0x39, 97);
    unsigned before = pressure_calls;
    assert(!pxa_memory_allocate(&allocators[0], 1));
    assert(!pxa_memory_resize(&allocators[0], p, 98));
    assert(p[0] == 0x39 && p[96] == 0x39);
    assert(pressure_calls == before); /* Cannot fix temporary pressure by eviction. */
    assert(stats(0).temporary_peak[1] == 2 * small);
    assert(stats(first).temporary_peak[1] == small);
    assert(stats(second).temporary_peak[1] == small);
    pxa_memory_allocator_t resident = allocators[0]; resident.kind = PXA_MEMORY_RASTER;
    void *r = pxa_memory_allocate(&resident, 97);
    assert(r && stats(0).charged[1] == 3 * small); /* Independent resident capacity. */
    pxa_memory_allocator_t internal = allocators[0]; internal.memory_class = PXA_MEMORY_INTERNAL;
    assert(!pxa_memory_allocate(&internal, 1)); /* Explicit zero forbids this class. */
    assert(pressure_calls == before);
    assert(pxa_memory_owner_close(&budget, second) == PXA_STATUS_WOULD_BLOCK);
    assert(!pxa_memory_allocate(&allocators[1], 1));
    /* Retiring owner's blocks continue to consume the global temporary cap. */
    assert(!pxa_memory_allocate(&allocators[0], 1));
    pxa_memory_release(q);
    assert(!pxa_memory_owner_close(&budget, second));
    assert(!pxa_memory_owner_open(&budget, limits, &second));
    init_allocator(1, second, PXA_MEMORY_TEMPORARY);
    fail_raw = 1;
    assert(!pxa_memory_allocate(&allocators[1], 97));
    assert(!stats(0).reserved[1] && stats(0).by_kind[PXA_MEMORY_TEMPORARY][1] == small);
    fail_raw = 0;
    q = pxa_memory_allocate(&allocators[1], 97); assert(q);
    pxa_memory_release(p); pxa_memory_release(q); pxa_memory_release(r);
    inspect_callbacks = 0;
    pthread_t threads[6];
    for (unsigned i = 0; i < 6; ++i)
        assert(!pthread_create(&threads[i], NULL, parallel_allocate, &allocators[i % 2]));
    for (unsigned i = 0; i < 6; ++i) assert(!pthread_join(threads[i], NULL));
    assert(!stats(0).charged[1] && !stats(0).reserved[1]);
    assert(stats(0).temporary_peak[1] == 2 * small);
    assert(!pxa_memory_owner_close(&budget, first));
    assert(!pxa_memory_owner_close(&budget, second));
}
int main(void) {
    const size_t small = pxa_memory_allocation_bytes(97);
    const size_t limits[2] = {1024, 4 * small};
    pxa_memory_budget_config_t config = {
        .limit = {1024, 4 * small}, .lock_context = &mutex,
        .lock = budget_lock, .unlock = budget_unlock, .reclaim = reclaim
    };
    pxa_memory_owner_t first, second, next;
    assert(pxa_memory_budget_init(&budget, &config) == PXA_STATUS_OK);
    assert(pxa_memory_owner_open(&budget, limits, &first) == PXA_STATUS_OK);
    const size_t second_limits[2] = {0, small};
    assert(pxa_memory_owner_open(&budget, second_limits, &second) == PXA_STATUS_OK);
    init_allocator(0, first, PXA_MEMORY_RASTER);
    init_allocator(1, second, PXA_MEMORY_AUDIO);
    inspect_callbacks = 1;
    unsigned char *p = pxa_memory_allocate(&allocators[0], 97);
    assert(p && (uintptr_t)p % _Alignof(long double) == 0);
    memset(p, 0x5a, 97);
    void *q = pxa_memory_allocate(&allocators[1], 97);
    assert(q);
    assert(!pxa_memory_allocate(&allocators[1], 1)); /* per-app quota */
    assert(pressure_calls == 1 && last_needed == pxa_memory_allocation_bytes(1));
    assert(stats(0).charged[1] == 2 * small);
    assert(stats(0).by_kind[PXA_MEMORY_RASTER][1] == small);
    assert(stats(0).by_kind[PXA_MEMORY_AUDIO][1] == small);
    void *r = pxa_memory_allocate(&allocators[0], 2 * small - pxa_memory_allocation_bytes(1) + 1);
    assert(r && stats(0).charged[1] == 4 * small);
    assert(!pxa_memory_allocate(&allocators[0], 1)); /* global quota */
    assert(!pxa_memory_resize(&allocators[0], p, 98)); /* old+new peak */
    assert(p[0] == 0x5a && p[96] == 0x5a);
    release_bytes = 2 * small;
    pxa_memory_release(r);
    release_bytes = small;
    pxa_memory_release(q);
    fail_raw = 1;
    assert(!pxa_memory_allocate(&allocators[0], 97));
    assert(stats(0).charged[1] == small && stats(0).reserved[1] == 0);
    assert(stats(0).allocation_failures == 1);
    fail_raw = 0;
    unsigned char *grown = pxa_memory_resize(&allocators[0], p, 150);
    assert(grown && grown[0] == 0x5a && grown[96] == 0x5a);
    release_bytes = pxa_memory_allocation_bytes(150);
    assert(pxa_memory_owner_close(&budget, first) == PXA_STATUS_WOULD_BLOCK);
    assert(!pxa_memory_allocate(&allocators[0], 1));
    pxa_memory_release(grown);
    assert(pxa_memory_owner_close(&budget, first) == PXA_STATUS_OK);
    assert(pxa_memory_owner_open(&budget, limits, &next) == PXA_STATUS_OK);
    assert(next != first && (uint32_t)next == (uint32_t)first);
    assert(!pxa_memory_allocate(&allocators[0], 1)); /* stale generation */
    assert(pxa_memory_owner_close(&budget, first) == PXA_STATUS_NOT_FOUND);
    init_allocator(0, next, PXA_MEMORY_RASTER);
    close_during_allocate = 1;
    p = pxa_memory_allocate(&allocators[0], 97);
    assert(p && !stats(next).reserved[1]);
    assert(!pxa_memory_allocate(&allocators[0], 1));
    release_bytes = small;
    pxa_memory_release(p);
    assert(pxa_memory_owner_close(&budget, next) == PXA_STATUS_OK);
    assert(pxa_memory_owner_open(&budget, limits, &next) == PXA_STATUS_OK);
    init_allocator(0, next, PXA_MEMORY_RASTER);
    close_during_allocate = inspect_callbacks = 0;
    inspect_callbacks = 1;
    unsigned before = pressure_calls;
    assert(!pxa_memory_allocate(&allocators[0], SIZE_MAX));
    assert(!pxa_memory_allocate(&allocators[0], config.limit[1] + 1));
    assert(pressure_calls == before); /* Impossible request must not evict. */
    assert(!pxa_memory_allocate(&allocators[0], 0));
    inspect_callbacks = 0;
    pthread_t threads[6];
    for (unsigned i = 0; i < 6; ++i)
        assert(!pthread_create(&threads[i], NULL, parallel_allocate, &allocators[i % 2]));
    for (unsigned i = 0; i < 6; ++i) assert(!pthread_join(threads[i], NULL));
    assert(!stats(0).charged[0] && !stats(0).charged[1] && !stats(0).reserved[1]);
    assert(stats(0).peak[1] == config.limit[1]);
    assert(pxa_memory_owner_close(&budget, next) == PXA_STATUS_OK);
    assert(pxa_memory_owner_close(&budget, second) == PXA_STATUS_OK);
    pxa_memory_owner_t owners[PXA_MEMORY_MAX_OWNERS];
    for (unsigned i = 0; i < PXA_MEMORY_MAX_OWNERS; ++i)
        assert(pxa_memory_owner_open(&budget, limits, &owners[i]) == PXA_STATUS_OK);
    assert(pxa_memory_owner_open(&budget, limits, &next) == PXA_STATUS_RESOURCE_LIMIT);
    for (unsigned i = 0; i < PXA_MEMORY_MAX_OWNERS; ++i)
        assert(pxa_memory_owner_close(&budget, owners[i]) == PXA_STATUS_OK);
    test_temporary_limit();
    printf("resource budget: shared/temporary quotas, peak reservations, rollback, owner generation and 240000 concurrent operations passed; counter storage=%zu, prefix=%zu\n",
        sizeof(budget), pxa_memory_allocation_bytes(1) - 1);
    return 0;
}
