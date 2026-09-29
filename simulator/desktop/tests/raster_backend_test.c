#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include "../../../libpxa/tests/raster_snapshot_scenario.h"

static void present_snapshot(void *context, uint64_t id, uint16_t color) {
    product_host_t *host = context;
    (void)id;
    assert(surface_process_pending(host));
    for (unsigned i = 0; i < 16; ++i)
        assert(((const uint16_t *)host->surface_display_buffer)[i] == color);
}

int main(void) {
    product_host_t *host = calloc(1, sizeof(*host));
    pxa_game_render_backend_t backend = {0};
    assert(host != NULL);
    assert(resource_memory_init(host, 128 * 1024, 2 * 1024 * 1024, 16 * 1024, 512 * 1024) == 0);
    lv_init();
    host->display = lv_display_create(4, 4);
    assert(host->display != NULL);
    host->width = host->height = 4;
    host->content_parent = lv_display_get_screen_active(host->display);
    backend.context = host;
    backend.create = game_render_create;
    backend.upload = surface_raster_upload;
    backend.submit = surface_raster_submit;
    backend.bind_assets = surface_raster_bind_assets;
    backend.close = surface_close;
    raster_snapshot_scenario(&backend, present_snapshot, host);
    assert(host->raster_bindings.palette == NULL);
    assert(host->raster_frame_bindings[0].palette == NULL);
    assert(host->raster_frame_bindings[1].palette == NULL);
    lv_display_delete(host->display);
    assert(resource_memory_end(host) == 0);
    free(host);
    puts("desktop raster: immutable frame snapshots, replacement and close passed");
    return 0;
}
