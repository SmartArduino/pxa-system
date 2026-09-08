#ifndef PXSYS_PXA_RUNTIME_H
#define PXSYS_PXA_RUNTIME_H

#include <stdint.h>

#include "pxsys/pxa_binding.h"
#include "pxsys/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    void* context;
    pxsys_status_t (*instantiate)(void* context, const pxsys_app_descriptor_t* app,
                                  uint64_t instance_id, void** backend_instance);
    pxsys_status_t (*start)(void* context, void* backend_instance, const pxsys_message_t* launch);
    pxsys_status_t (*foreground)(void* context, void* backend_instance);
    pxsys_status_t (*background)(void* context, void* backend_instance);
    pxsys_status_t (*deliver)(void* context, void* backend_instance,
                              const pxsys_message_t* message);
    pxsys_back_result_t (*back)(void* context, void* backend_instance);
    void (*stop)(void* context, void* backend_instance, pxsys_stop_reason_t reason);
    void (*destroy)(void* context, void* backend_instance);
    /* Append optional callbacks to preserve older struct_size prefixes. */
    void (*bound)(void* context, void* backend_instance, pxsys_instance_ref_t instance);
    pxsys_status_t (*request_stop)(void* context, void* backend_instance,
                                   pxsys_stop_reason_t reason);
} pxsys_pxa_runtime_backend_t;

typedef struct {
    uint32_t struct_size;
    pxsys_string_t runtime_id;
    pxsys_version_t version;
    uint64_t features;
    pxsys_pxa_runtime_backend_t backend;
    pxsys_allocator_t allocator;
} pxsys_pxa_runtime_config_t;

typedef struct pxsys_pxa_runtime pxsys_pxa_runtime_t;

pxsys_status_t pxsys_pxa_runtime_create(const pxsys_pxa_runtime_config_t* config,
                                        pxsys_pxa_runtime_t** output);
pxsys_status_t pxsys_pxa_runtime_destroy(pxsys_pxa_runtime_t* runtime);
pxsys_status_t pxsys_pxa_runtime_provider(pxsys_pxa_runtime_t* runtime,
                                          pxsys_runtime_provider_t* provider);

#ifdef __cplusplus
}
#endif

#endif
