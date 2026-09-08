#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pxsys/headless_renderer.h"
#include "pxsys/renderer_host.h"

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

int main(void) {
    static const uint8_t commands[] = {0x01, 0x02, 0x03};
    size_t allocations = 0;
    pxsys_headless_renderer_config_t config;
    pxsys_headless_renderer_t* renderer = NULL;
    pxsys_renderer_host_config_t host_config;
    pxsys_renderer_host_t* host = NULL;
    pxsys_renderer_provider_t provider;
    pxsys_surface_config_t surface_config = {0};
    pxsys_surface_ref_t surface;
    pxsys_ui_transaction_t transaction = {0};
    pxsys_headless_surface_snapshot_t snapshot = {0};

    pxsys_headless_renderer_config_init(&config);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_headless_renderer_create(&config, &renderer) == PXSYS_STATUS_OK);
    assert(pxsys_headless_renderer_provider(renderer, &provider) == PXSYS_STATUS_OK);
    pxsys_renderer_host_config_init(&host_config);
    host_config.allocator.context = &allocations;
    host_config.allocator.allocate = allocate;
    host_config.allocator.release = release;
    assert(pxsys_renderer_host_create(&host_config, &host) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_bind(host, &provider) == PXSYS_STATUS_OK);
    surface_config.struct_size = sizeof(surface_config);
    surface_config.width = 320;
    surface_config.height = 240;
    surface_config.role = PXSYS_SURFACE_APPLICATION;
    assert(pxsys_renderer_surface_create(host, &surface_config, &surface) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_surface_set_visible(host, surface, 1) == PXSYS_STATUS_OK);
    transaction.struct_size = sizeof(transaction);
    transaction.transaction_id = 9;
    transaction.commands = pxsys_bytes(commands, sizeof(commands));
    assert(pxsys_renderer_apply(host, surface, &transaction) == PXSYS_STATUS_OK);
    snapshot.struct_size = sizeof(snapshot);
    assert(pxsys_headless_renderer_snapshot(renderer, surface, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.config.width == 320 && snapshot.visible && snapshot.last_transaction_id == 9 &&
           snapshot.command_bytes == sizeof(commands));
    assert(pxsys_headless_renderer_destroy(renderer) == PXSYS_STATUS_BUSY);
    assert(pxsys_renderer_surface_destroy(host, surface) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_unbind(host, provider.renderer_id) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_destroy(host) == PXSYS_STATUS_OK);
    assert(pxsys_headless_renderer_destroy(renderer) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("headless renderer tests passed");
    return 0;
}
