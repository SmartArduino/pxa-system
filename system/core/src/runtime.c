#include "pxsys/runtime.h"

#include <stddef.h>
#include <string.h>

#define PXSYS_RUNTIME_MAGIC UINT32_C(0x50585359)

typedef struct {
    pxsys_runtime_provider_t provider;
    char* runtime_id;
    uint32_t active_instances;
    uint8_t occupied;
} pxsys_provider_entry_t;

typedef struct {
    pxsys_app_ref_t app_ref;
    const pxsys_app_descriptor_t* app;
    pxsys_app_lifecycle_t lifecycle;
    void* runtime_instance;
    uint64_t instance_id;
    uint32_t provider_slot;
    uint32_t generation;
    uint8_t occupied;
    uint8_t foreground_requested;
} pxsys_instance_entry_t;

struct pxsys_runtime {
    uint32_t magic;
    size_t provider_capacity;
    size_t provider_count;
    size_t instance_capacity;
    size_t instance_count;
    size_t max_runtime_id_bytes;
    uint64_t next_instance_id;
    pxsys_app_registry_t* apps;
    pxsys_allocator_t allocator;
    pxsys_provider_entry_t* providers;
    pxsys_instance_entry_t* instances;
};

static int runtime_valid(const pxsys_runtime_t* runtime) {
    return runtime != NULL && runtime->magic == PXSYS_RUNTIME_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size && left.data != NULL && right.data != NULL &&
           memcmp(left.data, right.data, left.size) == 0;
}

static pxsys_provider_entry_t* find_provider(const pxsys_runtime_t* runtime,
                                             pxsys_string_t runtime_id, size_t* slot) {
    size_t index;
    for (index = 0; index < runtime->provider_capacity; ++index) {
        pxsys_provider_entry_t* entry = &runtime->providers[index];
        if (entry->occupied && string_equal(entry->provider.runtime_id, runtime_id)) {
            if (slot != NULL)
                *slot = index;
            return entry;
        }
    }
    return NULL;
}

