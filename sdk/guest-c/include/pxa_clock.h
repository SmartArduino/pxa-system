#ifndef PXA_GUEST_CLOCK_H
#define PXA_GUEST_CLOCK_H

#include "pxa_core.h"

#define PXA_CLOCK_SERVICE 4u
#define PXA_CLOCK_SET_PERIOD 1u
#define PXA_CLOCK_NOW 2u
#define PXA_CLOCK_WALL_TIME 3u
#define PXA_CLOCK_TICK 0x8001u
#define PXA_CLOCK_MIN_PERIOD_MS 16u
#define PXA_CLOCK_MAX_PERIOD_MS 1000u

#define PXA_CLOCK_LIMITS 4u
#define PXA_CLOCK_CONFIGURE 5u
#define PXA_CLOCK_FEATURE_TIMING UINT64_C(1)
#define PXA_CLOCK_TIMING_COALESCED 1u
#define PXA_CLOCK_TIMING_FOREGROUND 2u

typedef struct {
    uint32_t min_period_us, max_period_us, period_step_us, dispatch_quantum_us, flags;
} pxa_clock_limits_t;

typedef struct {
    int32_t status;
    pxa_clock_limits_t limits;
} pxa_clock_limits_result_t;

typedef struct {
    int32_t status;
    uint32_t period_us;
} pxa_clock_configure_result_t;

static inline int pxa_clock_period_supported(const pxa_clock_limits_t *limits,
                                             uint32_t period_us) {
    if (limits == NULL || limits->min_period_us == 0 ||
        limits->max_period_us < limits->min_period_us ||
        limits->period_step_us == 0 || limits->dispatch_quantum_us == 0)
        return 0;
    return period_us == 0 || (period_us >= limits->min_period_us &&
        period_us <= limits->max_period_us && period_us % limits->period_step_us == 0);
}

static inline int pxa_clock_build_limits(uint8_t *packet, size_t capacity,
                                          uint64_t token, uint32_t *written) {
    return token != 0 && pxa_build_message(packet, capacity, PXA_CLOCK_SERVICE,
        PXA_CLOCK_LIMITS, token, NULL, 0, written);
}

/* Call limits first or require Clock 0.3 + the timing feature in the manifest.
 * No SDK-wide minimum: the Host returns a completion for device-specific limits. */
static inline int pxa_clock_build_configure(uint8_t *packet, size_t capacity,
    uint64_t token, uint32_t period_us, uint32_t *written) {
    uint8_t payload[4];
    pxa_store_u32(payload, period_us);
    return token != 0 && pxa_build_message(packet, capacity, PXA_CLOCK_SERVICE,
        PXA_CLOCK_CONFIGURE, token, payload, sizeof(payload), written);
}

static inline int32_t pxa_clock_limits(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES]; uint32_t size = 0;
    if (!pxa_clock_build_limits(packet, sizeof(packet), token, &size))
        return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
}

static inline int32_t pxa_clock_configure(uint64_t token, uint32_t period_us) {
    uint8_t packet[PXA_HEADER_BYTES + 4u]; uint32_t size = 0;
    if (!pxa_clock_build_configure(packet, sizeof(packet), token, period_us, &size))
        return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
}

static inline int pxa_clock_parse_limits(const pxa_event_t *event, uint64_t token,
                                         pxa_clock_limits_result_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || token == 0 || event->service != PXA_CLOCK_SERVICE ||
        event->opcode != PXA_CLOCK_LIMITS || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 24) return 0;
    out->limits.min_period_us = pxa_load_u32(event->payload + 4);
    out->limits.max_period_us = pxa_load_u32(event->payload + 8);
    out->limits.period_step_us = pxa_load_u32(event->payload + 12);
    out->limits.dispatch_quantum_us = pxa_load_u32(event->payload + 16);
    out->limits.flags = pxa_load_u32(event->payload + 20);
    return pxa_clock_period_supported(&out->limits, 0);
}

static inline int pxa_clock_parse_configure(const pxa_event_t *event, uint64_t token,
    uint32_t requested_us, pxa_clock_configure_result_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || token == 0 || event->service != PXA_CLOCK_SERVICE ||
        event->opcode != PXA_CLOCK_CONFIGURE || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 8) return 0;
    out->period_us = pxa_load_u32(event->payload + 4);
    return out->period_us == requested_us;
}

typedef struct {
    int32_t status;
    uint64_t timestamp_us;
} pxa_clock_now_result_t;

typedef struct {
    int32_t status;
    int64_t unix_us;
    uint64_t monotonic_us;
    int32_t utc_offset_seconds;
    uint32_t flags; /* bit 0: daylight saving at this sampled instant. */
} pxa_clock_wall_time_result_t;

static inline int32_t pxa_clock_wall_time(uint64_t token) {
    if (!token) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_call(PXA_CLOCK_SERVICE, PXA_CLOCK_WALL_TIME, token, NULL, 0);
}

static inline int pxa_clock_parse_wall_time(const pxa_event_t *event,
    uint64_t token, pxa_clock_wall_time_result_t *out) {
    int64_t delta;
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || !token || event->token != token ||
        event->service != PXA_CLOCK_SERVICE || event->opcode != PXA_CLOCK_WALL_TIME ||
        !event->payload || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 28) return 0;
    /* Decode two's complement without out-of-range unsigned-to-signed casts. */
    {
        uint64_t value = pxa_load_u64(event->payload + 4);
        out->unix_us = value <= INT64_MAX ? (int64_t)value : -1 - (int64_t)(~value);
    }
    out->monotonic_us = pxa_load_u64(event->payload + 12);
    {
        uint32_t value = pxa_load_u32(event->payload + 20);
        out->utc_offset_seconds = value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(~value);
    }
    out->flags = pxa_load_u32(event->payload + 24);
    if (out->utc_offset_seconds < -86400 || out->utc_offset_seconds > 86400 ||
        (out->flags & ~1u)) return 0;
    delta = (int64_t)out->utc_offset_seconds * 1000000;
    return !((delta > 0 && out->unix_us > INT64_MAX - delta) ||
             (delta < 0 && out->unix_us < INT64_MIN - delta));
}

static inline int pxa_clock_build_set_period(
    uint8_t *packet, size_t capacity, uint16_t period_ms,
    uint32_t *written) {
    uint8_t payload[2];
    if (period_ms != 0 && (period_ms < PXA_CLOCK_MIN_PERIOD_MS ||
                          period_ms > PXA_CLOCK_MAX_PERIOD_MS)) return 0;
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
                                        &size)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
}

static inline int32_t pxa_clock_now(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES];
    uint32_t size = 0;
    if (!pxa_clock_build_now(packet, sizeof(packet), token, &size))
        return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
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
