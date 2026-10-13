#ifndef PXA_POSIX_CLOCK_H
#define PXA_POSIX_CLOCK_H

#include "pxa/clock.h"
#include "pxa/pxa.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Encode one instant using the Host's configured POSIX time zone. No heap,
 * TZ mutation, or Guest time-zone database. Also supports deterministic tests. */
pxa_status_t pxa_posix_clock_wall_time(int64_t unix_us, uint64_t monotonic_us,
                                      uint8_t output[PXA_CLOCK_WALL_TIME_BYTES]);
pxa_status_t pxa_posix_clock_control(pxa_runtime_t *runtime,
                                    pxa_component_t component,
                                    const pxa_message_view_t *message,
                                    uint64_t monotonic_us);

#ifdef __cplusplus
}
#endif
#endif
