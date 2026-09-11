#ifndef VOXEL_CRAFT_SFX_H
#define VOXEL_CRAFT_SFX_H

/* Minimal SFX-only audio pump for Voxel Craft. It follows the same permission
 * and session flow as apps/pxa/common/pxa_game_sfx.h but mixes only short
 * effects, with no music score. */

#include "pxa_audio.h"
#include "pxa_permission.h"

#define VOXEL_SFX_PERMISSION_REQUEST UINT32_C(0x56435801)
#define VOXEL_SFX_OPEN_REQUEST UINT32_C(0x56435802)
#define VOXEL_SFX_GRAPH_REQUEST UINT32_C(0x56435803)
#define VOXEL_SFX_SAMPLE_RATE 16000u
#define VOXEL_SFX_FRAME_SAMPLES 320u
#define VOXEL_SFX_FRAME_US 20000u
#define VOXEL_SFX_MAX_FRAMES_PER_TICK 4u
#define VOXEL_SFX_VOICES 4u
#define VOXEL_SFX_GRAPH_GAIN_DB_Q8 (-320)
#define VOXEL_SFX_LIMIT_THRESHOLD 24576

enum {
    VOXEL_SFX_OFF = 0,
    VOXEL_SFX_WAIT_PERMISSION,
    VOXEL_SFX_WAIT_OPEN,
    VOXEL_SFX_WAIT_GRAPH,
    VOXEL_SFX_READY,
    VOXEL_SFX_UNAVAILABLE,
};

enum {
    VOXEL_SFX_MINE = 0,
    VOXEL_SFX_BREAK,
    VOXEL_SFX_PLACE,
    VOXEL_SFX_ATTACK,
    VOXEL_SFX_HIT,
    VOXEL_SFX_JUMP,
    VOXEL_SFX_KIND_COUNT,
};

typedef struct {
    uint16_t phase;
    uint16_t step;
    uint16_t samples_left;
    uint16_t samples_total;
    uint8_t kind;
} voxel_sfx_voice_t;

typedef struct {
    uint32_t permission_handle;
    uint32_t session_handle;
    uint32_t noise_state;
    uint64_t tick_us;
    uint32_t remainder_us;
    uint8_t state;
    uint8_t clock_started;
    uint8_t payload[96];
    voxel_sfx_voice_t voices[VOXEL_SFX_VOICES];
    int16_t frame[VOXEL_SFX_FRAME_SAMPLES];
} voxel_sfx_t;

