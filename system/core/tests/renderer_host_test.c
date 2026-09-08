#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/renderer_host.h"

typedef struct {
    size_t allocations;
    size_t surfaces;
    size_t theme_changes;
    pxsys_theme_snapshot_t theme;
} fixture_t;

static void* allocate(void* context, size_t size) {
    fixture_t* fixture = (fixture_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        fixture->allocations++;
    return memory;
}

static void release(void* context, void* memory) {
    fixture_t* fixture = (fixture_t*)context;
    if (memory != NULL)
        fixture->allocations--;
    free(memory);
}

static pxsys_status_t surface_create(void* context, const pxsys_surface_config_t* config,
                                     pxsys_surface_ref_t* surface) {
    fixture_t* fixture = (fixture_t*)context;
    if (config == NULL || config->struct_size < sizeof(*config) || surface == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    fixture->surfaces++;
    surface->slot = 4;
    surface->generation = 7;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_destroy(void* context, pxsys_surface_ref_t surface) {
    fixture_t* fixture = (fixture_t*)context;
    if (surface.slot != 4 || surface.generation != 7 || fixture->surfaces == 0)
        return PXSYS_STATUS_NOT_FOUND;
    fixture->surfaces--;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_visible(void* context, pxsys_surface_ref_t surface, int visible) {
    (void)context;
    return surface.slot == 4 && surface.generation == 7 && visible
               ? PXSYS_STATUS_OK
               : PXSYS_STATUS_INVALID_ARGUMENT;
}

static pxsys_status_t apply(void* context, pxsys_surface_ref_t surface,
                            const pxsys_ui_transaction_t* transaction) {
    (void)context;
    return surface.slot == 4 && surface.generation == 7 && transaction != NULL &&
                   transaction->struct_size >= sizeof(*transaction)
               ? PXSYS_STATUS_OK
               : PXSYS_STATUS_INVALID_ARGUMENT;
}

static pxsys_status_t theme_changed(void* context, const pxsys_theme_snapshot_t* theme) {
    fixture_t* fixture = (fixture_t*)context;
    fixture->theme = *theme;
    fixture->theme_changes++;
    return PXSYS_STATUS_OK;
}

int main(void) {
    fixture_t fixture = {0};
    pxsys_renderer_host_config_t config;
    pxsys_renderer_host_t* host = NULL;
    pxsys_renderer_provider_t provider = {0};
    pxsys_renderer_info_t info = {0};
    pxsys_surface_config_t surface_config = {0};
    pxsys_ui_transaction_t transaction = {0};
    pxsys_surface_ref_t surface;
    pxsys_theme_snapshot_t dark;

    pxsys_renderer_host_config_init(&config);
    config.allocator.context = &fixture;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    config.max_renderer_id_bytes = SIZE_MAX;
    assert(pxsys_renderer_host_create(&config, &host) == PXSYS_STATUS_INVALID_ARGUMENT);
    config.max_renderer_id_bytes = 64;
    assert(pxsys_renderer_host_create(&config, &host) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_surface_create(host, &surface_config, &surface) ==
           PXSYS_STATUS_UNAVAILABLE);

    provider.struct_size = sizeof(provider);
    provider.renderer_id = pxsys_string_from_cstr("test-renderer");
    provider.version = (pxsys_version_t){1, 2};
    provider.features = 9;
    provider.context = &fixture;
    provider.surface_create = surface_create;
    provider.surface_destroy = surface_destroy;
    provider.surface_set_visible = surface_visible;
    provider.apply = apply;
    provider.theme_changed = theme_changed;
    assert(pxsys_renderer_host_bind(host, &provider) == PXSYS_STATUS_OK);
    assert(fixture.theme_changes == 1 &&
           fixture.theme.effective_scheme == PXSYS_COLOR_SCHEME_LIGHT);
    assert(pxsys_renderer_host_bind(host, &provider) == PXSYS_STATUS_ALREADY_EXISTS);

    info.struct_size = sizeof(info);
    assert(pxsys_renderer_host_info(host, &info) == PXSYS_STATUS_OK);
    assert(info.version.major == 1 && info.version.minor == 2 && info.features == 9 &&
           info.surface_count == 0 && info.renderer_id.size == strlen("test-renderer"));

    surface_config.struct_size = sizeof(surface_config);
    surface_config.width = 320;
    surface_config.height = 240;
    assert(pxsys_renderer_surface_create(host, &surface_config, &surface) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_unbind(host, provider.renderer_id) == PXSYS_STATUS_BUSY);
    assert(pxsys_renderer_surface_set_visible(host, surface, 1) == PXSYS_STATUS_OK);
    transaction.struct_size = sizeof(transaction);
    transaction.transaction_id = 1;
    assert(pxsys_renderer_apply(host, surface, &transaction) == PXSYS_STATUS_OK);

    pxsys_theme_snapshot_init(&dark, PXSYS_COLOR_SCHEME_DARK);
    assert(pxsys_renderer_host_set_theme(host, &dark) == PXSYS_STATUS_OK);
    assert(fixture.theme_changes == 2 &&
           fixture.theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK);
    assert(pxsys_renderer_host_destroy(host) == PXSYS_STATUS_BUSY);
    assert(pxsys_renderer_surface_destroy(host, surface) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_unbind(host, provider.renderer_id) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_info(host, &info) == PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_renderer_host_destroy(host) == PXSYS_STATUS_OK);
    assert(fixture.allocations == 0 && fixture.surfaces == 0);
    return 0;
}
