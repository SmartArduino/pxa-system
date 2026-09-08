#ifndef PXA_WORK_H
#define PXA_WORK_H

#include "pxa.h"

#define PXA_SERVICE_WORK 13u

#define PXA_WORK_ENQUEUE 1u
#define PXA_WORK_COMPLETE 3u
#define PXA_WORK_STOP_REQUESTED 0x8001u
#define PXA_WORK_CANCEL 2u

#define PXA_WORK_WORKER 1u
#define PXA_WORK_INITIAL_DELAY_MS 2u
#define PXA_WORK_EXECUTION_HINT_MS 3u
#define PXA_WORK_ID 4u
#define PXA_WORK_INPUT 5u
#define PXA_WORK_RETRY_DELAY_MS 6u
#define PXA_WORK_MAX_ATTEMPTS_TAG 7u
#define PXA_WORK_GRANTED_EXECUTION_MS 8u
#define PXA_WORK_RESULT 9u

#define PXA_CONFIG_WORK_ID 7u
#define PXA_CONFIG_WORK_ATTEMPT 9u
#define PXA_CONFIG_WORK_DEADLINE_MS 10u
#define PXA_CONFIG_WORK_INPUT 11u

#define PXA_WORK_MAX_INPUT_BYTES 24u
#define PXA_WORK_MAX_ATTEMPTS 5u

#define PXA_WORK_SUCCESS 1u
#define PXA_WORK_RETRY 2u
#define PXA_WORK_FAILURE 3u

typedef struct {
    const char *worker;
    size_t worker_length;
    uint32_t initial_delay_ms;
    uint32_t execution_hint_ms;
    const uint8_t *input;
    size_t input_length;
    uint32_t retry_delay_ms;
    uint8_t max_attempts;
} pxa_work_request_t;

typedef struct {
    int32_t status;
    uint32_t id;
    uint32_t granted_execution_ms;
} pxa_work_enqueue_result_t;

typedef struct {
    uint32_t id;
    uint8_t attempt;
    uint64_t deadline_ms;
    const uint8_t *input;
    uint32_t input_length;
} pxa_work_context_t;

typedef struct {
    uint32_t id;
    uint64_t deadline_ms;
} pxa_work_stop_t;

static inline int pxa_work_enqueue(uint32_t request_id,
                                   const pxa_work_request_t *work,
                                   uint8_t *payload, size_t payload_capacity,
                                   uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t value[4];
    if (request_id == 0 || work == NULL || work->worker == NULL ||
        work->worker_length == 0 || work->worker_length > 64 ||
        work->initial_delay_ms > 604800000u ||
        work->input_length > PXA_WORK_MAX_INPUT_BYTES ||
        (work->input_length != 0 && work->input == NULL) ||
        work->max_attempts == 0 ||
        work->max_attempts > PXA_WORK_MAX_ATTEMPTS ||
        (work->max_attempts > 1 && work->retry_delay_ms < 1000u) ||
        work->retry_delay_ms > 604800000u || payload == NULL ||
        packet == NULL || packet_capacity < 12) {
        return 0;
    }
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_WORK_WORKER,
                    (const uint8_t *)work->worker, work->worker_length)) return 0;
    value[0] = (uint8_t)work->initial_delay_ms;
    value[1] = (uint8_t)(work->initial_delay_ms >> 8);
    value[2] = (uint8_t)(work->initial_delay_ms >> 16);
    value[3] = (uint8_t)(work->initial_delay_ms >> 24);
    if (!pxa_record(&request, PXA_WORK_INITIAL_DELAY_MS, value, sizeof(value))) return 0;
    value[0] = (uint8_t)work->execution_hint_ms;
    value[1] = (uint8_t)(work->execution_hint_ms >> 8);
    value[2] = (uint8_t)(work->execution_hint_ms >> 16);
    value[3] = (uint8_t)(work->execution_hint_ms >> 24);
    if (!pxa_record(&request, PXA_WORK_EXECUTION_HINT_MS, value, sizeof(value))) return 0;
    if (work->input_length != 0 &&
        !pxa_record(&request, PXA_WORK_INPUT, work->input,
                    work->input_length)) return 0;
    value[0] = (uint8_t)work->retry_delay_ms;
    value[1] = (uint8_t)(work->retry_delay_ms >> 8);
    value[2] = (uint8_t)(work->retry_delay_ms >> 16);
    value[3] = (uint8_t)(work->retry_delay_ms >> 24);
    if (!pxa_record(&request, PXA_WORK_RETRY_DELAY_MS, value, sizeof(value)) ||
        !pxa_record(&request, PXA_WORK_MAX_ATTEMPTS_TAG,
                    &work->max_attempts, 1)) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_WORK, PXA_WORK_ENQUEUE,
                       request_id, request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_work_parse_enqueue(
    const pxa_event_t *event, pxa_work_enqueue_result_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_WORK ||
        event->opcode != PXA_WORK_ENQUEUE || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->id = 0;
    output->granted_execution_ms = 0;
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 20 ||
        pxa_read_u16(event->payload + 4) != PXA_WORK_ID ||
        pxa_read_u16(event->payload + 6) != 4 ||
        pxa_read_u16(event->payload + 12) != PXA_WORK_GRANTED_EXECUTION_MS ||
        pxa_read_u16(event->payload + 14) != 4) return 0;
    output->id = pxa_read_u32(event->payload + 8);
    output->granted_execution_ms = pxa_read_u32(event->payload + 16);
    return output->id != 0 && output->granted_execution_ms != 0;
}

