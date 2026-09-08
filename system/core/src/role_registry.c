#include "pxsys/role_registry.h"

#include <string.h>

#define PXSYS_ROLE_MAGIC UINT32_C(0x5058524f)

typedef struct {
    pxsys_role_candidate_t candidate;
    pxsys_app_ref_t app_ref;
    const pxsys_app_descriptor_t* app;
    void* strings;
    uint8_t occupied;
} role_entry_t;

struct pxsys_role_registry {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_role_id_bytes;
    pxsys_app_registry_t* apps;
    void* policy_context;
    pxsys_role_policy_fn authorize;
    pxsys_allocator_t allocator;
    role_entry_t* entries;
};

static int registry_valid(const pxsys_role_registry_t* registry) {
    return registry != NULL && registry->magic == PXSYS_ROLE_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size && left.data != NULL && right.data != NULL &&
           memcmp(left.data, right.data, left.size) == 0;
}

static int config_valid(const pxsys_role_registry_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_candidates != 0 &&
           config->max_candidates <= SIZE_MAX / sizeof(role_entry_t) &&
           config->max_role_id_bytes != 0 && config->max_role_id_bytes != SIZE_MAX &&
           config->apps != NULL && config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

void pxsys_role_registry_config_init(pxsys_role_registry_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_candidates = 32;
    config->max_role_id_bytes = 96;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_role_registry_create(const pxsys_role_registry_config_t* config,
                                          pxsys_role_registry_t** output) {
    pxsys_role_registry_t* registry;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    registry = (pxsys_role_registry_t*)config->allocator.allocate(config->allocator.context,
                                                                  sizeof(*registry));
    if (registry == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(registry, 0, sizeof(*registry));
    registry->entries = (role_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_candidates * sizeof(*registry->entries));
    if (registry->entries == NULL) {
        config->allocator.release(config->allocator.context, registry);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(registry->entries, 0, config->max_candidates * sizeof(*registry->entries));
    registry->capacity = config->max_candidates;
    registry->max_role_id_bytes = config->max_role_id_bytes;
    registry->apps = config->apps;
    registry->policy_context = config->policy_context;
    registry->authorize = config->authorize;
    registry->allocator = config->allocator;
    registry->magic = PXSYS_ROLE_MAGIC;
    *output = registry;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_registry_destroy(pxsys_role_registry_t* registry) {
    pxsys_allocator_t allocator;
    size_t index;
    if (!registry_valid(registry))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    allocator = registry->allocator;
    for (index = 0; index < registry->capacity; ++index) {
        role_entry_t* entry = &registry->entries[index];
        if (!entry->occupied)
            continue;
        (void)pxsys_app_registry_release(registry->apps, entry->app_ref);
        allocator.release(allocator.context, entry->strings);
    }
    registry->magic = 0;
    allocator.release(allocator.context, registry->entries);
    allocator.release(allocator.context, registry);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_candidate_register(pxsys_role_registry_t* registry,
                                             const pxsys_role_candidate_t* candidate) {
    const pxsys_app_descriptor_t* app;
    pxsys_app_ref_t app_ref;
    pxsys_status_t status;
    role_entry_t* entry;
    char* cursor;
    size_t index;
    size_t bytes;
    if (!registry_valid(registry) || candidate == NULL ||
        candidate->struct_size < sizeof(*candidate) ||
        !pxsys_identifier_validate(candidate->role_id, registry->max_role_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (registry->authorize != NULL) {
        status = registry->authorize(registry->policy_context, candidate);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    for (index = 0; index < registry->capacity; ++index) {
        entry = &registry->entries[index];
        if (entry->occupied && string_equal(entry->candidate.role_id, candidate->role_id) &&
            pxsys_app_identity_equal(&entry->candidate.app, &candidate->app)) {
            return PXSYS_STATUS_ALREADY_EXISTS;
        }
    }
    if (registry->count == registry->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    status = pxsys_app_registry_acquire(registry->apps, &candidate->app, &app_ref, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    if ((app->flags & PXSYS_APP_FLAG_ENABLED) == 0) {
        (void)pxsys_app_registry_release(registry->apps, app_ref);
        return PXSYS_STATUS_DENIED;
    }
    bytes = candidate->role_id.size + candidate->app.app_id.size + 2u;
    for (index = 0; index < registry->capacity; ++index) {
        if (!registry->entries[index].occupied)
            break;
    }
    entry = &registry->entries[index];
    entry->strings = registry->allocator.allocate(registry->allocator.context, bytes);
    if (entry->strings == NULL) {
        (void)pxsys_app_registry_release(registry->apps, app_ref);
        return PXSYS_STATUS_NO_MEMORY;
    }
    entry->candidate = *candidate;
    entry->candidate.struct_size = sizeof(entry->candidate);
    memcpy(entry->candidate.app.publisher_root, candidate->app.publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    cursor = (char*)entry->strings;
    memcpy(cursor, candidate->role_id.data, candidate->role_id.size);
    cursor[candidate->role_id.size] = '\0';
    entry->candidate.role_id.data = cursor;
    cursor += candidate->role_id.size + 1u;
    memcpy(cursor, candidate->app.app_id.data, candidate->app.app_id.size);
    cursor[candidate->app.app_id.size] = '\0';
    entry->candidate.app.app_id.data = cursor;
    entry->app_ref = app_ref;
    entry->app = app;
    entry->occupied = 1;
    registry->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_candidates_unregister(pxsys_role_registry_t* registry,
                                                const pxsys_app_identity_t* app) {
    size_t index;
    size_t removed = 0;
    if (!registry_valid(registry) || app == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < registry->capacity; ++index) {
        role_entry_t* entry = &registry->entries[index];
        if (!entry->occupied || !pxsys_app_identity_equal(&entry->candidate.app, app))
            continue;
        (void)pxsys_app_registry_release(registry->apps, entry->app_ref);
        registry->allocator.release(registry->allocator.context, entry->strings);
        memset(entry, 0, sizeof(*entry));
        registry->count--;
        removed++;
    }
    return removed == 0 ? PXSYS_STATUS_NOT_FOUND : PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_resolve(const pxsys_role_registry_t* registry, pxsys_string_t role_id,
                                  const pxsys_app_descriptor_t** app) {
    const role_entry_t* best = NULL;
    size_t index;
    if (app == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *app = NULL;
    if (!registry_valid(registry) ||
        !pxsys_identifier_validate(role_id, registry->max_role_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < registry->capacity; ++index) {
        const role_entry_t* candidate = &registry->entries[index];
        if (!candidate->occupied || !string_equal(candidate->candidate.role_id, role_id))
            continue;
        if (best == NULL || candidate->candidate.priority > best->candidate.priority)
            best = candidate;
    }
    if (best == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    *app = best->app;
    return PXSYS_STATUS_OK;
}
