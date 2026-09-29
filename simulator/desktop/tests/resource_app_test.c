/* Run the signed resource-scenes AOT through the actual WAMR/product loop. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    uint64_t started;
    unsigned scenes;
    uint16_t previous_color;
    bool exit_loading;
    bool exit_reading;
    bool exit_sound_loading;
    bool observed_loading;
    uint64_t event_buffer_peak;
    unsigned storage_mode, storage_stage, stalled_pumps;
    uint64_t stall_started, recovered_us, exit_started, last_pump, max_pump_gap;

} test_state_t;

static void bind_test(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    test_state_t *test = context;
    (void)focus; (void)exit_app;
    test->host = runner;
}
/* Real Guest/render/audio threads all run while this injects one shared device
 * outage. The pump is an observer/controller, never a replacement decoder. */
static void storage_test(test_state_t *test) {
    product_host_t *host=test->host;
    if (!test->storage_mode || !host->storage_gate || !host->audio_device) return;
    const uint64_t now=now_us(NULL);
    pxa_audio_buffer_t buffer;
    SDL_LockAudioDevice(host->audio_device); buffer=host->music_buffer; SDL_UnlockAudioDevice(host->audio_device);
    if (test->storage_stage==1) {
        ++test->stalled_pumps;
        if (test->last_pump && now-test->last_pump>test->max_pump_gap)
            test->max_pump_gap=now-test->last_pump;
    }
    test->last_pump=now;
    if (test->storage_mode==1) return;
    if (!test->storage_stage && test->scenes>=2 && buffer.consumed_samples>=8192) {
        assert(!buffer.underruns && !buffer.recoveries);
        test->stall_started=now; test->storage_stage=1;
        pxa_posix_storage_gate_stall(host->storage_gate,test->storage_mode==3 ? 30000000 : 3000000);
    } else if (test->storage_stage==1 && test->storage_mode==3) {
        pxa_posix_storage_stats_t io;
        pxa_posix_storage_gate_stats(host->storage_gate,&io);
        /* Only these two workers use the gate during gameplay. One is inside
         * the injected transfer and the other is queued behind it. */
        if (io.active && io.waiting[0]+io.waiting[1]) {
            test->exit_started=now; test->storage_stage=2;
            host->exit_requested=1;
        }
    } else if (test->storage_stage==1 && buffer.recoveries) {
        assert(buffer.underruns==1 && buffer.recoveries==1 && buffer.missing_samples);
        test->recovered_us=now-test->stall_started; test->storage_stage=2;
        assert(test->recovered_us>=3000000 && test->recovered_us<5000000);
        assert(test->stalled_pumps>=20 && test->max_pump_gap<1000000);
    }
}

