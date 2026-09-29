#ifndef PXA_GUEST_WORK_H
#define PXA_GUEST_WORK_H

#include "pxa_core.h"

#define PXA_WORK_SERVICE 13u
#define PXA_WORK_ENQUEUE 1u
#define PXA_WORK_CANCEL 2u
#define PXA_WORK_COMPLETE 3u
#define PXA_WORK_STOP_REQUESTED 0x8001u
#define PXA_WORK_SUCCESS 1u
#define PXA_WORK_RETRY 2u
#define PXA_WORK_FAILURE 3u
#define PXA_WORK_MAX_WORKER 64u
#define PXA_WORK_MAX_INPUT 24u
#define PXA_WORK_MAX_ATTEMPTS 5u

typedef struct {
    const char *worker;
    uint16_t worker_size;
    uint32_t initial_delay_ms;
    uint32_t execution_hint_ms;
    const uint8_t *input;
    uint8_t input_size;
    uint32_t retry_delay_ms;
    uint8_t max_attempts;
} pxa_work_request_t;

typedef struct {
    int32_t status;
    uint32_t id;
    uint32_t granted_execution_ms;
} pxa_work_enqueue_result_t;

/* input borrows the start-config bytes for the callback lifetime. */
typedef struct {
    uint32_t id;
    uint8_t attempt;
    uint64_t deadline_ms;
    const uint8_t *input;
    uint8_t input_size;
} pxa_work_context_t;

typedef struct {
    uint32_t id;
    uint64_t deadline_ms;
} pxa_work_stop_t;

