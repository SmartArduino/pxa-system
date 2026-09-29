#ifndef PXA_GUEST_SYSTEM_H
#define PXA_GUEST_SYSTEM_H

#include "pxa.h"
#include "pxa_system_config.h"

static inline int pxa_system_parse_configuration_event(
    const pxa_event_t *event, pxa_system_configuration_event_t *output) {
    return event != NULL && event->service == PXA_SERVICE_SYSTEM &&
           event->opcode == PXA_SYSTEM_CONFIGURATION_EVENT &&
           event->token == 0 &&
           pxa_system_parse_configuration_records(event->payload,
                                                  event->payload_size, output);
}

#endif
