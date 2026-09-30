#include "pxa.h"

int pxa_parallel_answer(void);

int32_t pxa_app_start(const uint8_t *config, uint32_t length) {
    (void)config;
    (void)length;
    return pxa_parallel_answer() == 42 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t length) {
    (void)bytes;
    (void)length;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
