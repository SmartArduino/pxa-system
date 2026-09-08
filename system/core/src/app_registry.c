#include "pxsys/app_registry.h"

#include <limits.h>
#include <string.h>

#define PXSYS_REGISTRY_MAGIC UINT32_C(0x50585352)

typedef struct {
    pxsys_app_descriptor_t descriptor;
    void* strings;
    uint32_t generation;
    uint32_t references;
    uint8_t occupied;
} pxsys_app_entry_t;

struct pxsys_app_registry {
    uint32_t magic;
    size_t count;
    size_t capacity;
    uint64_t generation;
    size_t max_app_id_bytes;
    size_t max_display_name_bytes;
    size_t max_version_bytes;
    size_t max_runtime_id_bytes;
    pxsys_allocator_t allocator;
    pxsys_app_entry_t* entries;
};

static int registry_valid(const pxsys_app_registry_t* registry) {
    return registry != NULL && registry->magic == PXSYS_REGISTRY_MAGIC;
}

static pxsys_app_entry_t* find_entry(const pxsys_app_registry_t* registry,
                                     const pxsys_app_identity_t* identity, size_t* slot) {
    size_t index;
    for (index = 0; index < registry->capacity; ++index) {
        pxsys_app_entry_t* entry = &registry->entries[index];
        if (entry->occupied && pxsys_app_identity_equal(&entry->descriptor.identity, identity)) {
            if (slot != NULL)
                *slot = index;
            return entry;
        }
    }
    return NULL;
}

static int ascii_metadata_valid(pxsys_string_t value, size_t maximum) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > maximum)
        return 0;
    for (index = 0; index < value.size; ++index) {
        const unsigned char byte = (unsigned char)value.data[index];
        if (byte < 0x21u || byte > 0x7eu)
            return 0;
    }
    return 1;
}

static int add_size(size_t* total, size_t value) {
    if (*total > SIZE_MAX - value)
        return 0;
    *total += value;
    return 1;
}

static int add_string_size(size_t* total, pxsys_string_t value) {
    return value.size != SIZE_MAX && add_size(total, value.size + 1u);
}

static char* copy_string(char** cursor, pxsys_string_t value) {
    char* result = *cursor;
    memcpy(result, value.data, value.size);
    result[value.size] = '\0';
    *cursor += value.size + 1u;
    return result;
}

