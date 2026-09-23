#ifndef PXA_H
#define PXA_H

#include <stddef.h>
#include <stdint.h>

#define PXA_CORE_VERSION_MAJOR 0u
#define PXA_CORE_VERSION_MINOR 1u
#define PXA_CORE_VERSION_PATCH 0u
#define PXA_CORE_VERSION 0x00000001u

#define PXA_STATUS_OK 0
#define PXA_STATUS_INVALID_ARGUMENT (-1)
#define PXA_STATUS_BAD_STATE (-2)
#define PXA_STATUS_UNSUPPORTED (-3)
#define PXA_STATUS_DENIED (-4)
#define PXA_STATUS_NOT_FOUND (-5)
#define PXA_STATUS_BUSY (-6)
#define PXA_STATUS_WOULD_BLOCK (-7)
#define PXA_STATUS_QUOTA_EXCEEDED (-8)
#define PXA_STATUS_RESOURCE_LIMIT (-9)
#define PXA_STATUS_CANCELLED (-10)
#define PXA_STATUS_INTERNAL (-11)
#define PXA_STATUS_TIMED_OUT (-12)
#define PXA_STATUS_UNAVAILABLE (-13)
#define PXA_STATUS_IO_ERROR (-14)
#define PXA_STATUS_PROTOCOL_ERROR (-15)
#define PXA_STATUS_LIMIT_EXCEEDED (-16)

#define PXA_EVENT_UNHANDLED 0
#define PXA_EVENT_HANDLED 1

#define PXA_SERVICE_CORE 1u
#define PXA_SERVICE_WINDOW 2u
#define PXA_SERVICE_UI 3u
#define PXA_SERVICE_CLOCK 4u
#define PXA_SERVICE_FS 5u
#define PXA_SERVICE_STORAGE 6u
#define PXA_SERVICE_IPC 7u
#define PXA_SERVICE_SENSOR 8u
#define PXA_SERVICE_SURFACE 16u
#define PXA_SERVICE_SYSTEM 17u
#define PXA_SERVICE_GAME_RENDER 18u
#define PXA_SERVICE_LOG 19u
#define PXA_SERVICE_DEVICE 15u

#define PXA_IO_READ 1u
#define PXA_IO_WRITE 2u

#define PXA_CORE_CLOSE_HANDLE 2u

#define PXA_WINDOW_CONFIGURE 1u
#define PXA_WINDOW_GET_SNAPSHOT 2u
#define PXA_WINDOW_METRICS_CHANGED 0x8001u
#define PXA_WINDOW_BACK_REQUESTED 0x8002u
#define PXA_WINDOW_SNAPSHOT_SAFE_INSETS 5u
#define PXA_WINDOW_SNAPSHOT_BAR_INSETS 6u
#define PXA_WINDOW_EDGE_TO_EDGE 1u
#define PXA_WINDOW_STATUS_BAR_MODE 2u
#define PXA_WINDOW_NAVIGATION_BAR_MODE 3u
#define PXA_WINDOW_BAR_VISIBLE 0u
#define PXA_WINDOW_BAR_HIDDEN 1u
#define PXA_WINDOW_BAR_TRANSIENT 2u

#define PXA_POINTER_DOWN 0u
#define PXA_POINTER_MOVE 1u
#define PXA_POINTER_UP 2u
#define PXA_POINTER_CANCEL 3u

#define PXA_CLOCK_SET_PERIOD 1u
#define PXA_CLOCK_NOW 2u
#define PXA_CLOCK_TICK 0x8001u
#define PXA_CLOCK_NOW_RESULT 0x8002u

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t length;
    uint16_t records;
    uint8_t failed;
} pxa_writer_t;

typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint32_t request_id;
    const uint8_t* payload;
    uint32_t payload_length;
} pxa_event_t;

__attribute__((import_module("pxa.core.v0"), import_name("pxa_control")))
int32_t pxa_control(const uint8_t* data, uint32_t length);

