#ifndef PXA_POSIX_SCHEDULER_STORE_H
#define PXA_POSIX_SCHEDULER_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/scheduler.h"
#include "pxa/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: queued Work stored through a Storage backend.
 * The atomic set is the durability boundary. Entries from another monotonic
 * clock epoch are ignored instead of being assigned a false post-boot delay. */

typedef struct {
    uint32_t struct_size;
    pxa_storage_backend_t backend;
    pxa_bytes_t key;
    uint16_t max_entries;
    uint64_t epoch;
} pxa_posix_scheduler_store_config_t;

typedef struct pxa_posix_scheduler_store pxa_posix_scheduler_store_t;

size_t pxa_posix_scheduler_store_workspace_size(
    const pxa_posix_scheduler_store_config_t *config);
pxa_status_t pxa_posix_scheduler_store_init(
    void *workspace, size_t workspace_size,
    const pxa_posix_scheduler_store_config_t *config,
    pxa_posix_scheduler_store_t **output,
    pxa_scheduler_store_t *scheduler_store_output);
void pxa_posix_scheduler_store_deinit(pxa_posix_scheduler_store_t *store);

#ifdef __cplusplus
}
#endif

#endif
