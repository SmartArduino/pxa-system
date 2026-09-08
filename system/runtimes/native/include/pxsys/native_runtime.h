#ifndef PXSYS_NATIVE_RUNTIME_H
#define PXSYS_NATIVE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_NATIVE_RUNTIME_ID "native-static"

typedef pxsys_status_t (*pxsys_native_create_fn)(void* context, const pxsys_app_descriptor_t* app,
                                                 uint64_t instance_id, void** app_instance);
typedef pxsys_status_t (*pxsys_native_start_fn)(void* context, void* app_instance,
                                                const pxsys_message_t* launch);
typedef pxsys_status_t (*pxsys_native_state_fn)(void* context, void* app_instance);
typedef pxsys_status_t (*pxsys_native_event_fn)(void* context, void* app_instance,
                                                const pxsys_message_t* event);
typedef pxsys_back_result_t (*pxsys_native_back_fn)(void* context, void* app_instance);
typedef void (*pxsys_native_stop_fn)(void* context, void* app_instance, pxsys_stop_reason_t reason);
typedef void (*pxsys_native_destroy_fn)(void* context, void* app_instance);

typedef struct {
    uint32_t struct_size;
    pxsys_app_identity_t identity;
    void* context;
    pxsys_native_create_fn create;
    pxsys_native_start_fn start;
    pxsys_native_state_fn foreground;
    pxsys_native_state_fn background;
    pxsys_native_event_fn event;
    pxsys_native_back_fn back;
    pxsys_native_stop_fn stop;
    pxsys_native_destroy_fn destroy;
} pxsys_native_app_t;

typedef struct {
    uint32_t struct_size;
    size_t max_apps;
    size_t max_app_id_bytes;
    pxsys_allocator_t allocator;
} pxsys_native_runtime_config_t;

typedef struct pxsys_native_runtime pxsys_native_runtime_t;

void pxsys_native_runtime_config_init(pxsys_native_runtime_config_t* config);
pxsys_status_t pxsys_native_runtime_create(const pxsys_native_runtime_config_t* config,
                                           pxsys_native_runtime_t** output);
pxsys_status_t pxsys_native_runtime_destroy(pxsys_native_runtime_t* runtime);

pxsys_status_t pxsys_native_runtime_register_app(pxsys_native_runtime_t* runtime,
                                                 const pxsys_native_app_t* app);
pxsys_status_t pxsys_native_runtime_unregister_app(pxsys_native_runtime_t* runtime,
                                                   const pxsys_app_identity_t* identity);

pxsys_status_t pxsys_native_runtime_provider(pxsys_native_runtime_t* runtime,
                                             pxsys_runtime_provider_t* provider);
/* The native runtime must outlive every core runtime where this provider is
 * registered. Unregister the provider before destroying the native runtime. */

#ifdef __cplusplus
}
#endif

#endif
