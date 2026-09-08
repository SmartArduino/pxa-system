#include "pxsys/intent.h"

#include <string.h>

#define PXSYS_INTENT_MAGIC UINT32_C(0x5058494e)

typedef struct {
    pxsys_intent_filter_t filter;
    pxsys_app_ref_t app_ref;
    const pxsys_app_descriptor_t* app;
    void* strings;
    uint8_t occupied;
} pxsys_intent_filter_entry_t;

struct pxsys_intent_resolver {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_identifier_bytes;
    size_t max_mime_type_bytes;
    pxsys_app_registry_t* apps;
    pxsys_allocator_t allocator;
    pxsys_intent_filter_entry_t* entries;
};

static int resolver_valid(const pxsys_intent_resolver_t* resolver) {
    return resolver != NULL && resolver->magic == PXSYS_INTENT_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size &&
           (left.size == 0 || memcmp(left.data, right.data, left.size) == 0);
}

static int optional_identifier_valid(pxsys_string_t value, size_t maximum) {
    return value.size == 0 || pxsys_identifier_validate(value, maximum);
}

static int mime_valid(pxsys_string_t value, size_t maximum, int allow_empty) {
    size_t index;
    if (value.size == 0)
        return allow_empty;
    if (value.data == NULL || value.size > maximum)
        return 0;
    for (index = 0; index < value.size; ++index) {
        unsigned char byte = (unsigned char)value.data[index];
        if (byte < 0x21u || byte > 0x7eu)
            return 0;
    }
    return 1;
}