static void pump_test(void *context) {
    test_state_t *test = context;
    product_host_t *host = test->host;
    if (!host) return;
    storage_test(test);
    if (test->exit_started) return;
    pxa_wamr_memory_snapshot_t memory_snapshot;
    assert(!pxa_wamr_engine_memory_snapshot(host->engine,&memory_snapshot));
    if (memory_snapshot.event_buffer_bytes > test->event_buffer_peak)
        test->event_buffer_peak = memory_snapshot.event_buffer_bytes;
    assert(test->event_buffer_peak <= 4096);
    assert(now_us(NULL) - test->started < UINT64_C(60000000));
    if (test->exit_sound_loading && host->asset_worker) {
        pxa_memory_stats_t memory;
        pxa_asset_cache_stats_t cache;
        assert(!pxa_memory_budget_stats(&host->resource_budget,0,&memory));
        pxa_posix_asset_worker_stats(host->asset_worker,&cache,NULL);
        /* The sound allocation exists while its delayed file read is still
         * in flight. Observe the sound category, not unrelated temporary data. */
        if (memory.by_kind[PXA_MEMORY_AUDIO][PXA_MEMORY_EXTERNAL] &&
            cache.loading[PXA_ASSET_MEMORY_EXTERNAL]) {
            test->observed_loading = true;
            host->exit_requested = 1;
        }
        return;
    }
    if (test->exit_reading && host->asset_worker) {
        /* Observe the actual raw-data read callback, not another subsystem's
         * allocation in the same budget category. */
        if (pxa_posix_asset_worker_reading_blob(host->asset_worker)) {
            test->observed_loading = true;
            host->exit_requested = 1;
        }
        return;
    }
    if (test->exit_loading && host->asset_worker) {
        pxa_asset_cache_stats_t stats;
        pxa_posix_asset_worker_stats(host->asset_worker, &stats, NULL);
        /* Palette is internal; external loading identifies an actual texture
         * request after the Guest has entered its scene-loading state. */
        if (stats.loading[PXA_ASSET_MEMORY_EXTERNAL] && stats.requests &&
            host->raster_telemetry.rendered_frames) {
            assert(test->scenes == 0);
            test->observed_loading = true;
            host->exit_requested = 1;
        }
        return;
    }
    if (host->surface_display_buffer && host->surface_display_width == 240 && host->raster_telemetry.rendered_frames) {
        const uint16_t *pixels = (const uint16_t *)host->surface_display_buffer;
        size_t stride = host->surface_display_stride_bytes / 2;
        size_t origin_y = (host->surface_display_height - 240) / 2;
        uint16_t color = pixels[(origin_y + 10) * stride + 10];
        if (color && color != test->previous_color) {
            unsigned first = (test->scenes % 5) * 4;
            for (unsigned i = 0; i < 4; ++i) {
                size_t x = (i % 2) * 120 + 10, y = origin_y + (i / 2) * 120 + 10;
                assert(pixels[y * stride + x] == (first + i + 1) * 251);
                /* Adjacent checker tile also verifies file contents and UV sampling. */
                assert(pixels[y * stride + x + 16] == (first + i + 21) * 251);
            }
            test->previous_color = color;
            ++test->scenes;
        }
    }
    if (test->scenes && !host->surface_flags) {
        pxa_asset_cache_stats_t stats;
        pxa_posix_asset_worker_stats(host->asset_worker, &stats, NULL);
        assert(test->scenes == 100 && stats.requests == 0);
        assert(stats.peak_charged[1] <= 512 * 1024 && stats.peak_charged[0] <= 64 * 1024);
        fprintf(stderr,"resource completion: evictions=%llu failures=%llu prefetch_failures=%llu pressure_retries=%llu\n",
            (unsigned long long)stats.evictions,(unsigned long long)stats.load_failures,
            (unsigned long long)stats.prefetch_failures,(unsigned long long)stats.pressure_retries);
        assert(stats.evictions >= 380 && stats.load_failures == stats.prefetch_failures);
        assert(stats.prefetch_requests == 99);
        if (test->storage_mode) {
            pxa_posix_storage_stats_t io;
            pxa_posix_storage_gate_stats(host->storage_gate,&io);
            assert(io.lanes[0].bytes>20*256*256 && io.lanes[1].bytes);
            assert(io.lanes[0].max_read_bytes<=4096 && io.lanes[1].max_read_bytes<=4096);
            SDL_LockAudioDevice(host->audio_device);
            assert(host->music_decode_calls && host->music_decode_us && host->music_decode_max_us<=host->music_decode_us);
            assert(host->music_buffer.high_water<=8192 && host->music_buffer.low_water_valid);
            if (test->storage_mode==2) assert(host->music_buffer.low_water==0);
            if (test->storage_mode==1) assert(!host->music_buffer.underruns && !host->music_buffer.recoveries);
            else assert(test->storage_stage==2 && host->music_buffer.underruns==1 && host->music_buffer.recoveries==1);
            SDL_UnlockAudioDevice(host->audio_device);
            fprintf(stderr,"storage competition: mode=%u recovery_us=%llu pump_calls=%u max_pump_gap_us=%llu\n",
                test->storage_mode,(unsigned long long)test->recovered_us,test->stalled_pumps,(unsigned long long)test->max_pump_gap);
        }
        host->exit_requested = 1;
    }
}
int main(int argc, char **argv) {
    assert(argc == 3 || (argc == 4 && (!strcmp(argv[3], "exit-loading") || !strcmp(argv[3], "exit-reading") || !strcmp(argv[3], "exit-sound-loading") || !strcmp(argv[3], "storage-steady") || !strcmp(argv[3], "storage-stall") || !strcmp(argv[3], "storage-exit"))));
    test_state_t test = {0};
    if (argc==4) {
        if (!strcmp(argv[3],"storage-steady")) test.storage_mode=1;
        if (!strcmp(argv[3],"storage-stall")) test.storage_mode=2;
        if (!strcmp(argv[3],"storage-exit")) test.storage_mode=3;
    }
    if (test.storage_mode) {
        setenv("PXA_STORAGE_BYTES_PER_SECOND","2097152",1);
        setenv("PXA_STORAGE_LATENCY_US","500",1);
    }
    test.exit_loading = argc == 4 && !strcmp(argv[3], "exit-loading");
    test.exit_reading = argc == 4 && !strcmp(argv[3], "exit-reading");
    test.exit_sound_loading = argc == 4 && !strcmp(argv[3], "exit-sound-loading");
    options_t options = {0};
    options.package_path = argv[1]; options.publisher_key = argv[2];
    options.width = 240; options.height = 320; options.locale = "en-US";
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("PXA_ASSET_EXTERNAL_BYTES", "524288", 1);
    setenv("PXA_ASSET_READ_DELAY_US", (test.exit_loading || test.exit_reading || test.exit_sound_loading) ? "20000" : "1000", 0);
    test.started = now_us(NULL);
    assert(run_product_simulator(&options, NULL, NULL, pump_test, &test,
        NULL, &test, bind_test, NULL, NULL, NULL) == 0);
    assert(!test.host);
    if (test.storage_mode==3) {
        const uint64_t exit_us=now_us(NULL)-test.exit_started;
        assert(test.storage_stage==2 && test.exit_started && exit_us<2000000);
        printf("storage competition: blocked workers cancelled, exit_us=%llu\n",(unsigned long long)exit_us);
    } else if (test.exit_sound_loading) {
        assert(test.observed_loading);
        puts("resource app: real signed AOT exit during PCM loading, worker drain and zero remaining budget passed");
    } else if (test.exit_reading) {
        assert(test.observed_loading && test.scenes == 0);
        puts("resource app: real signed AOT exit during blob read and zero remaining budget passed");
    } else if (test.exit_loading) {
        assert(test.observed_loading && test.scenes == 0);
        puts("resource app: real signed AOT exit during texture loading, worker drain and zero remaining budget passed");
    } else {
        assert(test.scenes == 100 && test.event_buffer_peak == 4096);
        printf("resource app: Guest event buffer peak=%llu B (part of linear memory)\n",
            (unsigned long long)test.event_buffer_peak);
        puts("resource app: real signed AOT, 100 scenes, four checked textures per scene, slow I/O and clean exit passed");
    }
    return 0;
}
