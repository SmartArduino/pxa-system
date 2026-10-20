#ifndef PXA_AUDIO_MIX_H
#define PXA_AUDIO_MIX_H

#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio 0.11 synchronous PCM accumulation. All offsets refer to the calling
 * component's borrowed linear memory. Validation completes before any write;
 * no pointer, allocation or playback state survives this call.
 *
 * Header (16 LE bytes): target:u32, target_frames:u32, spans:u32,
 * count:u16, reserved:u16.
 * Span (32 LE bytes): source:u32, source_bytes:u32, position_q16:u32,
 * step_q16:u32, target_first:u32, frames:u32, left_gain_q8:u16,
 * right_gain_q8:u16, encoding:u8, channels:u8, reserved:u16.
 *
 * Each span adds nearest-neighbour PCM into interleaved S32LE L/R. Gains are
 * 0..256 (unity); S8/U8 expand to S16 before gain / 256, truncating toward zero.
 * Accumulation saturates at S32 limits, in span order. The caller owns clearing,
 * loop splitting, interpolation policy and final output quantization.
 * Targets cannot overlap source data, span records or the header. */
#define PXA_AUDIO_MIX_HEADER_BYTES 16u
#define PXA_AUDIO_MIX_SPAN_BYTES 32u
#define PXA_AUDIO_MIX_MAX_SPANS 32u
#define PXA_AUDIO_MIX_MAX_FRAMES 4096u
#define PXA_AUDIO_MIX_MAX_WORK 16384u
#define PXA_AUDIO_MIX_S8 1u
#define PXA_AUDIO_MIX_U8 2u
#define PXA_AUDIO_MIX_S16LE 3u

pxa_status_t pxa_audio_mix_pcm(const uint8_t *packet, size_t packet_size,
    uint8_t *memory, size_t memory_size);

#ifdef __cplusplus
}
#endif
#endif
