#include "pxa/audio_psg.h"
#include "pxa/wire.h"
#include <limits.h>
#include <string.h>

static int range(uint32_t offset, uint32_t bytes, size_t size) {
    return bytes && offset <= size && bytes <= size - offset;
}
static int disjoint(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn ||
           (uintptr_t)b >= (uintptr_t)a + an;
}
static void accumulate(uint8_t *out, int32_t value) {
    int32_t old;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&old, __builtin_assume_aligned(out, 4), 4);
#else
    uint32_t bits = pxa_read_u32(out);
    old = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
#endif
    int32_t result = value > 0 && old > INT32_MAX - value ? INT32_MAX :
        value < 0 && old < INT32_MIN - value ? INT32_MIN : old + value;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(__builtin_assume_aligned(out, 4), &result, 4);
#else
    pxa_write_u32(out, (uint32_t)result);
#endif
}

pxa_status_t pxa_audio_psg_render(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size) {
    if (!packet || !memory || size != PXA_AUDIO_PSG_HEADER_BYTES ||
        (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size)
        return PXA_STATUS_INVALID_ARGUMENT;
    uint32_t target_offset = pxa_read_u32(packet), frames = pxa_read_u32(packet + 4);
    uint32_t voices_offset = pxa_read_u32(packet + 8), ticks = pxa_read_u32(packet + 12);
    uint32_t count = pxa_read_u16(packet + 16);
    if (!frames || !count || !ticks || pxa_read_u16(packet + 18))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (frames > PXA_AUDIO_PSG_MAX_FRAMES || count > PXA_AUDIO_PSG_MAX_VOICES ||
        ticks > PXA_AUDIO_PSG_MAX_TICKS || frames * count > PXA_AUDIO_PSG_MAX_WORK)
        return PXA_STATUS_LIMIT_EXCEEDED;
    uint32_t target_bytes = frames * 8u, voice_bytes = count * PXA_AUDIO_PSG_VOICE_BYTES;
    if (!range(target_offset, target_bytes, memory_size) ||
        !range(voices_offset, voice_bytes, memory_size) ||
        ((uintptr_t)(memory + target_offset) & 3u))
        return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t *target = memory + target_offset, *voices = memory + voices_offset;
    if (!disjoint(target, target_bytes, voices, voice_bytes) ||
        !disjoint(target, target_bytes, packet, size) ||
        !disjoint(voices, voice_bytes, packet, size))
        return PXA_STATUS_INVALID_ARGUMENT;
    uint64_t noise_work = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *v = voices + i * PXA_AUDIO_PSG_VOICE_BYTES;
        uint32_t period = pxa_read_u32(v + 4), phase = pxa_read_u32(v + 8);
        uint32_t cursor = pxa_read_u32(v + 12), env_period = pxa_read_u32(v + 16);
        uint32_t env_phase = pxa_read_u32(v + 20), table = pxa_read_u32(v + 32);
        if (v[0] < PXA_AUDIO_PSG_PULSE || v[0] > PXA_AUDIO_PSG_NOISE ||
            !period || period > INT32_MAX || phase >= period || env_period > INT32_MAX ||
            (env_period ? env_phase >= env_period : env_phase != 0) ||
            pxa_read_u16(v + 24) > 256 || pxa_read_u16(v + 26) > 256 ||
            v[28] > 15 || (v[29] != 0 && v[29] != 1 && v[29] != 255) ||
            pxa_read_u32(v + 36))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (v[0] != PXA_AUDIO_PSG_NOISE && (v[1] || v[2] || v[3]))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (v[0] != PXA_AUDIO_PSG_PULSE && v[30]) return PXA_STATUS_INVALID_ARGUMENT;
        if (v[0] != PXA_AUDIO_PSG_WAVE && (v[31] || table)) return PXA_STATUS_INVALID_ARGUMENT;
        if (v[0] == PXA_AUDIO_PSG_PULSE && cursor >= 8) return PXA_STATUS_INVALID_ARGUMENT;
        if (v[0] == PXA_AUDIO_PSG_WAVE) {
            if (!v[31] || v[31] > 32 || cursor >= v[31] ||
                !range(table, v[31], memory_size) ||
                !disjoint(memory + table, v[31], target, target_bytes) ||
                !disjoint(memory + table, v[31], voices, voice_bytes))
                return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (v[0] == PXA_AUDIO_PSG_NOISE) {
            if (v[1] > 1 || (v[2] != 7 && v[2] != 15) ||
                !v[3] || v[3] >= v[2] || cursor >= (1u << v[2]))
                return PXA_STATUS_INVALID_ARGUMENT;
            noise_work += ((uint64_t)phase + (uint64_t)frames * ticks) / period;
        }
    }
    if (noise_work > PXA_AUDIO_PSG_MAX_NOISE_STEPS) return PXA_STATUS_LIMIT_EXCEEDED;
    /* Guest execution is serialized throughout the borrow. All writable ranges
     * are disjoint from configuration sources. One voice's scalar state fits
     * on the stack; no per-session state, allocation or PCM staging is needed. */
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *v = voices + i * PXA_AUDIO_PSG_VOICE_BYTES;
        uint32_t period = pxa_read_u32(v + 4), phase = pxa_read_u32(v + 8);
        uint32_t cursor = pxa_read_u32(v + 12), env_period = pxa_read_u32(v + 16);
        uint32_t env_phase = pxa_read_u32(v + 20), level = v[28];
        int direction = v[29] == 255 ? -1 : v[29];
        uint32_t left = pxa_read_u16(v + 24), right = pxa_read_u16(v + 26);
        const uint8_t type = v[0], invert = v[1], width = v[2], tap = v[3];
        const uint8_t mask = v[30], length = v[31];
        const uint8_t *table = memory + pxa_read_u32(v + 32);
        uint8_t *out = target;
        for (uint32_t n = 0; n < frames; ++n, out += 8) {
            /* Bounds on periods/ticks ensure both sums fit u32. */
            phase += ticks;
            uint32_t steps = 0;
            if (phase >= period) {
                steps = phase / period;
                phase -= steps * period;
            }
            if (env_period) {
                env_phase += ticks;
                if (env_phase >= env_period) {
                    uint32_t steps_env = env_phase / env_period;
                    env_phase -= steps_env * env_period;
                    if (direction > 0) level += steps_env > 15 - level ? 15 - level : steps_env;
                    else if (direction < 0) level -= steps_env > level ? level : steps_env;
                }
            }
            int32_t value;
            if (type == PXA_AUDIO_PSG_PULSE) {
                cursor = (cursor + steps) & 7u;
                value = (mask >> cursor) & 1u ? 127 : -128;
            } else if (type == PXA_AUDIO_PSG_WAVE) {
                cursor = (cursor + steps) % length;
                value = table[cursor] < 128 ? table[cursor] : (int)table[cursor] - 256;
            } else {
                for (uint32_t s = 0; s < steps; ++s) {
                    uint32_t bit = (cursor ^ (cursor >> tap) ^ invert) & 1u;
                    cursor = (cursor >> 1) | (bit << (width - 1));
                }
                value = cursor & 1u ? 127 : -128;
            }
            /* S8 to S16, envelope 0..15, Q8 stereo gain. One division keeps
             * rounding toward zero consistent across native architectures. */
            accumulate(out, value * (int32_t)level * (int32_t)left / 15);
            accumulate(out + 4, value * (int32_t)level * (int32_t)right / 15);
        }
        pxa_write_u32(v + 8, phase); pxa_write_u32(v + 12, cursor);
        pxa_write_u32(v + 20, env_phase); v[28] = (uint8_t)level;
    }
    return PXA_STATUS_OK;
}
