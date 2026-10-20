#ifndef PXA_GUEST_EVENT_PORT_H
#define PXA_GUEST_EVENT_PORT_H

#include "pxa_core.h"

#ifndef PXA_CORE_OPEN_EVENT_PORT
#define PXA_CORE_OPEN_EVENT_PORT UINT16_C(5)
#define PXA_EVENT_PORT_IO_WAIT UINT32_C(0x201)
#define PXA_EVENT_PORT_MAX_WAIT_US UINT64_C(60000000)
#endif

typedef struct {
    int32_t status;
    uint64_t handle;
} pxa_event_port_result_t;

/* Core 2.1, main-loop component flag and Host execution-model support required.
 * Obtain the response through the normal startup callback before waiting. */
static inline int32_t pxa_event_port_open(uint64_t token) {
    if (!token) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_call(PXA_CORE_SERVICE, PXA_CORE_OPEN_EVENT_PORT, token, NULL, 0);
}

static inline int pxa_event_port_parse_open(const pxa_event_t *event,
    uint64_t token, pxa_event_port_result_t *out) {
    if (!event || !out || !token || event->service != PXA_CORE_SERVICE ||
        event->opcode != PXA_CORE_OPEN_EVENT_PORT || event->token != token ||
        !event->payload || event->payload_size < 4) return 0;
    out->handle = 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    out->handle = pxa_load_u64(event->payload + 4);
    return out->handle != 0;
}

/* On success bytes contains one complete event and result is its byte size.
 * Zero timeout is a nonblocking Host-maintenance checkpoint. WOULD_BLOCK and
 * TIMED_OUT preserve queued events; RESOURCE_LIMIT preserves an oversized
 * queue head. The first eight bytes contain the request timeout after an
 * unsuccessful import. Cancellation terminates the active VM call; it cannot
 * be recovered inside the Guest loop. No Host pointer survives the import. */
static inline int32_t pxa_event_port_wait(uint64_t port, uint8_t *bytes,
    uint32_t capacity, uint64_t timeout_us) {
    if (!port || !bytes || capacity < PXA_HEADER_BYTES ||
        capacity > PXA_MAX_CONTROL_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
    if (timeout_us > PXA_EVENT_PORT_MAX_WAIT_US) return PXA_STATUS_LIMIT_EXCEEDED;
    pxa_store_u64(bytes, timeout_us);
    return pxa_io(port, PXA_EVENT_PORT_IO_WAIT, bytes, capacity);
}

#endif
