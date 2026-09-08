#ifndef PXA_FS_H
#define PXA_FS_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_FS_SERVICE_ID UINT16_C(5)
#define PXA_FS_SERVICE_MAJOR UINT16_C(0)
#define PXA_FS_SERVICE_MINOR UINT16_C(1)
#define PXA_FS_SERVICE_PATCH UINT16_C(0)

#define PXA_FS_OPEN UINT16_C(1)
#define PXA_FS_MAKE_DIRECTORY UINT16_C(2)
#define PXA_FS_REMOVE UINT16_C(3)
#define PXA_FS_RENAME UINT16_C(4)
#define PXA_FS_STAT UINT16_C(5)
#define PXA_FS_SEEK UINT16_C(6)
#define PXA_FS_READ_DIRECTORY UINT16_C(7)

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
} pxa_fs_backend_t;

typedef pxa_status_t (*pxa_fs_authorize_fn)(
    void *context, pxa_component_t component, pxa_authority_t *authority);

typedef struct {
    uint32_t struct_size;
    uint16_t max_open_resources;
    uint16_t reserved;
    pxa_fs_backend_t backend;
    void *authorization_context;
    pxa_fs_authorize_fn authorize;
} pxa_fs_config_t;

typedef struct pxa_fs_service pxa_fs_service_t;

int pxa_fs_path_is_valid(pxa_bytes_t path);
size_t pxa_fs_service_workspace_size(const pxa_fs_config_t *config);
pxa_status_t pxa_fs_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_fs_config_t *config, pxa_fs_service_t **output);
pxa_status_t pxa_fs_service_register(pxa_fs_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
