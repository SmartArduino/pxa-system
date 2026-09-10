#include "pxa.h"

__attribute__((import_module("wasi_snapshot_preview1"),
               import_name("random_get")))
uint32_t undeclared_random_get(uint8_t *buffer, uint32_t length);

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length)
{
    uint8_t value;

    (void)config;
    (void)config_length;
    return undeclared_random_get(&value, sizeof(value)) == 0
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
