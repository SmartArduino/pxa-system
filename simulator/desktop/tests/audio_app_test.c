/* Pixel Dungeon's signed AOT must route Assets completions into its audio
 * controller while the real Ogg decoder and SDL output are running. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    uint64_t started, changed;
    uint32_t paused_read;
    unsigned stage;
} audio_app_test_t;

static void bind_test(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    (void)focus; (void)exit_app;
    ((audio_app_test_t *)context)->host = runner;
}

static void pump_test(void *context) {
    audio_app_test_t *test = context;
    product_host_t *host = test->host;
    if (!host) return;
    uint64_t now = now_us(NULL);
    assert(now - test->started < UINT64_C(30000000));
    if (!host->audio_device || !host->asset_worker) return;
    if (!test->stage) {
        static const char step[] = "assets/sfx/step.pcm";
        pxa_asset_request_state_t state;
        assert(!pxa_posix_asset_worker_inspect(host->asset_worker,
            host->active_component, (pxa_bytes_t){(const uint8_t *)step,sizeof(step)-1}, &state));
        SDL_LockAudioDevice(host->audio_device);
        // The app only starts its fade after handling READY for the current
        // instance. Observing the changed gain proves that notification crossed
        // Core/WAMR and reached the actual Guest controller.
        int music = host->music_session && host->music_count && !host->music_finished &&
            host->music_gain_db_q8 > -40*256;
        SDL_UnlockAudioDevice(host->audio_device);
        if (!music || state.state != PXA_ASSET_REQUEST_READY) return;
        pxa_memory_stats_t memory;
        assert(!pxa_memory_budget_stats(&host->resource_budget,0,&memory));
        assert(memory.by_kind[PXA_MEMORY_AUDIO][PXA_MEMORY_EXTERNAL]);
        product_focus_changed(host,false);
        SDL_LockAudioDevice(host->audio_device);
        test->paused_read = host->music_read;
        SDL_UnlockAudioDevice(host->audio_device);
        test->stage = 1; test->changed = now;
    } else if (test->stage == 1 && now - test->changed >= 100000) {
        SDL_LockAudioDevice(host->audio_device);
        assert(host->music_read == test->paused_read);
        SDL_UnlockAudioDevice(host->audio_device);
        product_focus_changed(host,true);
        test->stage = 2;
    } else if (test->stage == 2) {
        SDL_LockAudioDevice(host->audio_device);
        int advanced = host->music_read != test->paused_read;
        SDL_UnlockAudioDevice(host->audio_device);
        if (advanced) { test->stage = 3; host->exit_requested = 1; }
    }
}

int main(int argc, char **argv) {
    assert(argc == 3);
    setenv("SDL_VIDEODRIVER","dummy",1);
    setenv("SDL_AUDIODRIVER","dummy",1);
    setenv("PXA_ASSET_READ_DELAY_US","1000",1);
    options_t options = {0};
    options.package_path = argv[1]; options.publisher_key = argv[2];
    options.width = 240; options.height = 320; options.locale = "en-US";
    audio_app_test_t test = {0}; test.started = now_us(NULL);
    assert(!run_product_simulator(&options,NULL,NULL,pump_test,&test,
        NULL,&test,bind_test,NULL,NULL,NULL));
    assert(!test.host && test.stage == 3);
    puts("audio app: signed Pixel Dungeon, prepared effect, actual Ogg output, focus pause/resume and clean exit passed");
    return 0;
}
