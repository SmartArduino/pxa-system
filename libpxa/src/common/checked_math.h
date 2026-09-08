#ifndef PXA_INTERNAL_CHECKED_LAYOUT_H
#define PXA_INTERNAL_CHECKED_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define PXA_INTERNAL_WORKSPACE_ALIGNMENT ((size_t)16)

static inline size_t pxa_internal_align_size(size_t value, size_t alignment) {
    size_t mask;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0)
        return SIZE_MAX;
    mask = alignment - 1u;
    if (value > SIZE_MAX - mask) return SIZE_MAX;
    return (value + mask) & ~mask;
}

static inline uintptr_t pxa_internal_align_pointer(uintptr_t value,
                                                   size_t alignment) {
    uintptr_t mask;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0)
        return UINTPTR_MAX;
    mask = (uintptr_t)alignment - 1u;
    if (value > UINTPTR_MAX - mask) return UINTPTR_MAX;
    return (value + mask) & ~mask;
}

static inline int pxa_internal_add_array_size(size_t *total, size_t count,
                                              size_t element_size,
                                              size_t alignment) {
    size_t aligned;
    if (total == NULL ||
        (count != 0 && element_size > SIZE_MAX / count)) {
        return 0;
    }
    aligned = pxa_internal_align_size(*total, alignment);
    if (aligned == SIZE_MAX || count * element_size > SIZE_MAX - aligned)
        return 0;
    *total = aligned + count * element_size;
    return 1;
}

static inline void *pxa_internal_layout_take(uint8_t **cursor,
                                             const uint8_t *end, size_t count,
                                             size_t element_size,
                                             size_t alignment) {
    uintptr_t aligned;
    size_t bytes;
    if (cursor == NULL || *cursor == NULL || end == NULL ||
        (count != 0 && element_size > SIZE_MAX / count)) {
        return NULL;
    }
    aligned = pxa_internal_align_pointer((uintptr_t)*cursor, alignment);
    if (aligned == UINTPTR_MAX) return NULL;
    bytes = count * element_size;
    if (aligned > (uintptr_t)end || bytes > (size_t)((uintptr_t)end - aligned))
        return NULL;
    *cursor = (uint8_t *)(aligned + bytes);
    return (void *)aligned;
}

#endif
