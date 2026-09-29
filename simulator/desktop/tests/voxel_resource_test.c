/* Capture signed voxel AOT menus and optional real gameplay frames. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <malloc.h>
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#define PXSYS_CLOCK_NOW_US() UINT64_C(0x5eed1234)
static void voxel_frame_observe(void *, void *, uint64_t, uint64_t);
#define PXSYS_PRODUCT_FRAME_OBSERVER(context, host, frame, cpu_us) \
    voxel_frame_observe((context), (host), (frame), (cpu_us))
#include "../product_runner.c"
#include <assert.h>
#include <sys/stat.h>

typedef struct {
    uint64_t elapsed_us;
    uint32_t wamr_current;
    uint32_t wamr_peak;
    uint64_t linear_current;
    uint64_t linear_peak;
    uint64_t artifact_buffers;
    size_t shared_internal;
    size_t shared_external;
    size_t cache_internal;
    size_t cache_external;
    size_t worker_metadata;
    size_t malloc_current;
    size_t malloc_mmap;
} memory_sample_t;

typedef struct {
    product_host_t *host;
    const char *directory;
    uint64_t started;
    unsigned captured;
    unsigned gameplay;
    unsigned timeline;
    uint64_t last_rendered;
    unsigned sample_count;
    uint32_t raster_us[60];
    uint32_t frame_cpu_us[60];
    memory_sample_t loading_samples[256];
    unsigned loading_count;
    uint64_t last_loading_us;
    memory_sample_t memory_samples[61];
} capture_t;

static void capture_file(capture_t *test, const char *name,
                          const void *data, size_t bytes) {
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/%s", test->directory, name) > 0);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, bytes, file) == bytes);
    assert(!fclose(file));
}
static void bind_capture(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    (void)focus; (void)exit_app;
    ((capture_t *)context)->host = runner;
}
static int compare_u32(const void *left, const void *right) {
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}
static void read_memory_sample(capture_t *test, product_host_t *host,
                               memory_sample_t *sample) {
    pxa_wamr_memory_snapshot_t wamr;
    pxa_memory_stats_t budget;
    pxa_asset_cache_stats_t cache = {0};
    size_t worker_metadata = 0;
    const struct mallinfo2 heap = mallinfo2();
    assert(pxa_wamr_engine_memory_snapshot(host->engine, &wamr) == 0);
    assert(pxa_memory_budget_stats(&host->resource_budget, 0, &budget) == PXA_STATUS_OK);
    if (host->asset_worker)
        pxa_posix_asset_worker_stats(host->asset_worker, &cache, &worker_metadata);
    sample->elapsed_us = now_us(NULL) - test->started;
    sample->wamr_current = wamr.current_bytes;
    sample->wamr_peak = wamr.peak_bytes;
    sample->linear_current = wamr.linear_current_bytes;
    sample->linear_peak = wamr.linear_peak_bytes;
    sample->artifact_buffers = wamr.artifact_buffer_bytes;
    sample->shared_internal = budget.charged[0];
    sample->shared_external = budget.charged[1];
    sample->cache_internal = cache.charged[0];
    sample->cache_external = cache.charged[1];
    sample->worker_metadata = worker_metadata;
    sample->malloc_current = heap.uordblks;
    sample->malloc_mmap = heap.hblkhd;
}
static void capture_memory_sample(capture_t *test, product_host_t *host,
                                  unsigned index) {
    assert(index <= 60);
    read_memory_sample(test, host, &test->memory_samples[index]);
}
static void write_memory_timeline(capture_t *test) {
    char rows[65536];
    size_t used = 0;
    int count = snprintf(rows, sizeof(rows),
        "phase,frame,elapsed_us,wamr_current,wamr_peak,linear_current,linear_peak,artifact_buffers,"
        "shared_internal,shared_external,cache_internal,cache_external,"
        "worker_metadata,process_malloc_current,process_malloc_mmap\n");
    assert(count > 0 && (size_t)count < sizeof(rows));
    used = (size_t)count;
    for (unsigned i = 0; i < test->loading_count; ++i) {
        const memory_sample_t *sample = &test->loading_samples[i];
        count = snprintf(rows + used, sizeof(rows) - used,
            "loading,-2,%llu,%u,%u,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
            (unsigned long long)sample->elapsed_us,
            sample->wamr_current, sample->wamr_peak,
            (unsigned long long)sample->linear_current,
            (unsigned long long)sample->linear_peak,
            (unsigned long long)sample->artifact_buffers,
            sample->shared_internal, sample->shared_external,
            sample->cache_internal, sample->cache_external,
            sample->worker_metadata, sample->malloc_current,
            sample->malloc_mmap);
        assert(count > 0 && (size_t)count < sizeof(rows) - used);
        used += (size_t)count;
    }
    for (unsigned i = 0; i <= test->sample_count; ++i) {
        const memory_sample_t *sample = &test->memory_samples[i];
        count = snprintf(rows + used, sizeof(rows) - used,
            "%s,%d,%llu,%u,%u,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
            i ? "gameplay" : "menu", (int)i - 1,
            (unsigned long long)sample->elapsed_us,
            sample->wamr_current, sample->wamr_peak,
            (unsigned long long)sample->linear_current,
            (unsigned long long)sample->linear_peak,
            (unsigned long long)sample->artifact_buffers,
            sample->shared_internal, sample->shared_external,
            sample->cache_internal, sample->cache_external,
            sample->worker_metadata, sample->malloc_current,
            sample->malloc_mmap);
        assert(count > 0 && (size_t)count < sizeof(rows) - used);
        used += (size_t)count;
    }
    capture_file(test, "gameplay-memory-timeline.csv", rows, used);
}
static void capture_gameplay(capture_t *test, product_host_t *host) {
    pxa_wamr_memory_snapshot_t memory;
    pxa_memory_stats_t budget;
    pxa_asset_cache_stats_t cache = {0};
    const struct mallinfo2 process_heap = mallinfo2();
    size_t worker_metadata = 0;
    char report[2048];
    char samples[4096];
    size_t sample_length = 0;
    int length;
    assert(host->surface_display_buffer && host->surface_display_frame_bytes);
    assert(pxa_wamr_engine_memory_snapshot(host->engine, &memory) == 0);
    assert(pxa_memory_budget_stats(&host->resource_budget, 0, &budget) == PXA_STATUS_OK);
    if (host->asset_worker)
        pxa_posix_asset_worker_stats(host->asset_worker, &cache, &worker_metadata);
    for (unsigned i = 0; i < test->sample_count; ++i) {
        length = snprintf(samples + sample_length, sizeof(samples) - sample_length,
                          "%u,%u,%u\n", i, test->raster_us[i],
                          test->frame_cpu_us[i]);
        assert(length > 0 && (size_t)length < sizeof(samples) - sample_length);
        sample_length += (size_t)length;
    }
    capture_file(test, "gameplay-frames.csv", samples, sample_length);
    if (test->timeline) write_memory_timeline(test);
    qsort(test->raster_us, test->sample_count, sizeof(test->raster_us[0]), compare_u32);
    qsort(test->frame_cpu_us, test->sample_count, sizeof(test->frame_cpu_us[0]), compare_u32);
    capture_file(test, "gameplay.rgb565", host->surface_display_buffer,
                 host->surface_display_frame_bytes);
    length = snprintf(report, sizeof(report),
        "{\"observed_frames\":%u,\"raster_p50_us\":%u,\"raster_p95_us\":%u,"
        "\"frame_cpu_p50_us\":%u,\"frame_cpu_p95_us\":%u,"
        "\"last_draw_list_bytes\":%u,\"rendered_frames\":%llu,"
        "\"visible_frames\":%llu,\"linear_peak\":%llu,"
        "\"artifact_buffers\":%llu,\"worker_metadata\":%zu,"
        "\"worker_configured_stack\":%u,\"cache_peak_internal\":%zu,"
        "\"cache_peak_external\":%zu,\"shared_current_internal\":%zu,"
        "\"shared_current_external\":%zu,\"shared_peak_internal\":%zu,"
        "\"shared_peak_external\":%zu,\"display_width\":%u,"
        "\"display_height\":%u,\"frame_bytes\":%u,"
        "\"process_malloc_current\":%zu,\"process_malloc_mmap\":%zu}\n",
        test->sample_count, test->raster_us[(test->sample_count - 1) / 2],
        test->raster_us[(test->sample_count * 95 + 99) / 100 - 1],
        test->frame_cpu_us[(test->sample_count - 1) / 2],
        test->frame_cpu_us[(test->sample_count * 95 + 99) / 100 - 1],
        host->raster_telemetry.last_draw_list_bytes,
        (unsigned long long)host->raster_telemetry.rendered_frames,
        (unsigned long long)host->raster_telemetry.visible_frames,
        (unsigned long long)memory.linear_peak_bytes,
        (unsigned long long)memory.artifact_buffer_bytes, worker_metadata,
        host->asset_worker ? 128u * 1024u : 0u,
        cache.peak_charged[0], cache.peak_charged[1],
        budget.charged[0], budget.charged[1], budget.peak[0], budget.peak[1],
        host->surface_display_width, host->surface_display_height,
        (unsigned)host->surface_display_frame_bytes,
        process_heap.uordblks, process_heap.hblkhd);
    assert(length > 0 && (size_t)length < sizeof(report));
    capture_file(test, "gameplay-memory.json", report, (size_t)length);
    test->captured = 2;
    host->exit_requested = 1;
}
static void voxel_frame_observe(void *context, void *runner,
                                uint64_t rendered, uint64_t cpu_us) {
    capture_t *test = context;
    product_host_t *host = runner;
    char name[32];
    if (!test || !test->gameplay || test->captured != 1 ||
        host != test->host || rendered <= test->last_rendered ||
        host->raster_telemetry.last_draw_list_bytes <= 128)
        return;
    test->last_rendered = rendered;
    assert(cpu_us <= UINT32_MAX && host->surface_display_buffer);
    assert(test->sample_count < 60);
    snprintf(name, sizeof(name), "gameplay-%02u.rgb565", test->sample_count);
    capture_file(test, name, host->surface_display_buffer,
                 host->surface_display_frame_bytes);
    test->raster_us[test->sample_count] =
        host->raster_telemetry.last_host_raster_us;
    test->frame_cpu_us[test->sample_count++] = (uint32_t)cpu_us;
    if (test->timeline) capture_memory_sample(test, host, test->sample_count);
    if (test->sample_count == 60) capture_gameplay(test, host);
}
static void start_gameplay(product_host_t *host) {
    uint8_t pointer[12] = {0};
    /* The first Voxel menu button is NEW GAME at (120, 105) on 240x320. */
    pxa_write_u32(pointer + 4, 120);
    pxa_write_u32(pointer + 8, 105);
    ui_event(1, 2, PXA_UI_EVENT_POINTER, PXA_UI_EVENT_FLAG_RELIABLE,
             pointer, sizeof(pointer), host);
    pointer[1] = 2;
    ui_event(1, 2, PXA_UI_EVENT_POINTER, PXA_UI_EVENT_FLAG_RELIABLE,
             pointer, sizeof(pointer), host);
}
static void pump_capture(void *context) {
    capture_t *test = context;
    product_host_t *host = test->host;
    if (!host || test->captured == 2) return;
    assert(now_us(NULL) - test->started < UINT64_C(60000000));
    if (test->timeline && test->captured == 0 && host->engine &&
        test->loading_count < 256 &&
        (test->loading_count == 0 ||
         now_us(NULL) - test->last_loading_us >= UINT64_C(10000))) {
        memory_sample_t *sample = &test->loading_samples[test->loading_count++];
        read_memory_sample(test, host, sample);
        test->last_loading_us = now_us(NULL);
    }
    if (test->captured == 1) return;
    if (!host->surface_display_buffer || !host->raster_telemetry.rendered_frames ||
        host->raster_telemetry.last_draw_list_bytes <= 128)
        return;
    pxa_raster_resources_t resources;
    pxa_wamr_memory_snapshot_t memory;
    size_t worker_metadata = 0;
    pxa_asset_cache_stats_t cache = {0};
    pxa_raster_bindings_view(&host->raster_bindings, PXA_RASTER_CAP_KNOWN_MASK,
                              &resources);
    /* Loading screens are valid frames; wait for every required binding. */
    if (!resources.palette || resources.palette_light_levels != 16) return;
    for (unsigned i = 0; i < 48; ++i)
        if (!resources.textures[i].pixels) return;
    assert(pxa_wamr_engine_memory_snapshot(host->engine, &memory) == 0);
    if (host->asset_worker)
        pxa_posix_asset_worker_stats(host->asset_worker, &cache, &worker_metadata);
    size_t resource_bytes = pxa_raster_asset_allocation_bytes(host->raster_bindings.palette);
    capture_file(test, "palette.bin", resources.palette, 256 * 16 * 2);
    for (unsigned i = 0; i < 48; ++i) {
        char name[32]; snprintf(name, sizeof(name), "texture-%02u.bin", i);
        const pxa_raster_texture_t *t = &resources.textures[i];
        capture_file(test, name, t->pixels, (size_t)t->width * t->height);
        resource_bytes += pxa_raster_asset_allocation_bytes(host->raster_bindings.textures[i]);
    }
    capture_file(test, "menu.rgb565", host->surface_display_buffer,
                  host->surface_display_frame_bytes);
    pxa_memory_stats_t budget;
    assert(pxa_memory_budget_stats(&host->resource_budget, 0, &budget) == PXA_STATUS_OK);
    char report[2048];
    int length = snprintf(report, sizeof(report),
        "{\"wamr_current\":%u,\"wamr_peak\":%u,\"linear_current\":%llu,"
        "\"linear_peak\":%llu,\"artifact_buffers\":%llu,\"bound_resource_bytes\":%zu,"
        "\"display_width\":%u,\"display_height\":%u,\"frame_bytes\":%u,"
        "\"worker_metadata\":%zu,\"worker_configured_stack\":%u,"
        "\"cache_peak_internal\":%zu,\"cache_peak_external\":%zu,"
        "\"shared_current_internal\":%zu,\"shared_current_external\":%zu,"
        "\"shared_peak_internal\":%zu,\"shared_peak_external\":%zu,"
        "\"shared_counter_storage\":%zu,\"shared_allocator_storage\":%zu}\n",
        memory.current_bytes, memory.peak_bytes,
        (unsigned long long)memory.linear_current_bytes,
        (unsigned long long)memory.linear_peak_bytes,
        (unsigned long long)memory.artifact_buffer_bytes, resource_bytes,
        host->surface_display_width, host->surface_display_height,
        (unsigned)host->surface_display_frame_bytes, worker_metadata,
        host->asset_worker ? 128u * 1024u : 0u,
        cache.peak_charged[0], cache.peak_charged[1],
        budget.charged[0], budget.charged[1], budget.peak[0], budget.peak[1],
        sizeof(host->resource_budget), sizeof(host->resource_allocators));
    assert(length > 0 && (size_t)length < sizeof(report));
    capture_file(test, "memory.json", report, (size_t)length);
    if (test->timeline) capture_memory_sample(test, host, 0);
    fputs(report, stdout);
    test->captured = 1;
    if (test->gameplay) {
        test->last_rendered = host->raster_telemetry.rendered_frames;
        start_gameplay(host);
    } else host->exit_requested = 1;
}
int main(int argc, char **argv) {
    assert(argc == 4 || (argc == 5 &&
        (!strcmp(argv[4], "gameplay") || !strcmp(argv[4], "timeline"))));
    assert(!mkdir(argv[3], 0755) || errno == EEXIST);
    capture_t test = {0}; options_t options = {0};
    test.directory = argv[3]; test.started = now_us(NULL);
    test.gameplay = argc == 5;
    test.timeline = argc == 5 && !strcmp(argv[4], "timeline");
    options.package_path = argv[1]; options.publisher_key = argv[2];
    options.width = 240; options.height = 320; options.locale = "en-US";
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(run_product_simulator(&options, NULL, NULL, pump_capture, &test,
        NULL, &test, bind_capture, NULL, NULL, NULL) == 0);
    assert(test.captured == (test.gameplay ? 2u : 1u) && !test.host);
    return 0;
}
