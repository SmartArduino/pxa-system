#include <assert.h>
#include <stdlib.h>

#include "pxsys/display.h"

static void* allocate(void* context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void changed(void* context, const pxsys_display_profile_t* profile) {
    unsigned* count = (unsigned*)context;
    assert(profile->generation != 0);
    (*count)++;
}

int main(void) {
    pxsys_display_profile_t profile;
    pxsys_rect_t safe;
    pxsys_display_service_config_t config;
    pxsys_display_service_t* service = NULL;
    unsigned notifications = 0;

    pxsys_display_profile_init(&profile, 320, 240);
    profile.shape = PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
    profile.corner_radii = (pxsys_corner_radii_t){32, 32, 32, 32};
    profile.safe_insets = (pxsys_insets_t){8, 10, 12, 10};
    profile.cutout_count = 1;
    profile.cutouts[0].struct_size = sizeof(profile.cutouts[0]);
    profile.cutouts[0].shape = PXSYS_CUTOUT_ROUNDED_RECTANGLE;
    profile.cutouts[0].bounds = (pxsys_rect_t){120, 0, 80, 20};
    profile.cutouts[0].radius = 10;
    assert(pxsys_display_profile_validate(&profile) == PXSYS_STATUS_OK);
    assert(pxsys_display_safe_rect(&profile, &safe) == PXSYS_STATUS_OK);
    assert(safe.x == 10 && safe.y == 8 && safe.width == 300 && safe.height == 220);
    assert(!pxsys_display_contains_point(&profile, 0, 0));
    assert(!pxsys_display_contains_point(&profile, 140, 10));
    assert(pxsys_display_contains_point(&profile, 120, 0));
    assert(pxsys_display_contains_point(&profile, 160, 100));

    pxsys_display_service_config_init(&config);
    config.allocator.struct_size = sizeof(config.allocator);
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_display_service_create(&config, &profile, &service) == PXSYS_STATUS_OK);
    profile.struct_size = sizeof(profile);
    assert(pxsys_display_service_get(service, &profile) == PXSYS_STATUS_OK);
    assert(pxsys_display_service_subscribe(service, &notifications, changed) == PXSYS_STATUS_OK);
    profile.width = 400;
    assert(pxsys_display_service_update(service, &profile) == PXSYS_STATUS_OK);
    assert(notifications == 1);
    assert(pxsys_display_service_unsubscribe(service, &notifications, changed) == PXSYS_STATUS_OK);
    assert(pxsys_display_service_destroy(service) == PXSYS_STATUS_OK);
    return 0;
}
