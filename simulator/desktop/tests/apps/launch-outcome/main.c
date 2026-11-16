/* SPDX-License-Identifier: MIT */
#include "pxa.h"

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    (void)config;
    (void)size;
#if PXA_TEST_START_FAIL
    return PXA_STATUS_UNSUPPORTED;
#else
    return PXA_STATUS_OK;
#endif
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t size) {
    (void)event;
    (void)size;
    return PXA_EVENT_HANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
