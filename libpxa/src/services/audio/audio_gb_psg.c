/* SPDX-License-Identifier: MPL-2.0
 * GB synthesis arithmetic adapted from mGBA src/gb/audio.c.
 * Copyright (c) 2013-2016 Jeffrey Pfau.
 * Upstream fd9dc5c2cbf234365068855851107e762fc6211b.
 * Register decoding, frame sequencer, scheduling and emulator pointers are
 * deliberately outside this synchronous PCM kernel. */
#include "pxa/audio_gb_psg.h"
#include "pxa/wire.h"
#include <limits.h>
#include <string.h>

typedef struct {
    uint32_t last, period;
    uint8_t index, duty, volume, sample, dead;
} square_t;
typedef struct {
    uint32_t time, interval, cap[2], next_wave, wave_period;
    uint32_t last_noise, noise_period, lfsr, noise_count, noise_sum;
    square_t square[2];
    uint16_t master;
    uint8_t volume[2], enabled, playing, routes[2], disabled, style;
    uint8_t window, wave_volume, wave_sample, readable, wave[16];
    uint8_t noise_sample, noise_volume, short_noise;
} state_t;
static int32_t signed32(uint32_t n) {
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static int16_t signed16(uint32_t n) {
    n &= 65535u;
    return n <= INT16_MAX ? (int16_t)n : (int16_t)(-1 - (int32_t)(65535u - n));
}
static int range(uint32_t offset, uint32_t bytes, size_t size) {
    return offset <= size && bytes <= size - offset;
}
static int separate(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn || (uintptr_t)b >= (uintptr_t)a + an;
}
static int zero(const uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (p[i]) return 0;
    return 1;
}
static const uint8_t duty_masks[4] = {0x80, 0x81, 0xe1, 0x7e};
static const uint8_t noise_mask[64] = {
    0x3f,0x3e,0x3c,0x3d,0x39,0x38,0x3a,0x3b,0x33,0x32,0x30,0x31,0x35,0x34,0x36,0x37,
    0x27,0x26,0x24,0x25,0x21,0x20,0x22,0x23,0x2b,0x2a,0x28,0x29,0x2d,0x2c,0x2e,0x2f,
    0x0f,0x0e,0x0c,0x0d,0x09,0x08,0x0a,0x0b,0x03,0x02,0x00,0x01,0x05,0x04,0x06,0x07,
    0x17,0x16,0x14,0x15,0x11,0x10,0x12,0x13,0x1b,0x1a,0x18,0x19,0x1d,0x1c,0x1e,0x1f};
static const uint8_t noise_population[64] = {
    6,5,4,5,4,3,4,5,4,3,2,3,4,3,4,5,4,3,2,3,2,1,2,3,4,3,2,3,4,3,4,5,
    4,3,2,3,2,1,2,3,2,1,0,1,2,1,2,3,4,3,2,3,2,1,2,3,4,3,2,3,4,3,4,5};

static pxa_status_t decode(state_t *s, const uint8_t *p, unsigned frames) {
    if (pxa_read_u16(p) != 1) return PXA_STATUS_UNSUPPORTED;
    if (pxa_read_u16(p + 2) || pxa_read_u32(p + 12) != 2 ||
        pxa_read_u32(p + 8) != 64 || p[28] > 1 || p[29] > 15 ||
        p[30] > 15 || p[31] > 15 || p[32] > 15 || p[33] > 2 ||
        p[26] > 7 || p[27] > 7 || pxa_read_u16(p + 24) > 256 ||
        !zero(p + 34, 2) || !zero(p + 120, 8)) return PXA_STATUS_INVALID_ARGUMENT;
    s->time = pxa_read_u32(p + 4); s->interval = 64;
    s->cap[0] = pxa_read_u32(p + 16); s->cap[1] = pxa_read_u32(p + 20);
    s->master = pxa_read_u16(p + 24); s->volume[0] = p[26]; s->volume[1] = p[27];
    s->enabled = p[28]; s->playing = p[29]; s->routes[0] = p[30]; s->routes[1] = p[31];
    s->disabled = p[32]; s->style = p[33];
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *q = p + 36 + 16 * i;
        square_t *c = s->square + i;
        c->last = pxa_read_u32(q); c->period = pxa_read_u32(q + 4);
        c->index = q[8]; c->duty = q[9]; c->volume = q[10]; c->sample = q[11]; c->dead = q[12];
        if (c->period < 8 || c->period > 16384 || c->period % 8 || c->index > 7 ||
            c->duty > 3 || c->volume > 15 || c->sample > 15 || c->dead > 2 ||
            !zero(q + 13, 3)) return PXA_STATUS_INVALID_ARGUMENT;
    }
    s->next_wave = pxa_read_u32(p + 68); s->wave_period = pxa_read_u32(p + 72);
    s->window = p[76]; s->wave_volume = p[77]; s->wave_sample = p[78]; s->readable = p[79];
    memcpy(s->wave, p + 80, 16);
    if (s->wave_period < 4 || s->wave_period > 8192 || s->wave_period % 4 ||
        s->window > 31 || s->wave_volume > 3 || s->wave_sample > 15 || s->readable > 1)
        return PXA_STATUS_INVALID_ARGUMENT;
    s->last_noise = pxa_read_u32(p + 96); s->noise_period = pxa_read_u32(p + 100);
    s->lfsr = pxa_read_u32(p + 104); s->noise_count = pxa_read_u32(p + 108);
    s->noise_sum = pxa_read_u32(p + 112); s->noise_sample = p[116];
    s->noise_volume = p[117]; s->short_noise = p[118];
    if (s->noise_period < 16 || s->noise_period > 7340032 || s->noise_period % 16 ||
        s->lfsr > 32767 || s->noise_sample > 15 || s->noise_volume > 15 ||
        s->short_noise > 1 || p[119] || s->noise_count > INT32_MAX || s->noise_sum > INT32_MAX)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)s->noise_sum > 18u * (uint64_t)s->noise_count + 15u)
        return PXA_STATUS_INVALID_ARGUMENT;
    int32_t first_diff = signed32(s->time - s->last_noise);
    int32_t diff = signed32(s->time + (frames - 1u) * 64u - s->last_noise);
    uint32_t greatest = first_diff >= 0 && diff < 0 ? INT32_MAX : diff > 0 ? (uint32_t)diff : 0;
    uint32_t steps = s->enabled && (s->playing & 8) ? greatest / s->noise_period : 0;
    if (steps > PXA_AUDIO_GB_PSG_MAX_NOISE_STEPS ||
        steps > (INT32_MAX - s->noise_count) ||
        (uint64_t)s->noise_sum + 18u * (uint64_t)steps > INT32_MAX / 8)
        return PXA_STATUS_LIMIT_EXCEEDED;
    return PXA_STATUS_OK;
}
static void advance(state_t *s) {
    if (!s->enabled) return;
    for (unsigned i = 0; i < 2; ++i) {
        square_t *c = s->square + i;
        int32_t diff = signed32(s->time - c->last);
        if ((((s->playing >> i) & 1) && c->dead != 2) || diff > 0x40000000) {
            if (diff >= (int32_t)c->period) {
                uint32_t steps = (uint32_t)diff / c->period;
                c->index = (c->index + steps) & 7;
                c->last += steps * c->period;
                c->sample = ((duty_masks[c->duty] >> c->index) & 1) * c->volume;
            }
        }
    }
    if (s->playing & 4) {
        int32_t diff = signed32(s->time - s->next_wave);
        if (diff >= 0) {
            uint32_t steps = (uint32_t)diff / s->wave_period + 1u;
            s->window = (s->window + steps) & 31;
            uint8_t sample = s->wave[s->window >> 1];
            if (!(s->window & 1)) sample >>= 4;
            sample &= 15;
            s->wave_sample = sample >> (s->wave_volume ? s->wave_volume - 1 : 4);
            s->next_wave += steps * s->wave_period;
            s->readable = 1;
        }
        if (!s->style && s->readable && signed32(s->time - s->next_wave + s->wave_period) >= 4)
            s->readable = 0;
    }
    if (s->playing & 8) {
        int32_t diff = signed32(s->time - s->last_noise);
        if (diff >= (int32_t)s->noise_period) {
            uint32_t steps = (uint32_t)diff / s->noise_period, remaining = steps;
            uint32_t positive = 0, bit = 0;
            if (!s->short_noise) {
                uint32_t bits = 0;
                for (; remaining >= 5; remaining -= 5) {
                    bits = s->lfsr & 63;
                    s->lfsr = ((s->lfsr >> 5) | (0x4000u * noise_mask[bits] >> 4)) & 32767;
                    positive += noise_population[bits];
                }
                bit = noise_mask[bits] & 1;
            }
            uint32_t coefficient = s->short_noise ? 0x4040 : 0x4000;
            for (; remaining; --remaining) {
                bit = (s->lfsr ^ (s->lfsr >> 1) ^ 1) & 1;
                s->lfsr >>= 1;
                if (bit) s->lfsr |= coefficient;
                else s->lfsr &= ~coefficient;
                positive += bit;
            }
            s->noise_sample = bit * s->noise_volume;
            s->noise_count += steps; s->noise_sum += positive * s->noise_volume;
            s->last_noise += steps * s->noise_period;
        }
    }
}
static void encode_updates(uint8_t *p, const state_t *s) {
    pxa_write_u32(p + 4, s->time);
    pxa_write_u32(p + 16, s->cap[0]); pxa_write_u32(p + 20, s->cap[1]);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *q = p + 36 + 16 * i;
        pxa_write_u32(q, s->square[i].last);
        q[8] = s->square[i].index; q[11] = s->square[i].sample;
    }
    pxa_write_u32(p + 68, s->next_wave); p[76] = s->window; p[78] = s->wave_sample; p[79] = s->readable;
    pxa_write_u32(p + 96, s->last_noise); pxa_write_u32(p + 104, s->lfsr);
    pxa_write_u32(p + 108, s->noise_count); pxa_write_u32(p + 112, s->noise_sum); p[116] = s->noise_sample;
}
pxa_status_t pxa_audio_gb_psg_render(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size) {
    if (!packet || !memory || size != 16 || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size) return PXA_STATUS_INVALID_ARGUMENT;
    uint32_t state_offset = pxa_read_u32(packet), output_offset = pxa_read_u32(packet + 4);
    unsigned frames = pxa_read_u16(packet + 8);
    if (pxa_read_u16(packet + 10) != 1) return PXA_STATUS_UNSUPPORTED;
    if (!frames || pxa_read_u32(packet + 12)) return PXA_STATUS_INVALID_ARGUMENT;
    if (frames > PXA_AUDIO_GB_PSG_MAX_FRAMES) return PXA_STATUS_LIMIT_EXCEEDED;
    if (!range(state_offset, 128, memory_size) || !range(output_offset, frames * 4u, memory_size))
        return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t *state = memory + state_offset, *output = memory + output_offset;
    if (!separate(state, 128, packet, size) || !separate(output, frames * 4u, packet, size) ||
        !separate(state, 128, output, frames * 4u)) return PXA_STATUS_INVALID_ARGUMENT;
    state_t s;
    pxa_status_t status = decode(&s, state, frames);
    if (status) return status;
    for (unsigned n = 0; n < frames; ++n) {
        advance(&s);
        int noise = s.noise_sample * 8;
        if (!(s.disabled & 8) && s.noise_count > 1) {
            noise = (int)((s.noise_sum * 8u) / s.noise_count);
            s.noise_count = s.noise_sum = 0;
        }
        const int samples[3] = {s.square[0].sample, s.square[1].sample, s.wave_sample};
        for (unsigned c = 0; c < 2; ++c) {
            int value = -8;
            unsigned routes = s.routes[c] & ~s.disabled;
            for (unsigned i = 0; i < 3; ++i) if (routes & (1u << i)) value += samples[i];
            value *= 8;
            if (routes & 8) value += noise;
            value *= 1 + s.volume[c];
            int16_t sample = signed16((uint32_t)((value * s.master * 6) >> 7));
            int16_t degraded = signed16((uint32_t)((int)sample - (signed32(s.cap[c]) >> 16)));
            s.cap[c] = (uint32_t)((int32_t)sample * 65536) - (uint32_t)(degraded * 65368);
            pxa_write_u16(output + n * 4 + c * 2, (uint16_t)degraded);
        }
        s.time += 64;
    }
    encode_updates(state, &s);
    return PXA_STATUS_OK;
}