static int config_valid(const pxsys_intent_resolver_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_filters != 0 &&
           config->max_filters <= SIZE_MAX / sizeof(pxsys_intent_filter_entry_t) &&
           config->max_identifier_bytes != 0 && config->max_identifier_bytes != SIZE_MAX &&
           config->max_mime_type_bytes != 0 && config->apps != NULL &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

void pxsys_intent_resolver_config_init(pxsys_intent_resolver_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_filters = 64;
    config->max_identifier_bytes = 96;
    config->max_mime_type_bytes = 96;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_intent_resolver_create(const pxsys_intent_resolver_config_t* config,
                                            pxsys_intent_resolver_t** output) {
    pxsys_intent_resolver_t* resolver;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    resolver = (pxsys_intent_resolver_t*)config->allocator.allocate(config->allocator.context,
                                                                    sizeof(*resolver));
    if (resolver == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(resolver, 0, sizeof(*resolver));
    resolver->entries = (pxsys_intent_filter_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_filters * sizeof(*resolver->entries));
    if (resolver->entries == NULL) {
        config->allocator.release(config->allocator.context, resolver);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(resolver->entries, 0, config->max_filters * sizeof(*resolver->entries));
    resolver->capacity = config->max_filters;
    resolver->max_identifier_bytes = config->max_identifier_bytes;
    resolver->max_mime_type_bytes = config->max_mime_type_bytes;
    resolver->apps = config->apps;
    resolver->allocator = config->allocator;
    resolver->magic = PXSYS_INTENT_MAGIC;
    *output = resolver;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_intent_resolver_destroy(pxsys_intent_resolver_t* resolver) {
    size_t index;
    pxsys_allocator_t allocator;
    if (!resolver_valid(resolver))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    allocator = resolver->allocator;
    for (index = 0; index < resolver->capacity; ++index) {
        pxsys_intent_filter_entry_t* entry = &resolver->entries[index];
        if (!entry->occupied)
            continue;
        (void)pxsys_app_registry_release(resolver->apps, entry->app_ref);
        allocator.release(allocator.context, entry->strings);
    }
    resolver->magic = 0;
    allocator.release(allocator.context, resolver->entries);
    allocator.release(allocator.context, resolver);
    return PXSYS_STATUS_OK;
}

static int filter_equal(const pxsys_intent_filter_entry_t* entry,
                        const pxsys_intent_filter_t* filter) {
    return pxsys_app_identity_equal(&entry->filter.app, &filter->app) &&
           string_equal(entry->filter.action, filter->action) &&
           string_equal(entry->filter.uri_scheme, filter->uri_scheme) &&
           string_equal(entry->filter.mime_type, filter->mime_type);
}

static char* copy_string(char** cursor, pxsys_string_t value) {
    char* result = *cursor;
    if (value.size != 0)
        memcpy(result, value.data, value.size);
    result[value.size] = '\0';
    *cursor += value.size + 1u;
    return result;
}

pxsys_status_t pxsys_intent_filter_register(pxsys_intent_resolver_t* resolver,
                                            const pxsys_intent_filter_t* filter) {
    pxsys_intent_filter_entry_t* entry;
    const pxsys_app_descriptor_t* app;
    pxsys_app_ref_t app_ref;
    pxsys_status_t status;
    size_t index;
    size_t strings_size;
    char* cursor;
    if (!resolver_valid(resolver) || filter == NULL || filter->struct_size < sizeof(*filter) ||
        !pxsys_identifier_validate(filter->action, resolver->max_identifier_bytes) ||
        !optional_identifier_valid(filter->uri_scheme, resolver->max_identifier_bytes) ||
        !mime_valid(filter->mime_type, resolver->max_mime_type_bytes, 1)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < resolver->capacity; ++index) {
        if (resolver->entries[index].occupied && filter_equal(&resolver->entries[index], filter))
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (resolver->count == resolver->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    status = pxsys_app_registry_acquire(resolver->apps, &filter->app, &app_ref, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    if ((app->flags & PXSYS_APP_FLAG_ENABLED) == 0) {
        (void)pxsys_app_registry_release(resolver->apps, app_ref);
        return PXSYS_STATUS_DENIED;
    }
    for (index = 0; index < resolver->capacity; ++index) {
        if (!resolver->entries[index].occupied)
            break;
    }
    if (filter->app.app_id.size > SIZE_MAX - filter->action.size - 4u ||
        filter->app.app_id.size + filter->action.size + 4u > SIZE_MAX - filter->uri_scheme.size ||
        filter->app.app_id.size + filter->action.size + filter->uri_scheme.size + 4u >
            SIZE_MAX - filter->mime_type.size) {
        (void)pxsys_app_registry_release(resolver->apps, app_ref);
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    strings_size = filter->app.app_id.size + filter->action.size + filter->uri_scheme.size +
                   filter->mime_type.size + 4u;
    entry = &resolver->entries[index];
    entry->strings = resolver->allocator.allocate(resolver->allocator.context, strings_size);
    if (entry->strings == NULL) {
        (void)pxsys_app_registry_release(resolver->apps, app_ref);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(&entry->filter, 0, sizeof(entry->filter));
    entry->filter.struct_size = sizeof(entry->filter);
    memcpy(entry->filter.app.publisher_root, filter->app.publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    cursor = (char*)entry->strings;
    entry->filter.app.app_id.data = copy_string(&cursor, filter->app.app_id);
    entry->filter.app.app_id.size = filter->app.app_id.size;
    entry->filter.action.data = copy_string(&cursor, filter->action);
    entry->filter.action.size = filter->action.size;
    entry->filter.uri_scheme.data = copy_string(&cursor, filter->uri_scheme);
    entry->filter.uri_scheme.size = filter->uri_scheme.size;
    entry->filter.mime_type.data = copy_string(&cursor, filter->mime_type);
    entry->filter.mime_type.size = filter->mime_type.size;
    entry->filter.priority = filter->priority;
    entry->app_ref = app_ref;
    entry->app = app;
    entry->occupied = 1;
    resolver->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_intent_filters_unregister(pxsys_intent_resolver_t* resolver,
                                               const pxsys_app_identity_t* app) {
    size_t index;
    size_t removed = 0;
    if (!resolver_valid(resolver) || app == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < resolver->capacity; ++index) {
        pxsys_intent_filter_entry_t* entry = &resolver->entries[index];
        if (!entry->occupied || !pxsys_app_identity_equal(&entry->filter.app, app))
            continue;
        (void)pxsys_app_registry_release(resolver->apps, entry->app_ref);
        resolver->allocator.release(resolver->allocator.context, entry->strings);
        memset(entry, 0, sizeof(*entry));
        resolver->count--;
        removed++;
    }
    return removed == 0 ? PXSYS_STATUS_NOT_FOUND : PXSYS_STATUS_OK;
}

static int uri_has_scheme(pxsys_string_t uri, pxsys_string_t scheme) {
    return scheme.size == 0 ||
           (uri.size > scheme.size && uri.data != NULL &&
            memcmp(uri.data, scheme.data, scheme.size) == 0 && uri.data[scheme.size] == ':');
}

static int identity_compare(const pxsys_app_identity_t* left, const pxsys_app_identity_t* right) {
    int result = memcmp(left->publisher_root, right->publisher_root, PXSYS_PUBLISHER_ROOT_BYTES);
    size_t common;
    if (result != 0)
        return result;
    common = left->app_id.size < right->app_id.size ? left->app_id.size : right->app_id.size;
    result = memcmp(left->app_id.data, right->app_id.data, common);
    if (result != 0)
        return result;
    return left->app_id.size < right->app_id.size ? -1 : left->app_id.size > right->app_id.size;
}

pxsys_status_t pxsys_intent_resolve(const pxsys_intent_resolver_t* resolver,
                                    const pxsys_intent_t* intent,
                                    const pxsys_app_descriptor_t** app) {
    const pxsys_intent_filter_entry_t* best = NULL;
    size_t index;
    if (app == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *app = NULL;
    if (!resolver_valid(resolver) || intent == NULL || intent->struct_size < sizeof(*intent) ||
        !pxsys_identifier_validate(intent->action, resolver->max_identifier_bytes) ||
        !mime_valid(intent->mime_type, resolver->max_mime_type_bytes, 1) ||
        (intent->uri.size != 0 && intent->uri.data == NULL)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (intent->target != NULL) {
        *app = pxsys_app_registry_find(resolver->apps, intent->target);
        if (*app == NULL)
            return PXSYS_STATUS_NOT_FOUND;
        return ((*app)->flags & PXSYS_APP_FLAG_ENABLED) != 0 ? PXSYS_STATUS_OK
                                                             : PXSYS_STATUS_DENIED;
    }
    for (index = 0; index < resolver->capacity; ++index) {
        const pxsys_intent_filter_entry_t* candidate = &resolver->entries[index];
        if (!candidate->occupied || !string_equal(candidate->filter.action, intent->action) ||
            !uri_has_scheme(intent->uri, candidate->filter.uri_scheme) ||
            (candidate->filter.mime_type.size != 0 &&
             !string_equal(candidate->filter.mime_type, intent->mime_type))) {
            continue;
        }
        if (best == NULL || candidate->filter.priority > best->filter.priority ||
            (candidate->filter.priority == best->filter.priority &&
             identity_compare(&candidate->filter.app, &best->filter.app) < 0)) {
            best = candidate;
        }
    }
    if (best == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    *app = best->app;
    return PXSYS_STATUS_OK;
}