__attribute__((import_module("pxa.core.v0"), import_name("pxa_io")))
int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length);

static inline void pxa_writer_init(pxa_writer_t* writer, uint8_t* data,
                                   size_t capacity) {
    writer->data = data;
    writer->capacity = capacity;
    writer->length = 0;
    writer->records = 0;
    writer->failed = 0;
}

static inline int pxa_put_u8(pxa_writer_t* writer, uint8_t value) {
    if (writer == NULL || writer->failed || writer->data == NULL ||
        writer->length >= writer->capacity) {
        if (writer != NULL) writer->failed = 1;
        return 0;
    }
    writer->data[writer->length++] = value;
    return 1;
}

static inline int pxa_put_u16(pxa_writer_t* writer, uint16_t value) {
    return pxa_put_u8(writer, (uint8_t)value) &&
           pxa_put_u8(writer, (uint8_t)(value >> 8));
}

static inline int pxa_put_u32(pxa_writer_t* writer, uint32_t value) {
    return pxa_put_u8(writer, (uint8_t)value) &&
           pxa_put_u8(writer, (uint8_t)(value >> 8)) &&
           pxa_put_u8(writer, (uint8_t)(value >> 16)) &&
           pxa_put_u8(writer, (uint8_t)(value >> 24));
}

static inline int pxa_put_bytes(pxa_writer_t* writer, const uint8_t* data,
                                size_t length) {
    if (writer == NULL || writer->failed || writer->data == NULL ||
        writer->length > writer->capacity ||
        (data == NULL && length != 0) ||
        length > writer->capacity - writer->length) {
        if (writer != NULL) writer->failed = 1;
        return 0;
    }
    for (size_t i = 0; i < length; ++i) writer->data[writer->length++] = data[i];
    return 1;
}

static inline uint16_t pxa_read_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static inline uint32_t pxa_read_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static inline uint64_t pxa_read_u64(const uint8_t* value) {
    return (uint64_t)pxa_read_u32(value) |
           ((uint64_t)pxa_read_u32(value + 4) << 32);
}

static inline int pxa_message(pxa_writer_t* writer, uint16_t service,
                              uint16_t opcode, uint32_t request_id,
                              const uint8_t* payload, size_t payload_length) {
    return payload_length <= UINT32_MAX && pxa_put_u16(writer, service) &&
           pxa_put_u16(writer, opcode) && pxa_put_u32(writer, request_id) &&
           pxa_put_u32(writer, (uint32_t)payload_length) &&
           pxa_put_bytes(writer, payload, payload_length);
}

