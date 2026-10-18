#ifndef PXA_FS_H
#define PXA_FS_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"
#include "pxa/permission.h"
#include "pxa/volume.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_FS_SERVICE_ID UINT16_C(5)
#define PXA_FS_SERVICE_MAJOR UINT16_C(0)
#define PXA_FS_SERVICE_MINOR UINT16_C(3)
#define PXA_FS_SERVICE_PATCH UINT16_C(0)

#define PXA_FS_OPEN UINT16_C(1)
#define PXA_FS_MAKE_DIRECTORY UINT16_C(2)
#define PXA_FS_REMOVE UINT16_C(3)
#define PXA_FS_RENAME UINT16_C(4)
#define PXA_FS_STAT UINT16_C(5)
#define PXA_FS_SEEK UINT16_C(6)
#define PXA_FS_READ_DIRECTORY UINT16_C(7)
#define PXA_FS_REPLACE UINT16_C(8)
#define PXA_FS_OPEN_ROOT UINT16_C(9)
#define PXA_FS_REQUEST_ACCESS UINT16_C(10)
#define PXA_FS_OPEN_AT UINT16_C(11)
#define PXA_FS_STAT_AT UINT16_C(16)
#define PXA_FS_QUERY_ROOT UINT16_C(17)
#define PXA_FS_FEATURE_SCOPED_ROOTS UINT64_C(1)
#define PXA_FS_FEATURE_READ_AT UINT64_C(2)
#define PXA_FS_FEATURE_USER_DIRECTORY_GRANTS UINT64_C(16)
#define PXA_FS_FEATURE_APP_MEDIA_ROOT UINT64_C(64)
#define PXA_FS_ROOT_PRIVATE UINT8_C(1)
#define PXA_FS_ROOT_CACHE UINT8_C(2)
#define PXA_FS_ROOT_TEMPORARY UINT8_C(3)
#define PXA_FS_ROOT_GRANT UINT8_C(4)
#define PXA_FS_ROOT_APP_MEDIA UINT8_C(5)
#define PXA_FS_RIGHT_READ UINT32_C(1)
#define PXA_FS_RIGHT_ENUMERATE UINT32_C(2)
#define PXA_FS_RIGHT_WRITE UINT32_C(4)
#define PXA_FS_RIGHT_CREATE UINT32_C(8)
#define PXA_FS_RIGHT_REMOVE UINT32_C(16)
#define PXA_FS_NAMING_BYTE_EXACT UINT8_C(1)
#define PXA_FS_NAMING_CASE_INSENSITIVE UINT8_C(2)
#define PXA_FS_ROOT_INFO_BYTES 72u
#define PXA_FS_SCOPED_MAX_PATH_BYTES 1024u
#define PXA_FS_SCOPED_MAX_SEGMENT_BYTES 255u

typedef struct {
    pxa_volume_id_t volume_id;
    uint64_t mount_epoch;
    uint32_t rights;
    uint64_t capabilities;
    uint16_t max_path_bytes;
    uint16_t max_segment_bytes;
    uint32_t max_read_at_bytes;
    uint8_t naming;
    uint8_t state;
    uint16_t space_known;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t quota_bytes;
} pxa_fs_root_info_t;

#define PXA_FS_OPEN_READ UINT32_C(1)
#define PXA_FS_OPEN_WRITE UINT32_C(2)
#define PXA_FS_OPEN_CREATE UINT32_C(4)
#define PXA_FS_OPEN_EXCLUSIVE UINT32_C(8)
#define PXA_FS_OPEN_TRUNCATE UINT32_C(16)
#define PXA_FS_OPEN_APPEND UINT32_C(32)
#define PXA_FS_OPEN_DIRECTORY UINT32_C(64)

#define PXA_FS_KIND_REGULAR UINT8_C(1)
#define PXA_FS_KIND_DIRECTORY UINT8_C(2)

#define PXA_FS_SEEK_START UINT8_C(0)
#define PXA_FS_SEEK_CURRENT UINT8_C(1)
#define PXA_FS_SEEK_END UINT8_C(2)

#define PXA_FS_IO_READ UINT32_C(1)
#define PXA_FS_IO_WRITE UINT32_C(2)
#define PXA_FS_IO_READ_AT UINT32_C(16)
#define PXA_FS_IO_CANCEL_READ_AT UINT32_C(17)
#define PXA_FS_FILE_FEATURE_READ_AT UINT64_C(1)
#define PXA_FS_READ_AT_HEADER_BYTES 24u
#define PXA_FS_READ_AT_MAX_BYTES 4096u

#define PXA_FS_MAX_PATH_BYTES ((size_t)255)
#define PXA_FS_MAX_SEGMENT_BYTES ((size_t)64)

typedef struct {
    pxa_bytes_t name;
    uint8_t kind;
    uint64_t size;
} pxa_fs_entry_t;

