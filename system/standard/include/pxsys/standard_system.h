#ifndef PXSYS_STANDARD_SYSTEM_H
#define PXSYS_STANDARD_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/native_runtime.h"
#include "pxsys/pxsys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    size_t max_apps;
    size_t max_runtime_providers;
    size_t max_instances;
    size_t max_native_apps;
    size_t max_intent_filters;
    size_t max_tasks;
    size_t max_role_candidates;
    size_t max_service_providers;
    size_t max_event_subscriptions;
    size_t max_theme_observers;
    size_t max_intent_wire_bytes;
    size_t max_renderer_id_bytes;
    pxsys_theme_snapshot_t initial_theme;
    const pxsys_renderer_provider_t* initial_renderer;
    pxsys_allocator_t allocator;
    void* service_policy_context;
    pxsys_service_policy_fn authorize_service;
    void* event_policy_context;
    pxsys_topic_policy_fn authorize_event;
    void* role_policy_context;
    pxsys_role_policy_fn authorize_role;
    void* navigation_policy_context;
    pxsys_navigation_policy_fn authorize_navigation;
    /* Appended fields preserve the prefix consumed by older product ports. */
    size_t max_active_system_roles;
    size_t max_display_observers;
    pxsys_display_profile_t initial_display;
    size_t max_system_status_observers;
    size_t max_toast_observers;
    size_t max_toast_message_bytes;
    pxsys_system_status_snapshot_t initial_system_status;
    size_t max_window_observers;
    pxsys_window_snapshot_t initial_window;
    void* network_control_context;
    pxsys_network_set_enabled_fn set_network_enabled;
    void* control_context;
    pxsys_level_control_set_fn set_level;
    pxsys_toggle_control_set_fn set_toggle;
} pxsys_standard_system_config_t;

typedef struct pxsys_standard_system pxsys_standard_system_t;

void pxsys_standard_system_config_init(pxsys_standard_system_config_t* config);
pxsys_status_t pxsys_standard_system_create(const pxsys_standard_system_config_t* config,
                                            pxsys_standard_system_t** output);
pxsys_status_t pxsys_standard_system_destroy(pxsys_standard_system_t* system);

pxsys_app_registry_t* pxsys_standard_system_apps(pxsys_standard_system_t* system);
pxsys_runtime_t* pxsys_standard_system_runtime(pxsys_standard_system_t* system);
pxsys_native_runtime_t* pxsys_standard_system_native_runtime(pxsys_standard_system_t* system);
pxsys_intent_resolver_t* pxsys_standard_system_intents(pxsys_standard_system_t* system);
pxsys_task_manager_t* pxsys_standard_system_tasks(pxsys_standard_system_t* system);
pxsys_role_registry_t* pxsys_standard_system_roles(pxsys_standard_system_t* system);
pxsys_role_host_t* pxsys_standard_system_role_host(pxsys_standard_system_t* system);
pxsys_theme_service_t* pxsys_standard_system_theme(pxsys_standard_system_t* system);
pxsys_display_service_t* pxsys_standard_system_display(pxsys_standard_system_t* system);
pxsys_system_status_service_t* pxsys_standard_system_status(pxsys_standard_system_t* system);
pxsys_toast_service_t* pxsys_standard_system_toasts(pxsys_standard_system_t* system);
pxsys_window_service_t* pxsys_standard_system_window(pxsys_standard_system_t* system);
pxsys_renderer_host_t* pxsys_standard_system_renderer(pxsys_standard_system_t* system);
pxsys_service_registry_t* pxsys_standard_system_services(pxsys_standard_system_t* system);
pxsys_event_broker_t* pxsys_standard_system_events(pxsys_standard_system_t* system);

#ifdef __cplusplus
}
#endif

#endif
