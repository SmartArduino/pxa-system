#ifndef PXSYS_TASK_MANAGER_H
#define PXSYS_TASK_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/intent.h"
#include "pxsys/role_registry.h"
#include "pxsys/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_INTENT_INTERFACE_ID "system.intent"
#define PXSYS_INTENT_OPERATION_START UINT32_C(1)
#define PXSYS_INTENT_OPERATION_DELIVER UINT32_C(2)

typedef pxsys_status_t (*pxsys_navigation_policy_fn)(void* context, const pxsys_caller_t* caller,
                                                     uint32_t operation,
                                                     const pxsys_intent_t* intent,
                                                     const pxsys_app_descriptor_t* target);

typedef struct {
    uint32_t struct_size;
    size_t max_tasks;
    size_t max_intent_wire_bytes;
    pxsys_intent_resolver_t* intents;
    pxsys_runtime_t* runtime;
    void* policy_context;
    pxsys_navigation_policy_fn authorize;
    pxsys_allocator_t allocator;
} pxsys_task_manager_config_t;

typedef struct pxsys_task_manager pxsys_task_manager_t;

void pxsys_task_manager_config_init(pxsys_task_manager_config_t* config);
pxsys_status_t pxsys_task_manager_create(const pxsys_task_manager_config_t* config,
                                         pxsys_task_manager_t** output);
pxsys_status_t pxsys_task_manager_destroy(pxsys_task_manager_t* manager);
pxsys_status_t pxsys_task_manager_start(pxsys_task_manager_t* manager, const pxsys_intent_t* intent,
                                        pxsys_instance_ref_t* instance);
pxsys_status_t pxsys_task_manager_start_as(pxsys_task_manager_t* manager,
                                           const pxsys_caller_t* caller,
                                           const pxsys_intent_t* intent,
                                           pxsys_instance_ref_t* instance);
/* Resolves the current role provider, then starts it through the same Intent,
 * caller identity and navigation-policy path as an explicitly targeted App. */
pxsys_status_t pxsys_task_manager_start_role(pxsys_task_manager_t* manager,
                                             const pxsys_role_registry_t* roles,
                                             const pxsys_caller_t* caller,
                                             pxsys_string_t role_id,
                                             const pxsys_intent_t* intent,
                                             pxsys_instance_ref_t* instance);
/* Completes a pending start. On failure the task is removed and the previous
 * top task is foregrounded. */
pxsys_status_t pxsys_task_manager_complete_start(pxsys_task_manager_t* manager,
                                                 pxsys_instance_ref_t instance,
                                                 pxsys_status_t result);
/* Removes an App which exited independently and restores the previous task
 * when the exited App was on top. */
pxsys_status_t pxsys_task_manager_report_stopped(pxsys_task_manager_t* manager,
                                                 pxsys_instance_ref_t instance,
                                                 pxsys_stop_reason_t reason);
pxsys_status_t pxsys_task_manager_back(pxsys_task_manager_t* manager, pxsys_back_result_t* result);
pxsys_status_t pxsys_task_manager_finish_top(pxsys_task_manager_t* manager,
                                             pxsys_stop_reason_t reason);
/* Stops one specific task (for example from a recents UI) and restores the
 * previous task when the finished App was on top. PENDING means the runtime
 * stops asynchronously; the instance is removed on report_stopped. */
pxsys_status_t pxsys_task_manager_finish_instance(pxsys_task_manager_t* manager,
                                                  pxsys_instance_ref_t instance,
                                                  pxsys_stop_reason_t reason);
pxsys_status_t pxsys_task_manager_finish_all(pxsys_task_manager_t* manager,
                                             pxsys_stop_reason_t reason);
size_t pxsys_task_manager_count(const pxsys_task_manager_t* manager);
pxsys_status_t pxsys_task_manager_current(const pxsys_task_manager_t* manager,
                                          pxsys_instance_ref_t* instance);
/* Recents are indexed from the foreground task (index 0) backwards. */
pxsys_status_t pxsys_task_manager_task(
    const pxsys_task_manager_t* manager, size_t recent_index,
    pxsys_instance_ref_t* instance, pxsys_instance_snapshot_t* snapshot);
pxsys_status_t pxsys_task_manager_activate(
    pxsys_task_manager_t* manager, pxsys_instance_ref_t instance);

#ifdef __cplusplus
}
#endif

#endif
