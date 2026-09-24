#ifndef PXSYS_DESKTOP_SIMULATOR_RUNTIME_H
#define PXSYS_DESKTOP_SIMULATOR_RUNTIME_H

#include "pxsys/lvgl_renderer.h"
#include "pxsys/pxa_runtime.h"
#include "pxsys/standard_system.h"

typedef struct pxsys_desktop_runtime pxsys_desktop_runtime_t;

typedef struct {
    uint8_t permission_allowed;
    uint32_t storage_bytes;
    const char *installed_packages_root;
    const char *publisher_key;
    const char *state_root;
    uint8_t navigation_gestures;
    void *pump_context;
    void (*pump)(void *context);
} pxsys_desktop_runtime_fixture_t;

pxsys_status_t pxsys_desktop_runtime_create(
    pxsys_standard_system_t* system, pxsys_lvgl_renderer_t* renderer,
    const pxsys_desktop_runtime_fixture_t* fixture,
    pxsys_allocator_t allocator, pxsys_desktop_runtime_t** output);
pxsys_status_t pxsys_desktop_runtime_provider(
    pxsys_desktop_runtime_t* runtime, pxsys_runtime_provider_t* output);
void pxsys_desktop_runtime_poll(pxsys_desktop_runtime_t* runtime);
pxsys_status_t pxsys_desktop_runtime_destroy(pxsys_desktop_runtime_t* runtime);

#endif
