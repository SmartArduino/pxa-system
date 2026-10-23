#undef NDEBUG
#include "pxa/audio_psg.h"
#include "pxa/wire.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { VOICES = 128, TABLE = 512, TARGET = 1024, FRAMES = 128 };
static _Alignas(8) uint8_t memory[65536], saved[65536], one_batch[65536];
static uint8_t header[20];
static uint32_t rng = 123;
static uint32_t random32(void) { return rng = rng * 1664525u + 1013904223u; }
static int32_t load32(const uint8_t *p) {
    uint32_t n = pxa_read_u32(p);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static void setup(unsigned count, unsigned frames, unsigned ticks) {
    memset(memory, 0, sizeof(memory)); memset(header, 0, sizeof(header));
    pxa_write_u32(header, TARGET); pxa_write_u32(header + 4, frames);
    pxa_write_u32(header + 8, VOICES); pxa_write_u32(header + 12, ticks);
    pxa_write_u16(header + 16, count);
}
static uint8_t *voice(unsigned i, unsigned type) {
    uint8_t *v = memory + VOICES + i * 40;
    memset(v, 0, 40); v[0] = type;
    pxa_write_u32(v + 4, 13);
    pxa_write_u16(v + 24, 256); pxa_write_u16(v + 26, 256); v[28] = 15;
    if (type == 1) v[30] = 0x81;
    if (type == 2) { v[31] = 32; pxa_write_u32(v + 32, TABLE); }
    if (type == 3) { v[2] = 15; v[3] = 1; pxa_write_u32(v + 12, 1); }
    return v;
}
static void run(void) { assert(!pxa_audio_psg_render(header, 20, memory, sizeof(memory))); }
static void rejected(pxa_status_t status) {
    memcpy(saved, memory, sizeof(memory));
    assert(pxa_audio_psg_render(header, 20, memory, sizeof(memory)) == status);
    assert(!memcmp(saved, memory, sizeof(memory)));
}
static void differential(void) {
    /* Independent per-tick model exercises simultaneous wave/envelope edges,
     * XNOR and XOR taps, zero seeds, signed rounding and nonzero accumulators. */
    for (unsigned trial = 0; trial < 1800; ++trial) {
        unsigned count = 1 + random32() % 8, frames = 1 + random32() % FRAMES;
        unsigned ticks = 1 + random32() % 31;
        setup(count, frames, ticks);
        for (unsigned n = 0; n < 32; ++n) memory[TABLE + n] = random32() >> 24;
        int32_t expected[FRAMES * 2];
        for (unsigned n = 0; n < frames * 2; ++n) {
            expected[n] = trial % 17 == 0 ? INT32_MIN : trial % 19 == 0 ?
                INT32_MAX : (int32_t)(random32() % 90000) - 45000;
            pxa_write_u32(memory + TARGET + 4 * n, (uint32_t)expected[n]);
        }
        uint8_t states[8][40];
        for (unsigned i = 0; i < count; ++i) {
            uint8_t *v = voice(i, 1 + (trial + i) % 3);
            unsigned period = 1 + random32() % 97;
            pxa_write_u32(v + 4, period); pxa_write_u32(v + 8, random32() % period);
            if (trial % 4) {
                unsigned env_period = 1 + random32() % 113;
                pxa_write_u32(v + 16, env_period);
                pxa_write_u32(v + 20, random32() % env_period);
            }
            pxa_write_u16(v + 24, random32() % 257); pxa_write_u16(v + 26, random32() % 257);
            v[28] = random32() % 16; v[29] = trial % 3 == 0 ? 255 : trial % 3 == 1 ? 0 : 1;
            if (v[0] == 1) { v[30] = random32() >> 24; pxa_write_u32(v + 12, random32() % 8); }
            if (v[0] == 2) { v[31] = 1 + random32() % 32; pxa_write_u32(v + 12, random32() % v[31]); }
            if (v[0] == 3) {
                v[1] = trial & 1; v[2] = trial & 2 ? 7 : 15;
                v[3] = 1 + random32() % (v[2] - 1);
                pxa_write_u32(v + 12, random32() % (1u << v[2]));
            }
            unsigned phase = pxa_read_u32(v + 8), cursor = pxa_read_u32(v + 12);
            unsigned ep = pxa_read_u32(v + 16), ephase = pxa_read_u32(v + 20), level = v[28];
            for (unsigned n = 0; n < frames; ++n) {
                for (unsigned t = 0; t < ticks; ++t) {
                    if (++phase == period) {
                        phase = 0;
                        if (v[0] == 1) cursor = (cursor + 1) % 8;
                        else if (v[0] == 2) cursor = (cursor + 1) % v[31];
                        else {
                            unsigned bit0 = cursor % 2, tap = (cursor / (1u << v[3])) % 2;
                            unsigned bit = bit0 != tap;
                            if (v[1]) bit = !bit;
                            cursor = cursor / 2 + bit * (1u << (v[2] - 1));
                        }
                    }
                    if (ep && ++ephase == ep) {
                        ephase = 0;
                        if (v[29] == 1 && level < 15) ++level;
                        if (v[29] == 255 && level) --level;
                    }
                }
                int32_t sample;
                if (v[0] == 1) sample = (v[30] & (1u << cursor)) ? 127 : -128;
                else if (v[0] == 2) {
                    sample = memory[TABLE + cursor]; if (sample >= 128) sample -= 256;
                } else sample = cursor % 2 ? 127 : -128;
                for (unsigned c = 0; c < 2; ++c) {
                    int64_t value = (int64_t)sample * level * pxa_read_u16(v + 24 + 2 * c) / 15;
                    value += expected[n * 2 + c];
                    expected[n * 2 + c] = value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value;
                }
            }
            memcpy(states[i], v, 40);
            pxa_write_u32(states[i] + 8, phase); pxa_write_u32(states[i] + 12, cursor);
            pxa_write_u32(states[i] + 20, ephase); states[i][28] = level;
        }
        memcpy(saved, memory, sizeof(memory));
        run();
        for (unsigned n = 0; n < frames * 2; ++n) assert(load32(memory + TARGET + 4 * n) == expected[n]);
        for (unsigned i = 0; i < count; ++i) assert(!memcmp(memory + VOICES + 40 * i, states[i], 40));
        /* Splitting the same timeline must preserve samples AND final state. */
        memcpy(one_batch, memory, sizeof(memory)); memcpy(memory, saved, sizeof(memory));
        unsigned first = frames / 2;
        if (first) { pxa_write_u32(header + 4, first); run(); }
        pxa_write_u32(header, TARGET + first * 8); pxa_write_u32(header + 4, frames - first); run();
        assert(!memcmp(memory, one_batch, sizeof(memory)));
    }
}
static void atomicity(void) {
    setup(2, 32, 64); voice(0, 1); uint8_t *v = voice(1, 2);
    /* Every byte of the final voice, followed by overlap/range/work failures.
     * Invalid final records must not change the valid first voice or output. */
    for (unsigned i = 0; i < 40; ++i) {
        uint8_t old = v[i]; v[i] = 255;
        memcpy(saved, memory, sizeof(memory));
        pxa_status_t status = pxa_audio_psg_render(header, 20, memory, sizeof(memory));
        if (status) assert(!memcmp(memory, saved, sizeof(memory)));
        memcpy(memory, saved, sizeof(memory)); v[i] = old;
    }
    pxa_write_u32(v + 32, TARGET); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(v + 32, VOICES); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(v + 32, UINT32_MAX); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(v + 32, TABLE);
    pxa_write_u32(header, VOICES); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(header, TARGET + 1); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(header, TARGET);
    header[18] = 1; rejected(PXA_STATUS_INVALID_ARGUMENT); header[18] = 0;
    pxa_write_u32(header + 4, 4097); rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u32(header + 4, 32);
    pxa_write_u16(header + 16, 9); rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u16(header + 16, 2);
    memcpy(memory + VOICES, header, 20);
    memcpy(saved, memory, sizeof(memory));
    assert(pxa_audio_psg_render(memory + VOICES, 20, memory, sizeof(memory)) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(saved, memory, sizeof(memory)));
    setup(1, 4096, 1048576); v = voice(0, 3); pxa_write_u32(v + 4, 1);
    rejected(PXA_STATUS_LIMIT_EXCEEDED);
    assert(pxa_audio_psg_render(header, 19, memory, sizeof(memory)) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_audio_psg_render(header, 20, memory, SIZE_MAX) == PXA_STATUS_INVALID_ARGUMENT);
}
static void golden(void) {
    setup(1, 8, 1); uint8_t *v = voice(0, 1); pxa_write_u32(v + 4, 1);
    v[30] = 0x0f; run();
    const int32_t expected[8] = {32512,32512,32512,-32768,-32768,-32768,-32768,32512};
    for (unsigned n = 0; n < 8; ++n) assert(load32(memory + TARGET + n * 8) == expected[n]);
    /* Known XOR 7-bit polynomial x^7+x+1, seed 1: complete period 127. */
    setup(1, 127, 1); v = voice(0, 3); v[2] = 7; pxa_write_u32(v + 4, 1); run();
    assert(pxa_read_u32(v + 12) == 1);
    unsigned positive = 0;
    for (unsigned n = 0; n < 127; ++n) positive += load32(memory + TARGET + n * 8) > 0;
    assert(positive == 64);
    /* u32 addition and 64-bit work accounting at the maximum legal period. */
    setup(1, 8, 1048576); v = voice(0, 1);
    pxa_write_u32(v + 4, INT32_MAX); pxa_write_u32(v + 8, INT32_MAX - 1);
    run(); assert(pxa_read_u32(v + 8) == 8u * 1048576 - 1);
    setup(1, 4096, 1024); v = voice(0, 3);
    pxa_write_u32(v + 4, 16); run(); // Exactly the 262144-step noise budget.
    setup(8, 2048, 1048576);
    for (unsigned i = 0; i < 8; ++i) {
        v = voice(i, 1); pxa_write_u32(v + 4, INT32_MAX);
    }
    run(); // Exactly 16384 voice-frames; pulse skipping is constant time.
}
int main(void) {
    golden(); differential(); atomicity();
    puts("PSG: golden wave/LFSR, 1800 differential timelines, split continuity and atomic rejection passed");
}
