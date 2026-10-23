#ifndef PXA_GUEST_AUDIO_PSG_H
#define PXA_GUEST_AUDIO_PSG_H

#include "pxa_core.h"

#define PXA_AUDIO_IO_PSG_RENDER UINT32_C(0x10a)
#define PXA_AUDIO_IO_GB_PSG_RENDER UINT32_C(0x10b)
#define PXA_AUDIO_GB_PSG_HEADER_BYTES 16u
#define PXA_AUDIO_GB_PSG_STATE_BYTES 128u
#define PXA_AUDIO_GB_PSG_MAX_FRAMES 128u
#define PXA_AUDIO_PSG_HEADER_BYTES 20u
#define PXA_AUDIO_PSG_VOICE_BYTES 40u
#define PXA_AUDIO_PSG_MAX_VOICES 8u
#define PXA_AUDIO_PSG_MAX_FRAMES 4096u
#define PXA_AUDIO_PSG_MAX_WORK 16384u
#define PXA_AUDIO_PSG_MAX_NOISE_STEPS 262144u
#define PXA_AUDIO_PSG_MAX_TICKS 1048576u
#define PXA_AUDIO_PSG_PULSE 1u
#define PXA_AUDIO_PSG_WAVE 2u
#define PXA_AUDIO_PSG_NOISE 3u

/* Native convenience structure, NOT a wire layout. Encode once, retain the
 * encoded state across batches; decode only when the Guest needs its phase.
 * Clock units are caller-defined integers, shared by all periods in a batch. */
typedef struct {
    uint32_t period, phase, cursor, envelope_period, envelope_phase;
    uint32_t table_offset;
    uint16_t left_gain_q8, right_gain_q8;
    uint8_t waveform, invert_feedback, noise_width, noise_tap;
    uint8_t level, pulse_mask, table_length;
    int8_t envelope_direction;
} pxa_audio_psg_voice_t;

static inline int pxa_audio_psg_voice_valid(const pxa_audio_psg_voice_t *v) {
    if (!v || v->waveform < PXA_AUDIO_PSG_PULSE || v->waveform > PXA_AUDIO_PSG_NOISE ||
        !v->period || v->period > INT32_MAX || v->phase >= v->period ||
        v->envelope_period > INT32_MAX || (v->envelope_period ?
        v->envelope_phase >= v->envelope_period : v->envelope_phase != 0) ||
        v->left_gain_q8 > 256 || v->right_gain_q8 > 256 || v->level > 15 ||
        v->envelope_direction < -1 || v->envelope_direction > 1)
        return 0;
    if (v->waveform != PXA_AUDIO_PSG_NOISE &&
        (v->invert_feedback || v->noise_width || v->noise_tap)) return 0;
    if (v->waveform != PXA_AUDIO_PSG_PULSE && v->pulse_mask) return 0;
    if (v->waveform != PXA_AUDIO_PSG_WAVE && (v->table_offset || v->table_length)) return 0;
    if (v->waveform == PXA_AUDIO_PSG_PULSE) return v->cursor < 8;
    if (v->waveform == PXA_AUDIO_PSG_WAVE)
        return v->table_length && v->table_length <= 32 && v->cursor < v->table_length;
    return v->invert_feedback <= 1 && (v->noise_width == 7 || v->noise_width == 15) &&
        v->noise_tap && v->noise_tap < v->noise_width && v->cursor < (1u << v->noise_width);
}

