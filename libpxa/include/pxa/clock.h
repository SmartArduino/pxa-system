#ifndef PXA_CLOCK_H
#define PXA_CLOCK_H

#include "pxa.h"

#define PXA_CLOCK_SERVICE_ID UINT16_C(4)
#define PXA_CLOCK_SERVICE_MAJOR UINT16_C(0)
#define PXA_CLOCK_SERVICE_MINOR UINT16_C(3)
#define PXA_CLOCK_SET_PERIOD UINT16_C(1)
#define PXA_CLOCK_NOW UINT16_C(2)
#define PXA_CLOCK_WALL_TIME UINT16_C(3)
#define PXA_CLOCK_LIMITS UINT16_C(4)
#define PXA_CLOCK_CONFIGURE UINT16_C(5)
#define PXA_CLOCK_FEATURE_TIMING UINT64_C(1)
#define PXA_CLOCK_LIMITS_BYTES 20u
#define PXA_CLOCK_TIMING_COALESCED UINT32_C(1)
#define PXA_CLOCK_TIMING_FOREGROUND UINT32_C(2)
#define PXA_CLOCK_WALL_TIME_BYTES 24u
#define PXA_CLOCK_DAYLIGHT_SAVING UINT32_C(1)
#define PXA_CLOCK_TICK UINT16_C(0x8001)
#define PXA_CLOCK_NOW_RESULT UINT16_C(0x8002)

typedef struct {
    uint32_t min_period_us;
    uint32_t max_period_us;
    uint32_t period_step_us;
    uint32_t dispatch_quantum_us;
    uint32_t flags;
} pxa_clock_limits_t;

typedef struct {
    pxa_clock_limits_t limits;
    void *context;
    /* Validate before mutation; failure must preserve the previous timer. */
    pxa_status_t (*configure)(void *context, pxa_component_t component,
                               uint32_t period_us);
} pxa_clock_timing_backend_t;

#ifdef __cplusplus
extern "C" {
#endif
int pxa_clock_period_supported(const pxa_clock_limits_t *limits,
                                uint32_t period_us);
/* Core 1/2: reserve a completion before any configuration side effect. */
pxa_status_t pxa_clock_timing_control(pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message,
    const pxa_clock_timing_backend_t *backend);
#ifdef __cplusplus
}
#endif

#endif