static int config_valid(const pxsys_runtime_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_providers != 0 &&
           config->max_instances != 0 && config->max_runtime_id_bytes != 0 &&
           config->max_runtime_id_bytes != SIZE_MAX && config->apps != NULL &&
           config->max_providers <= UINT32_MAX && config->max_instances <= UINT32_MAX &&
           config->max_providers <= SIZE_MAX / sizeof(pxsys_provider_entry_t) &&
           config->max_instances <= SIZE_MAX / sizeof(pxsys_instance_entry_t) &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

void pxsys_runtime_config_init(pxsys_runtime_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_providers = 4;
    config->max_instances = 16;
    config->max_runtime_id_bytes = 64;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_runtime_create(const pxsys_runtime_config_t* config,
                                    pxsys_runtime_t** output) {
    pxsys_runtime_t* runtime;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    runtime =
        (pxsys_runtime_t*)config->allocator.allocate(config->allocator.context, sizeof(*runtime));
    if (runtime == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->providers = (pxsys_provider_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_providers * sizeof(*runtime->providers));
    if (runtime->providers == NULL) {
        config->allocator.release(config->allocator.context, runtime);
        return PXSYS_STATUS_NO_MEMORY;
    }
    runtime->instances = (pxsys_instance_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_instances * sizeof(*runtime->instances));
    if (runtime->instances == NULL) {
        config->allocator.release(config->allocator.context, runtime->providers);
        config->allocator.release(config->allocator.context, runtime);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(runtime->providers, 0, config->max_providers * sizeof(*runtime->providers));
    memset(runtime->instances, 0, config->max_instances * sizeof(*runtime->instances));
    runtime->provider_capacity = config->max_providers;
    runtime->instance_capacity = config->max_instances;
    runtime->max_runtime_id_bytes = config->max_runtime_id_bytes;
    runtime->next_instance_id = 1;
    runtime->apps = config->apps;
    runtime->allocator = config->allocator;
    runtime->magic = PXSYS_RUNTIME_MAGIC;
    *output = runtime;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_destroy(pxsys_runtime_t* runtime) {
    size_t index;
    pxsys_allocator_t allocator;
    if (!runtime_valid(runtime))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (runtime->instance_count != 0)
        return PXSYS_STATUS_BUSY;
    allocator = runtime->allocator;
    for (index = 0; index < runtime->provider_capacity; ++index) {
        if (runtime->providers[index].occupied)
            allocator.release(allocator.context, runtime->providers[index].runtime_id);
    }
    runtime->magic = 0;
    allocator.release(allocator.context, runtime->instances);
    allocator.release(allocator.context, runtime->providers);
    allocator.release(allocator.context, runtime);
    return PXSYS_STATUS_OK;
}

static int provider_valid(const pxsys_runtime_t* runtime,
                          const pxsys_runtime_provider_t* provider) {
    return provider != NULL && provider->struct_size >= offsetof(pxsys_runtime_provider_t, bound) &&
           pxsys_identifier_validate(provider->runtime_id, runtime->max_runtime_id_bytes) &&
           provider->instantiate != NULL && provider->start != NULL &&
           provider->foreground != NULL && provider->background != NULL &&
           provider->event != NULL && provider->back != NULL && provider->stop != NULL &&
           provider->destroy != NULL;
}

pxsys_status_t pxsys_runtime_register_provider(pxsys_runtime_t* runtime,
                                               const pxsys_runtime_provider_t* provider) {
    pxsys_provider_entry_t* entry;
    size_t slot;
    if (!runtime_valid(runtime) || !provider_valid(runtime, provider))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (find_provider(runtime, provider->runtime_id, NULL) != NULL)
        return PXSYS_STATUS_ALREADY_EXISTS;
    if (runtime->provider_count == runtime->provider_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (slot = 0; slot < runtime->provider_capacity; ++slot) {
        if (!runtime->providers[slot].occupied)
            break;
    }
    if (slot == runtime->provider_capacity)
        return PXSYS_STATUS_INTERNAL;
    entry = &runtime->providers[slot];
    entry->runtime_id = (char*)runtime->allocator.allocate(runtime->allocator.context,
                                                           provider->runtime_id.size + 1u);
    if (entry->runtime_id == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memcpy(entry->runtime_id, provider->runtime_id.data, provider->runtime_id.size);
    entry->runtime_id[provider->runtime_id.size] = '\0';
    memset(&entry->provider, 0, sizeof(entry->provider));
    memcpy(&entry->provider, provider,
           provider->struct_size < sizeof(entry->provider) ? provider->struct_size
                                                           : sizeof(entry->provider));
    entry->provider.struct_size = sizeof(entry->provider);
    entry->provider.runtime_id.data = entry->runtime_id;
    entry->provider.runtime_id.size = provider->runtime_id.size;
    entry->active_instances = 0;
    entry->occupied = 1;
    runtime->provider_count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_unregister_provider(pxsys_runtime_t* runtime,
                                                 pxsys_string_t runtime_id) {
    pxsys_provider_entry_t* entry;
    if (!runtime_valid(runtime) ||
        !pxsys_identifier_validate(runtime_id, runtime->max_runtime_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_provider(runtime, runtime_id, NULL);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->active_instances != 0)
        return PXSYS_STATUS_BUSY;
    runtime->allocator.release(runtime->allocator.context, entry->runtime_id);
    memset(entry, 0, sizeof(*entry));
    runtime->provider_count--;
    return PXSYS_STATUS_OK;
}

pxsys_instance_ref_t pxsys_instance_ref_invalid(void) {
    pxsys_instance_ref_t reference;
    reference.slot = PXSYS_INSTANCE_REF_INVALID_SLOT;
    reference.generation = 0;
    return reference;
}

static pxsys_instance_entry_t* resolve_instance(const pxsys_runtime_t* runtime,
                                                pxsys_instance_ref_t reference) {
    pxsys_instance_entry_t* entry;
    if (!runtime_valid(runtime) || reference.slot == PXSYS_INSTANCE_REF_INVALID_SLOT ||
        (size_t)reference.slot >= runtime->instance_capacity) {
        return NULL;
    }
    entry = &runtime->instances[reference.slot];
    return entry->occupied && entry->generation == reference.generation ? entry : NULL;
}

static void release_instance(pxsys_runtime_t* runtime, pxsys_instance_entry_t* instance) {
    pxsys_provider_entry_t* provider = &runtime->providers[instance->provider_slot];
    (void)pxsys_app_registry_release(runtime->apps, instance->app_ref);
    if (provider->active_instances != 0)
        provider->active_instances--;
    instance->occupied = 0;
    instance->app = NULL;
    instance->runtime_instance = NULL;
    instance->instance_id = 0;
    instance->provider_slot = 0;
    instance->foreground_requested = 0;
    instance->app_ref = pxsys_app_ref_invalid();
    if (runtime->instance_count != 0)
        runtime->instance_count--;
}

static void stop_and_destroy(pxsys_runtime_t* runtime, pxsys_instance_entry_t* instance,
                             pxsys_stop_reason_t reason) {
    pxsys_provider_entry_t* provider = &runtime->providers[instance->provider_slot];
    if (instance->lifecycle.state == PXSYS_APP_BACKGROUND ||
        instance->lifecycle.state == PXSYS_APP_FOREGROUND) {
        (void)pxsys_app_lifecycle_request_stop(&instance->lifecycle, reason);
    }
    if (instance->lifecycle.state == PXSYS_APP_STOP_REQUESTED) {
        (void)pxsys_app_lifecycle_begin_stop(&instance->lifecycle);
        provider->provider.stop(provider->provider.context, instance->runtime_instance,
                                instance->lifecycle.stop_reason);
        (void)pxsys_app_lifecycle_finish_stop(&instance->lifecycle);
    }
    if (instance->lifecycle.state == PXSYS_APP_READY) {
        (void)pxsys_app_lifecycle_request_stop(&instance->lifecycle, reason);
    }
    if (instance->lifecycle.state == PXSYS_APP_DESTROY_PENDING) {
        provider->provider.destroy(provider->provider.context, instance->runtime_instance);
        (void)pxsys_app_lifecycle_finish_destroy(&instance->lifecycle);
    }
    release_instance(runtime, instance);
}

pxsys_status_t pxsys_runtime_launch(pxsys_runtime_t* runtime, const pxsys_app_identity_t* identity,
                                    const pxsys_message_t* launch, int foreground,
                                    pxsys_instance_ref_t* output) {
    pxsys_app_ref_t app_ref;
    const pxsys_app_descriptor_t* app;
    pxsys_provider_entry_t* provider;
    pxsys_instance_entry_t* instance;
    pxsys_status_t status;
    size_t provider_slot;
    size_t instance_slot;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = pxsys_instance_ref_invalid();
    if (!runtime_valid(runtime) || (launch != NULL && launch->struct_size < sizeof(*launch)))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    status = pxsys_app_registry_acquire(runtime->apps, identity, &app_ref, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    if ((app->flags & PXSYS_APP_FLAG_ENABLED) == 0) {
        (void)pxsys_app_registry_release(runtime->apps, app_ref);
        return PXSYS_STATUS_DENIED;
    }
    provider = find_provider(runtime, app->runtime_id, &provider_slot);
    if (provider == NULL) {
        (void)pxsys_app_registry_release(runtime->apps, app_ref);
        return PXSYS_STATUS_UNSUPPORTED;
    }
    if (runtime->instance_count == runtime->instance_capacity) {
        (void)pxsys_app_registry_release(runtime->apps, app_ref);
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    for (instance_slot = 0; instance_slot < runtime->instance_capacity; ++instance_slot) {
        if (!runtime->instances[instance_slot].occupied)
            break;
    }
    if (instance_slot == runtime->instance_capacity) {
        (void)pxsys_app_registry_release(runtime->apps, app_ref);
        return PXSYS_STATUS_INTERNAL;
    }
    instance = &runtime->instances[instance_slot];
    instance->generation = instance->generation == UINT32_MAX ? 1 : instance->generation + 1u;
    instance->occupied = 1;
    instance->app_ref = app_ref;
    instance->app = app;
    instance->provider_slot = (uint32_t)provider_slot;
    instance->foreground_requested = foreground != 0;
    instance->instance_id = runtime->next_instance_id++;
    if (runtime->next_instance_id == 0)
        runtime->next_instance_id = 1;
    instance->runtime_instance = NULL;
    pxsys_app_lifecycle_init(&instance->lifecycle);
    runtime->instance_count++;
    provider->active_instances++;

    (void)pxsys_app_lifecycle_begin_create(&instance->lifecycle);
    status = provider->provider.instantiate(provider->provider.context, app, instance->instance_id,
                                            &instance->runtime_instance);
    (void)pxsys_app_lifecycle_finish_create(&instance->lifecycle, status);
    if (status != PXSYS_STATUS_OK) {
        provider->provider.destroy(provider->provider.context, instance->runtime_instance);
        (void)pxsys_app_lifecycle_finish_destroy(&instance->lifecycle);
        release_instance(runtime, instance);
        return status;
    }
    if (provider->provider.bound != NULL) {
        pxsys_instance_ref_t reference;
        reference.slot = (uint32_t)instance_slot;
        reference.generation = instance->generation;
        provider->provider.bound(provider->provider.context, instance->runtime_instance, reference);
    }

    (void)pxsys_app_lifecycle_begin_start(&instance->lifecycle);
    status =
        provider->provider.start(provider->provider.context, instance->runtime_instance, launch);
    if (status == PXSYS_STATUS_PENDING) {
        output->slot = (uint32_t)instance_slot;
        output->generation = instance->generation;
        return status;
    }
    (void)pxsys_app_lifecycle_finish_start(&instance->lifecycle, status);
    if (status != PXSYS_STATUS_OK) {
        stop_and_destroy(runtime, instance, PXSYS_STOP_FAULT);
        return status;
    }
    if (foreground) {
        status =
            provider->provider.foreground(provider->provider.context, instance->runtime_instance);
        if (status != PXSYS_STATUS_OK) {
            stop_and_destroy(runtime, instance, PXSYS_STOP_FAULT);
            return status;
        }
        (void)pxsys_app_lifecycle_foreground(&instance->lifecycle);
    }
    output->slot = (uint32_t)instance_slot;
    output->generation = instance->generation;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_complete_start(pxsys_runtime_t* runtime,
                                            pxsys_instance_ref_t reference, pxsys_status_t result) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    pxsys_status_t status;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (result == PXSYS_STATUS_PENDING || !pxsys_status_is_known(result))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (instance->lifecycle.state != PXSYS_APP_STARTING)
        return PXSYS_STATUS_BAD_STATE;
    (void)pxsys_app_lifecycle_finish_start(&instance->lifecycle, result);
    if (result != PXSYS_STATUS_OK) {
        stop_and_destroy(runtime, instance, PXSYS_STOP_FAULT);
        return result;
    }
    if (!instance->foreground_requested)
        return PXSYS_STATUS_OK;
    provider = &runtime->providers[instance->provider_slot];
    status = provider->provider.foreground(provider->provider.context, instance->runtime_instance);
    if (status != PXSYS_STATUS_OK) {
        stop_and_destroy(runtime, instance, PXSYS_STOP_FAULT);
        return status;
    }
    return pxsys_app_lifecycle_foreground(&instance->lifecycle);
}

pxsys_status_t pxsys_runtime_report_stopped(pxsys_runtime_t* runtime,
                                            pxsys_instance_ref_t reference,
                                            pxsys_stop_reason_t reason) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    pxsys_status_t status;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (instance->lifecycle.state != PXSYS_APP_STOPPING) {
        status = pxsys_app_lifecycle_request_stop(&instance->lifecycle, reason);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    if (instance->lifecycle.state == PXSYS_APP_STOP_REQUESTED) {
        (void)pxsys_app_lifecycle_begin_stop(&instance->lifecycle);
    }
    if (instance->lifecycle.state == PXSYS_APP_STOPPING) {
        (void)pxsys_app_lifecycle_finish_stop(&instance->lifecycle);
    }
    if (instance->lifecycle.state != PXSYS_APP_DESTROY_PENDING)
        return PXSYS_STATUS_BAD_STATE;
    provider = &runtime->providers[instance->provider_slot];
    provider->provider.destroy(provider->provider.context, instance->runtime_instance);
    (void)pxsys_app_lifecycle_finish_destroy(&instance->lifecycle);
    release_instance(runtime, instance);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_foreground(pxsys_runtime_t* runtime, pxsys_instance_ref_t reference) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    pxsys_status_t status;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (instance->lifecycle.state == PXSYS_APP_FOREGROUND)
        return PXSYS_STATUS_OK;
    if (instance->lifecycle.state != PXSYS_APP_BACKGROUND)
        return PXSYS_STATUS_BAD_STATE;
    provider = &runtime->providers[instance->provider_slot];
    status = provider->provider.foreground(provider->provider.context, instance->runtime_instance);
    return status == PXSYS_STATUS_OK ? pxsys_app_lifecycle_foreground(&instance->lifecycle)
                                     : status;
}

pxsys_status_t pxsys_runtime_background(pxsys_runtime_t* runtime, pxsys_instance_ref_t reference) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    pxsys_status_t status;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (instance->lifecycle.state == PXSYS_APP_BACKGROUND)
        return PXSYS_STATUS_OK;
    if (instance->lifecycle.state != PXSYS_APP_FOREGROUND)
        return PXSYS_STATUS_BAD_STATE;
    provider = &runtime->providers[instance->provider_slot];
    status = provider->provider.background(provider->provider.context, instance->runtime_instance);
    return status == PXSYS_STATUS_OK ? pxsys_app_lifecycle_background(&instance->lifecycle)
                                     : status;
}

pxsys_status_t pxsys_runtime_deliver(pxsys_runtime_t* runtime, pxsys_instance_ref_t reference,
                                     const pxsys_message_t* event) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (event == NULL || event->struct_size < sizeof(*event))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (instance->lifecycle.state != PXSYS_APP_BACKGROUND &&
        instance->lifecycle.state != PXSYS_APP_FOREGROUND) {
        return PXSYS_STATUS_BAD_STATE;
    }
    provider = &runtime->providers[instance->provider_slot];
    return provider->provider.event(provider->provider.context, instance->runtime_instance, event);
}

pxsys_status_t pxsys_runtime_back(pxsys_runtime_t* runtime, pxsys_instance_ref_t reference,
                                  pxsys_back_result_t* result) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    if (result == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *result = PXSYS_BACK_UNHANDLED;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (instance->lifecycle.state != PXSYS_APP_FOREGROUND)
        return PXSYS_STATUS_BAD_STATE;
    provider = &runtime->providers[instance->provider_slot];
    *result = provider->provider.back(provider->provider.context, instance->runtime_instance);
    if (*result != PXSYS_BACK_UNHANDLED && *result != PXSYS_BACK_HANDLED) {
        *result = PXSYS_BACK_UNHANDLED;
        return PXSYS_STATUS_INTERNAL;
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_stop(pxsys_runtime_t* runtime, pxsys_instance_ref_t reference,
                                  pxsys_stop_reason_t reason) {
    pxsys_instance_entry_t* instance = resolve_instance(runtime, reference);
    pxsys_provider_entry_t* provider;
    pxsys_status_t status;
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_app_lifecycle_request_stop(&instance->lifecycle, reason);
    if (status != PXSYS_STATUS_OK)
        return status;
    provider = &runtime->providers[instance->provider_slot];
    if (provider->provider.request_stop != NULL) {
        status = provider->provider.request_stop(provider->provider.context,
                                                 instance->runtime_instance, reason);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return status;
        (void)pxsys_app_lifecycle_begin_stop(&instance->lifecycle);
        if (status == PXSYS_STATUS_PENDING)
            return status;
        (void)pxsys_app_lifecycle_finish_stop(&instance->lifecycle);
        provider->provider.destroy(provider->provider.context, instance->runtime_instance);
        (void)pxsys_app_lifecycle_finish_destroy(&instance->lifecycle);
        release_instance(runtime, instance);
        return PXSYS_STATUS_OK;
    }
    stop_and_destroy(runtime, instance, reason);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_runtime_snapshot(const pxsys_runtime_t* runtime,
                                      pxsys_instance_ref_t reference,
                                      pxsys_instance_snapshot_t* output) {
    pxsys_instance_entry_t* instance;
    if (output == NULL || output->struct_size < sizeof(*output))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    instance = resolve_instance(runtime, reference);
    if (instance == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    output->instance_id = instance->instance_id;
    output->lifecycle = instance->lifecycle;
    output->app = instance->app;
    return PXSYS_STATUS_OK;
}

size_t pxsys_runtime_instance_count(const pxsys_runtime_t* runtime) {
    return runtime_valid(runtime) ? runtime->instance_count : 0;
}