static inline int pxa_audio_encode_psg_voice(uint8_t *out, size_t size,
    const pxa_audio_psg_voice_t *v) {
    if (!out || size < PXA_AUDIO_PSG_VOICE_BYTES || !pxa_audio_psg_voice_valid(v)) return 0;
    uintptr_t target = (uintptr_t)out, source = (uintptr_t)v;
    if (target < source ? source - target < PXA_AUDIO_PSG_VOICE_BYTES :
        target - source < sizeof(*v)) return 0;
    out[0] = v->waveform; out[1] = v->invert_feedback;
    out[2] = v->noise_width; out[3] = v->noise_tap;
    pxa_store_u32(out + 4, v->period); pxa_store_u32(out + 8, v->phase);
    pxa_store_u32(out + 12, v->cursor); pxa_store_u32(out + 16, v->envelope_period);
    pxa_store_u32(out + 20, v->envelope_phase);
    pxa_store_u16(out + 24, v->left_gain_q8); pxa_store_u16(out + 26, v->right_gain_q8);
    out[28] = v->level; out[29] = (uint8_t)v->envelope_direction;
    out[30] = v->pulse_mask; out[31] = v->table_length;
    pxa_store_u32(out + 32, v->table_offset); pxa_store_u32(out + 36, 0);
    return 1;
}
static inline int pxa_audio_decode_psg_voice(const uint8_t *in, size_t size,
    pxa_audio_psg_voice_t *out) {
    if (!in || size < PXA_AUDIO_PSG_VOICE_BYTES || !out || pxa_load_u32(in + 36)) return 0;
    pxa_audio_psg_voice_t v;
    v.waveform = in[0]; v.invert_feedback = in[1];
    v.noise_width = in[2]; v.noise_tap = in[3];
    v.period = pxa_load_u32(in + 4); v.phase = pxa_load_u32(in + 8);
    v.cursor = pxa_load_u32(in + 12); v.envelope_period = pxa_load_u32(in + 16);
    v.envelope_phase = pxa_load_u32(in + 20);
    v.left_gain_q8 = pxa_load_u16(in + 24); v.right_gain_q8 = pxa_load_u16(in + 26);
    v.level = in[28]; v.envelope_direction = in[29] == 255 ? -1 : in[29] <= 1 ? (int8_t)in[29] : 2;
    v.pulse_mask = in[30]; v.table_length = in[31]; v.table_offset = pxa_load_u32(in + 32);
    if (!pxa_audio_psg_voice_valid(&v)) return 0;
    *out = v;
    return 1;
}

/* No playback side effect. State and interleaved S32LE target are borrowed
 * until return; the Host validates the entire batch before touching either.
 * Old Hosts return UNSUPPORTED: the application retains its software APU. */
static inline int32_t pxa_audio_psg_render(uint64_t session, uint32_t target_offset,
    uint32_t frames, uint32_t voices_offset, uint16_t count, uint32_t ticks_per_sample) {
    if (!(session >> 32) || !frames || frames > PXA_AUDIO_PSG_MAX_FRAMES ||
        !count || count > PXA_AUDIO_PSG_MAX_VOICES || frames * count > PXA_AUDIO_PSG_MAX_WORK ||
        !ticks_per_sample || ticks_per_sample > PXA_AUDIO_PSG_MAX_TICKS)
        return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t header[PXA_AUDIO_PSG_HEADER_BYTES] = {0};
    pxa_store_u32(header, target_offset); pxa_store_u32(header + 4, frames);
    pxa_store_u32(header + 8, voices_offset); pxa_store_u32(header + 12, ticks_per_sample);
    pxa_store_u16(header + 16, count);
    int32_t result = pxa_io(session, PXA_AUDIO_IO_PSG_RENDER, header, sizeof(header));
    return result < 0 ? result : result == sizeof(header) ? 0 : PXA_STATUS_PROTOCOL_ERROR;
}

/* Profile 1: GB synthesis/analog filter, preserving CPU-visible channel state.
 * Encoded 128-byte state and S16LE stereo output are updated atomically after
 * validation. No playback is enqueued. Old Hosts return UNSUPPORTED. */
static inline int32_t pxa_audio_gb_psg_render(uint64_t session, uint32_t state_offset,
    uint32_t output_offset, uint16_t frames) {
    if (!(session >> 32) || !frames || frames > PXA_AUDIO_GB_PSG_MAX_FRAMES)
        return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t command[PXA_AUDIO_GB_PSG_HEADER_BYTES] = {0};
    pxa_store_u32(command, state_offset); pxa_store_u32(command + 4, output_offset);
    pxa_store_u16(command + 8, frames); pxa_store_u16(command + 10, 1);
    int32_t result = pxa_io(session, PXA_AUDIO_IO_GB_PSG_RENDER, command, sizeof(command));
    return result < 0 ? result : result == sizeof(command) ? 0 : PXA_STATUS_PROTOCOL_ERROR;
}

#endif
