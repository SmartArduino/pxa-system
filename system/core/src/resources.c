#include "pxsys/resources.h"

#include <limits.h>
#include <string.h>

#include "pxsys/locale.h"

#define PXSYS_RESOURCES_MAGIC UINT32_C(0x50585253)

typedef struct {
    const pxsys_resource_catalog_t* source;
    pxsys_resource_catalog_t catalog;
    uint8_t occupied;
} catalog_slot_t;

struct pxsys_resource_service {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_key_bytes;
    size_t max_value_bytes;
    pxsys_allocator_t allocator;
    catalog_slot_t* catalogs;
};

static int service_valid(const pxsys_resource_service_t* service) {
    return service != NULL && service->magic == PXSYS_RESOURCES_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size &&
           (left.size == 0 || memcmp(left.data, right.data, left.size) == 0);
}

static int canonical_locale(pxsys_string_t value, int allow_empty) {
    pxsys_locale_snapshot_t locale;
    if (allow_empty && value.size == 0) return 1;
    if (pxsys_locale_snapshot_init(&locale, value) != PXSYS_STATUS_OK)
        return 0;
    return locale.tag_size == value.size &&
           memcmp(locale.tag, value.data, value.size) == 0;
}

static int catalog_valid(const pxsys_resource_service_t* service,
                         const pxsys_resource_catalog_t* catalog) {
    size_t index;
    if (catalog == NULL || catalog->struct_size < sizeof(*catalog) ||
        !pxsys_identifier_validate(catalog->resource_namespace,
                                   PXSYS_RESOURCE_NAMESPACE_MAX_BYTES) ||
        !canonical_locale(catalog->locale, 1) || catalog->entries == NULL ||
        catalog->entry_count == 0)
        return 0;
    for (index = 0; index < catalog->entry_count; ++index) {
        size_t previous;
        if (!pxsys_identifier_validate(catalog->entries[index].key,
                                       service->max_key_bytes) ||
            !pxsys_display_text_validate(catalog->entries[index].value,
                                         service->max_value_bytes))
            return 0;
        for (previous = 0; previous < index; ++previous) {
            if (string_equal(catalog->entries[index].key,
                             catalog->entries[previous].key))
                return 0;
        }
    }
    return 1;
}

void pxsys_resource_service_config_init(
    pxsys_resource_service_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_catalogs = 32;
    config->max_key_bytes = 95;
    config->max_value_bytes = 1024;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_resource_service_create(
    const pxsys_resource_service_config_t* config,
    pxsys_resource_service_t** output) {
    pxsys_resource_service_t* service;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_catalogs == 0 || config->max_key_bytes == 0 ||
        config->max_value_bytes == 0 ||
        config->max_catalogs > SIZE_MAX / sizeof(catalog_slot_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    service = (pxsys_resource_service_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*service));
    if (service == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->catalogs = (catalog_slot_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_catalogs * sizeof(*service->catalogs));
    if (service->catalogs == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->catalogs, 0,
           config->max_catalogs * sizeof(*service->catalogs));
    service->capacity = config->max_catalogs;
    service->max_key_bytes = config->max_key_bytes;
    service->max_value_bytes = config->max_value_bytes;
    service->allocator = config->allocator;
    service->magic = PXSYS_RESOURCES_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_resource_service_destroy(
    pxsys_resource_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service)) return PXSYS_STATUS_INVALID_ARGUMENT;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->catalogs);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_resource_catalog_register(
    pxsys_resource_service_t* service,
    const pxsys_resource_catalog_t* catalog) {
    size_t index;
    if (!service_valid(service) || !catalog_valid(service, catalog))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < service->capacity; ++index) {
        if (service->catalogs[index].occupied &&
            service->catalogs[index].source == catalog)
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (service->count == service->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->capacity; ++index) {
        if (!service->catalogs[index].occupied) break;
    }
    service->catalogs[index].source = catalog;
    service->catalogs[index].catalog = *catalog;
    service->catalogs[index].catalog.struct_size = sizeof(*catalog);
    service->catalogs[index].occupied = 1;
    service->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_resource_catalog_unregister(
    pxsys_resource_service_t* service,
    const pxsys_resource_catalog_t* catalog) {
    size_t index;
    if (!service_valid(service) || catalog == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < service->capacity; ++index) {
        if (service->catalogs[index].occupied &&
            service->catalogs[index].source == catalog) {
            memset(&service->catalogs[index], 0,
                   sizeof(service->catalogs[index]));
            service->count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}

static int locale_score(pxsys_string_t requested, pxsys_string_t available) {
    if (available.size == 0) return 1;
    if (available.size > requested.size ||
        memcmp(requested.data, available.data, available.size) != 0)
        return 0;
    if (available.size != requested.size &&
        requested.data[available.size] != '-')
        return 0;
    return 2 + (int)available.size;
}

pxsys_status_t pxsys_resource_resolve(
    const pxsys_resource_service_t* service, pxsys_string_t resource_namespace,
    pxsys_string_t key, pxsys_string_t locale, pxsys_string_t* value) {
    pxsys_locale_snapshot_t canonical;
    pxsys_string_t requested;
    int32_t best_priority = INT32_MIN;
    int best_score = 0;
    const pxsys_string_t* best = NULL;
    size_t catalog_index;
    if (!service_valid(service) || value == NULL ||
        !pxsys_identifier_validate(resource_namespace,
                                   PXSYS_RESOURCE_NAMESPACE_MAX_BYTES) ||
        !pxsys_identifier_validate(key, service->max_key_bytes) ||
        pxsys_locale_snapshot_init(&canonical, locale) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    requested = pxsys_string(canonical.tag, canonical.tag_size);
    for (catalog_index = 0; catalog_index < service->capacity;
         ++catalog_index) {
        const catalog_slot_t* slot = &service->catalogs[catalog_index];
        const pxsys_resource_catalog_t* catalog;
        int score;
        size_t entry_index;
        if (!slot->occupied) continue;
        catalog = &slot->catalog;
        if (!string_equal(catalog->resource_namespace, resource_namespace))
            continue;
        score = locale_score(requested, catalog->locale);
        if (score == 0 || catalog->priority < best_priority ||
            (catalog->priority == best_priority && score <= best_score))
            continue;
        for (entry_index = 0; entry_index < catalog->entry_count;
             ++entry_index) {
            if (string_equal(catalog->entries[entry_index].key, key)) {
                best = &catalog->entries[entry_index].value;
                best_priority = catalog->priority;
                best_score = score;
                break;
            }
        }
    }
    if (best == NULL) return PXSYS_STATUS_NOT_FOUND;
    *value = *best;
    return PXSYS_STATUS_OK;
}
