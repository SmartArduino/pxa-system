#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/theme.h"

typedef struct {
    size_t calls;
    uint64_t generation;
    pxsys_color_scheme_t scheme;
} observer_t;

static void* allocate(void* context, size_t size) {
    size_t* count = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*count)++;
    return memory;
}

static void release(void* context, void* memory) {
    size_t* count = (size_t*)context;
    if (memory != NULL)
        (*count)--;
    free(memory);
}

static void changed(void* context, const pxsys_theme_snapshot_t* snapshot) {
    observer_t* observer = (observer_t*)context;
    observer->calls++;
    observer->generation = snapshot->generation;
    observer->scheme = snapshot->effective_scheme;
}

static pxsys_theme_snapshot_t theme(pxsys_color_scheme_t scheme) {
    pxsys_theme_snapshot_t value = {0};
    pxsys_theme_snapshot_init(&value, scheme);
    value.colors[PXSYS_COLOR_BACKGROUND] =
        scheme == PXSYS_COLOR_SCHEME_DARK ? UINT32_C(0xff101010) : UINT32_C(0xfffafafa);
    return value;
}

int main(void) {
    size_t allocations = 0;
    pxsys_theme_service_config_t config;
    pxsys_theme_snapshot_t initial = theme(PXSYS_COLOR_SCHEME_LIGHT);
    pxsys_theme_snapshot_t dark = theme(PXSYS_COLOR_SCHEME_DARK);
    pxsys_theme_snapshot_t custom = {0};
    pxsys_theme_snapshot_t current = {0};
    pxsys_theme_service_t* service = NULL;
    observer_t observer = {0};

    pxsys_theme_service_config_init(&config);
    config.allocator.struct_size = sizeof(config.allocator);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_theme_service_create(&config, &initial, &service) == PXSYS_STATUS_OK);
    assert(pxsys_theme_service_subscribe(service, &observer, changed) == PXSYS_STATUS_OK);
    assert(observer.calls == 1 && observer.generation == 1 &&
           observer.scheme == PXSYS_COLOR_SCHEME_LIGHT);
    assert(pxsys_theme_typography_px(&initial, PXSYS_TYPOGRAPHY_DISPLAY) == 28);
    assert(pxsys_theme_typography_px(&initial, PXSYS_TYPOGRAPHY_CAPTION) == 12);
    assert(pxsys_theme_service_update(service, &dark) == PXSYS_STATUS_OK);
    assert(observer.calls == 2 && observer.generation == 2 &&
           observer.scheme == PXSYS_COLOR_SCHEME_DARK);
    current.struct_size = sizeof(current);
    assert(pxsys_theme_service_get(service, &current) == PXSYS_STATUS_OK);
    assert(current.generation == 2 &&
           current.colors[PXSYS_COLOR_BACKGROUND] == UINT32_C(0xff101010));
    assert(pxsys_theme_snapshot_init_custom(
               &custom, pxsys_string_from_cstr("vendor.ocean"),
               PXSYS_COLOR_SCHEME_DARK) == PXSYS_STATUS_OK);
    custom.colors[PXSYS_COLOR_ACCENT] = UINT32_C(0xff00a896);
    assert(pxsys_theme_service_update(service, &custom) == PXSYS_STATUS_OK);
    current.struct_size = sizeof(current);
    assert(pxsys_theme_service_get(service, &current) == PXSYS_STATUS_OK);
    assert(current.configured_mode == PXSYS_THEME_MODE_CUSTOM &&
           current.theme_id_size == strlen("vendor.ocean") &&
           strcmp(current.theme_id, "vendor.ocean") == 0 &&
           current.colors[PXSYS_COLOR_ACCENT] == UINT32_C(0xff00a896));
    assert(pxsys_theme_service_unsubscribe(service, &observer, changed) == PXSYS_STATUS_OK);
    assert(pxsys_theme_service_destroy(service) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("theme tests passed");
    return 0;
}
