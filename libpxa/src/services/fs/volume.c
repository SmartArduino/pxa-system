#include "pxa/volume.h"
#include "common/checked_math.h"
#include <string.h>

#define VOLUME_MAGIC UINT32_C(0x5058564c)
struct pxa_volume_registry {
    uint32_t magic;
    uint16_t capacity;
    uint64_t next_epoch;
    uint64_t revision;
    pxa_volume_info_t entries[];
};

static int valid(const pxa_volume_registry_t *r) {
    return r != NULL && r->magic == VOLUME_MAGIC;
}
static int nonzero_id(const pxa_volume_id_t *id) {
    unsigned i; uint8_t any = 0;
    for (i = 0; i < PXA_VOLUME_ID_BYTES; ++i) any |= id->bytes[i];
    return any != 0;
}
static int same_id(const pxa_volume_id_t *a, const pxa_volume_id_t *b) {
    return memcmp(a->bytes, b->bytes, PXA_VOLUME_ID_BYTES) == 0;
}
static void refresh_ambiguity(pxa_volume_registry_t *r) {
    uint16_t i, j;
    for (i = 0; i < r->capacity; ++i) r->entries[i].ambiguous = 0;
    for (i = 0; i < r->capacity; ++i) {
        if (!r->entries[i].mount_epoch) continue;
        for (j = i + 1; j < r->capacity; ++j) {
            if (r->entries[j].mount_epoch &&
                same_id(&r->entries[i].id, &r->entries[j].id)) {
                r->entries[i].ambiguous = r->entries[j].ambiguous = 1;
            }
        }
    }
}
size_t pxa_volume_registry_workspace_size(uint16_t capacity) {
    size_t bytes = sizeof(pxa_volume_registry_t);
    if (!capacity || !pxa_internal_add_array_size(&bytes, capacity,
        sizeof(pxa_volume_info_t), 1) ||
        bytes > SIZE_MAX - (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1)) return 0;
    return PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1 + bytes;
}
pxa_status_t pxa_volume_registry_init(void *workspace, size_t bytes,
    uint16_t capacity, uint64_t epoch_seed, pxa_volume_registry_t **output) {
    size_t need = pxa_volume_registry_workspace_size(capacity);
    pxa_volume_registry_t *r;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!workspace || !need || bytes < need || !epoch_seed ||
        epoch_seed == UINT64_MAX || (uintptr_t)workspace > UINTPTR_MAX - bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    r = (pxa_volume_registry_t *)pxa_internal_align_pointer(
        (uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(r, 0, sizeof(*r) + (size_t)capacity * sizeof(r->entries[0]));
    r->magic = VOLUME_MAGIC; r->capacity = capacity;
    r->next_epoch = epoch_seed; r->revision = 1;
    *output = r;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_volume_register(pxa_volume_registry_t *r,
    const pxa_volume_info_t *info, uint64_t *mount_epoch) {
    uint16_t i;
    if (mount_epoch) *mount_epoch = 0;
    if (!valid(r) || !info || !mount_epoch || !nonzero_id(&info->id) ||
        !nonzero_id(&info->device_id) || info->state > PXA_VOLUME_FAILED ||
        info->identity_strength > PXA_VOLUME_ID_MEDIA_BOUND ||
        (info->space_known & ~3u) ||
        ((info->space_known & 3u) == 3u && info->free_bytes > info->total_bytes) ||
        memchr(info->name, 0, sizeof(info->name)) == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (r->next_epoch == UINT64_MAX || r->revision == UINT64_MAX)
        return PXA_STATUS_RESOURCE_LIMIT;
    for (i = 0; i < r->capacity; ++i) if (!r->entries[i].mount_epoch) break;
    if (i == r->capacity) return PXA_STATUS_RESOURCE_LIMIT;
    r->entries[i] = *info;
    r->entries[i].mount_epoch = r->next_epoch++;
    *mount_epoch = r->entries[i].mount_epoch;
    ++r->revision; refresh_ambiguity(r);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_volume_get(const pxa_volume_registry_t *r,
    uint64_t epoch, pxa_volume_info_t *output) {
    uint16_t i;
    if (!valid(r) || !epoch || !output) return PXA_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < r->capacity; ++i) {
        if (r->entries[i].mount_epoch == epoch) {
            *output = r->entries[i];
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_UNAVAILABLE;
}
pxa_status_t pxa_volume_find(const pxa_volume_registry_t *r,
    const pxa_volume_id_t *id, pxa_volume_info_t *output) {
    uint16_t i;
    if (!valid(r) || !id || !output || !nonzero_id(id))
        return PXA_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < r->capacity; ++i) {
        if (r->entries[i].mount_epoch && same_id(id, &r->entries[i].id)) {
            if (r->entries[i].ambiguous) return PXA_STATUS_BUSY;
            *output = r->entries[i];
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_UNAVAILABLE;
}
pxa_status_t pxa_volume_update(pxa_volume_registry_t *r, uint64_t epoch,
    uint8_t state, uint64_t total, uint64_t free_bytes, uint8_t known) {
    uint16_t i;
    if (!valid(r) || !epoch || state > PXA_VOLUME_FAILED || (known & ~3u) ||
        ((known & 3u) == 3u && free_bytes > total))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (r->revision == UINT64_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    for (i = 0; i < r->capacity; ++i) if (r->entries[i].mount_epoch == epoch) {
        r->entries[i].state = state; r->entries[i].total_bytes = total;
        r->entries[i].free_bytes = free_bytes; r->entries[i].space_known = known;
        ++r->revision; return PXA_STATUS_OK;
    }
    return PXA_STATUS_UNAVAILABLE;
}
pxa_status_t pxa_volume_unregister(pxa_volume_registry_t *r, uint64_t epoch) {
    uint16_t i;
    if (!valid(r) || !epoch) return PXA_STATUS_INVALID_ARGUMENT;
    if (r->revision == UINT64_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    for (i = 0; i < r->capacity; ++i) if (r->entries[i].mount_epoch == epoch) {
        memset(&r->entries[i], 0, sizeof(r->entries[i]));
        ++r->revision; refresh_ambiguity(r); return PXA_STATUS_OK;
    }
    return PXA_STATUS_UNAVAILABLE;
}
pxa_status_t pxa_volume_next(const pxa_volume_registry_t *r,
    uint64_t revision, uint16_t *cursor, pxa_volume_info_t *output) {
    if (!valid(r) || !cursor || !output) return PXA_STATUS_INVALID_ARGUMENT;
    if (revision != r->revision) return PXA_STATUS_BAD_STATE;
    while (*cursor < r->capacity) {
        const pxa_volume_info_t *v = &r->entries[(*cursor)++];
        if (v->mount_epoch) { *output = *v; return PXA_STATUS_OK; }
    }
    return PXA_STATUS_NOT_FOUND;
}
uint64_t pxa_volume_revision(const pxa_volume_registry_t *r) {
    return valid(r) ? r->revision : 0;
}
