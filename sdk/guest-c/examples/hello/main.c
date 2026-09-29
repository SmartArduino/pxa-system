#include "pxa.h"
#include "pxa_clock.h"

static uint64_t last_token;

int32_t pxa_app_start(const uint8_t *config, uint32_t length) {
    (void)config;
    (void)length;
    if (pxa_log_write(PXA_LOG_LEVEL_INFO, "Hello from the Guest SDK") != PXA_STATUS_OK)
        return PXA_STATUS_INTERNAL;
    return pxa_clock_now(pxa_next_token(&last_token));
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t length) {
    pxa_event_t event;
    pxa_clock_now_result_t now;
    if (!pxa_parse_event(bytes, length, &event))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (!pxa_clock_parse_now(&event, last_token, &now))
        return PXA_EVENT_UNHANDLED;
    if (now.status != PXA_STATUS_OK)
        return now.status;
    return pxa_log_write(PXA_LOG_LEVEL_INFO, "Clock request completed") == PXA_STATUS_OK
               ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
