/* Real signed C++ SDK example -> Core/WAMR -> shared SDL mixer. The SDL
 * device is paused after initialization so admission/replacement is exact. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    const char *artifacts;
    uint64_t started;
    unsigned profile, completed;
} audio_sdk_test_t;

static lv_obj_t *find_label(lv_obj_t *object, const char *text) {
    if (lv_obj_check_type(object, &lv_label_class) &&
        !strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        lv_obj_t *found = find_label(lv_obj_get_child(object, i), text);
        if (found) return found;
    }
    return NULL;
}

static void click(audio_sdk_test_t *test, const char *text) {
    lv_obj_t *label = find_label(lv_screen_active(), text);
    assert(label);
    lv_obj_t *button = lv_obj_get_parent(label);
    assert(lv_obj_check_type(button, &lv_button_class));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    dispatch_component_events(test->host);
}

static pxa_audio_mixer_packet_t *effect(pxa_audio_mixer_voice_t *voice, uint8_t track) {
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_PACKETS; ++i) {
        pxa_audio_mixer_packet_t *packet = &voice->packets[i];
        if (packet->kind == PXA_AUDIO_PACKET_EFFECT && packet->position < packet->length &&
            packet->meta.effect.track == track) return packet;
    }
    return NULL;
}

static void capture(audio_sdk_test_t *test) {
    lv_draw_buf_t *image = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    assert(image);
    char path[2048];
    int size = snprintf(path, sizeof(path), "%s/profile-%u.bgra", test->artifacts, test->profile);
    assert(size > 0 && (size_t)size < sizeof(path));
    FILE *file = fopen(path, "wb");
    size_t bytes = (size_t)image->header.stride * image->header.h;
    assert(file && fwrite(image->data, 1, bytes, file) == bytes && !fclose(file));
    size = snprintf(path, sizeof(path), "%s/profile-%u.json", test->artifacts, test->profile);
    assert(size > 0 && (size_t)size < sizeof(path));
    file = fopen(path, "w");
    assert(file);
    fprintf(file, "{\"width\":%u,\"height\":%u,\"stride\":%u}\n",
        image->header.w, image->header.h, image->header.stride);
    assert(!fclose(file));
    lv_draw_buf_destroy(image);
}

static void bind_test(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    (void)focus; (void)exit_app;
    ((audio_sdk_test_t *)context)->host = runner;
}

static void pump_test(void *context) {
    audio_sdk_test_t *test = context;
    product_host_t *host = test->host;
    if (!host) return;
    assert(now_us(NULL) - test->started < UINT64_C(15000000));
    if (test->completed || !find_label(lv_screen_active(), "Ready")) return;
    assert(host->audio_device);
    SDL_PauseAudioDevice(host->audio_device, 1);
    int index = -1;
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_VOICES; ++i)
        if (host->audio_sessions[i]) { assert(index == -1); index = (int)i; }
    assert(index >= 0);
    pxa_audio_mixer_voice_t *voice = &host->audio_mixer.voices[index];
    assert(voice->committed && !voice->count);
    capture(test);

    click(test, "One-shot");
    assert(voice->count == 1);
    pxa_audio_mixer_packet_t *independent = effect(voice, PXA_AUDIO_SOUND_APPEND);
    assert(independent && independent->meta.effect.count == 2 && independent->length == 8000);
    assert(independent->data.layers[0].tone.frequency_hz == 660);
    assert(independent->data.layers[1].tone.delay_ms == 100);
    click(test, "Replace");
    assert(voice->count == 2 && effect(voice, 0));
    int16_t output[320];
    audio_callback(host, (uint8_t *)output, sizeof(output));
    unsigned audible = 0;
    for (unsigned i = 0; i < 320; ++i) audible |= output[i] != 0;
    assert(audible && independent->position == 320);

    click(test, "Replace");
    assert(voice->count == 2 && effect(voice, 0)->position == 0);
    assert(independent->position == 320); /* Separate category unaffected. */
    click(test, "Stop track");
    assert(voice->count == 1 && !effect(voice, 0));
    click(test, "Stop track");
    assert(voice->count == 1); /* Idempotent. */
    click(test, "Play tone");
    assert(voice->count == 2 && voice->read < PXA_AUDIO_MIXER_PACKETS);

    product_focus_changed(host, false); dispatch_lifecycle(host);
    assert(SDL_GetAudioDeviceStatus(host->audio_device) == SDL_AUDIO_PAUSED);
    pxa_audio_mixer_t paused = host->audio_mixer;
    SDL_Delay(30); /* The actual device must stop requesting samples. */
    assert(!memcmp(&paused, &host->audio_mixer, sizeof(paused)));
    product_focus_changed(host, true); dispatch_lifecycle(host);
    assert(SDL_GetAudioDeviceStatus(host->audio_device) == SDL_AUDIO_PLAYING);
    SDL_Delay(40);
    SDL_PauseAudioDevice(host->audio_device, 1);
    assert(voice->state.accepted_samples > paused.voices[index].state.accepted_samples);
    for (unsigned i = 0; i < 30; ++i) audio_callback(host, (uint8_t *)output, sizeof(output));
    assert(!voice->count);

    click(test, "Replace");
    for (unsigned i = 1; i < PXA_AUDIO_MIXER_PACKETS; ++i) click(test, "One-shot");
    assert(voice->count == PXA_AUDIO_MIXER_PACKETS);
    pxa_audio_mixer_t full = host->audio_mixer;
    click(test, "One-shot");
    assert(!memcmp(&full, &host->audio_mixer, sizeof(full)));
    assert(find_label(lv_screen_active(), "Audio busy"));
    click(test, "Replace");
    assert(voice->count == PXA_AUDIO_MIXER_PACKETS && find_label(lv_screen_active(), "Track replaced"));
    click(test, "Stop track");
    assert(voice->count == PXA_AUDIO_MIXER_PACKETS - 1);
    test->completed = 1;
    host->exit_requested = 1;
}

int main(int argc, char **argv) {
    assert(argc == 5);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (unsigned profile = 0; profile < 2; ++profile) {
        options_t options = {0};
        options.package_path = argv[1]; options.publisher_key = argv[2];
        options.state_root = argv[3]; options.locale = "en-US";
        options.width = profile ? 480 : 296;
        options.height = profile ? 480 : 240;
        options.density_dpi = profile ? 320 : 160;
        options.display_shape = profile ? 1 : 0;
        options.corner_radius = profile ? 48 : 0;
        for (unsigned i = 0; i < 4; ++i) options.safe_insets[i] = profile ? 24 : 0;
        audio_sdk_test_t test = {0};
        test.profile = profile; test.artifacts = argv[4]; test.started = now_us(NULL);
        assert(!run_product_simulator(&options, NULL, NULL, pump_test, &test,
            NULL, &test, bind_test, NULL, NULL, NULL));
        assert(test.completed && !test.host);
    }
    puts("audio SDK: signed C++ layers, track replacement/stop, full-slot rejection, legacy FIFO, focus pause/resume and repeated activation passed");
    return 0;
}
