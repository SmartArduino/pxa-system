#include "pxsys/native_runtime.h"

#include <stddef.h>
#include <string.h>

#define PXSYS_NATIVE_MAGIC UINT32_C(0x50584e52)

typedef struct {
    pxsys_native_app_t app;
    char* app_id;
    uint32_t active_instances;
    uint8_t occupied;
} pxsys_native_entry_t;

typedef struct {
    pxsys_native_entry_t* entry;
    void* app_instance;
} pxsys_native_instance_t;

struct pxsys_native_runtime {
    uint32_t magic;
    size_t count;
    size_t capacity;
    size_t max_app_id_bytes;
    uint32_t active_instances;
    pxsys_allocator_t allocator;
    pxsys_native_entry_t* entries;
};

static int native_runtime_valid(const pxsys_native_runtime_t* runtime) {
    return runtime != NULL && runtime->magic == PXSYS_NATIVE_MAGIC;
}

static pxsys_native_entry_t* find_app(const pxsys_native_runtime_t* runtime,
                                      const pxsys_app_identity_t* identity) {
    size_t index;
    for (index = 0; index < runtime->capacity; ++index) {
        pxsys_native_entry_t* entry = &runtime->entries[index];
        if (entry->occupied && pxsys_app_identity_equal(&entry->app.identity, identity))
            return entry;
    }
    return NULL;
}

void pxsys_native_runtime_config_init(pxsys_native_runtime_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_apps = 32;
    config->max_app_id_bytes = 64;
    config->allocator.struct_size = sizeof(config->allocator);
}

static int config_valid(const pxsys_native_runtime_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_apps != 0 &&
           config->max_app_id_bytes != 0 && config->max_app_id_bytes != SIZE_MAX &&
           config->max_apps <= SIZE_MAX / sizeof(pxsys_native_entry_t) &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

pxsys_status_t pxsys_native_runtime_create(const pxsys_native_runtime_config_t* config,
                                           pxsys_native_runtime_t** output) {
    pxsys_native_runtime_t* runtime;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    runtime = (pxsys_native_runtime_t*)config->allocator.allocate(config->allocator.context,
                                                                  sizeof(*runtime));
    if (runtime == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->entries = (pxsys_native_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_apps * sizeof(*runtime->entries));
    if (runtime->entries == NULL) {
        config->allocator.release(config->allocator.context, runtime);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(runtime->entries, 0, config->max_apps * sizeof(*runtime->entries));
    runtime->capacity = config->max_apps;
    runtime->max_app_id_bytes = config->max_app_id_bytes;
    runtime->allocator = config->allocator;
    runtime->magic = PXSYS_NATIVE_MAGIC;
    *output = runtime;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_native_runtime_destroy(pxsys_native_runtime_t* runtime) {
    size_t index;
    pxsys_allocator_t allocator;
    if (!native_runtime_valid(runtime))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (runtime->active_instances != 0)
        return PXSYS_STATUS_BUSY;
    allocator = runtime->allocator;
    for (index = 0; index < runtime->capacity; ++index) {
        if (runtime->entries[index].occupied)
            allocator.release(allocator.context, runtime->entries[index].app_id);
    }
    runtime->magic = 0;
    allocator.release(allocator.context, runtime->entries);
    allocator.release(allocator.context, runtime);
    return PXSYS_STATUS_OK;
}

static int native_app_valid(const pxsys_native_runtime_t* runtime, const pxsys_native_app_t* app) {
    const size_t base_size = offsetof(pxsys_native_app_t, display);
    int has_display_create;
    if (app == NULL || app->struct_size < base_size) return 0;
    has_display_create = app->struct_size >= sizeof(*app) &&
                         app->create_display != NULL;
    return
           pxsys_app_identity_validate(&app->identity, runtime->max_app_id_bytes) ==
               PXSYS_STATUS_OK &&
           (app->create != NULL || has_display_create) &&
           app->start != NULL && app->foreground != NULL &&
           app->background != NULL && app->event != NULL && app->back != NULL &&
           app->stop != NULL && app->destroy != NULL;
}

pxsys_status_t pxsys_native_runtime_register_app(pxsys_native_runtime_t* runtime,
                                                 const pxsys_native_app_t* app) {
    pxsys_native_entry_t* entry;
    size_t slot;
    if (!native_runtime_valid(runtime) || !native_app_valid(runtime, app))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (find_app(runtime, &app->identity) != NULL)
        return PXSYS_STATUS_ALREADY_EXISTS;
    if (runtime->count == runtime->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (slot = 0; slot < runtime->capacity; ++slot) {
        if (!runtime->entries[slot].occupied)
            break;
    }
    if (slot == runtime->capacity)
        return PXSYS_STATUS_INTERNAL;
    entry = &runtime->entries[slot];
    entry->app_id = (char*)runtime->allocator.allocate(runtime->allocator.context,
                                                       app->identity.app_id.size + 1u);
    if (entry->app_id == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memcpy(entry->app_id, app->identity.app_id.data, app->identity.app_id.size);
    entry->app_id[app->identity.app_id.size] = '\0';
    memset(&entry->app, 0, sizeof(entry->app));
    memcpy(&entry->app, app,
           app->struct_size < sizeof(entry->app) ? app->struct_size
                                                 : sizeof(entry->app));
    entry->app.struct_size = sizeof(entry->app);
    entry->app.identity.app_id.data = entry->app_id;
    entry->app.identity.app_id.size = app->identity.app_id.size;
    entry->active_instances = 0;
    entry->occupied = 1;
    runtime->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_native_runtime_unregister_app(pxsys_native_runtime_t* runtime,
                                                   const pxsys_app_identity_t* identity) {
    pxsys_native_entry_t* entry;
    if (!native_runtime_valid(runtime) ||
        pxsys_app_identity_validate(identity, runtime->max_app_id_bytes) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_app(runtime, identity);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->active_instances != 0)
        return PXSYS_STATUS_BUSY;
    runtime->allocator.release(runtime->allocator.context, entry->app_id);
    memset(entry, 0, sizeof(*entry));
    runtime->count--;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t provider_instantiate(void* context, const pxsys_app_descriptor_t* app,
                                           uint64_t instance_id, void** runtime_instance) {
    pxsys_native_runtime_t* runtime = (pxsys_native_runtime_t*)context;
    pxsys_native_entry_t* entry;
    pxsys_native_instance_t* instance;
    pxsys_status_t status;
    if (!native_runtime_valid(runtime) || app == NULL || runtime_instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *runtime_instance = NULL;
    entry = find_app(runtime, &app->identity);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    instance = (pxsys_native_instance_t*)runtime->allocator.allocate(runtime->allocator.context,
                                                                     sizeof(*instance));
    if (instance == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(instance, 0, sizeof(*instance));
    instance->entry = entry;
    entry->active_instances++;
    runtime->active_instances++;
    *runtime_instance = instance;
    if (entry->app.create_display != NULL) {
        pxsys_display_profile_t profile;
        const pxsys_display_profile_t* display = NULL;
        if (entry->app.display != NULL &&
            pxsys_display_service_get(entry->app.display, &profile) ==
                PXSYS_STATUS_OK) {
            display = &profile;
        }
        return entry->app.create_display(entry->app.context, app, instance_id,
                                         display, &instance->app_instance);
    }
    status = entry->app.create(entry->app.context, app, instance_id, &instance->app_instance);
    return status;
}

static pxsys_status_t provider_start(void* context, void* runtime_instance,
                                     const pxsys_message_t* launch) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance == NULL)
        return PXSYS_STATUS_BAD_STATE;
    return instance->entry->app.start(instance->entry->app.context, instance->app_instance, launch);
}

static pxsys_status_t provider_foreground(void* context, void* runtime_instance) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance == NULL)
        return PXSYS_STATUS_BAD_STATE;
    return instance->entry->app.foreground(instance->entry->app.context, instance->app_instance);
}

static pxsys_status_t provider_background(void* context, void* runtime_instance) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance == NULL)
        return PXSYS_STATUS_BAD_STATE;
    return instance->entry->app.background(instance->entry->app.context, instance->app_instance);
}

static pxsys_status_t provider_event(void* context, void* runtime_instance,
                                     const pxsys_message_t* event) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance == NULL)
        return PXSYS_STATUS_BAD_STATE;
    return instance->entry->app.event(instance->entry->app.context, instance->app_instance, event);
}

static pxsys_back_result_t provider_back(void* context, void* runtime_instance) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance == NULL)
        return PXSYS_BACK_UNHANDLED;
    return instance->entry->app.back(instance->entry->app.context, instance->app_instance);
}

