#ifndef PXA_DISPLAY_ROTATION_H
#define PXA_DISPLAY_ROTATION_H

#include <stdint.h>
#include <stddef.h>

/* Native means the board's normal reading orientation after fixed panel
 * mounting compensation. Rotation is clockwise graphics rotation into that
 * native display, in quarter turns, as with Android Display.getRotation(). */
enum {
    PXA_ORIENTATION_SYSTEM = 0,
    PXA_ORIENTATION_PORTRAIT = 1,
    PXA_ORIENTATION_LANDSCAPE = 2,
    PXA_ORIENTATION_REVERSE_PORTRAIT = 3,
    PXA_ORIENTATION_REVERSE_LANDSCAPE = 4,
    PXA_ORIENTATION_NATIVE = 5,
    PXA_ORIENTATION_ROTATION_0 = 6,
    PXA_ORIENTATION_ROTATION_90 = 7,
    PXA_ORIENTATION_ROTATION_180 = 8,
    PXA_ORIENTATION_ROTATION_270 = 9,
    PXA_ORIENTATION_LOCKED = 10
};

static inline uint32_t pxa_display_logical_width(uint32_t w, uint32_t h, uint8_t r) {
    return (r & 1u) ? h : w;
}
static inline uint32_t pxa_display_logical_height(uint32_t w, uint32_t h, uint8_t r) {
    return (r & 1u) ? w : h;
}
static inline uint8_t pxa_display_shape(uint32_t w, uint32_t h) {
    return w == h ? 3u : w > h ? 2u : 1u;
}
/* Right-handed motion axes in the current reading view (+X right, +Y up,
 * +Z out). Sensor samples themselves remain in the native device frame.
 * This is deliberately separate from raster coordinates, whose Y is down. */
static inline void pxa_display_motion_to_screen(const float device[3],
    uint8_t rotation, float screen[3]) {
    const float x = device[0], y = device[1], z = device[2];
    switch (rotation & 3u) {
        case 1: screen[0] = -y; screen[1] = x; break;
        case 2: screen[0] = -x; screen[1] = -y; break;
        case 3: screen[0] = y; screen[1] = -x; break;
        default: screen[0] = x; screen[1] = y; break;
    }
    screen[2] = z;
}

/* A shape preference on a square display never invents an arbitrary quarter
 * turn. Explicit angles remain available. Repeated requests are idempotent. */
static inline int pxa_display_resolve_rotation(uint32_t w, uint32_t h,
    uint8_t system_rotation, uint8_t current, uint8_t preference,
    uint8_t supported, uint8_t *output) {
    uint8_t r;
    if (!w || !h || !output || current > 3 || system_rotation > 3 ||
        preference > PXA_ORIENTATION_LOCKED || !supported || (supported & ~15u)) return 0;
    if (preference == PXA_ORIENTATION_LOCKED) r = current;
    else if (preference == PXA_ORIENTATION_SYSTEM) r = system_rotation;
    else if (preference == PXA_ORIENTATION_NATIVE) r = 0;
    else if (preference >= PXA_ORIENTATION_ROTATION_0)
        r = (uint8_t)(preference - PXA_ORIENTATION_ROTATION_0);
    else if (w == h) r = current;
    else {
        const int portrait = preference == PXA_ORIENTATION_PORTRAIT ||
                             preference == PXA_ORIENTATION_REVERSE_PORTRAIT;
        r = (uint8_t)((h > w) == portrait ? 0 : 1);
        if (preference >= PXA_ORIENTATION_REVERSE_PORTRAIT) r += 2;
        if (!(supported & (1u << r)) && (supported & (1u << (r ^ 2u)))) r ^= 2u;
    }
    if (!(supported & (1u << r))) return 0;
    *output = r;
    return 1;
}

/* Pixel centres, with native and logical raster Y increasing downward.
 * These are display/input transforms, never motion-sensor frame changes. */
static inline void pxa_display_logical_to_native(uint32_t w, uint32_t h,
    uint8_t r, uint32_t x, uint32_t y, uint32_t *nx, uint32_t *ny) {
    switch (r & 3u) {
        case 1: *nx = w - 1u - y; *ny = x; break;
        case 2: *nx = w - 1u - x; *ny = h - 1u - y; break;
        case 3: *nx = y; *ny = h - 1u - x; break;
        default: *nx = x; *ny = y; break;
    }
}
static inline void pxa_display_native_to_logical(uint32_t w, uint32_t h,
    uint8_t r, uint32_t x, uint32_t y, uint32_t *lx, uint32_t *ly) {
    switch (r & 3u) {
        case 1: *lx = y; *ly = w - 1u - x; break;
        case 2: *lx = w - 1u - x; *ly = h - 1u - y; break;
        case 3: *lx = h - 1u - y; *ly = x; break;
        default: *lx = x; *ly = y; break;
    }
}

/* Source and destination must not alias. Tiling bounds the scattered writes
 * for PSRAM scanout. RGB565 byte order is preserved, including panel order. */
static inline void pxa_display_rgb565_to_native(const uint16_t *source,
    uint16_t *destination, uint32_t w, uint32_t h, uint8_t r) {
    const uint32_t lw = pxa_display_logical_width(w, h, r);
    const uint32_t lh = pxa_display_logical_height(w, h, r);
    for (uint32_t ty = 0; ty < lh; ty += 16)
        for (uint32_t tx = 0; tx < lw; tx += 16)
            for (uint32_t y = ty; y < lh && y < ty + 16; ++y)
                for (uint32_t x = tx; x < lw && x < tx + 16; ++x) {
                    uint32_t nx, ny;
                    pxa_display_logical_to_native(w, h, r, x, y, &nx, &ny);
                    destination[(size_t)ny * w + nx] = source[(size_t)y * lw + x];
                }
}
#endif
