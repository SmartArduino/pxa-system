#ifndef PXA_POSIX_STORAGE_H
#define PXA_POSIX_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: POSIX implementation of the Storage service
 * backend. The complete sorted key/value map is stored in alternating
 * CRC-protected `.pxa-kv-a` and `.pxa-kv-b` snapshots. A mutation is visible
 * only after the inactive snapshot has been written and fsynced. Listing emits
 * keys in ascending byte order after the cursor, as the service requires.
 *
 * The adapter is single-owner-thread and does not allocate after
 * pxa_posix_storage_init. The caller-supplied workspace holds the active and
 * candidate snapshots, bounded by the configured key count and value quota.
 */

typedef struct {
    uint32_t struct_size;
    const char *root_path; /* owned by the caller, must outlive the store */
    uint16_t max_keys;
    size_t max_value_bytes;
    size_t quota_bytes; /* sum of all value bytes */
} pxa_posix_storage_config_t;

typedef struct pxa_posix_storage pxa_posix_storage_t;

size_t pxa_posix_storage_workspace_size(const pxa_posix_storage_config_t *config);
pxa_status_t pxa_posix_storage_init(void *workspace, size_t workspace_size,
                                    const pxa_posix_storage_config_t *config,
                                    pxa_posix_storage_t **output,
                                    pxa_storage_backend_t *backend_output);
void pxa_posix_storage_deinit(pxa_posix_storage_t *storage);

#ifdef __cplusplus
}
#endif

#endif
