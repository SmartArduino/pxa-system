#include "pxsys/standard_system.h"

#include <string.h>

#define PXSYS_STANDARD_MAGIC UINT32_C(0x50585353)

struct pxsys_standard_system {
    uint32_t magic;
    pxsys_allocator_t allocator;
    pxsys_app_registry_t* apps;
    pxsys_runtime_t* runtime;
    pxsys_native_runtime_t* native_runtime;
    pxsys_intent_resolver_t* intents;
    pxsys_task_manager_t* tasks;
    pxsys_role_registry_t* roles;
    pxsys_role_host_t* role_host;
    pxsys_theme_service_t* theme;
    pxsys_display_service_t* display;
    pxsys_system_status_service_t* status;
    pxsys_toast_service_t* toasts;
    pxsys_window_service_t* window;
    pxsys_renderer_host_t* renderer;
    pxsys_service_registry_t* services;
    pxsys_event_broker_t* events;
    uint8_t renderer_theme_subscribed;
};

static int system_valid(const pxsys_standard_system_t* system) {
    return system != NULL && system->magic == PXSYS_STANDARD_MAGIC;
}

static void renderer_theme_changed(void* context, const pxsys_theme_snapshot_t* theme);

static pxsys_status_t toast_service_invoke(
    void* context, const pxsys_service_request_t* request,
    void* completion_context, pxsys_service_complete_fn complete) {
    pxsys_toast_message_t toast;
    pxsys_status_t status;
    if (request->operation != PXSYS_TOAST_OPERATION_POST)
        return PXSYS_STATUS_UNSUPPORTED;
    status = pxsys_toast_wire_decode(request->payload, &toast);
    if (status != PXSYS_STATUS_OK) return status;
    status = pxsys_toast_service_post((pxsys_toast_service_t*)context, &toast);
    if (status == PXSYS_STATUS_OK)
        complete(completion_context, request->request_id, PXSYS_STATUS_OK,
                 (pxsys_bytes_t){NULL, 0});
    return status;
}

void pxsys_standard_system_config_init(pxsys_standard_system_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_apps = 32;
    config->max_runtime_providers = 4;
    config->max_instances = 16;
    config->max_native_apps = 24;
    config->max_intent_filters = 64;
    config->max_tasks = 16;
    config->max_role_candidates = 32;
    config->max_active_system_roles = 8;
    config->max_display_observers = 16;
    config->max_system_status_observers = 16;
    config->max_toast_observers = 8;
    config->max_toast_message_bytes = 512;
    config->max_window_observers = 8;
    config->max_service_providers = 32;
    config->max_event_subscriptions = 64;
    config->max_theme_observers = 16;
    config->max_intent_wire_bytes = 4096;
    config->max_renderer_id_bytes = 64;
    pxsys_theme_snapshot_init(&config->initial_theme, PXSYS_COLOR_SCHEME_LIGHT);
    pxsys_display_profile_init(&config->initial_display, 320, 240);
    pxsys_system_status_snapshot_init(&config->initial_system_status);
    pxsys_window_snapshot_init(&config->initial_window);
    config->allocator.struct_size = sizeof(config->allocator);
}

