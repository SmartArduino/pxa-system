#include "pxa_canvas.h"
#include "pxa_game_sfx.h"

#include <assert.h>

static uint32_t write_count;
static uint32_t last_handle;
static uint32_t last_length;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    (void)data;
    (void)length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length) {
    assert(handle == 77);
    assert(operation == PXA_IO_WRITE);
    assert(data != NULL);
    assert(length == PXA_GAME_SFX_FRAME_SAMPLES * sizeof(int16_t));
    ++write_count;
    last_handle = handle;
    last_length = length;
    return (int32_t)length;
}

static void tick(pxa_game_sfx_t* sfx, uint64_t timestamp_us) {
    uint8_t payload[8];
    uint8_t message[32];
    pxa_writer_t writer;
    pxa_event_t event;
    pxa_writer_init(&writer, payload, sizeof(payload));
    assert(pxa_put_u32(&writer, (uint32_t)timestamp_us));
    assert(pxa_put_u32(&writer, (uint32_t)(timestamp_us >> 32)));
    pxa_writer_init(&writer, message, sizeof(message));
    assert(pxa_message(&writer, PXA_SERVICE_CLOCK, PXA_CLOCK_TICK, 0,
                       payload, sizeof(payload)));
    assert(pxa_parse_event(message, (uint32_t)writer.length, &event));
    pxa_game_sfx_tick(sfx, &event);
}

static void test_song_catalog(void) {
    for (uint8_t theme = 0; theme < PXA_GAME_SFX_THEME_COUNT; ++theme) {
        const pxa_game_music_song_t* song = pxa_game_music_song(theme);
        uint32_t total_eighths = 0;
        int has_rest = 0;
        int has_varied_duration = 0;
        assert(song->bpm >= 90 && song->bpm <= 160);
        if (song->score != NULL) {
            assert(song->score_length > 20);
            assert(song->score_eighths >= 48);
            for (uint16_t index = 0; index < song->score_length; ++index) {
                const pxa_game_music_event_t* event = &song->score[index];
                assert(event->eighths != 0);
                assert(event->timbre <= PXA_MUSIC_TIMBRE_CLICK);
                assert(event->start_eighth < song->score_eighths);
            }
            continue;
        }
        assert(song->melody_length > 20);
        for (uint16_t index = 0; index < song->melody_length; ++index) {
            const pxa_game_music_note_t* note = &song->melody[index];
            assert(note->eighths != 0);
            total_eighths += note->eighths;
            if (note->phase_step == PXA_MUSIC_REST)
                has_rest = 1;
            if (index != 0 && note->eighths != song->melody[0].eighths)
                has_varied_duration = 1;
        }
        assert(total_eighths >= 48);
        assert(has_rest && has_varied_duration);
    }
}

static void test_named_themes(void) {
    const pxa_game_music_song_t* jump_jump =
        pxa_game_music_song(PXA_GAME_SFX_THEME_JUMP_JUMP);
    const pxa_game_music_song_t* tetris =
        pxa_game_music_song(PXA_GAME_SFX_THEME_TETRIS);

    /* Pop Goes the Weasel opens C C D D E G E C. */
    assert(jump_jump->melody[0].phase_step == PXA_MUSIC_C4);
    assert(jump_jump->melody[1].phase_step == PXA_MUSIC_C4);
    assert(jump_jump->melody[2].phase_step == PXA_MUSIC_D4);
    assert(jump_jump->melody[3].phase_step == PXA_MUSIC_D4);
    assert(jump_jump->melody[4].phase_step == PXA_MUSIC_E4);
    assert(jump_jump->melody[5].phase_step == PXA_MUSIC_G4);

    /* Korobeiniki (Huo Lang) must remain the Tetris theme. */
    assert(tetris->melody[0].phase_step == PXA_MUSIC_E5);
    assert(tetris->melody[1].phase_step == PXA_MUSIC_B4);
    assert(tetris->melody[2].phase_step == PXA_MUSIC_C5);
    assert(tetris->melody[3].phase_step == PXA_MUSIC_D5);
    assert(tetris->melody[4].phase_step == PXA_MUSIC_C5);
    assert(tetris->melody[5].phase_step == PXA_MUSIC_B4);
    assert(tetris->melody[6].phase_step == PXA_MUSIC_A4);
}

static void test_custom_song(void) {
    static const pxa_game_music_event_t score[] = {
        PXA_MUSIC_EVENT(0, PXA_MUSIC_C4, 1, 80, PXA_MUSIC_TIMBRE_SINE),
    };
    static const pxa_game_music_song_t song = {
        NULL, 0, 100, 0, 0, 0,
        {PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST,
         PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST},
        score, PXA_MUSIC_LENGTH(score), 1,
    };
    pxa_game_sfx_t sfx = {0};

    pxa_game_sfx_set_song(&sfx, &song);
    assert(sfx.music_song == &song);
    pxa_game_sfx_mix_frame(&sfx);
    assert(sfx.music_tick_samples == 4800u);

    pxa_game_sfx_set_theme(&sfx, PXA_GAME_SFX_THEME_GARDEN);
    assert(sfx.music_song == NULL);
    assert(sfx.music_theme == PXA_GAME_SFX_THEME_GARDEN);
    assert(sfx.music_tick_samples == 0);
}

