#include <stdio.h>
#include <string.h>

#include "arithmetic.h"
#include "pxa.h"

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length)
{
    char text[16] = {0};

    (void)config;
    (void)config_length;
    return strlen(text) == 0
                   && snprintf(text, sizeof(text), "%d",
                               pxa_wamr_wasi_guest_add(2, 3)) > 0
               ? PXA_STATUS_OK
               : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length)
{
    (void)event;
    (void)length;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
