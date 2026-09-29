#include "pxa/resource_budget.h"
#include <assert.h>
#include <string.h>

#define BUDGET_MAGIC UINT32_C(0x504d454d)
typedef union {
    struct { const pxa_memory_allocator_t *allocator; size_t bytes; } block;
    long double alignment;
    void *pointer_alignment;
    uint64_t integer_alignment;
} prefix_t;

static int valid(const pxa_memory_budget_t *budget) {
    return budget && budget->magic == BUDGET_MAGIC;
}
static void lock(pxa_memory_budget_t *b) {
    if (b->config.lock) b->config.lock(b->config.lock_context);
}
static void unlock(pxa_memory_budget_t *b) {
    if (b->config.unlock) b->config.unlock(b->config.lock_context);
}
static int owner_index(pxa_memory_budget_t *b, pxa_memory_owner_t token) {
    uint32_t index = (uint32_t)token;
    if (!index || index > PXA_MEMORY_MAX_OWNERS) return -1;
    --index;
    return b->owners[index].active &&
           b->owners[index].generation == (uint32_t)(token >> 32) ? (int)index : -1;
}
pxa_status_t pxa_memory_budget_init(pxa_memory_budget_t *b,
    const pxa_memory_budget_config_t *config) {
    if (!b || !config || (!!config->lock != !!config->unlock))
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(b, 0, sizeof(*b));
    b->config = *config;
    b->magic = BUDGET_MAGIC;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_memory_owner_open(pxa_memory_budget_t *b,
    const size_t limit[PXA_MEMORY_CLASSES], pxa_memory_owner_t *owner) {
    if (!valid(b) || !limit || !owner) return PXA_STATUS_INVALID_ARGUMENT;
    *owner = 0;
    lock(b);
    for (unsigned i = 0; i < PXA_MEMORY_MAX_OWNERS; ++i) {
        if (b->owners[i].active || b->owners[i].generation == UINT32_MAX) continue;
        ++b->owners[i].generation;
        memset(&b->owners[i].stats, 0, sizeof(b->owners[i].stats));
        memcpy(b->owners[i].limit, limit, sizeof(b->owners[i].limit));
        b->owners[i].active = 1; b->owners[i].closing = 0;
        *owner = (uint64_t)b->owners[i].generation << 32 | (i + 1u);
        unlock(b);
        return PXA_STATUS_OK;
    }
    unlock(b);
    return PXA_STATUS_RESOURCE_LIMIT;
}
pxa_status_t pxa_memory_owner_close(pxa_memory_budget_t *b,
    pxa_memory_owner_t owner) {
    if (!valid(b)) return PXA_STATUS_INVALID_ARGUMENT;
    lock(b);
    int i = owner_index(b, owner);
    if (i < 0) { unlock(b); return PXA_STATUS_NOT_FOUND; }
    b->owners[i].closing = 1;
    for (unsigned c = 0; c < PXA_MEMORY_CLASSES; ++c) {
        if (b->owners[i].stats.charged[c]) { unlock(b); return PXA_STATUS_WOULD_BLOCK; }
    }
    b->owners[i].active = 0;
    unlock(b);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_memory_budget_stats(pxa_memory_budget_t *b,
    pxa_memory_owner_t owner, pxa_memory_stats_t *out) {
    if (!valid(b) || !out) return PXA_STATUS_INVALID_ARGUMENT;
    lock(b);
    int i = owner ? owner_index(b, owner) : -1;
    if (owner && i < 0) { unlock(b); return PXA_STATUS_NOT_FOUND; }
    *out = owner ? b->owners[i].stats : b->stats;
    unlock(b);
    return PXA_STATUS_OK;
}
size_t pxa_memory_allocation_bytes(size_t bytes) {
    return !bytes || bytes > SIZE_MAX - sizeof(prefix_t) ? 0 : sizeof(prefix_t) + bytes;
}
static void charge(pxa_memory_stats_t *s, unsigned cls, unsigned kind, size_t bytes) {
    s->charged[cls] += bytes;
    s->reserved[cls] += bytes;
    s->by_kind[kind][cls] += bytes;
    if (s->charged[cls] > s->peak[cls]) s->peak[cls] = s->charged[cls];
    if (kind == PXA_MEMORY_TEMPORARY && s->by_kind[kind][cls] > s->temporary_peak[cls])
        s->temporary_peak[cls] = s->by_kind[kind][cls];
}
static void uncharge(pxa_memory_stats_t *s, unsigned cls, unsigned kind, size_t bytes) {
    assert(s->charged[cls] >= bytes && s->by_kind[kind][cls] >= bytes);
    s->charged[cls] -= bytes;
    s->by_kind[kind][cls] -= bytes;
}
void *pxa_memory_allocate(const pxa_memory_allocator_t *a, size_t bytes) {
    const size_t total = pxa_memory_allocation_bytes(bytes);
    if (!a || !valid(a->budget) || !a->allocate || !a->release || !total ||
        a->memory_class >= PXA_MEMORY_CLASSES || a->kind >= PXA_MEMORY_KINDS) return NULL;
    pxa_memory_budget_t *b = a->budget;
    unsigned cls = a->memory_class, kind = a->kind;
    lock(b);
    int i = owner_index(b, a->owner);
    if (i < 0 || b->owners[i].closing) { unlock(b); return NULL; }
    if (kind == PXA_MEMORY_TEMPORARY &&
        (total > b->config.temporary_limit[cls] ||
         b->stats.by_kind[kind][cls] > b->config.temporary_limit[cls] - total)) {
        ++b->stats.denied; ++b->owners[i].stats.denied;
        unlock(b);
        /* Evicting resident textures cannot restore temporary capacity. Do
         * not trigger unrelated cache churn or wait for a decoder here. */
        return NULL;
    }
    if (total > b->config.limit[cls] || b->stats.charged[cls] > b->config.limit[cls] - total ||
        total > b->owners[i].limit[cls] || b->owners[i].stats.charged[cls] > b->owners[i].limit[cls] - total) {
        size_t needed = 0;
        /* Never evict useful data for an allocation that cannot fit even in
         * an empty budget. Subtraction avoids overflow for SIZE_MAX limits. */
        if (total <= b->config.limit[cls] && total <= b->owners[i].limit[cls]) {
            size_t available = b->config.limit[cls] - b->stats.charged[cls];
            size_t own_available = b->owners[i].limit[cls] - b->owners[i].stats.charged[cls];
            if (own_available < available) available = own_available;
            needed = total - available;
        }
        ++b->stats.denied; ++b->owners[i].stats.denied;
        unlock(b);
        if (needed && b->config.reclaim)
            b->config.reclaim(b->config.reclaim_context, a->owner, (uint8_t)cls, needed);
        return NULL;
    }
    charge(&b->stats, cls, kind, total);
    charge(&b->owners[i].stats, cls, kind, total);
    unlock(b);
    prefix_t *memory = a->allocate(a->context, total);
    lock(b);
    b->stats.reserved[cls] -= total;
    b->owners[i].stats.reserved[cls] -= total;
    if (!memory) {
        uncharge(&b->stats, cls, kind, total);
        uncharge(&b->owners[i].stats, cls, kind, total);
        ++b->stats.allocation_failures; ++b->owners[i].stats.allocation_failures;
    }
    unlock(b);
    if (!memory) {
        if (b->config.reclaim)
            b->config.reclaim(b->config.reclaim_context, a->owner, (uint8_t)cls, total);
        return NULL;
    }
    memory->block.allocator = a;
    memory->block.bytes = total;
    return memory + 1;
}
void pxa_memory_release(void *memory) {
    if (!memory) return;
    prefix_t *block = (prefix_t *)memory - 1;
    const pxa_memory_allocator_t *a = block->block.allocator;
    size_t bytes = block->block.bytes;
    pxa_memory_budget_t *b = a->budget;
    a->release(a->context, block);
    lock(b);
    int i = owner_index(b, a->owner);
    assert(i >= 0); /* close cannot succeed while this block is charged. */
    uncharge(&b->stats, a->memory_class, a->kind, bytes);
    if (i >= 0) uncharge(&b->owners[i].stats, a->memory_class, a->kind, bytes);
    unlock(b);
}
void *pxa_memory_resize(const pxa_memory_allocator_t *a, void *memory, size_t bytes) {
    if (!bytes) { pxa_memory_release(memory); return NULL; }
    if (!memory) return pxa_memory_allocate(a, bytes);
    prefix_t *block = (prefix_t *)memory - 1;
    if (block->block.allocator != a) return NULL;
    size_t old_bytes = block->block.bytes - sizeof(prefix_t);
    if (old_bytes == bytes) return memory;
    void *replacement = pxa_memory_allocate(a, bytes);
    if (!replacement) return NULL;
    memcpy(replacement, memory, bytes < old_bytes ? bytes : old_bytes);
    pxa_memory_release(memory);
    return replacement;
}
