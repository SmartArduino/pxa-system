#ifndef PXA_STORAGE_H
#define PXA_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_STORAGE_SERVICE_ID UINT16_C(6)
#define PXA_STORAGE_SERVICE_MAJOR UINT16_C(0)
#define PXA_STORAGE_SERVICE_MINOR UINT16_C(1)
#define PXA_STORAGE_SERVICE_PATCH UINT16_C(0)
#define PXA_STORAGE_GET UINT16_C(1)
#define PXA_STORAGE_SET UINT16_C(2)
#define PXA_STORAGE_REMOVE UINT16_C(3)
#define PXA_STORAGE_LIST UINT16_C(4)
#define PXA_STORAGE_MAX_KEY_BYTES ((size_t)64)
#define PXA_STORAGE_MAX_VALUE_BYTES ((size_t)2048)
#define PXA_STORAGE_MAX_LIST_ENTRIES ((size_t)14)

typedef pxa_status_t (*pxa_storage_get_fn)(
    void *context, pxa_bytes_t key, uint8_t *output, size_t capacity,
    size_t *size);
typedef pxa_status_t (*pxa_storage_set_fn)(
    void *context, pxa_bytes_t key, pxa_bytes_t value);
typedef pxa_status_t (*pxa_storage_remove_fn)(
    void *context, pxa_bytes_t key);
typedef pxa_status_t (*pxa_storage_emit_key_fn)(
    void *context, pxa_bytes_t key);
typedef pxa_status_t (*pxa_storage_list_fn)(
    void *context, pxa_bytes_t cursor, pxa_storage_emit_key_fn emit,
    void *emit_context);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_storage_get_fn get;
    pxa_storage_set_fn set;
    pxa_storage_remove_fn remove;
    pxa_storage_list_fn list;
} pxa_storage_backend_t;

typedef struct {
    uint32_t struct_size;
    size_t max_value_bytes;
    pxa_storage_backend_t backend;
} pxa_storage_config_t;

typedef struct pxa_storage_service pxa_storage_service_t;

int pxa_storage_key_is_valid(pxa_bytes_t key);
size_t pxa_storage_service_workspace_size(const pxa_storage_config_t *config);
pxa_status_t pxa_storage_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_storage_config_t *config, pxa_storage_service_t **output);
pxa_status_t pxa_storage_service_register(pxa_storage_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
