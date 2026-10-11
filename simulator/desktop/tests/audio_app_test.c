/* Pixel Dungeon's signed AOT must route Assets completions into its audio
 * controller while the real Ogg decoder and SDL output are running. */
#undef NDEBUG
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static unsigned app_ready, app_phase;
static void observe_log(uint32_t component, unsigned level,
    const uint8_t *data, size_t size) {
    (void)component; (void)level;
    static const char ready[] = "PDCPP ready ";
    static const char phase[] = "PDCPP phase=";
    if (size >= sizeof(ready)-1 && !memcmp(data, ready, sizeof(ready)-1))
        app_ready = 1;
    if (size == sizeof(phase) && !memcmp(data, phase, sizeof(phase)-1) &&
        data[sizeof(phase)-1] >= '0' && data[sizeof(phase)-1] <= '9')
        app_phase = data[sizeof(phase)-1] - '0';
}
#define PXSYS_PRODUCT_LOG_OBSERVER observe_log
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    uint64_t started, changed;
    uint32_t paused_read;
    unsigned stage;
} audio_app_test_t;

static void tap(product_host_t *host, int x, int y) {
    for (unsigned phase = 0; phase <= 2; phase += 2) {
        uint8_t value[12] = {0}; value[1] = (uint8_t)phase;
        pxa_write_u32(value+4, (uint32_t)x);
        pxa_write_u32(value+8, (uint32_t)y);
        assert(!pxa_ui_queue_event(host->ui, host->active_component,
            PXA_UI_PRIMARY_SURFACE, 2, PXA_UI_EVENT_POINTER,
            PXA_UI_EVENT_FLAG_RELIABLE, now_us(NULL), value, sizeof(value)));
        dispatch_component_events(host);
    }
}

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
        if (!app_ready || !music) return;
        // Effects are intentionally lazy in C++: startup must not require or
        // allocate every sound. Trigger an actual game action below instead.
        assert(state.state != PXA_ASSET_REQUEST_READY);
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
        if (advanced) {
            test->stage = 3;
            tap(host,120,162); // title -> empty save slots
        }
    } else if (test->stage == 3 && app_phase == 5) {
        test->stage = 4;
        tap(host,120,160); // empty slot -> class selection
    } else if (test->stage == 4 && app_phase == 6) {
        test->stage = 5;
        tap(host,120,78); // warrior -> new game, DESCEND effect
    } else if (test->stage == 5 && app_phase == 1) {
        static const char descend[] = "assets/sfx/descend.pcm";
        pxa_asset_request_state_t state;
        assert(!pxa_posix_asset_worker_inspect(host->asset_worker,
            host->active_component,
            (pxa_bytes_t){(const uint8_t *)descend,sizeof(descend)-1}, &state));
        if (state.state != PXA_ASSET_REQUEST_READY) return;
        SDL_LockAudioDevice(host->audio_device);
        int played = 0;
        for (unsigned i = 0; i < PRODUCT_AUDIO_SOUND_VOICES; ++i)
            if (host->sound_voices[i].asset && host->sound_voices[i].position)
                played = 1;
        SDL_UnlockAudioDevice(host->audio_device);
        if (played) {
            pxa_memory_stats_t memory;
            assert(!pxa_memory_budget_stats(&host->resource_budget,0,&memory));
            assert(memory.by_kind[PXA_MEMORY_AUDIO][PXA_MEMORY_EXTERNAL]);
            test->stage = 6; host->exit_requested = 1;
        }
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
    // A persistent prior save would take a different UI path on repeated runs.
    char state_root[] = "/tmp/pxa-pixel-audio-test-XXXXXX";
    assert(mkdtemp(state_root)); options.state_root = state_root;
    audio_app_test_t test = {0}; test.started = now_us(NULL);
    assert(!run_product_simulator(&options,NULL,NULL,pump_test,&test,
        NULL,&test,bind_test,NULL,NULL,NULL));
    assert(!test.host && test.stage == 6);
    assert(!pxa_posix_fs_remove_tree(state_root));
    puts("audio app: signed Pixel Dungeon C++, lazy effect through actual new-game input, PCM voice playback, Ogg READY/fade, focus pause/resume and clean exit passed");
    return 0;
}
