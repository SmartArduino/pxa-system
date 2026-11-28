#ifndef PXA_GUEST_H
#define PXA_GUEST_H

/* The Guest SDK entry point. */
#include "pxa_common.h"
#include "pxa_core.h"

#define PXA_CORE_VERSION_MAJOR 2u
#define PXA_CORE_VERSION_MINOR 1u
#define PXA_CORE_VERSION_PATCH 0u

/* Optional pre-start export: void pxa_app_set_cpu_features(uint32_t mask).
 * Initialize every kernel to its baseline implementation for older Hosts.
 * A variant must be reached only after its feature bit has been supplied. */
#define PXA_CPU_FEATURE_RV32_ZBB 1u
#ifdef __wasm__
#define PXA_RV32_ZBB_VARIANT(name) __attribute__((noinline, export_name("__wamr_rv32_zbb_" #name)))
#define PXA_CPU_FEATURE_CALLBACK __attribute__((export_name("pxa_app_set_cpu_features")))
#else
#define PXA_RV32_ZBB_VARIANT(name)
#define PXA_CPU_FEATURE_CALLBACK
#endif

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
