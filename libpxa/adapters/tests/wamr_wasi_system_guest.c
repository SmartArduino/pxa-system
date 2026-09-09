#include "pxa.h"

#define WASI_CLOCK_REALTIME UINT32_C(0)
#define WASI_CLOCK_MONOTONIC UINT32_C(1)

__attribute__((import_module("wasi_snapshot_preview1"),
               import_name("clock_time_get"))) uint32_t
wasi_clock_time_get(uint32_t clock_id, uint64_t precision,
                    uint64_t *timestamp);
__attribute__((import_module("wasi_snapshot_preview1"),
               import_name("random_get"))) uint32_t
wasi_random_get(uint8_t *buffer, uint32_t length);


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    uint8_t entropy[16];
    uint64_t monotonic_ns;
    uint64_t wall_ns;
    (void)config;
    (void)config_length;
    if (wasi_clock_time_get(WASI_CLOCK_MONOTONIC, 1, &monotonic_ns) != 0 ||
        wasi_clock_time_get(WASI_CLOCK_REALTIME, 1, &wall_ns) != 0 ||
        wasi_random_get(entropy, sizeof(entropy)) != 0) {
        return PXA_STATUS_INTERNAL;
    }
    return monotonic_ns != 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    (void)event;
    (void)length;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
