/* Generic signed-app startup and lifecycle smoke, on the actual product Host.
 * App-specific tests cover interaction and rendering correctness separately. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    const char *output;
    uint64_t started, paused_at, visible_before, submitted_before;
    unsigned stage, density_dpi;
} matrix_probe_t;

static void capture_display(const matrix_probe_t *probe) {
    lv_display_t *display = lv_display_get_default();
    assert(display != NULL);
    lv_refr_now(display);
    const lv_draw_buf_t *buffer = lv_display_get_buf_active(display);
    const unsigned width = lv_display_get_horizontal_resolution(display);
    const unsigned height = lv_display_get_vertical_resolution(display);
    const unsigned stride = lv_draw_buf_width_to_stride(width, lv_display_get_color_format(display));
    assert(buffer && buffer->data && buffer->data_size >= (size_t)stride * height);
    // Test-only artifact allocation, after the runtime memory sample.
    uint8_t *row = malloc((size_t)width * 3);
    assert(row != NULL);
    char path[2048];
    assert(snprintf(path, sizeof(path), "%s.ppm", probe->output) < (int)sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file != NULL && fprintf(file, "P6\n%u %u\n255\n", width, height) > 0);
    for (unsigned y = 0; y < height; ++y) {
        assert(SDL_ConvertPixels(width, 1, SDL_PIXELFORMAT_RGB888,
            buffer->data + (size_t)y * stride, stride, SDL_PIXELFORMAT_RGB24, row, width * 3) == 0);
        assert(fwrite(row, 3, width, file) == width);
    }
    assert(fclose(file) == 0);
    free(row);
}

static void bind_probe(void *context, void (*focus)(void *, bool),
                       void (*exit_app)(void *), void *runner) {
    (void)exit_app;
    ((matrix_probe_t *)context)->host = runner;
    if (runner != NULL) focus(runner, true);
}

static void pump_probe(void *context) {
    matrix_probe_t *probe = context;
    product_host_t *host = probe->host;
    if (host == NULL) return;
    pxa_component_snapshot_t component = {0};
    assert(pxa_component_snapshot(host->runtime, host->active_component, &component) == 0);
    assert(component.state == PXA_COMPONENT_RUNNING);
    const uint64_t now = now_us(NULL);
    assert(now - probe->started < UINT64_C(45000000));
    if (probe->stage == 0 && now - probe->started > UINT64_C(2500000)) {
        probe->visible_before = host->raster_telemetry.visible_frames;
        product_focus_changed(host, false);
        dispatch_lifecycle(host);
        probe->submitted_before = host->surface_submitted_frames;
        probe->paused_at = now;
        probe->stage = 1;
    } else if (probe->stage == 1 && now - probe->paused_at > UINT64_C(200000)) {
        assert(host->raster_telemetry.visible_frames == probe->visible_before);
        assert(host->surface_submitted_frames == probe->submitted_before);
        product_focus_changed(host, true);
        dispatch_lifecycle(host);
        probe->stage = 2;
    } else if (probe->stage == 2 && now - probe->paused_at > UINT64_C(1000000)) {
        pxa_wamr_memory_snapshot_t memory = {0};
        assert(pxa_wamr_engine_memory_snapshot(host->engine, &memory) == 0);
        char path[2048];
        assert(snprintf(path, sizeof(path), "%s.json", probe->output) < (int)sizeof(path));
        FILE *file = fopen(path, "w");
        assert(file != NULL);
        if (host->surface_registered)
            assert(host->surface_presented_frames != 0);
        fprintf(file, "{\"width\":%u,\"height\":%u,\"density_dpi\":%u,"
                      "\"surface_submitted\":%llu,\"surface_presented\":%llu,"
                      "\"rendered\":%llu,\"visible\":%llu,\"dropped\":%llu,"
                      "\"wamr\":%u,\"wamr_peak\":%u,\"linear\":%llu,"
                      "\"linear_peak\":%llu,\"lifecycle_passed\":true}\n",
                host->width, host->height, probe->density_dpi,
                (unsigned long long)host->surface_submitted_frames,
                (unsigned long long)host->surface_presented_frames,
                (unsigned long long)host->raster_telemetry.rendered_frames,
                (unsigned long long)host->raster_telemetry.visible_frames,
                (unsigned long long)host->raster_telemetry.dropped_frames,
                memory.current_bytes, memory.peak_bytes,
                (unsigned long long)memory.linear_current_bytes,
                (unsigned long long)memory.linear_peak_bytes);
        assert(fclose(file) == 0);
        if (host->surface_display_buffer != NULL) {
            assert(snprintf(path, sizeof(path), "%s.rgb565", probe->output) < (int)sizeof(path));
            file = fopen(path, "wb");
            assert(file != NULL);
            assert(fwrite(host->surface_display_buffer, 1, host->surface_display_frame_bytes, file)
                   == host->surface_display_frame_bytes);
            assert(fclose(file) == 0);
        }
        capture_display(probe);
        probe->stage = 3;
        host->exit_requested = 1;
    }
}

int main(int argc, char **argv) {
    assert(argc == 9);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    options_t options = {0};
    options.package_path = argv[1];
    options.publisher_key = argv[2];
    options.state_root = argv[3];
    options.width = (uint32_t)strtoul(argv[4], NULL, 10);
    options.height = (uint32_t)strtoul(argv[5], NULL, 10);
    options.density_dpi = (uint32_t)strtoul(argv[6], NULL, 10);
    options.display_shape = (uint8_t)strtoul(argv[7], NULL, 10);
    options.locale = "zh-CN";
    options.corner_radius = options.display_shape == PXA_UI_DISPLAY_SHAPE_ROUNDED_RECTANGLE ? 24 : 0;
    for (unsigned i = 0; i < 4; ++i)
        options.safe_insets[i] = options.display_shape == PXA_UI_DISPLAY_SHAPE_CIRCLE ? 26 : 8;
    matrix_probe_t probe = {.output = argv[8], .started = now_us(NULL),
                             .density_dpi = options.density_dpi};
    // Exercise the embedded product Host used by the system shell. Standalone
    // mode intentionally lacks system text-input control and cannot activate
    // applications which require that capability (Reader / system-input).
    lv_init();
    lv_display_t *display = lv_sdl_window_create(options.width, options.height);
    assert(display != NULL);
    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_t *keyboard = lv_sdl_keyboard_create();
    assert(mouse != NULL && keyboard != NULL);
    lv_indev_set_display(mouse, display);
    lv_indev_set_display(keyboard, display);
    shape_mask_image_t shape_mask = {0};
    assert(install_shape_mask(display, &options, &shape_mask));
    const int result = run_product_simulator(&options, display, NULL, pump_probe, &probe,
                                             NULL, &probe, bind_probe, NULL, NULL, NULL);
    assert(result == 0 && probe.stage == 3 && probe.host == NULL);
    lv_display_delete(display);
    lv_sdl_quit();
    lv_deinit();
    free(shape_mask.pixels);
    puts("Signed app: startup, background/resume and shutdown passed");
    return 0;
}
