#include <stdio.h>
#include <stdlib.h>

#include "pxsys/headless_renderer.h"
#include "pxsys/standard_system.h"

static void* host_allocate(void* context, size_t size) {
    size_t* allocations = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*allocations)++;
    return memory;
}

static void host_release(void* context, void* memory) {
    size_t* allocations = (size_t*)context;
    if (memory != NULL)
        (*allocations)--;
    free(memory);
}

int main(void) {
    size_t allocations = 0;
    pxsys_allocator_t allocator = {0};
    pxsys_headless_renderer_config_t renderer_config;
    pxsys_headless_renderer_t* renderer = NULL;
    pxsys_renderer_provider_t renderer_provider;
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_t* system = NULL;
    pxsys_theme_snapshot_t theme;
    pxsys_status_t status;

    allocator.struct_size = sizeof(allocator);
    allocator.context = &allocations;
    allocator.allocate = host_allocate;
    allocator.release = host_release;

    pxsys_headless_renderer_config_init(&renderer_config);
    renderer_config.allocator = allocator;
    status = pxsys_headless_renderer_create(&renderer_config, &renderer);
    if (status != PXSYS_STATUS_OK)
        return 1;
    status = pxsys_headless_renderer_provider(renderer, &renderer_provider);
    if (status != PXSYS_STATUS_OK)
        return 2;

    pxsys_standard_system_config_init(&system_config);
    system_config.allocator = allocator;
    system_config.initial_renderer = &renderer_provider;
    status = pxsys_standard_system_create(&system_config, &system);
    if (status != PXSYS_STATUS_OK)
        return 3;

    pxsys_theme_snapshot_init(&theme, PXSYS_COLOR_SCHEME_DARK);
    status = pxsys_theme_service_update(pxsys_standard_system_theme(system), &theme);
    if (status != PXSYS_STATUS_OK)
        return 4;
    status = pxsys_theme_service_get(pxsys_standard_system_theme(system), &theme);
    if (status != PXSYS_STATUS_OK || theme.effective_scheme != PXSYS_COLOR_SCHEME_DARK)
        return 5;

    printf("PXA System simulator: renderer=headless theme=dark generation=%llu\n",
           (unsigned long long)theme.generation);

    if (pxsys_standard_system_destroy(system) != PXSYS_STATUS_OK)
        return 6;
    if (pxsys_headless_renderer_destroy(renderer) != PXSYS_STATUS_OK)
        return 7;
    if (allocations != 0)
        return 8;
    return 0;
}