static inline int pxa_work_worker_valid(const char *worker,
                                            size_t size) {
    if (worker == NULL || size == 0 || size > PXA_WORK_MAX_WORKER ||
        worker[0] < 'a' || worker[0] > 'z') return 0;
    for (size_t i = 1; i < size; ++i) {
        const char c = worker[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static inline int pxa_work_append(uint8_t *packet, size_t capacity,
                                      size_t *offset, uint16_t tag,
                                      const uint8_t *value, size_t size) {
    size_t written = 0;
    if (*offset > capacity ||
        !pxa_wire_record_encode(packet + *offset, capacity - *offset,
                                tag, value, size, &written)) return 0;
    *offset += written;
    return 1;
}

static inline int pxa_work_build_enqueue(
    uint8_t *packet, size_t capacity, uint64_t token,
    const pxa_work_request_t *request, uint32_t *written) {
    uint8_t value[4];
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || request == NULL || token == 0 ||
        capacity < PXA_HEADER_BYTES ||
        !pxa_work_worker_valid(request->worker, request->worker_size) ||
        request->initial_delay_ms > 604800000u ||
        request->input_size > PXA_WORK_MAX_INPUT ||
        (request->input == NULL && request->input_size != 0) ||
        request->retry_delay_ms > 604800000u ||
        request->max_attempts == 0 ||
        request->max_attempts > PXA_WORK_MAX_ATTEMPTS ||
        (request->max_attempts > 1 && request->retry_delay_ms < 1000u))
        return 0;
    if (!pxa_work_append(packet, capacity, &offset, 1,
                            (const uint8_t *)request->worker,
                            request->worker_size)) return 0;
    pxa_store_u32(value, request->initial_delay_ms);
    if (!pxa_work_append(packet, capacity, &offset, 2, value, 4)) return 0;
    pxa_store_u32(value, request->execution_hint_ms);
    if (!pxa_work_append(packet, capacity, &offset, 3, value, 4)) return 0;
    if (request->input_size != 0 &&
        !pxa_work_append(packet, capacity, &offset, 5,
                            request->input, request->input_size)) return 0;
    pxa_store_u32(value, request->retry_delay_ms);
    if (!pxa_work_append(packet, capacity, &offset, 6, value, 4) ||
        !pxa_work_append(packet, capacity, &offset, 7,
                            &request->max_attempts, 1)) return 0;
    return pxa_finish_message_in_place(packet, capacity,
                                          PXA_WORK_SERVICE,
                                          PXA_WORK_ENQUEUE,
                                          token, offset, written);
}

static inline int32_t pxa_work_enqueue(
    uint64_t token, const pxa_work_request_t *request) {
    uint8_t packet[PXA_HEADER_BYTES + 125u];
    uint32_t size = 0;
    if (!pxa_work_build_enqueue(packet, sizeof(packet), token,
                                    request, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_work_build_id_request(
    uint8_t *packet, size_t capacity, uint16_t opcode, uint64_t token,
    uint32_t id, uint8_t result, uint32_t *written) {
    uint8_t value[4];
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 || id == 0 ||
        capacity < PXA_HEADER_BYTES ||
        (opcode != PXA_WORK_CANCEL && opcode != PXA_WORK_COMPLETE) ||
        (opcode == PXA_WORK_COMPLETE &&
         (result < PXA_WORK_SUCCESS || result > PXA_WORK_FAILURE)))
        return 0;
    pxa_store_u32(value, id);
    if (!pxa_work_append(packet, capacity, &offset, 4, value, 4)) return 0;
    if (opcode == PXA_WORK_COMPLETE &&
        !pxa_work_append(packet, capacity, &offset, 9, &result, 1))
        return 0;
    return pxa_finish_message_in_place(packet, capacity,
                                          PXA_WORK_SERVICE, opcode,
                                          token, offset, written);
}

static inline int32_t pxa_work_cancel(uint64_t token, uint32_t id) {
    uint8_t packet[PXA_HEADER_BYTES + 8u];
    uint32_t size = 0;
    if (!pxa_work_build_id_request(packet, sizeof(packet),
                                      PXA_WORK_CANCEL, token, id, 0,
                                      &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_work_complete(uint64_t token, uint32_t id,
                                             uint8_t result) {
    uint8_t packet[PXA_HEADER_BYTES + 13u];
    uint32_t size = 0;
    if (!pxa_work_build_id_request(packet, sizeof(packet),
                                      PXA_WORK_COMPLETE, token, id,
                                      result, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_work_parse_enqueue(
    const pxa_event_t *event, uint64_t token,
    pxa_work_enqueue_result_t *out) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t offset = 4;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_WORK_SERVICE ||
        event->opcode != PXA_WORK_ENQUEUE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 20 ||
        !pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 4 ||
        record.payload_size != 4) return 0;
    out->id = pxa_load_u32(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 8 ||
        record.payload_size != 4 ||
        offset + consumed != event->payload_size) return 0;
    out->granted_execution_ms = pxa_load_u32(record.payload);
    return out->id != 0 && out->granted_execution_ms != 0;
}

static inline int pxa_work_parse_status(
    const pxa_event_t *event, uint64_t token, uint16_t opcode,
    int32_t *status) {
    if (event == NULL || status == NULL || token == 0 ||
        event->service != PXA_WORK_SERVICE || event->opcode != opcode ||
        event->token != token || event->payload == NULL ||
        event->payload_size != 4 ||
        (opcode != PXA_WORK_CANCEL && opcode != PXA_WORK_COMPLETE))
        return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_work_parse_context(
    const uint8_t *config, size_t size, pxa_work_context_t *out) {
    pxa_wire_record_view_t record;
    size_t offset = 0;
    size_t consumed = 0;
    unsigned seen = 0;
    uint16_t previous = 0;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (config == NULL) return 0;
    while (offset < size) {
        if (!pxa_wire_record_decode(config + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag < previous) return 0;
        previous = record.raw_tag;
        if (record.raw_tag == 7 && !(seen & 1u) &&
            record.payload_size == 4) {
            out->id = pxa_load_u32(record.payload);
            seen |= 1u;
        } else if (record.raw_tag == 9 && !(seen & 2u) &&
                   record.payload_size == 1) {
            out->attempt = record.payload[0];
            seen |= 2u;
        } else if (record.raw_tag == 10 && !(seen & 4u) &&
                   record.payload_size == 8) {
            out->deadline_ms = pxa_load_u64(record.payload);
            seen |= 4u;
        } else if (record.raw_tag == 11 && !(seen & 8u) &&
                   record.payload_size <= PXA_WORK_MAX_INPUT) {
            out->input = record.payload;
            out->input_size = (uint8_t)record.payload_size;
            seen |= 8u;
        } else if (!record.optional) return 0;
        offset += consumed;
    }
    return (seen & 7u) == 7u && out->id != 0 && out->attempt != 0 &&
           out->deadline_ms != 0;
}

static inline int pxa_work_parse_stop(
    const pxa_event_t *event, pxa_work_stop_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_WORK_SERVICE ||
        event->opcode != PXA_WORK_STOP_REQUESTED || event->token != 0 ||
        event->payload == NULL || event->payload_size != 12) return 0;
    out->id = pxa_load_u32(event->payload);
    out->deadline_ms = pxa_load_u64(event->payload + 4);
    return out->id != 0 && out->deadline_ms != 0;
}

#endif