static pxsys_status_t copy_descriptor(pxsys_app_registry_t* registry,
                                      const pxsys_app_descriptor_t* source,
                                      pxsys_app_descriptor_t* destination, void** strings) {
    size_t strings_size = 0;
    char* cursor;
    if (!add_string_size(&strings_size, source->identity.app_id) ||
        !add_string_size(&strings_size, source->display_name) ||
        !add_string_size(&strings_size, source->version) ||
        !add_string_size(&strings_size, source->runtime_id)) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    *strings = registry->allocator.allocate(registry->allocator.context, strings_size);
    if (*strings == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(destination, 0, sizeof(*destination));
    destination->struct_size = sizeof(*destination);
    memcpy(destination->identity.publisher_root, source->identity.publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    cursor = (char*)*strings;
    destination->identity.app_id.data = copy_string(&cursor, source->identity.app_id);
    destination->identity.app_id.size = source->identity.app_id.size;
    destination->display_name.data = copy_string(&cursor, source->display_name);
    destination->display_name.size = source->display_name.size;
    destination->version.data = copy_string(&cursor, source->version);
    destination->version.size = source->version.size;
    destination->runtime_id.data = copy_string(&cursor, source->runtime_id);
    destination->runtime_id.size = source->runtime_id.size;
    destination->flags = source->flags;
    return PXSYS_STATUS_OK;
}

void pxsys_app_registry_config_init(pxsys_app_registry_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_apps = 32;
    config->max_app_id_bytes = 64;
    config->max_display_name_bytes = 128;
    config->max_version_bytes = 64;
    config->max_runtime_id_bytes = 64;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_app_identity_validate(const pxsys_app_identity_t* identity,
                                           size_t max_app_id_bytes) {
    if (identity == NULL || max_app_id_bytes == 0 ||
        !pxsys_identifier_validate(identity->app_id, max_app_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    return PXSYS_STATUS_OK;
}

int pxsys_app_identity_equal(const pxsys_app_identity_t* left, const pxsys_app_identity_t* right) {
    if (left == NULL || right == NULL || left->app_id.size != right->app_id.size ||
        left->app_id.data == NULL || right->app_id.data == NULL) {
        return 0;
    }
    return memcmp(left->publisher_root, right->publisher_root, PXSYS_PUBLISHER_ROOT_BYTES) == 0 &&
           memcmp(left->app_id.data, right->app_id.data, left->app_id.size) == 0;
}

static int config_valid(const pxsys_app_registry_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_apps != 0 &&
           config->max_app_id_bytes != 0 && config->max_display_name_bytes != 0 &&
           config->max_version_bytes != 0 && config->max_runtime_id_bytes != 0 &&
           config->max_app_id_bytes != SIZE_MAX && config->max_display_name_bytes != SIZE_MAX &&
           config->max_version_bytes != SIZE_MAX && config->max_runtime_id_bytes != SIZE_MAX &&
           config->max_apps <= UINT32_MAX &&
           config->max_apps <= SIZE_MAX / sizeof(pxsys_app_entry_t) &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

pxsys_status_t pxsys_app_registry_create(const pxsys_app_registry_config_t* config,
                                         pxsys_app_registry_t** output) {
    pxsys_app_registry_t* registry;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    registry = (pxsys_app_registry_t*)config->allocator.allocate(config->allocator.context,
                                                                 sizeof(*registry));
    if (registry == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(registry, 0, sizeof(*registry));
    registry->entries = (pxsys_app_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_apps * sizeof(*registry->entries));
    if (registry->entries == NULL) {
        config->allocator.release(config->allocator.context, registry);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(registry->entries, 0, config->max_apps * sizeof(*registry->entries));
    registry->capacity = config->max_apps;
    registry->max_app_id_bytes = config->max_app_id_bytes;
    registry->max_display_name_bytes = config->max_display_name_bytes;
    registry->max_version_bytes = config->max_version_bytes;
    registry->max_runtime_id_bytes = config->max_runtime_id_bytes;
    registry->allocator = config->allocator;
    registry->generation = 1;
    registry->magic = PXSYS_REGISTRY_MAGIC;
    *output = registry;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_registry_destroy(pxsys_app_registry_t* registry) {
    size_t index;
    pxsys_allocator_t allocator;
    if (!registry_valid(registry))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < registry->capacity; ++index) {
        if (registry->entries[index].occupied && registry->entries[index].references != 0)
            return PXSYS_STATUS_BUSY;
    }
    allocator = registry->allocator;
    for (index = 0; index < registry->capacity; ++index) {
        if (registry->entries[index].occupied)
            allocator.release(allocator.context, registry->entries[index].strings);
    }
    registry->magic = 0;
    allocator.release(allocator.context, registry->entries);
    allocator.release(allocator.context, registry);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t descriptor_validate(const pxsys_app_registry_t* registry,
                                          const pxsys_app_descriptor_t* descriptor) {
    if (descriptor == NULL || descriptor->struct_size < sizeof(*descriptor) ||
        pxsys_app_identity_validate(&descriptor->identity, registry->max_app_id_bytes) !=
            PXSYS_STATUS_OK ||
        !pxsys_display_text_validate(descriptor->display_name, registry->max_display_name_bytes) ||
        !ascii_metadata_valid(descriptor->version, registry->max_version_bytes) ||
        !pxsys_identifier_validate(descriptor->runtime_id, registry->max_runtime_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    return PXSYS_STATUS_OK;
}

const pxsys_app_descriptor_t* pxsys_app_registry_find(const pxsys_app_registry_t* registry,
                                                      const pxsys_app_identity_t* identity) {
    pxsys_app_entry_t* entry;
    if (!registry_valid(registry) ||
        pxsys_app_identity_validate(identity, registry->max_app_id_bytes) != PXSYS_STATUS_OK) {
        return NULL;
    }
    entry = find_entry(registry, identity, NULL);
    return entry == NULL ? NULL : &entry->descriptor;
}

pxsys_status_t pxsys_app_registry_register(pxsys_app_registry_t* registry,
                                           const pxsys_app_descriptor_t* descriptor) {
    pxsys_app_entry_t* entry;
    pxsys_status_t status;
    size_t slot;
    if (!registry_valid(registry))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (descriptor_validate(registry, descriptor) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (pxsys_app_registry_find(registry, &descriptor->identity) != NULL) {
        return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (registry->count == registry->capacity) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    for (slot = 0; slot < registry->capacity; ++slot) {
        if (!registry->entries[slot].occupied)
            break;
    }
    if (slot == registry->capacity)
        return PXSYS_STATUS_INTERNAL;
    entry = &registry->entries[slot];
    status = copy_descriptor(registry, descriptor, &entry->descriptor, &entry->strings);
    if (status != PXSYS_STATUS_OK)
        return status;
    entry->generation = entry->generation == UINT32_MAX ? 1 : entry->generation + 1u;
    entry->references = 0;
    entry->occupied = 1;
    registry->count++;
    registry->generation++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_registry_update(pxsys_app_registry_t* registry,
                                         const pxsys_app_descriptor_t* descriptor) {
    pxsys_app_descriptor_t replacement;
    pxsys_app_entry_t* entry;
    pxsys_status_t status;
    void* replacement_strings = NULL;
    if (!registry_valid(registry) || descriptor_validate(registry, descriptor) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    entry = find_entry(registry, &descriptor->identity, NULL);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->references != 0)
        return PXSYS_STATUS_BUSY;
    status = copy_descriptor(registry, descriptor, &replacement, &replacement_strings);
    if (status != PXSYS_STATUS_OK)
        return status;
    registry->allocator.release(registry->allocator.context, entry->strings);
    entry->descriptor = replacement;
    entry->strings = replacement_strings;
    entry->generation = entry->generation == UINT32_MAX ? 1 : entry->generation + 1u;
    registry->generation++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_registry_unregister(pxsys_app_registry_t* registry,
                                             const pxsys_app_identity_t* identity) {
    pxsys_app_entry_t* entry;
    if (!registry_valid(registry) ||
        pxsys_app_identity_validate(identity, registry->max_app_id_bytes) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_entry(registry, identity, NULL);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->references != 0)
        return PXSYS_STATUS_BUSY;
    registry->allocator.release(registry->allocator.context, entry->strings);
    entry->strings = NULL;
    memset(&entry->descriptor, 0, sizeof(entry->descriptor));
    entry->occupied = 0;
    registry->count--;
    registry->generation++;
    return PXSYS_STATUS_OK;
}

size_t pxsys_app_registry_count(const pxsys_app_registry_t* registry) {
    return registry_valid(registry) ? registry->count : 0;
}

uint64_t pxsys_app_registry_generation(const pxsys_app_registry_t* registry) {
    return registry_valid(registry) ? registry->generation : 0;
}

const pxsys_app_descriptor_t* pxsys_app_registry_at(const pxsys_app_registry_t* registry,
                                                    size_t index) {
    size_t slot;
    size_t current = 0;
    if (!registry_valid(registry) || index >= registry->count)
        return NULL;
    for (slot = 0; slot < registry->capacity; ++slot) {
        if (!registry->entries[slot].occupied)
            continue;
        if (current++ == index)
            return &registry->entries[slot].descriptor;
    }
    return NULL;
}

pxsys_app_ref_t pxsys_app_ref_invalid(void) {
    pxsys_app_ref_t reference;
    reference.slot = PXSYS_APP_REF_INVALID_SLOT;
    reference.generation = 0;
    return reference;
}

pxsys_status_t pxsys_app_registry_acquire(pxsys_app_registry_t* registry,
                                          const pxsys_app_identity_t* identity,
                                          pxsys_app_ref_t* reference,
                                          const pxsys_app_descriptor_t** descriptor) {
    pxsys_app_entry_t* entry;
    size_t slot;
    if (reference == NULL || descriptor == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *reference = pxsys_app_ref_invalid();
    *descriptor = NULL;
    if (!registry_valid(registry) ||
        pxsys_app_identity_validate(identity, registry->max_app_id_bytes) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_entry(registry, identity, &slot);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->references == UINT32_MAX)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    entry->references++;
    reference->slot = (uint32_t)slot;
    reference->generation = entry->generation;
    *descriptor = &entry->descriptor;
    return PXSYS_STATUS_OK;
}

static pxsys_app_entry_t* resolve_reference(const pxsys_app_registry_t* registry,
                                            pxsys_app_ref_t reference) {
    pxsys_app_entry_t* entry;
    if (!registry_valid(registry) || reference.slot == PXSYS_APP_REF_INVALID_SLOT ||
        (size_t)reference.slot >= registry->capacity) {
        return NULL;
    }
    entry = &registry->entries[reference.slot];
    return entry->occupied && entry->generation == reference.generation ? entry : NULL;
}

const pxsys_app_descriptor_t* pxsys_app_registry_get(const pxsys_app_registry_t* registry,
                                                     pxsys_app_ref_t reference) {
    pxsys_app_entry_t* entry = resolve_reference(registry, reference);
    return entry == NULL ? NULL : &entry->descriptor;
}

pxsys_status_t pxsys_app_registry_release(pxsys_app_registry_t* registry,
                                          pxsys_app_ref_t reference) {
    pxsys_app_entry_t* entry = resolve_reference(registry, reference);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (entry->references == 0)
        return PXSYS_STATUS_BAD_STATE;
    entry->references--;
    return PXSYS_STATUS_OK;
}