static inline int16_t voxel_sfx_sine(uint16_t phase) {
    const int32_t x = (int16_t)phase;
    const uint32_t magnitude = (uint32_t)(x < 0 ? -(int64_t)x : x);
    int32_t sample = (int32_t)((int64_t)4 * x * (32768u - magnitude) / 32768);
    const uint32_t sample_magnitude =
        (uint32_t)(sample < 0 ? -(int64_t)sample : sample);
    const int32_t correction =
        (int32_t)((int64_t)sample * sample_magnitude / 32768) - sample;
    sample += (int32_t)((int64_t)7373 * correction / 32768);
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static inline int16_t voxel_sfx_square(uint16_t phase) {
    return (phase & UINT16_C(0x8000)) != 0 ? INT16_MAX : INT16_MIN;
}

static inline int16_t voxel_sfx_triangle(uint16_t phase) {
    const uint16_t ramp = (phase & UINT16_C(0x8000)) != 0
                              ? (uint16_t)(UINT16_MAX - phase)
                              : phase;
    return (int16_t)(((int32_t)ramp - 16384) * 2);
}

static inline int16_t voxel_sfx_noise(voxel_sfx_t *sfx) {
    sfx->noise_state = sfx->noise_state * UINT32_C(1664525) +
                       UINT32_C(1013904223);
    return (int16_t)(sfx->noise_state >> 16);
}

static inline int16_t voxel_sfx_limit(int32_t sample) {
    const uint32_t headroom =
        (uint32_t)INT16_MAX - VOXEL_SFX_LIMIT_THRESHOLD;
    const int negative = sample < 0;
    uint32_t magnitude =
        (uint32_t)(negative ? -(int64_t)sample : (int64_t)sample);
    if (magnitude > VOXEL_SFX_LIMIT_THRESHOLD) {
        const uint32_t excess = magnitude - VOXEL_SFX_LIMIT_THRESHOLD;
        magnitude = VOXEL_SFX_LIMIT_THRESHOLD +
                    (uint32_t)((uint64_t)excess * headroom /
                               (excess + headroom));
    }
    return (int16_t)(negative ? -(int32_t)magnitude : (int32_t)magnitude);
}

static inline void voxel_sfx_mix(voxel_sfx_t *sfx) {
    static const uint16_t base_steps[VOXEL_SFX_KIND_COUNT] = {
        2600, 900, 1500, 1900, 1200, 1400,
    };
    static const int16_t amplitudes[VOXEL_SFX_KIND_COUNT] = {
        7200, 11000, 9000, 7000, 9000, 8000,
    };
    int index;
    for (index = 0; index < (int)VOXEL_SFX_FRAME_SAMPLES; ++index) {
        int32_t mixed = 0;
        int voice_index;
        for (voice_index = 0; voice_index < (int)VOXEL_SFX_VOICES;
             ++voice_index) {
            voxel_sfx_voice_t *voice = &sfx->voices[voice_index];
            const uint8_t kind = voice->kind;
            uint16_t step;
            uint32_t elapsed;
            uint32_t envelope;
            int32_t tone;
            if (voice->samples_left == 0 || kind >= VOXEL_SFX_KIND_COUNT) {
                continue;
            }
            elapsed = (uint32_t)(voice->samples_total - voice->samples_left);
            step = base_steps[kind];
            switch (kind) {
                case VOXEL_SFX_MINE:
                    if (elapsed < 240u) {
                        step = (uint16_t)(step + (240u - elapsed) * 3u);
                    }
                    tone = (int32_t)voxel_sfx_square(voice->phase) / 3 +
                           (int32_t)voxel_sfx_triangle(voice->phase) / 4;
                    break;
                case VOXEL_SFX_BREAK:
                    tone = (int32_t)voxel_sfx_noise(sfx) * 2 / 3 +
                           (int32_t)voxel_sfx_triangle(voice->phase) / 3;
                    if (elapsed < 900u) {
                        step = (uint16_t)(step + (900u - elapsed));
                    }
                    break;
                case VOXEL_SFX_PLACE:
                    tone = (int32_t)voxel_sfx_triangle(voice->phase) * 3 / 4 +
                           (int32_t)voxel_sfx_noise(sfx) / 6;
                    break;
                case VOXEL_SFX_ATTACK:
                    tone = (int32_t)voxel_sfx_noise(sfx) * 3 / 4 +
                           (int32_t)voxel_sfx_square(voice->phase) / 8;
                    step = (uint16_t)(step + elapsed / 3u);
                    break;
                case VOXEL_SFX_HIT:
                    tone = (int32_t)voxel_sfx_noise(sfx) / 2 +
                           (int32_t)voxel_sfx_square(voice->phase) / 3;
                    break;
                default:
                    tone = (int32_t)voxel_sfx_sine(voice->phase) * 3 / 4 +
                           (int32_t)voxel_sfx_triangle(voice->phase) / 4;
                    step = (uint16_t)(step + elapsed * 2u);
                    break;
            }
            if (elapsed < 24u) {
                envelope = elapsed * 800u / 24u;
            } else if (voice->samples_left < 128u) {
                envelope = (uint32_t)voice->samples_left * 800u / 128u;
            } else {
                envelope = 800u;
            }
            mixed += tone * amplitudes[kind] / 32768 * (int32_t)envelope / 800;
            voice->phase = (uint16_t)(voice->phase + step);
            --voice->samples_left;
        }
        sfx->frame[index] = voxel_sfx_limit(mixed);
    }
}

static inline int voxel_sfx_submit_frame(voxel_sfx_t *sfx) {
    int32_t result;
    if (sfx == NULL || sfx->state != VOXEL_SFX_READY) {
        return 0;
    }
    voxel_sfx_mix(sfx);
    result = pxa_audio_write_pcm(sfx->session_handle, (uint8_t *)sfx->frame,
                                 sizeof(sfx->frame));
    return result == (int32_t)sizeof(sfx->frame) ||
           result == PXA_STATUS_WOULD_BLOCK;
}

static inline void voxel_sfx_start(voxel_sfx_t *sfx, uint8_t *packet,
                                   size_t packet_capacity) {
    static const char permission_name[] = "audio.playback";
    static const uint8_t permission_scope[] = "media";
    if (sfx == NULL || packet == NULL || sfx->state != VOXEL_SFX_OFF) {
        return;
    }
    sfx->state = VOXEL_SFX_WAIT_PERMISSION;
    if (!pxa_permission_acquire(VOXEL_SFX_PERMISSION_REQUEST, permission_name,
                                sizeof(permission_name) - 1, permission_scope,
                                sizeof(permission_scope) - 1, sfx->payload,
                                sizeof(sfx->payload), packet, packet_capacity)) {
        sfx->state = VOXEL_SFX_UNAVAILABLE;
    }
}

static inline int voxel_sfx_handle_event(voxel_sfx_t *sfx,
                                         const pxa_event_t *event,
                                         uint8_t *packet,
                                         size_t packet_capacity) {
    if (sfx == NULL || event == NULL || packet == NULL) {
        return 0;
    }
    if (event->service == PXA_SERVICE_PERMISSION &&
        event->opcode == PXA_PERMISSION_ACQUIRE &&
        event->request_id == VOXEL_SFX_PERMISSION_REQUEST) {
        pxa_permission_acquire_result_t result;
        if (!pxa_permission_parse_acquire(event, &result) ||
            result.status != PXA_STATUS_OK) {
            sfx->state = VOXEL_SFX_UNAVAILABLE;
            return 1;
        }
        sfx->permission_handle = result.handle;
        sfx->state = VOXEL_SFX_WAIT_OPEN;
        if (!pxa_audio_open_media(VOXEL_SFX_OPEN_REQUEST,
                                  sfx->permission_handle, sfx->payload,
                                  sizeof(sfx->payload), packet,
                                  packet_capacity)) {
            sfx->state = VOXEL_SFX_UNAVAILABLE;
        }
        return 1;
    }
    if (event->service == PXA_SERVICE_AUDIO &&
        event->opcode == PXA_AUDIO_OPEN_SESSION &&
        event->request_id == VOXEL_SFX_OPEN_REQUEST) {
        pxa_audio_open_result_t result;
        if (!pxa_audio_parse_open(event, &result) ||
            result.status != PXA_STATUS_OK ||
            result.sample_rate != VOXEL_SFX_SAMPLE_RATE ||
            result.channels != 1 || result.frame_ms != 20) {
            sfx->state = VOXEL_SFX_UNAVAILABLE;
            return 1;
        }
        sfx->session_handle = result.session_handle;
        sfx->state = VOXEL_SFX_WAIT_GRAPH;
        if (!pxa_audio_commit_speaker_graph(
                VOXEL_SFX_GRAPH_REQUEST, sfx->session_handle,
                VOXEL_SFX_GRAPH_GAIN_DB_Q8, 1500, 256, 256, sfx->payload,
                sizeof(sfx->payload), packet, packet_capacity)) {
            sfx->state = VOXEL_SFX_UNAVAILABLE;
        }
        return 1;
    }
    if (event->service == PXA_SERVICE_AUDIO &&
        event->opcode == PXA_AUDIO_COMMIT_GRAPH &&
        event->request_id == VOXEL_SFX_GRAPH_REQUEST) {
        int32_t status;
        if (!pxa_audio_parse_status(event, PXA_AUDIO_COMMIT_GRAPH, &status) ||
            status != PXA_STATUS_OK) {
            sfx->state = VOXEL_SFX_UNAVAILABLE;
        } else {
            sfx->state = VOXEL_SFX_READY;
            if (sfx->noise_state == 0) {
                sfx->noise_state = UINT32_C(0x9E3779B9);
            }
            (void)voxel_sfx_submit_frame(sfx);
            (void)voxel_sfx_submit_frame(sfx);
        }
        return 1;
    }
    return 0;
}

static inline void voxel_sfx_play(voxel_sfx_t *sfx, uint8_t kind) {
    static const uint8_t frames[VOXEL_SFX_KIND_COUNT] = {2, 6, 4, 3, 4, 3};
    uint8_t voice_index = 0;
    uint16_t shortest = UINT16_MAX;
    uint8_t index;
    if (sfx == NULL || sfx->state != VOXEL_SFX_READY ||
        kind >= VOXEL_SFX_KIND_COUNT) {
        return;
    }
    for (index = 0; index < VOXEL_SFX_VOICES; ++index) {
        if (sfx->voices[index].samples_left == 0) {
            voice_index = index;
            shortest = 0;
            break;
        }
        if (sfx->voices[index].samples_left < shortest) {
            shortest = sfx->voices[index].samples_left;
            voice_index = index;
        }
    }
    sfx->voices[voice_index].kind = kind;
    sfx->voices[voice_index].phase = 0;
    sfx->voices[voice_index].samples_total =
        (uint16_t)(frames[kind] * VOXEL_SFX_FRAME_SAMPLES);
    sfx->voices[voice_index].samples_left =
        sfx->voices[voice_index].samples_total;
}

static inline void voxel_sfx_tick(voxel_sfx_t *sfx, const pxa_event_t *event) {
    uint64_t timestamp_us;
    uint64_t elapsed_us;
    uint8_t frames = 0;
    if (sfx == NULL || event == NULL || sfx->state != VOXEL_SFX_READY ||
        event->service != PXA_SERVICE_CLOCK || event->opcode != PXA_CLOCK_TICK ||
        event->payload_length != 8) {
        return;
    }
    timestamp_us = pxa_read_u64(event->payload);
    if (!sfx->clock_started) {
        sfx->clock_started = 1;
        sfx->tick_us = timestamp_us;
        frames = 2;
    } else {
        elapsed_us = timestamp_us - sfx->tick_us;
        sfx->tick_us = timestamp_us;
        if (elapsed_us > 100000u) {
            elapsed_us = 100000u;
        }
        sfx->remainder_us += (uint32_t)elapsed_us;
        while (sfx->remainder_us >= VOXEL_SFX_FRAME_US &&
               frames < VOXEL_SFX_MAX_FRAMES_PER_TICK) {
            sfx->remainder_us -= VOXEL_SFX_FRAME_US;
            ++frames;
        }
    }
    while (frames-- != 0) {
        if (!voxel_sfx_submit_frame(sfx)) {
            break;
        }
    }
}

#endif