static inline int pxa_close_handle(uint32_t handle) {
    uint8_t packet[16];
    pxa_writer_t writer;
    uint8_t payload[4];
    if (handle == 0) return 0;
    payload[0] = (uint8_t)handle;
    payload[1] = (uint8_t)(handle >> 8);
    payload[2] = (uint8_t)(handle >> 16);
    payload[3] = (uint8_t)(handle >> 24);
    pxa_writer_init(&writer, packet, sizeof(packet));
    return pxa_message(&writer, PXA_SERVICE_CORE, PXA_CORE_CLOSE_HANDLE, 0,
                       payload, sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_record(pxa_writer_t* writer, uint16_t tag,
                             const uint8_t* payload, size_t payload_length) {
    return tag != 0 && payload_length <= UINT16_MAX &&
           pxa_put_u16(writer, tag) &&
           pxa_put_u16(writer, (uint16_t)payload_length) &&
           pxa_put_bytes(writer, payload, payload_length);
}

static inline int pxa_send(uint16_t service, uint16_t opcode,
                           uint32_t request_id, const uint8_t* payload,
                           size_t payload_length) {
    uint8_t packet[64];
    pxa_writer_t message;
    if (payload_length > sizeof(packet) - 12) return 0;
    pxa_writer_init(&message, packet, sizeof(packet));
    return pxa_message(&message, service, opcode, request_id, payload,
                       payload_length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

/* Window 0.1 snapshot: the Host reports the panel safe area and the status and
 * navigation bar insets, ordered top, right, bottom, left. A Guest keeps its
 * interactive content inside the larger value of both. */
typedef struct {
    uint32_t safe_insets[4];
    uint32_t bar_insets[4];
    uint8_t has_safe_insets;
    uint8_t has_bar_insets;
} pxa_window_insets_view_t;

/* Decodes one snapshot record list. with_status skips the i32 status that
 * prefixes a completed request; metrics-changed events carry no status. */
static inline int pxa_window_parse_snapshot(const uint8_t* data, size_t size,
                                            int with_status,
                                            pxa_window_insets_view_t* output) {
    size_t offset = with_status ? 4u : 0u;
    if (data == NULL || output == NULL || size < offset) return 0;
    if (with_status && pxa_read_u32(data) != (uint32_t)PXA_STATUS_OK) return 0;
    output->has_safe_insets = 0;
    output->has_bar_insets = 0;
    while (offset + 4u <= size) {
        uint16_t tag = pxa_read_u16(data + offset);
        uint16_t length = pxa_read_u16(data + offset + 2u);
        const uint8_t* record;
        uint32_t* target;
        offset += 4u;
        if (length > size - offset) return 0;
        record = data + offset;
        offset += length;
        if (tag == PXA_WINDOW_SNAPSHOT_SAFE_INSETS) {
            target = output->safe_insets;
            output->has_safe_insets = 1;
        } else if (tag == PXA_WINDOW_SNAPSHOT_BAR_INSETS) {
            target = output->bar_insets;
            output->has_bar_insets = 1;
        } else {
            continue;
        }
        if (length != 16u) return 0;
        /* Record order is left, top, right, bottom. */
        target[3] = pxa_read_u32(record);
        target[0] = pxa_read_u32(record + 4);
        target[1] = pxa_read_u32(record + 8);
        target[2] = pxa_read_u32(record + 12);
    }
    return output->has_safe_insets || output->has_bar_insets;
}

static inline int pxa_window_fullscreen(void) {
    uint8_t records[15];
    pxa_writer_t writer;
    const uint8_t enabled = 1;
    const uint8_t transient = PXA_WINDOW_BAR_TRANSIENT;
    pxa_writer_init(&writer, records, sizeof(records));
    return pxa_record(&writer, PXA_WINDOW_EDGE_TO_EDGE, &enabled, 1) &&
           pxa_record(&writer, PXA_WINDOW_STATUS_BAR_MODE, &transient, 1) &&
           pxa_record(&writer, PXA_WINDOW_NAVIGATION_BAR_MODE, &transient, 1) &&
           pxa_send(PXA_SERVICE_WINDOW, PXA_WINDOW_CONFIGURE, 0,
                    writer.data, writer.length);
}

static inline int pxa_clock_set_period(uint16_t period_ms) {
    uint8_t payload[2] = {(uint8_t)period_ms, (uint8_t)(period_ms >> 8)};
    if (period_ms != 0 && (period_ms < 16 || period_ms > 1000)) return 0;
    return pxa_send(PXA_SERVICE_CLOCK, PXA_CLOCK_SET_PERIOD, 0, payload,
                    sizeof(payload));
}

/* Requests the Host monotonic clock. The result is delivered asynchronously
 * as PXA_CLOCK_NOW_RESULT with a status i32 followed by a timestamp u64. */
static inline int pxa_clock_now(uint32_t request_id) {
    if (request_id == 0) return 0;
    return pxa_send(PXA_SERVICE_CLOCK, PXA_CLOCK_NOW, request_id, NULL, 0);
}

static inline int pxa_parse_event(const uint8_t* event, uint32_t length,
                                  pxa_event_t* output) {
    if (event == NULL || output == NULL || length < 12 ||
        pxa_read_u32(event + 8) != length - 12) return 0;
    output->service = pxa_read_u16(event);
    output->opcode = pxa_read_u16(event + 2);
    output->request_id = pxa_read_u32(event + 4);
    output->payload = event + 12;
    output->payload_length = pxa_read_u32(event + 8);
    return 1;
}

/* Clock ticks carry the host's monotonic timestamp in microseconds. Keep the
 * previous value in application state to derive elapsed wall-clock time. */
static inline int pxa_clock_tick_timestamp_us(const pxa_event_t* event,
                                              uint64_t* timestamp_us) {
    if (event == NULL || timestamp_us == NULL ||
        event->service != PXA_SERVICE_CLOCK ||
        event->opcode != PXA_CLOCK_TICK || event->payload_length != 8 ||
        event->payload == NULL) {
        return 0;
    }
    *timestamp_us = pxa_read_u64(event->payload);
    return 1;
}

static inline int pxa_clock_parse_now(const pxa_event_t* event,
                                      int32_t* status,
                                      uint64_t* timestamp_us) {
    if (event == NULL || status == NULL || timestamp_us == NULL ||
        event->service != PXA_SERVICE_CLOCK ||
        event->opcode != PXA_CLOCK_NOW_RESULT || event->request_id == 0 ||
        event->payload == NULL || event->payload_length != 12) {
        return 0;
    }
    *status = (int32_t)pxa_read_u32(event->payload);
    *timestamp_us = pxa_read_u64(event->payload + 4);
    return 1;
}

/* Returns a rounded elapsed duration. The first tick establishes the time
 * origin and returns zero; cap nonzero values to bound recovery after a stall. */
static inline uint32_t pxa_clock_delta_ms(uint64_t* previous_timestamp_us,
                                          uint64_t timestamp_us,
                                          uint32_t maximum_delta_ms) {
    uint64_t elapsed_us;
    uint64_t elapsed_ms;
    if (previous_timestamp_us == NULL || timestamp_us == 0) return 0;
    if (*previous_timestamp_us == 0 || timestamp_us <= *previous_timestamp_us) {
        *previous_timestamp_us = timestamp_us;
        return 0;
    }
    elapsed_us = timestamp_us - *previous_timestamp_us;
    *previous_timestamp_us = timestamp_us;
    elapsed_ms = (elapsed_us + 500u) / 1000u;
    if (maximum_delta_ms != 0 && elapsed_ms > maximum_delta_ms)
        return maximum_delta_ms;
    return elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
}

/* Converts one measured frame into whole nominal simulation steps. Rounding
 * absorbs normal timer jitter; zero steps suppresses a stale burst, while the
 * cap prevents an extended stall from causing a physics or render storm. */
static inline uint8_t pxa_clock_tick_steps(uint64_t* previous_timestamp_us,
                                           const pxa_event_t* event,
                                           uint16_t step_ms,
                                           uint8_t maximum_steps) {
    uint64_t timestamp_us;
    const uint64_t step_us = (uint64_t)step_ms * 1000u;
    uint32_t elapsed_ms;
    uint32_t steps;
    if (step_ms == 0 || maximum_steps == 0 ||
        !pxa_clock_tick_timestamp_us(event, &timestamp_us)) {
        return 0;
    }
    if (previous_timestamp_us == NULL) return 0;
    if (*previous_timestamp_us == 0) {
        *previous_timestamp_us = timestamp_us;
        return 1;
    }
    if (timestamp_us <= *previous_timestamp_us) return 0;
    elapsed_ms = pxa_clock_delta_ms(previous_timestamp_us, timestamp_us,
                                    (uint32_t)step_ms * maximum_steps);
    if (elapsed_ms == 0) return 0;
    steps = ((uint64_t)elapsed_ms * 1000u + step_us / 2u) / step_us;
    if (steps == 0) return 0;
    return steps > maximum_steps ? maximum_steps : (uint8_t)steps;
}

#endif
