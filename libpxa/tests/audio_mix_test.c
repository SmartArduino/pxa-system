#undef NDEBUG
#include "pxa/audio_mix.h"
#include "pxa/wire.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { TARGET = 4096, SOURCE = 40960, SPANS = 256, FRAMES = 4096 };
static _Alignas(8) uint8_t memory[131072], saved[131072];
static uint8_t header[16];
static uint32_t rng = 7;
static uint32_t random32(void) { return rng = rng * 1664525u + 1013904223u; }
static int32_t load32(const uint8_t *p) {
    uint32_t bits = pxa_read_u32(p);
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}
static void reset(unsigned count) {
    memset(header, 0, sizeof(header)); memset(memory, 0, sizeof(memory));
    pxa_write_u32(header, TARGET); pxa_write_u32(header + 4, FRAMES);
    pxa_write_u32(header + 8, SPANS); pxa_write_u16(header + 12, count);
}
static uint8_t *span(unsigned index, unsigned frames) {
    uint8_t *p = memory + SPANS + index * 32;
    memset(p, 0, 32);
    pxa_write_u32(p, SOURCE); pxa_write_u32(p + 4, 65536);
    pxa_write_u32(p + 12, 65536); pxa_write_u32(p + 20, frames);
    pxa_write_u16(p + 24, 256); pxa_write_u16(p + 26, 256);
    p[28] = PXA_AUDIO_MIX_S8; p[29] = 1;
    return p;
}
static void rejected(pxa_status_t expected) {
    memcpy(saved, memory, sizeof(memory));
    assert(pxa_audio_mix_pcm(header, sizeof(header), memory, sizeof(memory)) == expected);
    assert(!memcmp(memory, saved, sizeof(memory)));
}
static int32_t reference_sample(unsigned source, unsigned offset, unsigned encoding) {
    const uint8_t *p = memory + source + offset;
    int32_t value = p[0];
    if (encoding == PXA_AUDIO_MIX_U8) return (value - 128) * 256;
    if (encoding == PXA_AUDIO_MIX_S8) return (value >= 128 ? value - 256 : value) * 256;
    value += (int32_t)p[1] * 256;
    return value >= 32768 ? value - 65536 : value;
}
static void differential(void) {
    /* Includes signed truncation, every encoding/channel arrangement, pitch,
     * fractional positions, odd source alignment and existing accumulators. */
    for (unsigned trial = 0; trial < 1200; ++trial) {
        reset(1); uint8_t *p = span(0, 1 + random32() % 1024);
        unsigned encoding = 1 + trial % 3, channels = 1 + (trial / 3) % 2;
        unsigned width = encoding == PXA_AUDIO_MIX_S16LE ? 2 : 1;
        unsigned source = SOURCE + trial % 4;
        pxa_write_u32(p, source);
        uint32_t frames = pxa_read_u32(p + 20), first = random32() % 17;
        uint32_t position = random32() % 65536, step = 1 + random32() % (16u << 16);
        unsigned lg = random32() % 257, rg = random32() % 257;
        pxa_write_u32(p + 8, position); pxa_write_u32(p + 12, step);
        pxa_write_u32(p + 16, first);
        pxa_write_u16(p + 24, lg); pxa_write_u16(p + 26, rg);
        p[28] = encoding; p[29] = channels;
        for (unsigned n = 0; n < 65536; ++n) memory[source + n] = random32() >> 24;
        int32_t expected[2048];
        for (unsigned n = 0; n < frames; ++n) {
            unsigned index = ((position + (uint64_t)n * step) >> 16) * channels * width;
            int32_t left = reference_sample(source, index, encoding);
            int32_t right = channels == 1 ? left : reference_sample(source, index + width, encoding);
            for (unsigned c = 0; c < 2; ++c) {
                int32_t previous = trial % 11 == 0 ? INT32_MAX : trial % 13 == 0 ?
                    INT32_MIN : (int32_t)(random32() % 10000000) - 5000000;
                pxa_write_u32(memory + TARGET + (first + n) * 8 + c * 4, (uint32_t)previous);
                int64_t sum = previous + (int64_t)(c ? right : left) * (c ? rg : lg) / 256;
                expected[n * 2 + c] = sum > INT32_MAX ? INT32_MAX :
                    sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
            }
        }
        assert(!pxa_audio_mix_pcm(header, sizeof(header), memory, sizeof(memory)));
        for (unsigned n = 0; n < frames * 2; ++n)
            assert(load32(memory + TARGET + first * 8 + n * 4) == expected[n]);
    }
}
static void atomicity(void) {
    reset(2); span(0, 4096); uint8_t *p = span(1, 4096);
    memset(memory + SOURCE, 127, 65536);
    /* A late-invalid span must not publish the valid first span. */
    p[30] = 1; rejected(PXA_STATUS_INVALID_ARGUMENT); p[30] = 0;
    pxa_write_u32(p + 12, 0); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(p + 12, 16u << 16);
    pxa_write_u32(p + 8, UINT32_MAX); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(p + 8, 0);
    pxa_write_u32(p + 4, 65520); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(p + 4, 65536);
    pxa_write_u32(p, UINT32_MAX); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(p, TARGET); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(p, SOURCE);
    pxa_write_u32(header, SPANS); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(header, TARGET + 1); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(header, TARGET);
    p[28] = 4; rejected(PXA_STATUS_INVALID_ARGUMENT); p[28] = 1;
    p[29] = 3; rejected(PXA_STATUS_INVALID_ARGUMENT); p[29] = 1;
    pxa_write_u16(p + 24, 257); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(p + 24, 256);
    assert(!pxa_audio_mix_pcm(header, sizeof(header), memory, sizeof(memory)));
    assert(load32(memory + TARGET) == 127 * 256 * 2);
    reset(5);
    for (unsigned i = 0; i < 5; ++i) span(i, FRAMES);
    rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u16(header + 12, 33); rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u16(header + 12, 1);
    pxa_write_u32(header + 4, FRAMES + 1); rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u32(header + 4, FRAMES);
    memcpy(memory + TARGET, header, 16);
    memcpy(saved, memory, sizeof(memory));
    assert(pxa_audio_mix_pcm(memory + TARGET, 16, memory, sizeof(memory)) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(saved, memory, sizeof(memory)));
    assert(pxa_audio_mix_pcm(header, 15, memory, sizeof(memory)) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_audio_mix_pcm(header, 16, memory, SIZE_MAX) == PXA_STATUS_INVALID_ARGUMENT);
}
static void exact_fast_path(void) {
    reset(8);
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t *p = span(i, 1023);
        pxa_write_u16(p + 24, i * 32); pxa_write_u16(p + 26, 248 - i * 32);
    }
    for (unsigned n = 0; n < 1023; ++n) memory[SOURCE + n] = n;
    assert(!pxa_audio_mix_pcm(header, 16, memory, sizeof(memory)));
    for (unsigned n = 0; n < 1023; ++n) {
        int32_t value = (n & 255) < 128 ? (int32_t)(n & 255) : (int32_t)(n & 255) - 256;
        assert(load32(memory + TARGET + n * 8) == value * 896);
        assert(load32(memory + TARGET + n * 8 + 4) == value * (8 * 248 - 896));
    }
    reset(1); span(0, 2);
    memory[SOURCE] = 127; memory[SOURCE + 1] = 128;
    pxa_write_u32(memory + TARGET, INT32_MAX);
    pxa_write_u32(memory + TARGET + 8, (uint32_t)INT32_MIN);
    assert(!pxa_audio_mix_pcm(header, 16, memory, sizeof(memory)));
    assert(load32(memory + TARGET) == INT32_MAX);
    assert(load32(memory + TARGET + 8) == INT32_MIN);
    /* Exactly consume the bounded work allowance using all descriptor slots. */
    reset(32);
    for (unsigned i = 0; i < 32; ++i) span(i, 512);
    memset(memory + SOURCE, 127, 512);
    assert(!pxa_audio_mix_pcm(header, 16, memory, sizeof(memory)));
    assert(load32(memory + TARGET + 511 * 8) == 32 * 127 * 256);
}
int main(void) {
    differential(); atomicity(); exact_fast_path();
    puts("audio mix: formats, pitch, saturation, atomic rejection and bounded work PASS");
}
