#ifndef PXA_GUEST_RESOURCE_H
#define PXA_GUEST_RESOURCE_H

#include "pxa_core.h"

#ifndef PXA_CORE_WAIT_RESOURCE
#define PXA_CORE_WAIT_RESOURCE UINT16_C(4)
#endif
#define PXA_CORE_HANDLE_READY 0x8001u
#define PXA_CORE_HANDLE_CLOSED 0x8002u
#define PXA_RESOURCE_IO_POLL 3u
#define PXA_RESOURCE_READY_READ 1u
#define PXA_RESOURCE_READY_WRITE 2u
#define PXA_RESOURCE_READY_HEADERS 4u
#define PXA_RESOURCE_READY_END 8u

typedef struct {
    int32_t status;
    uint32_t flags;
} pxa_resource_wait_result_t;

typedef struct {
    uint64_t handle;
    uint32_t flags;
} pxa_resource_ready_t;

typedef struct {
    uint64_t handle;
    int32_t reason;
} pxa_resource_closed_t;

/* One accepted wait, one completion. Retry the original I/O after readiness;
 * readiness is a snapshot, not a reservation. Cancel with pxa_cancel(token). */
static inline int32_t pxa_resource_wait(uint64_t token, uint64_t handle,
                                       uint32_t interests) {
    uint8_t payload[12];
    if (!token || !handle || !interests || (interests & ~15u))
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(payload, handle);
    pxa_store_u32(payload + 8, interests);
    return pxa_control_call(PXA_CORE_SERVICE, PXA_CORE_WAIT_RESOURCE, token,
                            payload, sizeof(payload));
}

static inline int32_t pxa_resource_poll(uint64_t handle) {
    if (!handle) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle, PXA_RESOURCE_IO_POLL, NULL, 0);
}

static inline int pxa_resource_parse_wait(const pxa_event_t *event,
    uint64_t token, pxa_resource_wait_result_t *out) {
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || !token || event->token != token ||
        event->service != PXA_CORE_SERVICE || event->opcode != PXA_CORE_WAIT_RESOURCE ||
        !event->payload || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 8) return 0;
    out->flags = pxa_load_u32(event->payload + 4);
    return out->flags != 0 && !(out->flags & ~15u);
}

static inline int pxa_resource_parse_ready(const pxa_event_t *event,
                                           pxa_resource_ready_t *out) {
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || event->token || event->service != PXA_CORE_SERVICE ||
        event->opcode != PXA_CORE_HANDLE_READY || !event->payload ||
        event->payload_size != 12) return 0;
    out->handle = pxa_load_u64(event->payload);
    out->flags = pxa_load_u32(event->payload + 8);
    return out->handle != 0 && out->flags != 0 && !(out->flags & ~15u);
}

static inline int pxa_resource_parse_closed(const pxa_event_t *event,
                                            pxa_resource_closed_t *out) {
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || event->token || event->service != PXA_CORE_SERVICE ||
        event->opcode != PXA_CORE_HANDLE_CLOSED || !event->payload ||
        event->payload_size != 12) return 0;
    out->handle = pxa_load_u64(event->payload);
    out->reason = (int32_t)pxa_load_u32(event->payload + 8);
    return out->handle != 0;
}

#endif