static inline int pxa_work_complete(uint32_t request_id, uint32_t work_id,
                                    uint8_t result, uint8_t *payload,
                                    size_t payload_capacity, uint8_t *packet,
                                    size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t id[4];
    if (request_id == 0 || work_id == 0 || result < PXA_WORK_SUCCESS ||
        result > PXA_WORK_FAILURE || payload == NULL || packet == NULL ||
        packet_capacity < 12) return 0;
    id[0] = (uint8_t)work_id;
    id[1] = (uint8_t)(work_id >> 8);
    id[2] = (uint8_t)(work_id >> 16);
    id[3] = (uint8_t)(work_id >> 24);
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_WORK_ID, id, sizeof(id)) ||
        !pxa_record(&request, PXA_WORK_RESULT, &result, 1)) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_WORK, PXA_WORK_COMPLETE,
                       request_id, request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_work_cancel(uint32_t request_id, uint32_t work_id,
                                  uint8_t *payload,
                                  size_t payload_capacity, uint8_t *packet,
                                  size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t id[4];
    if (request_id == 0 || work_id == 0 || payload == NULL || packet == NULL ||
        packet_capacity < 12) return 0;
    id[0] = (uint8_t)work_id;
    id[1] = (uint8_t)(work_id >> 8);
    id[2] = (uint8_t)(work_id >> 16);
    id[3] = (uint8_t)(work_id >> 24);
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_WORK_ID, id, sizeof(id))) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_WORK, PXA_WORK_CANCEL,
                       request_id, request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_work_parse_status(const pxa_event_t *event,
                                        uint16_t opcode, int32_t *status) {
    if (event == NULL || status == NULL || event->service != PXA_SERVICE_WORK ||
        event->opcode != opcode || event->request_id == 0 ||
        event->payload_length != 4) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}

static inline int pxa_work_parse_context(const uint8_t *config,
                                         uint32_t config_length,
                                         pxa_work_context_t *output) {
    uint32_t offset = 0;
    uint8_t seen = 0;
    if (config == NULL || output == NULL) return 0;
    output->id = 0;
    output->attempt = 0;
    output->deadline_ms = 0;
    output->input = NULL;
    output->input_length = 0;
    while (offset + 4u <= config_length) {
        uint16_t tag = pxa_read_u16(config + offset);
        uint16_t size = pxa_read_u16(config + offset + 2u);
        offset += 4u;
        if ((uint32_t)size > config_length - offset) return 0;
        if (tag == PXA_CONFIG_WORK_ID && size == 4 && !(seen & 1u)) {
            output->id = pxa_read_u32(config + offset);
            seen |= 1u;
        } else if (tag == PXA_CONFIG_WORK_ATTEMPT && size == 1 && !(seen & 2u)) {
            output->attempt = config[offset];
            seen |= 2u;
        } else if (tag == PXA_CONFIG_WORK_DEADLINE_MS && size == 8 && !(seen & 4u)) {
            output->deadline_ms = pxa_read_u64(config + offset);
            seen |= 4u;
        } else if (tag == PXA_CONFIG_WORK_INPUT && size <= PXA_WORK_MAX_INPUT_BYTES &&
                   !(seen & 8u)) {
            output->input = config + offset;
            output->input_length = size;
            seen |= 8u;
        } else {
            return 0;
        }
        offset += size;
    }
    return offset == config_length && (seen & 7u) == 7u && output->id != 0 &&
           output->attempt != 0 && output->deadline_ms != 0;
}

static inline int pxa_work_parse_stop(const pxa_event_t *event,
                                      pxa_work_stop_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_WORK ||
        event->opcode != PXA_WORK_STOP_REQUESTED || event->request_id != 0 ||
        event->payload_length != 12) return 0;
    output->id = pxa_read_u32(event->payload);
    output->deadline_ms = pxa_read_u64(event->payload + 4);
    return output->id != 0;
}

#endif
