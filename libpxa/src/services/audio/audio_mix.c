#include "pxa/audio_mix.h"
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
static inline int32_t signed32(uint32_t bits) {
    return bits <= INT32_MAX ? (int32_t)bits :
        -1 - (int32_t)(UINT32_MAX - bits);
}
static int32_t sample(const uint8_t *p, uint8_t encoding) {
    if (encoding == PXA_AUDIO_MIX_S8)
        return (p[0] < 128 ? (int32_t)p[0] : (int32_t)p[0] - 256) * 256;
    if (encoding == PXA_AUDIO_MIX_U8) return ((int32_t)p[0] - 128) * 256;
    uint32_t bits = (uint32_t)p[0] | (uint32_t)p[1] << 8;
    return bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536;
}
static void add(uint8_t *target, int32_t value) {
    int32_t previous;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&previous, __builtin_assume_aligned(target, 4), 4);
#else
    previous = signed32(pxa_read_u32(target));
#endif
    int32_t result = value > 0 && previous > INT32_MAX - value ? INT32_MAX :
        value < 0 && previous < INT32_MIN - value ? INT32_MIN : previous + value;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(__builtin_assume_aligned(target, 4), &result, 4);
#else
    pxa_write_u32(target, (uint32_t)result);
#endif
}

pxa_status_t pxa_audio_mix_pcm(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size) {
    if (!packet || !memory || size != PXA_AUDIO_MIX_HEADER_BYTES ||
        (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size)
        return PXA_STATUS_INVALID_ARGUMENT;
    const uint32_t target_offset = pxa_read_u32(packet);
    const uint32_t target_frames = pxa_read_u32(packet + 4);
    const uint32_t spans_offset = pxa_read_u32(packet + 8);
    const uint32_t count = pxa_read_u16(packet + 12);
    if (!target_frames || !count || pxa_read_u16(packet + 14))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (target_frames > PXA_AUDIO_MIX_MAX_FRAMES || count > PXA_AUDIO_MIX_MAX_SPANS)
        return PXA_STATUS_LIMIT_EXCEEDED;
    const uint32_t target_bytes = target_frames * 8u;
    const uint32_t span_bytes = count * PXA_AUDIO_MIX_SPAN_BYTES;
    if (!range(target_offset, target_bytes, memory_size) ||
        !range(spans_offset, span_bytes, memory_size) ||
        ((uintptr_t)(memory + target_offset) & 3u))
        return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t *target = memory + target_offset;
    const uint8_t *spans = memory + spans_offset;
    if (!disjoint(target, target_bytes, packet, size) ||
        !disjoint(target, target_bytes, spans, span_bytes))
        return PXA_STATUS_INVALID_ARGUMENT;
    uint32_t work = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = spans + i * PXA_AUDIO_MIX_SPAN_BYTES;
        uint32_t offset = pxa_read_u32(p), bytes = pxa_read_u32(p + 4);
        uint32_t position = pxa_read_u32(p + 8), step = pxa_read_u32(p + 12);
        uint32_t first = pxa_read_u32(p + 16), frames = pxa_read_u32(p + 20);
        uint8_t encoding = p[28], channels = p[29];
        if (!frames || first >= target_frames || frames > target_frames - first ||
            !step || step > (16u << 16) ||
            pxa_read_u16(p + 24) > 256 || pxa_read_u16(p + 26) > 256 ||
            encoding < PXA_AUDIO_MIX_S8 || encoding > PXA_AUDIO_MIX_S16LE ||
            (channels != 1 && channels != 2) || pxa_read_u16(p + 30) ||
            !range(offset, bytes, memory_size))
            return PXA_STATUS_INVALID_ARGUMENT;
        uint32_t stride = channels * (encoding == PXA_AUDIO_MIX_S16LE ? 2u : 1u);
        uint64_t last = position + (uint64_t)(frames - 1u) * step;
        if (bytes % stride || last > UINT32_MAX ||
            ((last >> 16) + 1u) * stride > bytes ||
            !disjoint(target, target_bytes, memory + offset, bytes))
            return PXA_STATUS_INVALID_ARGUMENT;
        work += frames;
        if (work > PXA_AUDIO_MIX_MAX_WORK) return PXA_STATUS_LIMIT_EXCEEDED;
    }
    /* The engine adapter serializes the Guest for this entire call. Re-reading
     * validated, disjoint descriptors avoids a second 1 KiB Host stack array. */
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = spans + i * PXA_AUDIO_MIX_SPAN_BYTES;
        const uint8_t *source = memory + pxa_read_u32(p);
        uint32_t position = pxa_read_u32(p + 8), step = pxa_read_u32(p + 12);
        uint32_t frames = pxa_read_u32(p + 20);
        int32_t left_gain = pxa_read_u16(p + 24), right_gain = pxa_read_u16(p + 26);
        uint8_t *out = target + pxa_read_u32(p + 16) * 8u;
        uint8_t encoding = p[28], channels = p[29];
        if (encoding == PXA_AUDIO_MIX_S8 && channels == 1 && step == 65536u) {
            source += position >> 16;
            for (uint32_t n = 0; n < frames; ++n, out += 8) {
                int32_t value = source[n] < 128 ? source[n] : (int32_t)source[n] - 256;
                add(out, value * left_gain);
                add(out + 4, value * right_gain);
            }
        } else {
            uint32_t width = encoding == PXA_AUDIO_MIX_S16LE ? 2u : 1u;
            for (uint32_t n = 0; n < frames; ++n, position += step, out += 8) {
                const uint8_t *at = source + (position >> 16) * channels * width;
                int32_t left = sample(at, encoding);
                int32_t right = channels == 1 ? left : sample(at + width, encoding);
                add(out, left * left_gain / 256);
                add(out + 4, right * right_gain / 256);
            }
        }
    }
    return PXA_STATUS_OK;
}
