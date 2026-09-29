#ifndef PXA_GUEST_GAME_RENDER_WIRE_H
#define PXA_GUEST_GAME_RENDER_WIRE_H

#include <stdint.h>

/* GameRender DrawList and upload formats. */
#define PXA_GAME_RENDER_IO_UPLOAD UINT32_C(0x100)
#define PXA_GAME_RENDER_IO_SUBMIT UINT32_C(0x101)
#define PXA_GAME_RENDER_IO_TELEMETRY UINT32_C(0x102)
#ifndef PXA_GAME_RENDER_TELEMETRY_BYTES
#define PXA_GAME_RENDER_TELEMETRY_BYTES UINT32_C(104)
#endif

static inline void pxa_game_render_store_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static inline void pxa_game_render_store_u32(uint8_t *out, uint32_t value) {
    pxa_game_render_store_u16(out, (uint16_t)value);
    pxa_game_render_store_u16(out + 2, (uint16_t)(value >> 16));
}

static inline void pxa_game_render_store_u64(uint8_t *out, uint64_t value) {
    pxa_game_render_store_u32(out, (uint32_t)value);
    pxa_game_render_store_u32(out + 4, (uint32_t)(value >> 32));
}

#endif