static void test_score_percussion(void) {
    pxa_game_sfx_t sfx = {0};
    pxa_game_sfx_score_voice_t voice = {
        .phase = 1000,
        .note_step = 1800,
        .note_sample = 40,
        .note_samples = 4000,
        .gain = 640,
        .timbre = PXA_MUSIC_TIMBRE_SNARE,
    };
    sfx.noise_state = UINT32_C(0x9E3779B9);
    const uint32_t noise_before = sfx.noise_state;
    (void)pxa_game_sfx_score_tone(&sfx, &voice);
    assert(sfx.noise_state != noise_before);

    voice.note_sample = 900;
    assert(pxa_game_sfx_score_gain(&voice, 4000) == 0);
    voice.note_sample = 321;
    voice.timbre = PXA_MUSIC_TIMBRE_CLICK;
    assert(pxa_game_sfx_score_gain(&voice, 4000) == 0);
    assert(PXA_GAME_SFX_SCORE_VOICES == 19u);
}

static void test_high_resolution_score(void) {
    static const pxa_game_music_event_t score[] = {
        PXA_MUSIC_EVENT(0, PXA_MUSIC_C4, 64, 64, PXA_MUSIC_TIMBRE_STRINGS),
    };
    static const pxa_game_music_song_t song = {
        NULL, 0, 320, 0, 0, 0,
        {PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST,
         PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST, PXA_MUSIC_REST},
        score, PXA_MUSIC_LENGTH(score), 64,
    };
    pxa_game_sfx_t sfx = {0};

    pxa_game_sfx_set_song(&sfx, &song);
    pxa_game_sfx_mix_frame(&sfx);
    assert(sfx.music_tick_samples == 1500u);
    assert(sfx.score_voices[0].note_samples == 96000u);
    assert(sfx.score_voices[0].note_sample == PXA_GAME_SFX_FRAME_SAMPLES);
}

static uint32_t effect_signature(uint8_t kind) {
    pxa_game_sfx_t sfx = {0};
    uint32_t signature = 2166136261u;
    sfx.state = PXA_GAME_SFX_READY;
    sfx.noise_state = UINT32_C(0x9E3779B9);
    pxa_game_sfx_play(&sfx, kind);
    pxa_game_sfx_mix_frame(&sfx);
    for (size_t index = 0; index < PXA_GAME_SFX_FRAME_SAMPLES; ++index) {
        signature ^= (uint16_t)sfx.frame[index];
        signature *= 16777619u;
    }
    return signature;
}

static void test_distinct_gameplay_effects(void) {
    static const uint8_t effects[] = {
        PXA_GAME_SFX_SCORE, PXA_GAME_SFX_FIRE, PXA_GAME_SFX_EXPLODE,
        PXA_GAME_SFX_JUMP, PXA_GAME_SFX_CHARGE, PXA_GAME_SFX_PLACE,
        PXA_GAME_SFX_COLLECT, PXA_GAME_SFX_BITE, PXA_GAME_SFX_DEATH,
    };
    uint32_t signatures[sizeof(effects) / sizeof(effects[0])];
    for (size_t effect = 0; effect < sizeof(effects) / sizeof(effects[0]); ++effect) {
        signatures[effect] = effect_signature(effects[effect]);
        for (size_t previous = 0; previous < effect; ++previous)
            assert(signatures[effect] != signatures[previous]);
    }
}

