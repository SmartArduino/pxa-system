#include "pxsys/locale.h"

#include <ctype.h>
#include <string.h>

#define PXSYS_LOCALE_MAGIC UINT32_C(0x50584c43)

typedef struct {
    void* context;
    pxsys_locale_changed_fn callback;
} locale_observer_t;

struct pxsys_locale_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    uint8_t notifying;
    pxsys_allocator_t allocator;
    pxsys_locale_snapshot_t current;
    locale_observer_t* observers;
};

static int service_valid(const pxsys_locale_service_t* service) {
    return service != NULL && service->magic == PXSYS_LOCALE_MAGIC;
}

static int primary_is_rtl(const char* tag, size_t size) {
    static const char* const rtl[] = {
        "ar", "dv", "fa", "he", "ps", "sd", "ug", "ur", "yi"};
    size_t primary = 0;
    size_t index;
    while (primary < size && tag[primary] != '-') primary++;
    for (index = 0; index < sizeof(rtl) / sizeof(rtl[0]); ++index) {
        if (strlen(rtl[index]) == primary &&
            memcmp(tag, rtl[index], primary) == 0)
            return 1;
    }
    return 0;
}

static int valid_tag(pxsys_string_t tag) {
    size_t index;
    size_t subtag_size = 0;
    if (tag.data == NULL || tag.size < 2 ||
        tag.size > PXSYS_LOCALE_TAG_MAX_BYTES)
        return 0;
    for (index = 0; index < tag.size; ++index) {
        unsigned char ch = (unsigned char)tag.data[index];
        if (ch == '-') {
            if (subtag_size == 0 || subtag_size > 8) return 0;
            subtag_size = 0;
        } else {
            if (!isalnum(ch)) return 0;
            subtag_size++;
        }
    }
    if (subtag_size == 0 || subtag_size > 8) return 0;
    for (index = 0; index < tag.size && tag.data[index] != '-'; ++index) {
        if (!isalpha((unsigned char)tag.data[index])) return 0;
    }
    return index >= 2 && index <= 8;
}

static void canonicalize(char* output, pxsys_string_t tag) {
    size_t index = 0;
    unsigned subtag = 0;
    size_t subtag_start = 0;
    for (index = 0; index <= tag.size; ++index) {
        if (index == tag.size || tag.data[index] == '-') {
            size_t cursor;
            size_t length = index - subtag_start;
            for (cursor = subtag_start; cursor < index; ++cursor) {
                unsigned char ch = (unsigned char)tag.data[cursor];
                if (subtag > 0 && length == 4)
                    output[cursor] = cursor == subtag_start
                                         ? (char)toupper(ch)
                                         : (char)tolower(ch);
                else if (subtag > 0 && length == 2)
                    output[cursor] = (char)toupper(ch);
                else
                    output[cursor] = (char)tolower(ch);
            }
            if (index < tag.size) {
                output[index] = '-';
                subtag_start = index + 1;
                subtag++;
            }
        }
    }
    output[tag.size] = '\0';
}

pxsys_status_t pxsys_locale_snapshot_init(pxsys_locale_snapshot_t* snapshot,
                                          pxsys_string_t tag) {
    if (snapshot == NULL || !valid_tag(tag))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = sizeof(*snapshot);
    canonicalize(snapshot->tag, tag);
    snapshot->tag_size = (uint16_t)tag.size;
    snapshot->direction = primary_is_rtl(snapshot->tag, tag.size)
                              ? PXSYS_TEXT_DIRECTION_RTL
                              : PXSYS_TEXT_DIRECTION_LTR;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_snapshot_validate(
    const pxsys_locale_snapshot_t* snapshot) {
    pxsys_locale_snapshot_t canonical;
    if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot) ||
        snapshot->tag_size > PXSYS_LOCALE_TAG_MAX_BYTES ||
        snapshot->tag[snapshot->tag_size] != '\0' ||
        snapshot->direction > PXSYS_TEXT_DIRECTION_RTL ||
        pxsys_locale_snapshot_init(
            &canonical,
            pxsys_string(snapshot->tag, snapshot->tag_size)) != PXSYS_STATUS_OK ||
        canonical.tag_size != snapshot->tag_size ||
        canonical.direction != snapshot->direction ||
        memcmp(canonical.tag, snapshot->tag, snapshot->tag_size + 1u) != 0)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    return PXSYS_STATUS_OK;
}

void pxsys_locale_service_config_init(pxsys_locale_service_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_locale_service_create(
    const pxsys_locale_service_config_t* config,
    const pxsys_locale_snapshot_t* initial, pxsys_locale_service_t** output) {
    pxsys_locale_service_t* service;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_observers == 0 ||
        config->max_observers > SIZE_MAX / sizeof(locale_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        pxsys_locale_snapshot_validate(initial) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    service = (pxsys_locale_service_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*service));
    if (service == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (locale_observer_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0,
           config->max_observers * sizeof(*service->observers));
    service->allocator = config->allocator;
    service->observer_capacity = config->max_observers;
    service->current = *initial;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = 1;
    service->magic = PXSYS_LOCALE_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_service_destroy(pxsys_locale_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_service_update(
    pxsys_locale_service_t* service, const pxsys_locale_snapshot_t* locale) {
    size_t index;
    uint64_t generation;
    if (!service_valid(service) ||
        pxsys_locale_snapshot_validate(locale) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    generation = service->current.generation == UINT64_MAX
                     ? 1
                     : service->current.generation + 1u;
    service->current = *locale;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = generation;
    service->notifying = 1;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback != NULL)
            service->observers[index].callback(
                service->observers[index].context, &service->current);
    }
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_service_get(const pxsys_locale_service_t* service,
                                        pxsys_locale_snapshot_t* locale) {
    if (!service_valid(service) || locale == NULL ||
        locale->struct_size < sizeof(*locale))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *locale = service->current;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_service_subscribe(pxsys_locale_service_t* service,
                                              void* context,
                                              pxsys_locale_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].context == context &&
            service->observers[index].callback == callback)
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL) break;
    }
    service->observers[index].context = context;
    service->observers[index].callback = callback;
    service->observer_count++;
    service->notifying = 1;
    callback(context, &service->current);
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_locale_service_unsubscribe(
    pxsys_locale_service_t* service, void* context,
    pxsys_locale_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].context == context &&
            service->observers[index].callback == callback) {
            memset(&service->observers[index], 0,
                   sizeof(service->observers[index]));
            service->observer_count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}
