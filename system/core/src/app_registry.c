#include "pxsys/app_registry.h"

#include <limits.h>
#include <string.h>

#define PXSYS_REGISTRY_MAGIC UINT32_C(0x50585352)

typedef struct {
    pxsys_app_descriptor_t descriptor;
    void* strings;
    uint32_t generation;
    uint32_t references;
    uint32_t next_in_bucket;
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
    uint32_t* buckets;
};

static int registry_valid(const pxsys_app_registry_t* registry) {
    return registry != NULL && registry->magic == PXSYS_REGISTRY_MAGIC;
}

static uint32_t* identity_bucket(const pxsys_app_registry_t* registry,
                                 const pxsys_app_identity_t* identity) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < sizeof(identity->publisher_root); ++i)
        hash = (hash ^ identity->publisher_root[i]) * UINT32_C(16777619);
    for (size_t i = 0; i < identity->app_id.size; ++i)
        hash = (hash ^ (uint8_t)identity->app_id.data[i]) * UINT32_C(16777619);
    return &registry->buckets[hash % registry->capacity];
}

static uint32_t* find_entry_link(const pxsys_app_registry_t* registry,
                                  const pxsys_app_identity_t* identity) {
    uint32_t* link = identity_bucket(registry, identity);
    while (*link != 0) {
        pxsys_app_entry_t* entry = &registry->entries[*link - 1u];
        if (pxsys_app_identity_equal(&entry->descriptor.identity, identity)) break;
        link = &entry->next_in_bucket;
    }
    return link;
}

