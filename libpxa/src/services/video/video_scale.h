// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
typedef struct { uint16_t dest, source, groups; } pxa_scale_run;
enum { PXA_SCALE_MAX_RUNS = 8 };

/* Near-equal widths are long contiguous spans separated by a repeated or
 * skipped pixel. The boundaries preserve floor(x * sw / dw) exactly. */
enum { PXA_SCALE_MAX_DELTA = 4 };
typedef struct { uint16_t dest, source, length; } pxa_scale_copy_span;

static inline unsigned pxa_scale_copy_spans(uint32_t sw, uint32_t dw,
        pxa_scale_copy_span spans[PXA_SCALE_MAX_DELTA + 1]) {
    const uint32_t delta = sw > dw ? sw - dw : dw - sw;
    if (!delta || delta > PXA_SCALE_MAX_DELTA || dw < 32) return 0;
    uint32_t start = 0;
    unsigned count = 0;
    while (start < dw) {
        const uint32_t stop = count == delta ? dw : sw > dw
            ? ((count + 1) * dw + delta - 1) / delta : count * dw / delta + 1;
        spans[count++] = (pxa_scale_copy_span){(uint16_t)start,
            (uint16_t)(start * sw / dw), (uint16_t)(stop - start)};
        start = stop;
    }
    return count;
}

static inline void pxa_scale_copy_row(uint8_t *dest, const uint8_t *src,
        const pxa_scale_copy_span *spans, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        memcpy(dest + 2u*spans[i].dest, src + 2u*spans[i].source, 2u*spans[i].length);
}
/* Rounding the display width near 4:3 shifts the repeated pixel a few times
 * per row. Find the runs once per frame using the exact floor sampler. */
static inline unsigned pxa_scale_four_thirds_runs(uint32_t sw, uint32_t dw,
        pxa_scale_run runs[PXA_SCALE_MAX_RUNS]) {
    if (dw < 32 || 4 * sw + 4 < 3 * dw || 4 * sw > 3 * dw + 4) return 0;
    unsigned count = 0;
    for (uint32_t x = 0; x + 4 <= dw;) {
        const uint32_t start = x * sw / dw;
        if ((x + 1) * sw / dw != start || (x + 2) * sw / dw != start + 1 ||
            (x + 3) * sw / dw != start + 2) {
            ++x;
            continue;
        }
        const pxa_scale_run *last = count ? &runs[count - 1] : NULL;
        if (last && x == last->dest + 4u * last->groups &&
            start == last->source + 3u * last->groups) {
            ++runs[count - 1].groups;
        } else {
            if (count == PXA_SCALE_MAX_RUNS) return 0;
            runs[count++] = (pxa_scale_run){(uint16_t)x, (uint16_t)start, 1};
        }
        x += 4;
    }
    return count;
}

static inline void pxa_scale_expand_groups(uint8_t *dest, const uint8_t *src, uint32_t groups) {
#if defined(PXA_VIDEO_PACKED_SCALE) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (groups && ((uintptr_t)dest & 2u)) {
        /* After the first A, each group is four consecutive source pixels:
         * [A,B,C,D] [D,E,F,G] ... End on three to stay inside the row. */
        memcpy(dest, src, 2);
        dest += 2;
        for (; groups > 1; --groups, dest += 8, src += 6)
            memcpy(__builtin_assume_aligned(dest,4), __builtin_assume_aligned(src,2), 8);
        memcpy(dest, src, 6);
        return;
    }
    if (groups && ((uintptr_t)src & 2u)) {
        memcpy(dest, src, 2);
        memcpy(dest+2, src, 6);
        --groups; dest+=8; src+=6;
    }
    for (; groups >= 2; groups-=2, dest+=16, src+=12) {
        uint32_t ab, cd, ef;
        const uint8_t *in=__builtin_assume_aligned(src,4);
        uint8_t *out=__builtin_assume_aligned(dest,4);
        memcpy(&ab,in,4); memcpy(&cd,in+4,4); memcpy(&ef,in+8,4);
        const uint32_t aa=(ab&0xffffu)|(ab<<16);
        const uint32_t bc=(ab>>16)|(cd<<16);
        const uint32_t dd=(cd>>16)|(cd&0xffff0000u);
        memcpy(out,&aa,4); memcpy(out+4,&bc,4);
        memcpy(out+8,&dd,4); memcpy(out+12,&ef,4);
    }
#endif
    for (; groups; --groups, dest += 8, src += 6) {
        uint8_t *out = __builtin_assume_aligned(dest, 2);
        const uint8_t *in = __builtin_assume_aligned(src, 2);
        uint16_t first;
        memcpy(&first, in, 2);
        memcpy(out, &first, 2);
        memcpy(out + 2, &first, 2);
        memcpy(out + 4, in + 2, 4);
    }
}
static inline void pxa_scale_run_row(uint8_t *dest, const uint8_t *src,
        uint32_t sw, uint32_t dw, const pxa_scale_run *runs, unsigned count) {
    uint32_t x = 0;
    for (unsigned i = 0; i <= count; ++i) {
        const uint32_t stop = i < count ? runs[i].dest : dw;
        for (; x < stop; ++x) memcpy(dest + 2*x, src + 2*(x*sw/dw), 2);
        if (i < count) {
            pxa_scale_expand_groups(dest + 2*x, src + 2*runs[i].source, runs[i].groups);
            x += 4u * runs[i].groups;
        }
    }
}
#endif
