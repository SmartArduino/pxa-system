#include "pxa.h"

__attribute__((import_module("env"), import_name("puts")))
int env_puts(const char *text);


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return env_puts("not allowed");
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    (void)event;
    (void)length;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
