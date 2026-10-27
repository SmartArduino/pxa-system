#ifndef PXA_SCOPED_FS_H
#define PXA_SCOPED_FS_H
#include "pxa/fs.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only data roots. Native mode requires link-free filesystems; the Host
 * must register all nested mounts. POSIX mode walks directory descriptors. */
/* Host root plus the Guest's separately bounded relative path. Immutable
 * publisher/app/digest roots must not reduce the usable Guest path length. */
#define PXA_SCOPED_FS_PATH_BYTES (PXA_FS_SCOPED_MAX_PATH_BYTES + 1u)
typedef struct {
    uint64_t mount_epoch;
    const char *host_path;
    uint8_t native_fat;
} pxa_scoped_fs_mount_t;
typedef struct {
    uint8_t id[16];
    pxa_volume_id_t volume_id;
    uint64_t revision;
    uint32_t rights;
    uint8_t active;
    char relative_path[PXA_SCOPED_FS_PATH_BYTES];
} pxa_scoped_fs_grant_t;
typedef struct {
    uint32_t struct_size;
    pxa_volume_registry_t *volumes;
    /* Stable owner-managed slot array. Zero-initialized unused slots permit
     * registering/removing mounts after a Guest was activated. The owner must
     * invalidate the old epoch before reusing a slot or releasing its path. */
    const pxa_scoped_fs_mount_t *mounts;
    uint16_t mount_count;
    const pxa_scoped_fs_grant_t *grants;
    uint16_t grant_count;
    uint16_t max_open_resources;
    /* Optional owner guard, held for one backend operation. Serialize mount
     * replacement with readers so a removed path can be freed safely. Enter
     * and leave must either both be supplied or both be NULL. */
    void *inventory_context;
    void (*enter_inventory)(void *);
    void (*leave_inventory)(void *);
    /* Volume-relative private subtrees, hidden from grants and enumeration.
     * Matching is ASCII case-insensitive, including on FAT. Owner retains
     * this immutable array for the backend lifetime. */
    const char *const *protected_paths;
    uint16_t protected_path_count;
} pxa_scoped_fs_config_t;
typedef struct pxa_scoped_fs pxa_scoped_fs_t;
size_t pxa_scoped_fs_workspace_size(const pxa_scoped_fs_config_t *config);
pxa_status_t pxa_scoped_fs_init(void *workspace, size_t bytes,
    const pxa_scoped_fs_config_t *config, pxa_scoped_fs_t **output,
    pxa_fs_roots_backend_t *backend);
void pxa_scoped_fs_deinit(pxa_scoped_fs_t *fs);
/* Host-only validation before persisting a chosen directory. Empty path
 * means the volume root. Does not create or modify the directory. */
pxa_status_t pxa_scoped_fs_validate_grant(pxa_scoped_fs_t *fs,
    const pxa_scoped_fs_grant_t *grant);
/* Trusted Host browser/import only. This never accepts a Guest request or
 * creates a persistent grant. The resource retains the exact mount epoch,
 * uses the same no-link/protected-path checks, and is closed through files.
 * Empty relative path is permitted only for a directory (volume browser). */
pxa_status_t pxa_scoped_fs_open_trusted(pxa_scoped_fs_t *,uint64_t epoch,
    const char *relative,uint8_t kind,void **resource,pxa_fs_entry_t *info);
#ifdef __cplusplus
}
#endif
#endif
