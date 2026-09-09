#include "pxsys/theme.h"

#include <string.h>

#define PXSYS_THEME_MAGIC UINT32_C(0x50585448)

typedef struct {
    void* context;
    pxsys_theme_changed_fn callback;
} theme_observer_t;

struct pxsys_theme_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    uint8_t notifying;
    pxsys_allocator_t allocator;
    pxsys_theme_snapshot_t current;
    theme_observer_t* observers;
};

static int service_valid(const pxsys_theme_service_t* service) {
    return service != NULL && service->magic == PXSYS_THEME_MAGIC;
}

pxsys_status_t pxsys_theme_snapshot_validate(
    const pxsys_theme_snapshot_t* snapshot) {
    size_t role;
    if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot) ||
        snapshot->configured_mode > PXSYS_THEME_MODE_CUSTOM ||
        snapshot->effective_scheme > PXSYS_COLOR_SCHEME_DARK ||
        snapshot->contrast > PXSYS_CONTRAST_HIGH || snapshot->base_font_px == 0 ||
        snapshot->base_spacing_px == 0 || snapshot->motion_scale_per_mille > 1000 ||
        snapshot->theme_id_size > PXSYS_THEME_ID_MAX_BYTES ||
        snapshot->theme_id[snapshot->theme_id_size] != '\0' ||
        (snapshot->configured_mode == PXSYS_THEME_MODE_CUSTOM &&
         snapshot->theme_id_size == 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (role = 0; role < PXSYS_TYPOGRAPHY_ROLE_COUNT; ++role) {
        if (snapshot->typography_px[role] == 0)
            return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    return PXSYS_STATUS_OK;
}

static int snapshot_valid(const pxsys_theme_snapshot_t* snapshot) {
    return pxsys_theme_snapshot_validate(snapshot) == PXSYS_STATUS_OK;
}

void pxsys_theme_snapshot_init(pxsys_theme_snapshot_t* snapshot, pxsys_color_scheme_t scheme) {
    static const uint32_t light[PXSYS_COLOR_TOKEN_COUNT] = {
        UINT32_C(0xfff7f7f8), UINT32_C(0xffffffff), UINT32_C(0xff171719), UINT32_C(0xff626269),
        UINT32_C(0xffd8d8dc), UINT32_C(0xff1769e0), UINT32_C(0xffffffff), UINT32_C(0xffb42318),
        UINT32_C(0xffa15c00), UINT32_C(0xff18794e), UINT32_C(0x66000000),
    };
    static const uint32_t dark[PXSYS_COLOR_TOKEN_COUNT] = {
        UINT32_C(0xff111214), UINT32_C(0xff1c1d20), UINT32_C(0xfff1f1f2), UINT32_C(0xffaaaab0),
        UINT32_C(0xff3a3b40), UINT32_C(0xff69a7ff), UINT32_C(0xff0d203d), UINT32_C(0xffff7b72),
        UINT32_C(0xffffb454), UINT32_C(0xff5bd69a), UINT32_C(0x99000000),
    };
    if (snapshot == NULL)
        return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->configured_mode = PXSYS_THEME_MODE_SYSTEM;
    snapshot->effective_scheme =
        scheme <= PXSYS_COLOR_SCHEME_DARK ? scheme : PXSYS_COLOR_SCHEME_LIGHT;
    snapshot->contrast = PXSYS_CONTRAST_NORMAL;
    memcpy(snapshot->colors, snapshot->effective_scheme == PXSYS_COLOR_SCHEME_DARK ? dark : light,
           sizeof(snapshot->colors));
    snapshot->base_font_px = 16;
    snapshot->base_spacing_px = 4;
    snapshot->base_radius_px = 4;
    snapshot->motion_scale_per_mille = 1000;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_DISPLAY] = 28;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_HEADLINE] = 24;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_TITLE] = 20;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_BODY] = 16;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_LABEL] = 14;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_CAPTION] = 12;
}

uint16_t pxsys_theme_typography_px(const pxsys_theme_snapshot_t* snapshot,
                                   pxsys_typography_role_t role) {
    if (pxsys_theme_snapshot_validate(snapshot) != PXSYS_STATUS_OK ||
        role >= PXSYS_TYPOGRAPHY_ROLE_COUNT)
        return 0;
    return snapshot->typography_px[role];
}

pxsys_status_t pxsys_theme_snapshot_init_custom(pxsys_theme_snapshot_t* snapshot,
                                                pxsys_string_t theme_id,
                                                pxsys_color_scheme_t base_scheme) {
    if (snapshot == NULL || theme_id.data == NULL || theme_id.size == 0 ||
        theme_id.size > PXSYS_THEME_ID_MAX_BYTES ||
        base_scheme > PXSYS_COLOR_SCHEME_DARK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    pxsys_theme_snapshot_init(snapshot, base_scheme);
    snapshot->configured_mode = PXSYS_THEME_MODE_CUSTOM;
    snapshot->theme_id_size = (uint16_t)theme_id.size;
    memcpy(snapshot->theme_id, theme_id.data, theme_id.size);
    snapshot->theme_id[theme_id.size] = '\0';
    return PXSYS_STATUS_OK;
}

void pxsys_theme_service_config_init(pxsys_theme_service_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_theme_service_create(const pxsys_theme_service_config_t* config,
                                          const pxsys_theme_snapshot_t* initial,
                                          pxsys_theme_service_t** output) {
    pxsys_theme_service_t* service;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) || config->max_observers == 0 ||
        config->max_observers > SIZE_MAX / sizeof(theme_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        !snapshot_valid(initial)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    service = (pxsys_theme_service_t*)config->allocator.allocate(config->allocator.context,
                                                                 sizeof(*service));
    if (service == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (theme_observer_t*)config->allocator.allocate(
        config->allocator.context, config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0, config->max_observers * sizeof(*service->observers));
    service->observer_capacity = config->max_observers;
    service->allocator = config->allocator;
    service->current = *initial;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = 1;
    service->magic = PXSYS_THEME_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_destroy(pxsys_theme_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_update(pxsys_theme_service_t* service,
                                          const pxsys_theme_snapshot_t* snapshot) {
    size_t index;
    uint64_t generation;
    if (!service_valid(service) || !snapshot_valid(snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    generation = service->current.generation == UINT64_MAX ? 1 : service->current.generation + 1u;
    service->current = *snapshot;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = generation;
    service->notifying = 1;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback != NULL) {
            service->observers[index].callback(service->observers[index].context,
                                               &service->current);
        }
    }
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_get(const pxsys_theme_service_t* service,
                                       pxsys_theme_snapshot_t* snapshot) {
    if (!service_valid(service) || snapshot == NULL || snapshot->struct_size < sizeof(*snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *snapshot = service->current;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_subscribe(pxsys_theme_service_t* service, void* context,
                                             pxsys_theme_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            return PXSYS_STATUS_ALREADY_EXISTS;
        }
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL)
            break;
    }
    service->observers[index].context = context;
    service->observers[index].callback = callback;
    service->observer_count++;
    service->notifying = 1;
    callback(context, &service->current);
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_unsubscribe(pxsys_theme_service_t* service, void* context,
                                               pxsys_theme_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            memset(&service->observers[index], 0, sizeof(service->observers[index]));
            service->observer_count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}