static int config_valid(const pxsys_standard_system_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_apps != 0 &&
           config->max_runtime_providers != 0 && config->max_instances != 0 &&
           config->max_native_apps != 0 && config->max_intent_filters != 0 &&
           config->max_tasks != 0 && config->max_role_candidates != 0 &&
           config->max_active_system_roles != 0 &&
           config->max_display_observers != 0 &&
           config->max_system_status_observers != 0 &&
           config->max_toast_observers != 0 && config->max_toast_message_bytes != 0 &&
           config->max_window_observers != 0 &&
           config->max_service_providers != 0 && config->max_theme_observers != 0 &&
           config->max_event_subscriptions != 0 && config->max_intent_wire_bytes != 0 &&
           config->max_renderer_id_bytes != 0 && config->max_renderer_id_bytes != SIZE_MAX &&
           pxsys_display_profile_validate(&config->initial_display) == PXSYS_STATUS_OK &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

static void destroy_partial(pxsys_standard_system_t* system) {
    if (system->role_host != NULL)
        (void)pxsys_role_host_destroy(system->role_host);
    if (system->tasks != NULL) {
        (void)pxsys_task_manager_finish_all(system->tasks, PXSYS_STOP_SHUTDOWN);
        (void)pxsys_task_manager_destroy(system->tasks);
    }
    if (system->roles != NULL)
        (void)pxsys_role_registry_destroy(system->roles);
    if (system->intents != NULL)
        (void)pxsys_intent_resolver_destroy(system->intents);
    if (system->runtime != NULL)
        (void)pxsys_runtime_destroy(system->runtime);
    if (system->native_runtime != NULL)
        (void)pxsys_native_runtime_destroy(system->native_runtime);
    if (system->services != NULL)
        (void)pxsys_service_registry_destroy(system->services);
    if (system->events != NULL)
        (void)pxsys_event_broker_destroy(system->events);
    if (system->renderer_theme_subscribed && system->theme != NULL) {
        (void)pxsys_theme_service_unsubscribe(system->theme, system->renderer,
                                              renderer_theme_changed);
        system->renderer_theme_subscribed = 0;
    }
    if (system->renderer != NULL)
        (void)pxsys_renderer_host_destroy(system->renderer);
    if (system->theme != NULL)
        (void)pxsys_theme_service_destroy(system->theme);
    if (system->display != NULL)
        (void)pxsys_display_service_destroy(system->display);
    if (system->status != NULL)
        (void)pxsys_system_status_service_destroy(system->status);
    if (system->toasts != NULL)
        (void)pxsys_toast_service_destroy(system->toasts);
    if (system->window != NULL)
        (void)pxsys_window_service_destroy(system->window);
    if (system->apps != NULL)
        (void)pxsys_app_registry_destroy(system->apps);
}

static void renderer_theme_changed(void* context, const pxsys_theme_snapshot_t* theme) {
    (void)pxsys_renderer_host_set_theme((pxsys_renderer_host_t*)context, theme);
}

pxsys_status_t pxsys_standard_system_create(const pxsys_standard_system_config_t* config,
                                            pxsys_standard_system_t** output) {
    pxsys_standard_system_t* system;
    pxsys_app_registry_config_t apps;
    pxsys_native_runtime_config_t native;
    pxsys_runtime_config_t runtime;
    pxsys_intent_resolver_config_t intents;
    pxsys_task_manager_config_t tasks;
    pxsys_role_registry_config_t roles;
    pxsys_role_host_config_t role_host;
    pxsys_theme_service_config_t theme;
    pxsys_display_service_config_t display;
    pxsys_system_status_service_config_t system_status;
    pxsys_toast_service_config_t toasts;
    pxsys_window_service_config_t window;
    pxsys_renderer_host_config_t renderer;
    pxsys_service_registry_config_t services;
    pxsys_event_broker_config_t events;
    pxsys_runtime_provider_t native_provider;
    pxsys_service_provider_t toast_provider;
    pxsys_status_t status;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    system = (pxsys_standard_system_t*)config->allocator.allocate(config->allocator.context,
                                                                  sizeof(*system));
    if (system == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(system, 0, sizeof(*system));
    system->allocator = config->allocator;
    system->magic = PXSYS_STANDARD_MAGIC;

    pxsys_app_registry_config_init(&apps);
    apps.max_apps = config->max_apps;
    apps.allocator = config->allocator;
    if ((status = pxsys_app_registry_create(&apps, &system->apps)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_event_broker_config_init(&events);
    events.max_subscriptions = config->max_event_subscriptions;
    events.apps = system->apps;
    events.policy_context = config->event_policy_context;
    events.authorize = config->authorize_event;
    events.allocator = config->allocator;
    if ((status = pxsys_event_broker_create(&events, &system->events)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_native_runtime_config_init(&native);
    native.max_apps = config->max_native_apps;
    native.allocator = config->allocator;
    if ((status = pxsys_native_runtime_create(&native, &system->native_runtime)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_runtime_config_init(&runtime);
    runtime.max_providers = config->max_runtime_providers;
    runtime.max_instances = config->max_instances;
    runtime.apps = system->apps;
    runtime.allocator = config->allocator;
    if ((status = pxsys_runtime_create(&runtime, &system->runtime)) != PXSYS_STATUS_OK)
        goto failed;
    if ((status = pxsys_native_runtime_provider(system->native_runtime, &native_provider)) !=
            PXSYS_STATUS_OK ||
        (status = pxsys_runtime_register_provider(system->runtime, &native_provider)) !=
            PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_intent_resolver_config_init(&intents);
    intents.max_filters = config->max_intent_filters;
    intents.apps = system->apps;
    intents.allocator = config->allocator;
    if ((status = pxsys_intent_resolver_create(&intents, &system->intents)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_task_manager_config_init(&tasks);
    tasks.max_tasks = config->max_tasks;
    tasks.max_intent_wire_bytes = config->max_intent_wire_bytes;
    tasks.intents = system->intents;
    tasks.runtime = system->runtime;
    tasks.policy_context = config->navigation_policy_context;
    tasks.authorize = config->authorize_navigation;
    tasks.allocator = config->allocator;
    if ((status = pxsys_task_manager_create(&tasks, &system->tasks)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_role_registry_config_init(&roles);
    roles.max_candidates = config->max_role_candidates;
    roles.apps = system->apps;
    roles.policy_context = config->role_policy_context;
    roles.authorize = config->authorize_role;
    roles.allocator = config->allocator;
    if ((status = pxsys_role_registry_create(&roles, &system->roles)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_role_host_config_init(&role_host);
    role_host.max_active_roles = config->max_active_system_roles;
    role_host.max_intent_wire_bytes = config->max_intent_wire_bytes;
    role_host.roles = system->roles;
    role_host.runtime = system->runtime;
    role_host.policy_context = config->navigation_policy_context;
    role_host.authorize = config->authorize_navigation;
    role_host.allocator = config->allocator;
    if ((status = pxsys_role_host_create(&role_host, &system->role_host)) != PXSYS_STATUS_OK)
        goto failed;
    pxsys_theme_service_config_init(&theme);
    theme.max_observers = config->max_theme_observers;
    theme.allocator = config->allocator;
    if ((status = pxsys_theme_service_create(&theme, &config->initial_theme, &system->theme)) !=
        PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_display_service_config_init(&display);
    display.max_observers = config->max_display_observers;
    display.allocator = config->allocator;
    if ((status = pxsys_display_service_create(&display, &config->initial_display,
                                               &system->display)) != PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_system_status_service_config_init(&system_status);
    system_status.max_observers = config->max_system_status_observers;
    system_status.allocator = config->allocator;
    if ((status = pxsys_system_status_service_create(
             &system_status, &config->initial_system_status,
             &system->status)) != PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_toast_service_config_init(&toasts);
    toasts.max_observers = config->max_toast_observers;
    toasts.max_message_bytes = config->max_toast_message_bytes;
    toasts.allocator = config->allocator;
    if ((status = pxsys_toast_service_create(&toasts, &system->toasts)) !=
        PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_window_service_config_init(&window);
    window.max_observers = config->max_window_observers;
    window.allocator = config->allocator;
    if ((status = pxsys_window_service_create(
             &window, &config->initial_window, &system->window)) !=
        PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_renderer_host_config_init(&renderer);
    renderer.max_renderer_id_bytes = config->max_renderer_id_bytes;
    renderer.initial_theme = config->initial_theme;
    renderer.allocator = config->allocator;
    if ((status = pxsys_renderer_host_create(&renderer, &system->renderer)) != PXSYS_STATUS_OK)
        goto failed;
    status = pxsys_theme_service_subscribe(system->theme, system->renderer,
                                           renderer_theme_changed);
    if (status != PXSYS_STATUS_OK)
        goto failed;
    system->renderer_theme_subscribed = 1;
    if (config->initial_renderer != NULL &&
        (status = pxsys_renderer_host_bind(system->renderer, config->initial_renderer)) !=
            PXSYS_STATUS_OK) {
        goto failed;
    }
    pxsys_service_registry_config_init(&services);
    services.max_providers = config->max_service_providers;
    services.policy_context = config->service_policy_context;
    services.authorize = config->authorize_service;
    services.allocator = config->allocator;
    if ((status = pxsys_service_registry_create(&services, &system->services)) != PXSYS_STATUS_OK)
        goto failed;
    memset(&toast_provider, 0, sizeof(toast_provider));
    toast_provider.struct_size = sizeof(toast_provider);
    toast_provider.interface_id = pxsys_string_from_cstr(PXSYS_TOAST_INTERFACE_ID);
    toast_provider.version = (pxsys_version_t){1, 0};
    toast_provider.context = system->toasts;
    toast_provider.invoke = toast_service_invoke;
    if ((status = pxsys_service_register(system->services, &toast_provider)) !=
        PXSYS_STATUS_OK)
        goto failed;
    *output = system;
    return PXSYS_STATUS_OK;

failed:
    destroy_partial(system);
    system->magic = 0;
    config->allocator.release(config->allocator.context, system);
    return status;
}

pxsys_status_t pxsys_standard_system_destroy(pxsys_standard_system_t* system) {
    pxsys_allocator_t allocator;
    pxsys_status_t status;
    if (!system_valid(system))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (pxsys_service_active_call_count(system->services) != 0)
        return PXSYS_STATUS_BUSY;
    if (pxsys_renderer_host_surface_count(system->renderer) != 0)
        return PXSYS_STATUS_BUSY;
    status = pxsys_role_host_stop_all(system->role_host, PXSYS_STOP_SHUTDOWN);
    if (status == PXSYS_STATUS_PENDING || status == PXSYS_STATUS_BUSY)
        return PXSYS_STATUS_BUSY;
    if (status != PXSYS_STATUS_OK)
        return status;
    status = pxsys_task_manager_finish_all(system->tasks, PXSYS_STOP_SHUTDOWN);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (pxsys_runtime_instance_count(system->runtime) != 0)
        return PXSYS_STATUS_BUSY;
    allocator = system->allocator;
    destroy_partial(system);
    system->magic = 0;
    allocator.release(allocator.context, system);
    return PXSYS_STATUS_OK;
}

#define PXSYS_GETTER(name, type, field)                     \
    type* name(pxsys_standard_system_t* system) {           \
        return system_valid(system) ? system->field : NULL; \
    }

PXSYS_GETTER(pxsys_standard_system_apps, pxsys_app_registry_t, apps)
PXSYS_GETTER(pxsys_standard_system_runtime, pxsys_runtime_t, runtime)
PXSYS_GETTER(pxsys_standard_system_native_runtime, pxsys_native_runtime_t, native_runtime)
PXSYS_GETTER(pxsys_standard_system_intents, pxsys_intent_resolver_t, intents)
PXSYS_GETTER(pxsys_standard_system_tasks, pxsys_task_manager_t, tasks)
PXSYS_GETTER(pxsys_standard_system_roles, pxsys_role_registry_t, roles)
PXSYS_GETTER(pxsys_standard_system_role_host, pxsys_role_host_t, role_host)
PXSYS_GETTER(pxsys_standard_system_theme, pxsys_theme_service_t, theme)
PXSYS_GETTER(pxsys_standard_system_display, pxsys_display_service_t, display)
PXSYS_GETTER(pxsys_standard_system_status, pxsys_system_status_service_t, status)
PXSYS_GETTER(pxsys_standard_system_toasts, pxsys_toast_service_t, toasts)
PXSYS_GETTER(pxsys_standard_system_window, pxsys_window_service_t, window)
PXSYS_GETTER(pxsys_standard_system_renderer, pxsys_renderer_host_t, renderer)
PXSYS_GETTER(pxsys_standard_system_services, pxsys_service_registry_t, services)
PXSYS_GETTER(pxsys_standard_system_events, pxsys_event_broker_t, events)
