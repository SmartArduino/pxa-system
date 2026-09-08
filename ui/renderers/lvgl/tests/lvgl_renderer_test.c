#include <assert.h>
#include <stdlib.h>

#include "pxsys/lvgl_renderer.h"
#include "pxsys/renderer_host.h"

typedef struct {
    size_t allocations;
    size_t applied;
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

static pxsys_status_t apply_transaction(void* context, lv_obj_t* root,
                                        const pxsys_ui_transaction_t* transaction) {
    fixture_t* fixture = (fixture_t*)context;
    if (root == NULL || transaction->commands.size != 1 ||
        transaction->commands.data[0] != 7) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    fixture->applied++;
    return PXSYS_STATUS_OK;
}

int main(void) {
    fixture_t fixture = {0};
    lv_obj_t parent = {0};
    pxsys_lvgl_renderer_config_t renderer_config;
    pxsys_renderer_host_config_t host_config;
    pxsys_lvgl_renderer_t* renderer = NULL;
    pxsys_renderer_host_t* host = NULL;
    pxsys_renderer_provider_t provider;
    pxsys_surface_config_t surface_config = {0};
    pxsys_surface_ref_t surface;
    pxsys_theme_snapshot_t dark;
    pxsys_ui_transaction_t transaction = {0};
    lv_obj_t* root = NULL;
    const uint8_t command = 7;

    pxsys_lvgl_renderer_config_init(&renderer_config);
    renderer_config.max_surfaces = 2;
    renderer_config.parent = &parent;
    renderer_config.transaction_context = &fixture;
    renderer_config.apply_transaction = apply_transaction;
    renderer_config.allocator.context = &fixture;
    renderer_config.allocator.allocate = allocate;
    renderer_config.allocator.release = release;
    assert(pxsys_lvgl_renderer_create(&renderer_config, &renderer) == PXSYS_STATUS_OK);
    assert(pxsys_lvgl_renderer_provider(renderer, &provider) == PXSYS_STATUS_OK);

    pxsys_renderer_host_config_init(&host_config);
    host_config.allocator = renderer_config.allocator;
    assert(pxsys_renderer_host_create(&host_config, &host) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_bind(host, &provider) == PXSYS_STATUS_OK);

    surface_config.struct_size = sizeof(surface_config);
    surface_config.width = 320;
    surface_config.height = 240;
    surface_config.role = PXSYS_SURFACE_APPLICATION;
    assert(pxsys_renderer_surface_create(host, &surface_config, &surface) == PXSYS_STATUS_OK);
    assert(pxsys_lvgl_renderer_surface_root(renderer, surface, &root) == PXSYS_STATUS_OK);
    assert(root != NULL && root->parent == &parent && root->width == 320 && root->height == 240);
    assert((root->flags & LV_OBJ_FLAG_HIDDEN) != 0);
    assert(root->background == UINT32_C(0xf7f7f8));

    assert(pxsys_renderer_surface_set_visible(host, surface, 1) == PXSYS_STATUS_OK);
    assert((root->flags & LV_OBJ_FLAG_HIDDEN) == 0);
    transaction.struct_size = sizeof(transaction);
    transaction.transaction_id = 4;
    transaction.commands.data = &command;
    transaction.commands.size = 1;
    assert(pxsys_renderer_apply(host, surface, &transaction) == PXSYS_STATUS_OK);
    assert(fixture.applied == 1);

    pxsys_theme_snapshot_init(&dark, PXSYS_COLOR_SCHEME_DARK);
    assert(pxsys_renderer_host_set_theme(host, &dark) == PXSYS_STATUS_OK);
    assert(root->background == UINT32_C(0x111214));
    assert(root->text == UINT32_C(0xf1f1f2));
    assert(pxsys_lvgl_renderer_destroy(renderer) == PXSYS_STATUS_BUSY);

    assert(pxsys_renderer_surface_destroy(host, surface) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_unbind(host, provider.renderer_id) == PXSYS_STATUS_OK);
    assert(pxsys_renderer_host_destroy(host) == PXSYS_STATUS_OK);
    assert(pxsys_lvgl_renderer_destroy(renderer) == PXSYS_STATUS_OK);
    assert(fixture.allocations == 0);
    return 0;
}
