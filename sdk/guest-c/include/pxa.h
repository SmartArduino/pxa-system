#ifndef PXA_GUEST_H
#define PXA_GUEST_H

/* The Guest SDK entry point. */
#include "pxa_common.h"
#include "pxa_core.h"

#define PXA_CORE_VERSION_MAJOR 1u
#define PXA_CORE_VERSION_MINOR 0u
#define PXA_CORE_VERSION_PATCH 0u

/* The lifecycle payload is borrowed from the event callback. */
static inline int pxa_lifecycle_parse(const pxa_event_t *event,
                                      uint8_t *state) {
    if (event == NULL || state == NULL ||
        event->service != PXA_SERVICE_SYSTEM ||
        event->opcode != PXA_SYSTEM_LIFECYCLE_EVENT || event->token != 0 ||
        event->payload == NULL || event->payload_size != 1u ||
        event->payload[0] > PXA_SYSTEM_LIFECYCLE_FOREGROUND)
        return 0;
    *state = event->payload[0];
    return 1;
}

#endif