static void test_output_level(void) {
    pxa_game_sfx_t music = {0};
    pxa_game_sfx_t effect = {0};
    uint16_t music_peak = 0;
    uint16_t effect_peak = 0;
    uint64_t music_energy = 0;
    uint64_t effect_energy = 0;
    music.state = PXA_GAME_SFX_READY;
    pxa_game_sfx_set_theme(&music, PXA_GAME_SFX_THEME_TETRIS);
    for (uint8_t frame = 0; frame < 12; ++frame) {
        pxa_game_sfx_mix_frame(&music);
        for (size_t index = 0; index < PXA_GAME_SFX_FRAME_SAMPLES; ++index) {
            const int32_t sample = music.frame[index];
            const uint16_t magnitude =
                (uint16_t)(sample < 0 ? -sample : sample);
            if (magnitude > music_peak)
                music_peak = magnitude;
            music_energy += magnitude;
        }
    }
    effect.state = PXA_GAME_SFX_READY;
    pxa_game_sfx_set_theme(&effect, PXA_GAME_SFX_THEME_TETRIS);
    pxa_game_sfx_play(&effect, PXA_GAME_SFX_PLACE);
    for (uint8_t frame = 0; frame < 4; ++frame) {
        pxa_game_sfx_mix_frame(&effect);
        for (size_t index = 0; index < PXA_GAME_SFX_FRAME_SAMPLES; ++index) {
            const int32_t sample = effect.frame[index];
            const uint16_t magnitude =
                (uint16_t)(sample < 0 ? -sample : sample);
            if (magnitude > effect_peak)
                effect_peak = magnitude;
            effect_energy += magnitude;
        }
    }
    const uint32_t music_average =
        (uint32_t)(music_energy / (12u * PXA_GAME_SFX_FRAME_SAMPLES));
    const uint32_t effect_average =
        (uint32_t)(effect_energy / (4u * PXA_GAME_SFX_FRAME_SAMPLES));
    assert(PXA_GAME_SFX_MUSIC_GAIN_Q10 == 4096u);
    assert(PXA_GAME_SFX_DUCK_HIGH == 384u);
    assert(PXA_GAME_SFX_DUCK_LOW == 640u);
    assert(PXA_GAME_SFX_GRAPH_GAIN_DB_Q8 == -256);
    assert(music_peak >= 8500u && music_peak <= 12000u);
    assert(music_average >= 2800u && music_average <= 4200u);
    assert(effect_average * 5u > music_average * 4u);
    assert(effect_peak < PXA_GAME_SFX_LIMIT_THRESHOLD);
}

static void test_music_waveform(void) {
    assert(pxa_game_sfx_sine(0) == 0);
    assert(pxa_game_sfx_sine(UINT16_C(16384)) == INT16_MAX);
    assert(pxa_game_sfx_sine(UINT16_C(32768)) == 0);
    assert(pxa_game_sfx_sine(UINT16_C(49152)) == INT16_MIN);
    assert(pxa_game_sfx_sine(UINT16_C(8192)) >= 23000);
    assert(pxa_game_sfx_sine(UINT16_C(8192)) <= 23300);
    assert(pxa_game_sfx_scale_q10(-1024, 640u) == -640);
    assert(pxa_game_sfx_scale_q10(-640, 640u) == -400);
}

static void test_soft_limiter(void) {
    assert(pxa_game_sfx_soft_limit(12000) == 12000);
    assert(pxa_game_sfx_soft_limit(-12000) == -12000);
    const int16_t positive = pxa_game_sfx_soft_limit(50000);
    const int16_t negative = pxa_game_sfx_soft_limit(-50000);
    assert(positive > (int16_t)PXA_GAME_SFX_LIMIT_THRESHOLD);
    assert(positive < INT16_MAX);
    assert(negative == -positive);
}

int main(void) {
    pxa_game_sfx_t sfx = {0};
    sfx.state = PXA_GAME_SFX_READY;
    sfx.session_handle = 77;
    pxa_game_sfx_set_theme(&sfx, PXA_GAME_SFX_THEME_TETRIS);
    assert(sfx.music_theme == PXA_GAME_SFX_THEME_TETRIS);
    test_song_catalog();
    test_named_themes();
    test_custom_song();
    test_score_percussion();
    test_high_resolution_score();
    test_distinct_gameplay_effects();
    test_music_waveform();
    test_output_level();
    test_soft_limiter();

    pxa_game_sfx_play(&sfx, PXA_GAME_SFX_SCORE);
    assert(write_count == 0);
    pxa_game_sfx_play(&sfx, PXA_GAME_SFX_EXPLODE);
    pxa_game_sfx_play(&sfx, PXA_GAME_SFX_FIRE);
    assert(write_count == 0);
    assert(sfx.effects[0].kind == PXA_GAME_SFX_SCORE);
    assert(sfx.effects[1].kind == PXA_GAME_SFX_EXPLODE);
    assert(sfx.effects[2].kind == PXA_GAME_SFX_FIRE);
    assert(sfx.effects[0].samples_left != 0 && sfx.effects[1].samples_left != 0 &&
           sfx.effects[2].samples_left != 0);
    pxa_game_sfx_play(&sfx, PXA_GAME_SFX_ALERT);
    assert(sfx.effects[2].kind == PXA_GAME_SFX_ALERT);
    pxa_game_sfx_play(&sfx, PXA_GAME_SFX_TAP);
    assert(sfx.effects[0].kind == PXA_GAME_SFX_SCORE);
    assert(sfx.effects[1].kind == PXA_GAME_SFX_EXPLODE);
    assert(sfx.effects[2].kind == PXA_GAME_SFX_ALERT);

    tick(&sfx, 0);
    assert(write_count == 2 && last_handle == 77);
    assert(last_length == PXA_GAME_SFX_FRAME_SAMPLES * sizeof(int16_t));
    tick(&sfx, 50000);
    assert(write_count >= 4);
    tick(&sfx, 150000);
    assert(write_count >= 8);
    tick(&sfx, 250000);
    assert(write_count >= 12);
    assert(sfx.music_tick != 0 && sfx.music_tick_samples != 0);
    return 0;
}
