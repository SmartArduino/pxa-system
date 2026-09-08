#ifndef PXSYS_SERVICE_REGISTRY_H
#define PXSYS_SERVICE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    pxsys_string_t interface_id;
    pxsys_version_t version;
    uint32_t operation;
    uint64_t request_id;
    uint32_t flags;
    pxsys_bytes_t payload;
    const pxsys_caller_t* caller;
} pxsys_service_request_t;

typedef void (*pxsys_service_complete_fn)(void* context, uint64_t request_id, pxsys_status_t status,
                                          pxsys_bytes_t payload);

typedef pxsys_status_t (*pxsys_service_invoke_fn)(void* context,
                                                  const pxsys_service_request_t* request,
                                                  void* completion_context,
                                                  pxsys_service_complete_fn complete);

typedef struct {
    uint32_t struct_size;
    pxsys_string_t interface_id;
    pxsys_version_t version;
    uint64_t features;
    void* context;
    pxsys_service_invoke_fn invoke;
} pxsys_service_provider_t;

typedef struct {
    uint32_t struct_size;
    pxsys_string_t interface_id;
    pxsys_version_t version;
    uint64_t features;
} pxsys_service_info_t;

typedef pxsys_status_t (*pxsys_service_policy_fn)(void* context,
                                                  const pxsys_service_request_t* request);

typedef struct {
    uint32_t struct_size;
    size_t max_providers;
    size_t max_interface_id_bytes;
    size_t max_app_id_bytes;
    size_t max_component_id_bytes;
    void* policy_context;
    pxsys_service_policy_fn authorize;
    pxsys_allocator_t allocator;
} pxsys_service_registry_config_t;

typedef struct pxsys_service_registry pxsys_service_registry_t;

void pxsys_service_registry_config_init(pxsys_service_registry_config_t* config);
pxsys_status_t pxsys_service_registry_create(const pxsys_service_registry_config_t* config,
                                             pxsys_service_registry_t** output);
pxsys_status_t pxsys_service_registry_destroy(pxsys_service_registry_t* registry);
pxsys_status_t pxsys_service_register(pxsys_service_registry_t* registry,
                                      const pxsys_service_provider_t* provider);
pxsys_status_t pxsys_service_unregister(pxsys_service_registry_t* registry,
                                        pxsys_string_t interface_id, uint16_t major_version);
pxsys_status_t pxsys_service_invoke(pxsys_service_registry_t* registry,
                                    const pxsys_service_request_t* request,
                                    void* completion_context, pxsys_service_complete_fn complete);
size_t pxsys_service_count(const pxsys_service_registry_t* registry);
pxsys_status_t pxsys_service_at(const pxsys_service_registry_t* registry, size_t index,
                                pxsys_service_info_t* output);
pxsys_status_t pxsys_service_resolve(const pxsys_service_registry_t* registry,
                                     pxsys_string_t interface_id, pxsys_version_t minimum_version,
                                     pxsys_service_info_t* output);
size_t pxsys_service_active_call_count(const pxsys_service_registry_t* registry);

#ifdef __cplusplus
}
#endif

#endif