static void provider_stop(void* context, void* runtime_instance, pxsys_stop_reason_t reason) {
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    (void)context;
    if (instance != NULL) {
        instance->entry->app.stop(instance->entry->app.context, instance->app_instance, reason);
    }
}

static void provider_destroy(void* context, void* runtime_instance) {
    pxsys_native_runtime_t* runtime = (pxsys_native_runtime_t*)context;
    pxsys_native_instance_t* instance = (pxsys_native_instance_t*)runtime_instance;
    if (!native_runtime_valid(runtime) || instance == NULL)
        return;
    instance->entry->app.destroy(instance->entry->app.context, instance->app_instance);
    if (instance->entry->active_instances != 0)
        instance->entry->active_instances--;
    if (runtime->active_instances != 0)
        runtime->active_instances--;
    runtime->allocator.release(runtime->allocator.context, instance);
}

pxsys_status_t pxsys_native_runtime_provider(pxsys_native_runtime_t* runtime,
                                             pxsys_runtime_provider_t* provider) {
    if (!native_runtime_valid(runtime) || provider == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(provider, 0, sizeof(*provider));
    provider->struct_size = sizeof(*provider);
    provider->runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
    provider->version = (pxsys_version_t){0, 1};
    provider->context = runtime;
    provider->instantiate = provider_instantiate;
    provider->start = provider_start;
    provider->foreground = provider_foreground;
    provider->background = provider_background;
    provider->event = provider_event;
    provider->back = provider_back;
    provider->stop = provider_stop;
    provider->destroy = provider_destroy;
    return PXSYS_STATUS_OK;
}
