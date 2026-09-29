#ifndef PXA_AUDIO_BUFFER_H
#define PXA_AUDIO_BUFFER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Caller serializes this small state with its PCM ring. No heap, I/O or locks.
 * Counters span plays; start only resets the current playback's readiness.
 * Default product threshold: 4096 samples = 256 ms at 16 kHz, within the
 * existing 8192-sample ring. EOF permits short tracks and final partial blocks.
 * Low water samples the remainder after an active non-EOF take; initial
 * prebuffer and natural EOF do not create a false zero. Check low_water_valid. */
typedef struct {
    uint64_t underruns, recoveries, missing_samples, consumed_samples;
    uint32_t threshold, high_water, low_water;
    uint8_t started, buffering, low_water_valid;
} pxa_audio_buffer_t;
void pxa_audio_buffer_start(pxa_audio_buffer_t *, uint32_t threshold);
/* Returns 1 exactly once per play when initial output is ready. */
int pxa_audio_buffer_publish(pxa_audio_buffer_t *, uint32_t available, int eof);
/* Allowed samples for one active output callback. Never call while paused or
 * stopped. An underrun is one transition into rebuffering, not each callback. */
uint32_t pxa_audio_buffer_take(pxa_audio_buffer_t *, uint32_t available,
    uint32_t wanted, int eof);
#ifdef __cplusplus
}
#endif
#endif
