#ifndef PXA_POSIX_FS_H
#define PXA_POSIX_FS_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: POSIX implementation of the FS service backend
 * with quota accounting and path safety. Mirrors the semantics of the legacy
 * C++ PrivateFileSystem:
 *  - all path walks use openat with O_NOFOLLOW, never following symlinks;
 *  - regular files must have link count 1;
 *  - names with the reserved ".pxa-" prefix are excluded from listings and
 *    rejected; invalid UTF-8 and control characters are rejected;
 *  - write quota is enforced against bytes actually present on disk.
 *
 * The adapter is single-owner-thread (the libpxa platform contract). It does
 * not allocate after pxa_posix_fs_init: all state lives in the caller-supplied
 * workspace, including the bounded open-resource table.
 */

#define PXA_POSIX_FS_DEFAULT_QUOTA UINT64_C(0xffffffffffffffff)
#define PXA_POSIX_FS_MAX_TREE_ENTRIES ((size_t)4096)

typedef struct {
    uint32_t struct_size;
    const char *root_path;   /* owned by the caller, must outlive the fs */
    uint64_t quota_bytes;
    uint16_t max_open_resources; /* capacity of the open-resource table */
} pxa_posix_fs_config_t;

typedef struct pxa_posix_fs pxa_posix_fs_t;

size_t pxa_posix_fs_workspace_size(const pxa_posix_fs_config_t *config);
pxa_status_t pxa_posix_fs_init(void *workspace, size_t workspace_size,
                               const pxa_posix_fs_config_t *config,
                               pxa_posix_fs_t **output,
                               pxa_fs_backend_t *backend_output);
void pxa_posix_fs_deinit(pxa_posix_fs_t *fs);

/* Host-side clear-data helper. Removes one directory tree without following
 * symbolic links. The root itself is removed after all descendants. */
pxa_status_t pxa_posix_fs_remove_tree(const char *root_path);

#ifdef __cplusplus
}
#endif

#endif
