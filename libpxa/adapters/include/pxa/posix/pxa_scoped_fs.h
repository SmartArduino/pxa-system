#ifndef PXA_SCOPED_FS_H
#define PXA_SCOPED_FS_H
#include "pxa/fs.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only data roots. No tree scan, write quota, private-FS migration or
 * package installation is performed. Native FAT mode is ONLY for filesystems
 * without links or nested mounts; POSIX mode always walks directory fds. */
#define PXA_SCOPED_FS_PATH_BYTES 256u
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
    const pxa_scoped_fs_mount_t *mounts;
    uint16_t mount_count;
    const pxa_scoped_fs_grant_t *grants;
    uint16_t grant_count;
    uint16_t max_open_resources;
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
#ifdef __cplusplus
}
#endif
#endif
