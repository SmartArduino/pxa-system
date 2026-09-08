#ifndef PXSYS_RUNTIME_H
#define PXSYS_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/app_lifecycle.h"
#include "pxsys/app_registry.h"
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
    /* Supplied by a system binding, never decoded from app-controlled payload. */
    const pxsys_caller_t* caller;
} pxsys_message_t;

typedef uint8_t pxsys_back_result_t;
#define PXSYS_BACK_UNHANDLED ((pxsys_back_result_t)0)
#define PXSYS_BACK_HANDLED ((pxsys_back_result_t)1)

typedef struct {
    uint32_t slot;
    uint32_t generation;
} pxsys_instance_ref_t;

#define PXSYS_INSTANCE_REF_INVALID_SLOT UINT32_MAX

typedef pxsys_status_t (*pxsys_runtime_instantiate_fn)(void* context,
                                                       const pxsys_app_descriptor_t* app,
                                                       uint64_t instance_id,
                                                       void** runtime_instance);
typedef void (*pxsys_runtime_bound_fn)(void* context, void* runtime_instance,
                                       pxsys_instance_ref_t instance);
/* destroy is called after every instantiate attempt, including failure. The
 * runtime_instance value may be NULL when instantiate could not allocate. */
typedef pxsys_status_t (*pxsys_runtime_start_fn)(void* context, void* runtime_instance,
                                                 const pxsys_message_t* launch);
typedef pxsys_status_t (*pxsys_runtime_state_fn)(void* context, void* runtime_instance);
typedef pxsys_status_t (*pxsys_runtime_event_fn)(void* context, void* runtime_instance,
                                                 const pxsys_message_t* event);
typedef pxsys_back_result_t (*pxsys_runtime_back_fn)(void* context, void* runtime_instance);
typedef void (*pxsys_runtime_stop_fn)(void* context, void* runtime_instance,
                                      pxsys_stop_reason_t reason);
typedef pxsys_status_t (*pxsys_runtime_request_stop_fn)(void* context, void* runtime_instance,
                                                        pxsys_stop_reason_t reason);
typedef void (*pxsys_runtime_destroy_fn)(void* context, void* runtime_instance);

typedef struct {
    uint32_t struct_size;
    pxsys_string_t runtime_id;
    pxsys_version_t version;
    uint64_t features;
    void* context;
    pxsys_runtime_instantiate_fn instantiate;
    pxsys_runtime_start_fn start;
    pxsys_runtime_state_fn foreground;
    pxsys_runtime_state_fn background;
    pxsys_runtime_event_fn event;
    pxsys_runtime_back_fn back;
    pxsys_runtime_stop_fn stop;
    pxsys_runtime_destroy_fn destroy;
    /* Optional: called after instantiate succeeds and before start. New SPI
     * fields are appended so older struct_size prefixes retain their layout. */
    pxsys_runtime_bound_fn bound;
    /* Optional asynchronous stop. OK means already stopped; PENDING keeps the
     * instance in STOPPING until pxsys_runtime_report_stopped(). */
    pxsys_runtime_request_stop_fn request_stop;
} pxsys_runtime_provider_t;

typedef struct {
    uint32_t struct_size;
    size_t max_providers;
    size_t max_instances;
    size_t max_runtime_id_bytes;
    pxsys_app_registry_t* apps;
    pxsys_allocator_t allocator;
} pxsys_runtime_config_t;

typedef struct {
    uint32_t struct_size;
    uint64_t instance_id;
    pxsys_app_lifecycle_t lifecycle;
    const pxsys_app_descriptor_t* app;
} pxsys_instance_snapshot_t;

typedef struct pxsys_runtime pxsys_runtime_t;

void pxsys_runtime_config_init(pxsys_runtime_config_t* config);
pxsys_status_t pxsys_runtime_create(const pxsys_runtime_config_t* config, pxsys_runtime_t** output);
pxsys_status_t pxsys_runtime_destroy(pxsys_runtime_t* runtime);

pxsys_status_t pxsys_runtime_register_provider(pxsys_runtime_t* runtime,
                                               const pxsys_runtime_provider_t* provider);
pxsys_status_t pxsys_runtime_unregister_provider(pxsys_runtime_t* runtime,
                                                 pxsys_string_t runtime_id);

pxsys_instance_ref_t pxsys_instance_ref_invalid(void);
pxsys_status_t pxsys_runtime_launch(pxsys_runtime_t* runtime, const pxsys_app_identity_t* identity,
                                    const pxsys_message_t* launch, int foreground,
                                    pxsys_instance_ref_t* output);
/* Completes a launch whose provider start callback returned PENDING. Must be
 * called on the runtime owner thread. A failed completion releases the
 * instance and invalidates its reference. */
pxsys_status_t pxsys_runtime_complete_start(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                            pxsys_status_t result);
/* Reaps an instance already stopped by an external runtime. This does not call
 * the provider stop callback, but always calls destroy and releases ownership. */
pxsys_status_t pxsys_runtime_report_stopped(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                            pxsys_stop_reason_t reason);
pxsys_status_t pxsys_runtime_foreground(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance);
pxsys_status_t pxsys_runtime_background(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance);
pxsys_status_t pxsys_runtime_deliver(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                     const pxsys_message_t* event);
pxsys_status_t pxsys_runtime_back(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                  pxsys_back_result_t* result);
pxsys_status_t pxsys_runtime_stop(pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                  pxsys_stop_reason_t reason);
pxsys_status_t pxsys_runtime_snapshot(const pxsys_runtime_t* runtime, pxsys_instance_ref_t instance,
                                      pxsys_instance_snapshot_t* output);
size_t pxsys_runtime_instance_count(const pxsys_runtime_t* runtime);

#ifdef __cplusplus
}
#endif

#endif
