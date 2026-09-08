#include "pxsys/display.h"

#include <limits.h>
#include <string.h>

#define PXSYS_DISPLAY_MAGIC UINT32_C(0x50584450)

typedef struct {
    void* context;
    pxsys_display_changed_fn callback;
} display_observer_t;

struct pxsys_display_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    uint8_t notifying;
    pxsys_allocator_t allocator;
    pxsys_display_profile_t current;
    display_observer_t* observers;
};

static int service_valid(const pxsys_display_service_t* service) {
    return service != NULL && service->magic == PXSYS_DISPLAY_MAGIC;
}

void pxsys_display_profile_init(pxsys_display_profile_t* profile,
                                uint32_t width, uint32_t height) {
    if (profile == NULL) return;
    memset(profile, 0, sizeof(*profile));
    profile->struct_size = sizeof(*profile);
    profile->width = width;
    profile->height = height;
    profile->density_dpi = 160;
    profile->refresh_rate_hz = 60;
    profile->shape = PXSYS_DISPLAY_SHAPE_RECTANGLE;
}

static int rect_valid(const pxsys_display_profile_t* profile,
                      const pxsys_rect_t* rect) {
    uint64_t right;
    uint64_t bottom;
    if (rect->x < 0 || rect->y < 0 || rect->width == 0 || rect->height == 0)
        return 0;
    right = (uint64_t)(uint32_t)rect->x + rect->width;
    bottom = (uint64_t)(uint32_t)rect->y + rect->height;
    return right <= profile->width && bottom <= profile->height;
}