typedef pxa_status_t (*pxa_fs_open_fn)(
    void *context, pxa_bytes_t path, uint32_t flags, void **resource,
    uint8_t *kind);
typedef pxa_status_t (*pxa_fs_path_fn)(void *context, pxa_bytes_t path);
typedef pxa_status_t (*pxa_fs_rename_fn)(
    void *context, pxa_bytes_t source, pxa_bytes_t destination);
typedef pxa_status_t (*pxa_fs_stat_fn)(
    void *context, pxa_bytes_t path, pxa_fs_entry_t *entry);
typedef pxa_status_t (*pxa_fs_read_fn)(
    void *context, void *resource, uint8_t *output, size_t capacity,
    size_t *size);
typedef pxa_status_t (*pxa_fs_write_fn)(
    void *context, void *resource, const uint8_t *input, size_t size,
    size_t *written);
typedef pxa_status_t (*pxa_fs_seek_fn)(
    void *context, void *resource, int64_t offset, uint8_t origin,
    uint64_t *position);
typedef pxa_status_t (*pxa_fs_read_directory_fn)(
    void *context, void *resource, pxa_fs_entry_t *entry, uint8_t *end);
typedef void (*pxa_fs_close_fn)(
    void *context, void *resource, uint8_t kind);
/* Bounded synchronous positional read. Never retain output or return
 * WOULD_BLOCK; it leaves the sequential file cursor unchanged. */
typedef pxa_status_t (*pxa_fs_read_at_fn)(void *context, void *resource,
    uint64_t offset, uint8_t *output, size_t capacity, size_t *size);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_fs_open_fn open;
    pxa_fs_path_fn make_directory;
    pxa_fs_path_fn remove;
    pxa_fs_rename_fn rename;
    pxa_fs_stat_fn stat;
    pxa_fs_read_fn read;
    pxa_fs_write_fn write;
    pxa_fs_seek_fn seek;
    pxa_fs_read_directory_fn read_directory;
    pxa_fs_close_fn close;
    /* Optional atomic replacement of a regular file; rename remains exclusive. */
    pxa_fs_rename_fn replace;
} pxa_fs_backend_t;

/* Optional scoped-directory provider. All resources returned here use files'
 * read/write/seek/readdir/close callbacks, independent of the legacy backend.
 * The provider is configured for exactly this verified app identity. It must
 * enforce grant rights, mount epochs and safe traversal on EVERY operation.
 * Callbacks run on the runtime owner; request_access only queues trusted UI. */
typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_fs_backend_t files;
    pxa_status_t (*open_root)(void *, uint8_t kind, const uint8_t grant_id[16],
        void **directory, uint32_t *rights);
    pxa_status_t (*open_at)(void *, void *directory, pxa_bytes_t path,
        uint32_t flags, void **resource, pxa_fs_entry_t *info,
        uint64_t *file_features);
    pxa_status_t (*stat_at)(void *, void *directory, pxa_bytes_t path,
        pxa_fs_entry_t *info);
    pxa_status_t (*query_root)(void *, void *directory,
        pxa_fs_root_info_t *info);
    pxa_status_t (*request_access)(void *, pxa_component_t,
        pxa_request_token_t, pxa_bytes_t purpose, uint32_t rights,
        uint8_t persistent);
    void (*cancel_access)(void *, pxa_component_t, pxa_request_token_t);
    /* Optional tail: additional implemented roots. Currently only bit 6
     * (app-media-root) is defined. Old providers default to no extra roots. */
    uint64_t features;
    /* Optional tail; context is files.context. Older providers omit it. */
    pxa_fs_read_at_fn read_at;
} pxa_fs_roots_backend_t;

typedef pxa_status_t (*pxa_fs_authorize_fn)(
    void *context, pxa_component_t component, pxa_authority_t *authority);

typedef struct {
    uint32_t struct_size;
    uint16_t max_open_resources;
    uint16_t reserved;
    pxa_fs_backend_t backend;
    void *authorization_context;
    pxa_fs_authorize_fn authorize;
    const pxa_fs_roots_backend_t *roots;
    pxa_permission_service_t *permissions;
} pxa_fs_config_t;

typedef struct pxa_fs_service pxa_fs_service_t;

int pxa_fs_path_is_valid(pxa_bytes_t path);
int pxa_fs_scoped_path_is_valid(pxa_bytes_t path);
size_t pxa_fs_service_workspace_size(const pxa_fs_config_t *config);
pxa_status_t pxa_fs_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_fs_config_t *config, pxa_fs_service_t **output);
pxa_status_t pxa_fs_service_register(pxa_fs_service_t *service);
/* Complete a trusted picker on the runtime owner thread. A cancelled/stale
 * request cannot publish a grant or resource. grant_id was created by Host. */
pxa_status_t pxa_fs_access_complete(pxa_fs_service_t *service,
    pxa_component_t component, pxa_request_token_t token,
    pxa_status_t status, const uint8_t grant_id[16]);
void pxa_fs_service_poll(pxa_fs_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
