#ifndef PXA_GUEST_CLOCK_H
#define PXA_GUEST_CLOCK_H

#include "pxa_core.h"

#define PXA_CLOCK_SERVICE 4u
#define PXA_CLOCK_SET_PERIOD 1u
#define PXA_CLOCK_NOW 2u
#define PXA_CLOCK_TICK 0x8001u

typedef struct {
    int32_t status;
    uint64_t timestamp_us;
} pxa_clock_now_result_t;

static inline int pxa_clock_build_set_period(
    uint8_t *packet, size_t capacity, uint16_t period_ms,
    uint32_t *written) {
    uint8_t payload[2];
    if (period_ms != 0 && (period_ms < 16 || period_ms > 1000)) return 0;
    pxa_store_u16(payload, period_ms);
    return pxa_build_message(packet, capacity, PXA_CLOCK_SERVICE,
                                 PXA_CLOCK_SET_PERIOD, 0, payload,
                                 sizeof(payload), written);
}

static inline int pxa_clock_build_now(
    uint8_t *packet, size_t capacity, uint64_t token,
    uint32_t *written) {
    if (token == 0) return 0;
    return pxa_build_message(packet, capacity, PXA_CLOCK_SERVICE,
                                 PXA_CLOCK_NOW, token, NULL, 0, written);
}

static inline int32_t pxa_clock_set_period(uint16_t period_ms) {
    uint8_t packet[PXA_HEADER_BYTES + 2u];
    uint32_t size = 0;
    if (!pxa_clock_build_set_period(packet, sizeof(packet), period_ms,
                                        &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_clock_now(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES];
    uint32_t size = 0;
    if (!pxa_clock_build_now(packet, sizeof(packet), token, &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_clock_parse_tick(
    const pxa_event_t *event, uint64_t *timestamp_us) {
    if (event == NULL || timestamp_us == NULL ||
        event->service != PXA_CLOCK_SERVICE ||
        event->opcode != PXA_CLOCK_TICK || event->token != 0 ||
        event->payload == NULL || event->payload_size != 8) return 0;
    *timestamp_us = pxa_load_u64(event->payload);
    return 1;
}

/* Clamp catch-up after a delayed callback so one tick cannot trigger an
 * unbounded burst of simulation and rendering work. */
static inline uint8_t pxa_clock_tick_steps(uint64_t *previous_timestamp_us,
                                               uint64_t timestamp_us,
                                               uint16_t step_ms,
                                               uint8_t maximum_steps) {
    uint64_t elapsed_us;
    uint64_t step_us;
    uint64_t steps;
    if (previous_timestamp_us == NULL || timestamp_us == 0 ||
        step_ms == 0 || maximum_steps == 0) return 0;
    if (*previous_timestamp_us == 0) {
        *previous_timestamp_us = timestamp_us;
        return 1;
    }
    if (timestamp_us <= *previous_timestamp_us) return 0;
    elapsed_us = timestamp_us - *previous_timestamp_us;
    *previous_timestamp_us = timestamp_us;
    step_us = (uint64_t)step_ms * 1000u;
    steps = (elapsed_us + step_us / 2u) / step_us;
    if (steps == 0) return 0;
    return steps > maximum_steps ? maximum_steps : (uint8_t)steps;
}

static inline int pxa_clock_parse_now(
    const pxa_event_t *event, uint64_t token,
    pxa_clock_now_result_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || token == 0 ||
        event->service != PXA_CLOCK_SERVICE ||
        event->opcode != PXA_CLOCK_NOW || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    out->timestamp_us = pxa_load_u64(event->payload + 4);
    return 1;
}

#endif