pxsys_status_t pxsys_display_profile_validate(const pxsys_display_profile_t* profile) {
    size_t index;
    uint32_t maximum_radius;
    if (profile == NULL || profile->struct_size < sizeof(*profile) ||
        profile->width == 0 || profile->height == 0 ||
        profile->width > INT32_MAX || profile->height > INT32_MAX ||
        profile->shape > PXSYS_DISPLAY_SHAPE_CUSTOM ||
        profile->cutout_count > PXSYS_DISPLAY_MAX_CUTOUTS ||
        (uint32_t)profile->safe_insets.left + profile->safe_insets.right >= profile->width ||
        (uint32_t)profile->safe_insets.top + profile->safe_insets.bottom >= profile->height) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    maximum_radius = (profile->width < profile->height ? profile->width
                                                       : profile->height) / 2u;
    if (profile->corner_radii.top_left > maximum_radius ||
        profile->corner_radii.top_right > maximum_radius ||
        profile->corner_radii.bottom_right > maximum_radius ||
        profile->corner_radii.bottom_left > maximum_radius)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < profile->cutout_count; ++index) {
        const pxsys_display_cutout_t* cutout = &profile->cutouts[index];
        if (cutout->struct_size < sizeof(*cutout) ||
            cutout->shape > PXSYS_CUTOUT_ELLIPSE ||
            !rect_valid(profile, &cutout->bounds) ||
            (cutout->shape == PXSYS_CUTOUT_ROUNDED_RECTANGLE &&
             (uint32_t)cutout->radius * 2u >
                 (cutout->bounds.width < cutout->bounds.height
                      ? cutout->bounds.width : cutout->bounds.height))) {
            return PXSYS_STATUS_INVALID_ARGUMENT;
        }
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_display_safe_rect(const pxsys_display_profile_t* profile,
                                       pxsys_rect_t* output) {
    if (output == NULL || pxsys_display_profile_validate(profile) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    output->x = profile->safe_insets.left;
    output->y = profile->safe_insets.top;
    output->width = profile->width - profile->safe_insets.left -
                    profile->safe_insets.right;
    output->height = profile->height - profile->safe_insets.top -
                     profile->safe_insets.bottom;
    return PXSYS_STATUS_OK;
}

static int outside_rounded_corner(int32_t x, int32_t y, int32_t width,
                                  int32_t height, int32_t radius,
                                  int32_t corner_x, int32_t corner_y) {
    int64_t dx;
    int64_t dy;
    if (radius <= 0) return 0;
    if ((corner_x == 0 && x >= radius) ||
        (corner_x != 0 && x < width - radius) ||
        (corner_y == 0 && y >= radius) ||
        (corner_y != 0 && y < height - radius)) return 0;
    dx = x - (corner_x == 0 ? radius : width - radius - 1);
    dy = y - (corner_y == 0 ? radius : height - radius - 1);
    return dx * dx + dy * dy > (int64_t)radius * radius;
}

static int cutout_contains(const pxsys_display_cutout_t* cutout,
                           int32_t x, int32_t y) {
    const pxsys_rect_t* bounds = &cutout->bounds;
    int32_t local_x;
    int32_t local_y;
    int64_t dx;
    int64_t dy;
    if (x < bounds->x || y < bounds->y ||
        x >= bounds->x + (int32_t)bounds->width ||
        y >= bounds->y + (int32_t)bounds->height) return 0;
    if (cutout->shape == PXSYS_CUTOUT_ROUNDED_RECTANGLE) {
        local_x = x - bounds->x;
        local_y = y - bounds->y;
        return !outside_rounded_corner(local_x, local_y, (int32_t)bounds->width,
                                       (int32_t)bounds->height, cutout->radius, 0, 0) &&
               !outside_rounded_corner(local_x, local_y, (int32_t)bounds->width,
                                       (int32_t)bounds->height, cutout->radius, 1, 0) &&
               !outside_rounded_corner(local_x, local_y, (int32_t)bounds->width,
                                       (int32_t)bounds->height, cutout->radius, 1, 1) &&
               !outside_rounded_corner(local_x, local_y, (int32_t)bounds->width,
                                       (int32_t)bounds->height, cutout->radius, 0, 1);
    }
    if (cutout->shape != PXSYS_CUTOUT_ELLIPSE) return 1;
    dx = (int64_t)(2 * (x - bounds->x) + 1) - bounds->width;
    dy = (int64_t)(2 * (y - bounds->y) + 1) - bounds->height;
    return dx * dx * bounds->height * bounds->height +
               dy * dy * bounds->width * bounds->width <=
           (int64_t)bounds->width * bounds->width * bounds->height * bounds->height;
}

int pxsys_display_contains_point(const pxsys_display_profile_t* profile,
                                 int32_t x, int32_t y) {
    size_t index;
    int32_t width;
    int32_t height;
    if (pxsys_display_profile_validate(profile) != PXSYS_STATUS_OK ||
        x < 0 || y < 0 || x >= (int32_t)profile->width ||
        y >= (int32_t)profile->height) return 0;
    width = (int32_t)profile->width;
    height = (int32_t)profile->height;
    if (profile->shape == PXSYS_DISPLAY_SHAPE_CIRCLE) {
        int64_t dx = 2 * (int64_t)x + 1 - width;
        int64_t dy = 2 * (int64_t)y + 1 - height;
        int32_t diameter = width < height ? width : height;
        if (dx * dx + dy * dy > (int64_t)diameter * diameter) return 0;
    } else if (profile->shape == PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE) {
        if (outside_rounded_corner(x, y, width, height,
                                   profile->corner_radii.top_left, 0, 0) ||
            outside_rounded_corner(x, y, width, height,
                                   profile->corner_radii.top_right, 1, 0) ||
            outside_rounded_corner(x, y, width, height,
                                   profile->corner_radii.bottom_right, 1, 1) ||
            outside_rounded_corner(x, y, width, height,
                                   profile->corner_radii.bottom_left, 0, 1)) return 0;
    }
    for (index = 0; index < profile->cutout_count; ++index) {
        if (cutout_contains(&profile->cutouts[index], x, y)) return 0;
    }
    return 1;
}

void pxsys_display_service_config_init(pxsys_display_service_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_display_service_create(
    const pxsys_display_service_config_t* config,
    const pxsys_display_profile_t* initial,
    pxsys_display_service_t** output) {
    pxsys_display_service_t* service;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_observers == 0 ||
        config->max_observers > SIZE_MAX / sizeof(display_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        pxsys_display_profile_validate(initial) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    service = (pxsys_display_service_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*service));
    if (service == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (display_observer_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0,
           config->max_observers * sizeof(*service->observers));
    service->observer_capacity = config->max_observers;
    service->allocator = config->allocator;
    service->current = *initial;
    service->current.struct_size = sizeof(service->current);
    service->magic = PXSYS_DISPLAY_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_display_service_destroy(pxsys_display_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_display_service_update(pxsys_display_service_t* service,
                                            const pxsys_display_profile_t* profile) {
    size_t index;
    if (!service_valid(service) || service->notifying ||
        pxsys_display_profile_validate(profile) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    service->current = *profile;
    service->current.struct_size = sizeof(service->current);
    service->current.generation++;
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

pxsys_status_t pxsys_display_service_get(const pxsys_display_service_t* service,
                                         pxsys_display_profile_t* profile) {
    if (!service_valid(service) || profile == NULL ||
        profile->struct_size < sizeof(*profile))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *profile = service->current;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_display_service_subscribe(pxsys_display_service_t* service,
                                               void* context,
                                               pxsys_display_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL || service->notifying)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context)
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL) {
            service->observers[index].context = context;
            service->observers[index].callback = callback;
            service->observer_count++;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_INTERNAL;
}

pxsys_status_t pxsys_display_service_unsubscribe(pxsys_display_service_t* service,
                                                 void* context,
                                                 pxsys_display_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL || service->notifying)
        return PXSYS_STATUS_INVALID_ARGUMENT;
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
