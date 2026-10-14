/* Direct numerical regression for the private scanline kernel. Including the
 * implementation keeps this test from adding a production ABI or test hook. */
#undef NDEBUG
#include "../src/services/surface/raster.c"
#include <assert.h>

static uint32_t random_state = UINT32_C(0x416cf251);
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(void) {
    static uint8_t texels[256 * 256];
    uint16_t palette[256];
    for (unsigned i = 0; i < sizeof(texels); ++i)
        texels[i] = i % 5 == 0 ? 0 : (uint8_t)(i ^ (i >> 8));
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = (uint16_t)next_random();
    }
    for (unsigned trial = 0; trial < 3000; ++trial) {
        uint16_t actual[66], expected[66], depths[66], expected_depths[66];
        const uint32_t count = next_random() % 65;
        const uint8_t width_log2 = next_random() % 9;
        const uint32_t width = 1u << width_log2;
        const uint32_t height = 1u << (next_random() % 9);
        const uint32_t u = next_random(), v = next_random();
        const int32_t du = (int32_t)(next_random() % 524289) - 262144;
        const int32_t dv = (int32_t)(next_random() % 524289) - 262144;
        const int32_t initial[] = {
            INT32_MIN, -1, 0, 1, 65534, 65535, 65536, 80000, INT32_MAX};
        const int32_t steps[] = {INT32_MIN, INT32_MAX, -65536, 65536};
        const int32_t w = trial % 2 ? initial[trial % 9]
                                     : (int32_t)(next_random() % 80001);
        const int32_t dw = trial % 3 ? (int32_t)(next_random() % 2001) - 1000
                                    : steps[trial % 4];
        const int transparent = trial % 2;
        uint32_t expected_covered = 0;
        for (unsigned i = 0; i < 66; ++i) {
            actual[i] = expected[i] = (uint16_t)next_random();
            depths[i] = expected_depths[i] = (uint16_t)next_random();
        }
        // Independent per-pixel oracle: evaluate each interpolant from its
        // origin, without the kernel's incremental steps or endpoint shortcut.
        for (uint32_t i = 0; i < count; ++i) {
            int64_t z = (int64_t)w + (int64_t)i * dw;
            if (z > UINT16_MAX) z = UINT16_MAX;
            if (z <= 0 || z <= expected_depths[i + 1]) continue;
            const uint32_t tx = ((u + (uint32_t)du * i) >> 16) & (width - 1u);
            const uint32_t ty = ((v + (uint32_t)dv * i) >> 16) & (height - 1u);
            const uint8_t texel = texels[ty * width + tx];
            if (transparent && texel == 0) continue;
            expected[i + 1] = palette[texel];
            expected_depths[i + 1] = (uint16_t)z;
            ++expected_covered;
        }
        assert(painter_span_depth_textured(actual + 1, depths + 1, count,
            u, v, du, dv, texels, palette, width - 1u, height - 1u,
            width_log2, w, dw, transparent) == expected_covered);
        assert(memcmp(actual, expected, sizeof(actual)) == 0);
        assert(memcmp(depths, expected_depths, sizeof(depths)) == 0);
    }
    for (unsigned trial = 0; trial < 3000; ++trial) {
        uint16_t actual[66], expected[66], depths[66], expected_depths[66];
        const uint32_t count = next_random() % 65;
        const int32_t initial[] = {
            INT32_MIN, -1, 0, 1, 65534, 65535, 65536, INT32_MAX};
        const int32_t steps[] = {0, 1, -1, INT32_MIN, INT32_MAX};
        const int32_t w = trial % 2 ? initial[trial % 8]
                                     : (int32_t)(next_random() % 80001);
        const int32_t dw = trial % 3 ? steps[trial % 5]
                                      : (int32_t)(next_random() % 2001) - 1000;
        const uint16_t color = (uint16_t)next_random();
        uint32_t covered = 0;
        for (unsigned i = 0; i < 66; ++i) {
            actual[i] = expected[i] = (uint16_t)next_random();
            /* Exercise equal-depth and fully occluded spans as well. */
            depths[i] = expected_depths[i] = trial % 3 == 0 ? (uint16_t)w
                : trial % 3 == 1 ? UINT16_MAX : (uint16_t)next_random();
        }
        for (uint32_t i = 0; i < count; ++i) {
            int64_t z = (int64_t)w + (int64_t)i * dw;
            if (z > UINT16_MAX) z = UINT16_MAX;
            if (z <= 0 || z <= expected_depths[i + 1]) continue;
            expected[i + 1] = color;
            expected_depths[i + 1] = (uint16_t)z;
            ++covered;
        }
        assert(painter_span_depth_flat(actual + 1, depths + 1, count,
                                      color, w, dw) == covered);
        assert(memcmp(actual, expected, sizeof(actual)) == 0);
        assert(memcmp(depths, expected_depths, sizeof(depths)) == 0);
    }
    return 0;
}
