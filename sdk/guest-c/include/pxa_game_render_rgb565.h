/* SPDX-License-Identifier: MIT */
#ifndef PXA_GUEST_GAME_RENDER_RGB565_H
#define PXA_GUEST_GAME_RENDER_RGB565_H
#include "pxa_game_render_compute.h"

#define PXA_GAME_RENDER_IO_EXECUTE_RGB565 0x10du
#define PXA_GAME_RENDER_FEATURE_RGB565_BATCHES UINT64_C(128)
#define PXA_GAME_RENDER_RGB565_FILL 0u
#define PXA_GAME_RENDER_RGB565_BLIT 1u

/* Ordered fills and RGB565LE blits into a borrowed Guest buffer. The whole
 * packet is checked before writes; no pointers survive this synchronous call.
 * Blits sample floor(x*source_width/width), then optionally flip the source X.
 * Transparent zero is tested before light (floor(channel*light/255)).
 * Source ranges and descriptor bytes must be disjoint from the target range.
 * All offsets are relative to the beginning of Guest linear memory. */
static inline int32_t pxa_game_render_execute_rgb565(uint64_t context,
    const void *packet, uint32_t bytes) {
    if (!context || !packet || bytes < PXA_GAME_RENDER_RGB565_HEADER_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_EXECUTE_RGB565,
        (uint8_t *)packet, bytes);
    return result < 0 ? result : result == (int32_t)bytes ? result : PXA_STATUS_PROTOCOL_ERROR;
}
#endif
