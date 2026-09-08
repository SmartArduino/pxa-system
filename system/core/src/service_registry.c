#include "pxsys/service_registry.h"

#include <string.h>

#include "pxsys/app_registry.h"

#define PXSYS_SERVICE_MAGIC UINT32_C(0x50585356)

typedef struct {
    pxsys_service_provider_t provider;
    char* interface_id;
    uint32_t active_calls;
    uint8_t occupied;
} service_entry_t;

struct pxsys_service_registry {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_interface_id_bytes;
    size_t max_app_id_bytes;
    size_t max_component_id_bytes;
    void* policy_context;
    pxsys_service_policy_fn authorize;
    pxsys_allocator_t allocator;
    service_entry_t* entries;
};

typedef struct {
    pxsys_service_registry_t* registry;
    service_entry_t* entry;
    void* user_context;
    pxsys_service_complete_fn user_complete;
    uint8_t completed;
    uint8_t invoke_returned;
} completion_gate_t;

static int registry_valid(const pxsys_service_registry_t* registry) {
    return registry != NULL && registry->magic == PXSYS_SERVICE_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size && left.data != NULL && right.data != NULL &&
           memcmp(left.data, right.data, left.size) == 0;
}

void pxsys_service_registry_config_init(pxsys_service_registry_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_providers = 32;
    config->max_interface_id_bytes = 128;
    config->max_app_id_bytes = 64;
    config->max_component_id_bytes = 64;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_service_registry_create(const pxsys_service_registry_config_t* config,
                                             pxsys_service_registry_t** output) {
    pxsys_service_registry_t* registry;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) || config->max_providers == 0 ||
        config->max_providers > SIZE_MAX / sizeof(service_entry_t) ||
        config->max_interface_id_bytes == 0 || config->max_interface_id_bytes == SIZE_MAX ||
        config->max_app_id_bytes == 0 || config->max_app_id_bytes == SIZE_MAX ||
        config->max_component_id_bytes == 0 || config->max_component_id_bytes == SIZE_MAX ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    registry = (pxsys_service_registry_t*)config->allocator.allocate(config->allocator.context,
                                                                     sizeof(*registry));
    if (registry == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(registry, 0, sizeof(*registry));
    registry->entries = (service_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_providers * sizeof(*registry->entries));
    if (registry->entries == NULL) {
        config->allocator.release(config->allocator.context, registry);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(registry->entries, 0, config->max_providers * sizeof(*registry->entries));
    registry->capacity = config->max_providers;
    registry->max_interface_id_bytes = config->max_interface_id_bytes;
    registry->max_app_id_bytes = config->max_app_id_bytes;
    registry->max_component_id_bytes = config->max_component_id_bytes;
    registry->policy_context = config->policy_context;
    registry->authorize = config->authorize;
    registry->allocator = config->allocator;
    registry->magic = PXSYS_SERVICE_MAGIC;
    *output = registry;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_service_registry_destroy(pxsys_service_registry_t* registry) {
    pxsys_allocator_t allocator;
    size_t index;
    if (!registry_valid(registry))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < registry->capacity; ++index) {
        if (registry->entries[index].occupied && registry->entries[index].active_calls != 0)
            return PXSYS_STATUS_BUSY;
    }
    allocator = registry->allocator;
    for (index = 0; index < registry->capacity; ++index) {
        if (registry->entries[index].occupied)
            allocator.release(allocator.context, registry->entries[index].interface_id);
    }
    registry->magic = 0;
    allocator.release(allocator.context, registry->entries);
    allocator.release(allocator.context, registry);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_service_register(pxsys_service_registry_t* registry,
                                      const pxsys_service_provider_t* provider) {
    service_entry_t* entry;
    size_t index;
    if (!registry_valid(registry) || provider == NULL ||
        provider->struct_size < sizeof(*provider) ||
        !pxsys_identifier_validate(provider->interface_id, registry->max_interface_id_bytes) ||
        provider->invoke == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < registry->capacity; ++index) {
        entry = &registry->entries[index];
        if (entry->occupied && string_equal(entry->provider.interface_id, provider->interface_id) &&
            entry->provider.version.major == provider->version.major) {
            return PXSYS_STATUS_ALREADY_EXISTS;
        }
    }
    if (registry->count == registry->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < registry->capacity; ++index) {
        if (!registry->entries[index].occupied)
            break;
    }
    entry = &registry->entries[index];
    entry->interface_id = (char*)registry->allocator.allocate(registry->allocator.context,
                                                              provider->interface_id.size + 1u);
    if (entry->interface_id == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memcpy(entry->interface_id, provider->interface_id.data, provider->interface_id.size);
    entry->interface_id[provider->interface_id.size] = '\0';
    entry->provider = *provider;
    entry->provider.struct_size = sizeof(entry->provider);
    entry->provider.interface_id.data = entry->interface_id;
    entry->occupied = 1;
    registry->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_service_unregister(pxsys_service_registry_t* registry,
                                        pxsys_string_t interface_id, uint16_t major_version) {
    size_t index;
    if (!registry_valid(registry) ||
        !pxsys_identifier_validate(interface_id, registry->max_interface_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < registry->capacity; ++index) {
        service_entry_t* entry = &registry->entries[index];
        if (!entry->occupied || !string_equal(entry->provider.interface_id, interface_id) ||
            entry->provider.version.major != major_version) {
            continue;
        }
        if (entry->active_calls != 0)
            return PXSYS_STATUS_BUSY;
        registry->allocator.release(registry->allocator.context, entry->interface_id);
        memset(entry, 0, sizeof(*entry));
        registry->count--;
        return PXSYS_STATUS_OK;
    }
    return PXSYS_STATUS_NOT_FOUND;
}

static void complete_once(void* context, uint64_t request_id, pxsys_status_t status,
                          pxsys_bytes_t payload) {
    completion_gate_t* gate = (completion_gate_t*)context;
    if (gate->completed)
        return;
    gate->completed = 1;
    gate->user_complete(gate->user_context, request_id, status, payload);
    if (gate->entry->active_calls != 0)
        gate->entry->active_calls--;
    if (gate->invoke_returned)
        gate->registry->allocator.release(gate->registry->allocator.context, gate);
}

pxsys_status_t pxsys_service_invoke(pxsys_service_registry_t* registry,
                                    const pxsys_service_request_t* request,
                                    void* completion_context, pxsys_service_complete_fn complete) {
    completion_gate_t* gate;
    service_entry_t* selected = NULL;
    pxsys_status_t status;
    size_t index;
    if (!registry_valid(registry) || request == NULL || request->struct_size < sizeof(*request) ||
        !pxsys_identifier_validate(request->interface_id, registry->max_interface_id_bytes) ||
        request->caller == NULL || request->caller->struct_size < sizeof(*request->caller) ||
        pxsys_app_identity_validate(&request->caller->app, registry->max_app_id_bytes) !=
            PXSYS_STATUS_OK ||
        !pxsys_identifier_validate(request->caller->component_id,
                                   registry->max_component_id_bytes) ||
        complete == NULL || (request->payload.size != 0 && request->payload.data == NULL)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (registry->authorize != NULL) {
        status = registry->authorize(registry->policy_context, request);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    for (index = 0; index < registry->capacity; ++index) {
        service_entry_t* candidate = &registry->entries[index];
        if (candidate->occupied &&
            string_equal(candidate->provider.interface_id, request->interface_id) &&
            candidate->provider.version.major == request->version.major &&
            candidate->provider.version.minor >= request->version.minor) {
            selected = candidate;
            break;
        }
    }
    if (selected == NULL)
        return PXSYS_STATUS_UNSUPPORTED;
    gate = (completion_gate_t*)registry->allocator.allocate(registry->allocator.context,
                                                            sizeof(*gate));
    if (gate == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(gate, 0, sizeof(*gate));
    gate->registry = registry;
    gate->entry = selected;
    gate->user_context = completion_context;
    gate->user_complete = complete;
    selected->active_calls++;
    status = selected->provider.invoke(selected->provider.context, request, gate, complete_once);
    gate->invoke_returned = 1;
    if (status != PXSYS_STATUS_OK && !gate->completed)
        selected->active_calls--;
    if (gate->completed || status != PXSYS_STATUS_OK)
        registry->allocator.release(registry->allocator.context, gate);
    return status;
}

size_t pxsys_service_count(const pxsys_service_registry_t* registry) {
    return registry_valid(registry) ? registry->count : 0;
}

static void project_service_info(const service_entry_t* entry, pxsys_service_info_t* output) {
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->interface_id = entry->provider.interface_id;
    output->version = entry->provider.version;
    output->features = entry->provider.features;
}

pxsys_status_t pxsys_service_at(const pxsys_service_registry_t* registry, size_t index,
                                pxsys_service_info_t* output) {
    size_t slot;
    size_t current = 0;
    if (!registry_valid(registry) || output == NULL || index >= registry->count)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (slot = 0; slot < registry->capacity; ++slot) {
        if (!registry->entries[slot].occupied)
            continue;
        if (current++ == index) {
            project_service_info(&registry->entries[slot], output);
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_INTERNAL;
}

pxsys_status_t pxsys_service_resolve(const pxsys_service_registry_t* registry,
                                     pxsys_string_t interface_id, pxsys_version_t minimum_version,
                                     pxsys_service_info_t* output) {
    const service_entry_t* selected = NULL;
    size_t index;
    if (!registry_valid(registry) || output == NULL ||
        !pxsys_identifier_validate(interface_id, registry->max_interface_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < registry->capacity; ++index) {
        const service_entry_t* candidate = &registry->entries[index];
        if (!candidate->occupied || !string_equal(candidate->provider.interface_id, interface_id) ||
            candidate->provider.version.major != minimum_version.major ||
            candidate->provider.version.minor < minimum_version.minor) {
            continue;
        }
        if (selected == NULL ||
            candidate->provider.version.minor < selected->provider.version.minor)
            selected = candidate;
    }
    if (selected == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    project_service_info(selected, output);
    return PXSYS_STATUS_OK;
}

size_t pxsys_service_active_call_count(const pxsys_service_registry_t* registry) {
    size_t count = 0;
    size_t index;
    if (!registry_valid(registry))
        return 0;
    for (index = 0; index < registry->capacity; ++index) {
        if (registry->entries[index].occupied)
            count += registry->entries[index].active_calls;
    }
    return count;
}