static pxsys_app_entry_t* find_entry(const pxsys_app_registry_t* registry,
                                     const pxsys_app_identity_t* identity, size_t* slot) {
    const uint32_t link = *find_entry_link(registry, identity);
    if (link == 0) return NULL;
    if (slot != NULL) *slot = link - 1u;
    return &registry->entries[link - 1u];
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

static int descriptor_has(const pxsys_app_descriptor_t* descriptor,
                          size_t end) {
    return descriptor->struct_size >= end;
}

#define DESCRIPTOR_HAS(descriptor, member)                                \
    descriptor_has((descriptor), offsetof(pxsys_app_descriptor_t, member) + \
                                      sizeof((descriptor)->member))

static int add_optional_string_size(size_t* total, pxsys_string_t value) {
    return value.size == 0 || add_string_size(total, value);
}

static pxsys_string_t copy_optional_string(char** cursor,
                                           pxsys_string_t value) {
    pxsys_string_t result = {0};
    if (value.size == 0) return result;
    result.data = copy_string(cursor, value);
    result.size = value.size;
    return result;
}

static pxsys_status_t copy_descriptor(pxsys_app_registry_t* registry,
                                      const pxsys_app_descriptor_t* source,
                                      pxsys_app_descriptor_t* destination, void** strings) {
    pxsys_string_t description = {0};
    pxsys_string_t icon_reference = {0};
    pxsys_string_t resource_namespace = {0};
    pxsys_string_t display_name_resource_key = {0};
    pxsys_string_t description_resource_key = {0};
    pxsys_string_t icon_resource_key = {0};
    size_t strings_size = 0;
    char* cursor;
    if (DESCRIPTOR_HAS(source, description)) description = source->description;
    if (DESCRIPTOR_HAS(source, icon_reference))
        icon_reference = source->icon_reference;
    if (DESCRIPTOR_HAS(source, resource_namespace))
        resource_namespace = source->resource_namespace;
    if (DESCRIPTOR_HAS(source, display_name_resource_key))
        display_name_resource_key = source->display_name_resource_key;
    if (DESCRIPTOR_HAS(source, description_resource_key))
        description_resource_key = source->description_resource_key;
    if (DESCRIPTOR_HAS(source, icon_resource_key))
        icon_resource_key = source->icon_resource_key;
    if (!add_string_size(&strings_size, source->identity.app_id) ||
        !add_string_size(&strings_size, source->display_name) ||
        !add_string_size(&strings_size, source->version) ||
        !add_string_size(&strings_size, source->runtime_id) ||
        !add_optional_string_size(&strings_size, description) ||
        !add_optional_string_size(&strings_size, icon_reference) ||
        !add_optional_string_size(&strings_size, resource_namespace) ||
        !add_optional_string_size(&strings_size, display_name_resource_key) ||
        !add_optional_string_size(&strings_size, description_resource_key) ||
        !add_optional_string_size(&strings_size, icon_resource_key)) {
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
    destination->description = copy_optional_string(&cursor, description);
    destination->icon_reference =
        copy_optional_string(&cursor, icon_reference);
    destination->resource_namespace =
        copy_optional_string(&cursor, resource_namespace);
    destination->display_name_resource_key =
        copy_optional_string(&cursor, display_name_resource_key);
    destination->description_resource_key =
        copy_optional_string(&cursor, description_resource_key);
    destination->icon_resource_key =
        copy_optional_string(&cursor, icon_resource_key);
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

int pxsys_app_identity_parse(pxsys_string_t text, pxsys_app_identity_t* identity) {
    const size_t prefix = PXSYS_PUBLISHER_ROOT_BYTES * 2u;
    static const char hex[] = "0123456789abcdef";
    if (identity == NULL || text.data == NULL || text.size <= prefix + 1u ||
        text.data[prefix] != ':')
        return 0;
    pxsys_app_identity_t parsed = {
        .app_id = {text.data + prefix + 1u, text.size - prefix - 1u},
    };
    for (size_t i = 0; i < PXSYS_PUBLISHER_ROOT_BYTES; ++i) {
        const char* high = memchr(hex, text.data[2u * i], sizeof(hex) - 1u);
        const char* low = memchr(hex, text.data[2u * i + 1u], sizeof(hex) - 1u);
        if (high == NULL || low == NULL) return 0;
        parsed.publisher_root[i] = (uint8_t)((high - hex) * 16 + (low - hex));
    }
    if (pxsys_app_identity_validate(&parsed, parsed.app_id.size) != PXSYS_STATUS_OK) return 0;
    *identity = parsed;
    return 1;
}

static int config_valid(const pxsys_app_registry_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_apps != 0 &&
           config->max_app_id_bytes != 0 && config->max_display_name_bytes != 0 &&
           config->max_version_bytes != 0 && config->max_runtime_id_bytes != 0 &&
           config->max_app_id_bytes != SIZE_MAX && config->max_display_name_bytes != SIZE_MAX &&
           config->max_version_bytes != SIZE_MAX && config->max_runtime_id_bytes != SIZE_MAX &&
           config->max_apps <= UINT32_MAX &&
           config->max_apps <= SIZE_MAX / (sizeof(pxsys_app_entry_t) + sizeof(uint32_t)) &&
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
    const size_t entries_bytes = config->max_apps * sizeof(*registry->entries);
    const size_t index_bytes = config->max_apps * sizeof(*registry->buckets);
    registry->entries = (pxsys_app_entry_t*)config->allocator.allocate(
        config->allocator.context, entries_bytes + index_bytes);
    if (registry->entries == NULL) {
        config->allocator.release(config->allocator.context, registry);
        return PXSYS_STATUS_NO_MEMORY;
    }
    registry->buckets = (uint32_t*)((char*)registry->entries + entries_bytes);
    memset(registry->entries, 0, entries_bytes + index_bytes);
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
    pxsys_string_t description = {0};
    pxsys_string_t icon_reference = {0};
    pxsys_string_t resource_namespace = {0};
    pxsys_string_t display_name_resource_key = {0};
    pxsys_string_t description_resource_key = {0};
    pxsys_string_t icon_resource_key = {0};
    if (descriptor == NULL ||
        descriptor->struct_size < PXSYS_APP_DESCRIPTOR_V1_SIZE ||
        pxsys_app_identity_validate(&descriptor->identity, registry->max_app_id_bytes) !=
            PXSYS_STATUS_OK ||
        !pxsys_display_text_validate(descriptor->display_name, registry->max_display_name_bytes) ||
        !ascii_metadata_valid(descriptor->version, registry->max_version_bytes) ||
        !pxsys_identifier_validate(descriptor->runtime_id, registry->max_runtime_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (DESCRIPTOR_HAS(descriptor, description))
        description = descriptor->description;
    if (DESCRIPTOR_HAS(descriptor, icon_reference))
        icon_reference = descriptor->icon_reference;
    if (DESCRIPTOR_HAS(descriptor, resource_namespace))
        resource_namespace = descriptor->resource_namespace;
    if (DESCRIPTOR_HAS(descriptor, display_name_resource_key))
        display_name_resource_key = descriptor->display_name_resource_key;
    if (DESCRIPTOR_HAS(descriptor, description_resource_key))
        description_resource_key = descriptor->description_resource_key;
    if (DESCRIPTOR_HAS(descriptor, icon_resource_key))
        icon_resource_key = descriptor->icon_resource_key;
    if ((description.size != 0 &&
         !pxsys_display_text_validate(description,
                                      PXSYS_APP_DESCRIPTION_MAX_BYTES)) ||
        (icon_reference.size != 0 &&
         !pxsys_display_text_validate(icon_reference,
                                      PXSYS_APP_ICON_REFERENCE_MAX_BYTES)) ||
        (resource_namespace.size != 0 &&
         !pxsys_identifier_validate(resource_namespace,
                                    PXSYS_APP_RESOURCE_NAMESPACE_MAX_BYTES)) ||
        (display_name_resource_key.size != 0 &&
         !pxsys_identifier_validate(display_name_resource_key,
                                    PXSYS_APP_RESOURCE_KEY_MAX_BYTES)) ||
        (description_resource_key.size != 0 &&
         !pxsys_identifier_validate(description_resource_key,
                                    PXSYS_APP_RESOURCE_KEY_MAX_BYTES)) ||
        (icon_resource_key.size != 0 &&
         !pxsys_identifier_validate(icon_resource_key,
                                    PXSYS_APP_RESOURCE_KEY_MAX_BYTES)) ||
        (resource_namespace.size == 0 &&
         (display_name_resource_key.size != 0 ||
          description_resource_key.size != 0 || icon_resource_key.size != 0))) {
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
    uint32_t* link = find_entry_link(registry, &descriptor->identity);
    if (*link != 0) {
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
    entry->next_in_bucket = 0;
    *link = (uint32_t)slot + 1u;
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
    uint32_t* link = find_entry_link(registry, identity);
    if (*link == 0)
        return PXSYS_STATUS_NOT_FOUND;
    entry = &registry->entries[*link - 1u];
    if (entry->references != 0)
        return PXSYS_STATUS_BUSY;
    *link = entry->next_in_bucket;
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

const pxsys_app_descriptor_t* pxsys_app_registry_next(const pxsys_app_registry_t* registry,
                                                       size_t* cursor) {
    if (!registry_valid(registry) || cursor == NULL) return NULL;
    while (*cursor < registry->capacity) {
        const pxsys_app_entry_t* entry = &registry->entries[(*cursor)++];
        if (entry->occupied) return &entry->descriptor;
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
