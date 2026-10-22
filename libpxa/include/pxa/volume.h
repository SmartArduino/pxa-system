#ifndef PXA_VOLUME_H
#define PXA_VOLUME_H

#include "pxa/status.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-only inventory. Mount paths and device pointers never enter Guest ABI.
 * The owner serializes mutations and publishes snapshots to other threads. */
#define PXA_VOLUME_ID_BYTES 16u
#define PXA_VOLUME_NAME_BYTES 64u
#define PXA_VOLUME_OFFLINE UINT8_C(0)
#define PXA_VOLUME_MOUNTING UINT8_C(1)
#define PXA_VOLUME_READY UINT8_C(2)
#define PXA_VOLUME_DRAINING UINT8_C(3)
#define PXA_VOLUME_FAILED UINT8_C(4)
#define PXA_VOLUME_ID_WEAK UINT8_C(0)
#define PXA_VOLUME_ID_FILESYSTEM UINT8_C(1)
#define PXA_VOLUME_ID_MEDIA_BOUND UINT8_C(2)
#define PXA_VOLUME_READ UINT64_C(1)
#define PXA_VOLUME_WRITE UINT64_C(2)
#define PXA_VOLUME_REMOVABLE UINT64_C(4)
#define PXA_VOLUME_HOTPLUG UINT64_C(8)
#define PXA_VOLUME_CASE_INSENSITIVE UINT64_C(16)
#define PXA_VOLUME_RANDOM_READ UINT64_C(32)
#define PXA_VOLUME_INSTALLABLE UINT64_C(64)
/* Directories already satisfy the PXA private FS naming and atomic-replace
 * contract. Otherwise a private filesystem backend (e.g. image) is required. */
#define PXA_VOLUME_PRIVATE_NATIVE UINT64_C(128)
#define PXA_VOLUME_TOTAL_KNOWN UINT8_C(1)
#define PXA_VOLUME_FREE_KNOWN UINT8_C(2)

typedef struct { uint8_t bytes[PXA_VOLUME_ID_BYTES]; } pxa_volume_id_t;
typedef struct {
    pxa_volume_id_t id;
    pxa_volume_id_t device_id;
    uint64_t mount_epoch;
    uint64_t capabilities;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t reserved_bytes;
    uint64_t max_file_bytes;
    uint32_t allocation_unit;
    uint8_t state;
    uint8_t identity_strength;
    uint8_t space_known;
    uint8_t ambiguous;
    char name[PXA_VOLUME_NAME_BYTES];
} pxa_volume_info_t;

typedef struct pxa_volume_registry pxa_volume_registry_t;

size_t pxa_volume_registry_workspace_size(uint16_t capacity);
pxa_status_t pxa_volume_registry_init(void *workspace, size_t bytes,
    uint16_t capacity, uint64_t epoch_seed, pxa_volume_registry_t **output);
/* Adds a mount instance. A duplicate persistent ID makes BOTH instances
 * ambiguous. Neither may resolve by ID until that ambiguity is removed. */
pxa_status_t pxa_volume_register(pxa_volume_registry_t *registry,
    const pxa_volume_info_t *info, uint64_t *mount_epoch);
pxa_status_t pxa_volume_update(pxa_volume_registry_t *registry,
    uint64_t mount_epoch, uint8_t state, uint64_t total_bytes,
    uint64_t free_bytes, uint8_t space_known);
pxa_status_t pxa_volume_unregister(pxa_volume_registry_t *registry,
    uint64_t mount_epoch);
pxa_status_t pxa_volume_find(const pxa_volume_registry_t *registry,
    const pxa_volume_id_t *id, pxa_volume_info_t *output);
/* An outstanding handle must use its original epoch, never find() again. */
pxa_status_t pxa_volume_get(const pxa_volume_registry_t *registry,
    uint64_t mount_epoch, pxa_volume_info_t *output);
/* Slot-based bounded enumeration. NOT_FOUND marks the end. A changed revision
 * requires restarting enumeration; free slots are skipped. */
pxa_status_t pxa_volume_next(const pxa_volume_registry_t *registry,
    uint64_t expected_revision, uint16_t *cursor, pxa_volume_info_t *output);
uint64_t pxa_volume_revision(const pxa_volume_registry_t *registry);

/* Host-owner admission accounting. Bytes are reservations against the latest
 * free-space snapshot, not an on-disk allocation. A lease is tied to this
 * mount instance; unregistering it never transfers reservations to a new one.
 * Callers serialize reserve/release with filesystem space refreshes. */
pxa_status_t pxa_volume_reserve(pxa_volume_registry_t *, uint64_t mount_epoch,
    uint64_t bytes);
pxa_status_t pxa_volume_release(pxa_volume_registry_t *, uint64_t mount_epoch,
    uint64_t bytes);

#ifdef __cplusplus
}
#endif
#endif
