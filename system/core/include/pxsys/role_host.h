#ifndef PXSYS_ROLE_HOST_H
#define PXSYS_ROLE_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/intent.h"
#include "pxsys/role_registry.h"
#include "pxsys/runtime.h"
#include "pxsys/task_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    size_t max_active_roles;
    size_t max_role_id_bytes;
    size_t max_intent_wire_bytes;
    pxsys_role_registry_t* roles;
    pxsys_runtime_t* runtime;
    void* policy_context;
    pxsys_navigation_policy_fn authorize;
    pxsys_allocator_t allocator;
} pxsys_role_host_config_t;

typedef struct pxsys_role_host pxsys_role_host_t;

void pxsys_role_host_config_init(pxsys_role_host_config_t* config);
pxsys_status_t pxsys_role_host_create(const pxsys_role_host_config_t* config,
                                      pxsys_role_host_t** output);
pxsys_status_t pxsys_role_host_destroy(pxsys_role_host_t* host);

/* Starts or atomically replaces a persistent system role. Existing providers
 * stay active until an asynchronous replacement reports a successful start. */
pxsys_status_t pxsys_role_host_start(pxsys_role_host_t* host,
                                     const pxsys_caller_t* caller,
                                     pxsys_string_t role_id,
                                     const pxsys_intent_t* intent,
                                     pxsys_instance_ref_t* instance);
pxsys_status_t pxsys_role_host_complete_start(pxsys_role_host_t* host,
                                              pxsys_instance_ref_t instance,
                                              pxsys_status_t result);
pxsys_status_t pxsys_role_host_report_stopped(pxsys_role_host_t* host,
                                              pxsys_instance_ref_t instance,
                                              pxsys_stop_reason_t reason);
pxsys_status_t pxsys_role_host_stop(pxsys_role_host_t* host,
                                    pxsys_string_t role_id,
                                    pxsys_stop_reason_t reason);
pxsys_status_t pxsys_role_host_stop_all(pxsys_role_host_t* host,
                                        pxsys_stop_reason_t reason);
pxsys_status_t pxsys_role_host_current(const pxsys_role_host_t* host,
                                       pxsys_string_t role_id,
                                       pxsys_instance_ref_t* instance);
size_t pxsys_role_host_count(const pxsys_role_host_t* host);

#ifdef __cplusplus
}
#endif

#endif
