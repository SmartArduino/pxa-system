#ifndef PXA_GUEST_IPC_H
#define PXA_GUEST_IPC_H

#include "pxa_core.h"

#define PXA_IPC_SERVICE 7u
#define PXA_IPC_CALL 1u
#define PXA_IPC_REPLY 2u
#define PXA_IPC_REQUEST_EVENT 0x8001u
#define PXA_IPC_RESULT_EVENT 0x8002u
#define PXA_IPC_MAX_ENDPOINT 64u
#define PXA_IPC_MAX_PAYLOAD 1024u
#define PXA_IPC_MAX_CALL_PACKET \
    (PXA_HEADER_BYTES + 8u + PXA_IPC_MAX_ENDPOINT + \
     PXA_IPC_MAX_PAYLOAD)
#define PXA_IPC_MAX_REPLY_PACKET \
    (PXA_HEADER_BYTES + 20u + PXA_IPC_MAX_PAYLOAD)

typedef struct {
    const uint8_t *data;
    uint16_t size;
} pxa_ipc_bytes_t;

typedef struct {
    int32_t status;
    uint32_t call_id;
} pxa_ipc_call_result_t;

/* Views into the callback event; copy data if it must outlive the callback. */
typedef struct {
    uint32_t call_id;
    pxa_ipc_bytes_t endpoint;
    pxa_ipc_bytes_t payload;
} pxa_ipc_request_t;

typedef struct {
    uint32_t call_id;
    int32_t status;
    pxa_ipc_bytes_t payload;
} pxa_ipc_result_t;

static inline int pxa_ipc_endpoint_valid(const char *endpoint,
                                             size_t size) {
    if (endpoint == NULL || size == 0 || size > PXA_IPC_MAX_ENDPOINT ||
        !((endpoint[0] >= 'A' && endpoint[0] <= 'Z') ||
          (endpoint[0] >= 'a' && endpoint[0] <= 'z'))) return 0;
    for (size_t i = 1; i < size; ++i) {
        const char value = endpoint[i];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) return 0;
    }
    return 1;
}

/* Both builders write records into the final packet; the caller owns it. */
static inline int pxa_ipc_build_call(
    uint8_t *out, size_t capacity, uint64_t token,
    const char *endpoint, size_t endpoint_size,
    const uint8_t *payload, size_t payload_size, uint32_t *written) {
    size_t first_size = 0;
    size_t second_size = 0;
    uint8_t *records;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        !pxa_ipc_endpoint_valid(endpoint, endpoint_size) ||
        (payload == NULL && payload_size != 0) ||
        payload_size > PXA_IPC_MAX_PAYLOAD ||
        capacity < PXA_HEADER_BYTES + 4u + endpoint_size +
                       (payload_size == 0 ? 0u : 4u + payload_size)) return 0;
    records = out + PXA_HEADER_BYTES;
    if (!pxa_wire_record_encode(records,
                                capacity - PXA_HEADER_BYTES, 1,
                                (const uint8_t *)endpoint, endpoint_size,
                                &first_size)) return 0;
    if (payload_size != 0 &&
        !pxa_wire_record_encode(records + first_size,
                                capacity - PXA_HEADER_BYTES - first_size,
                                2, payload, payload_size,
                                &second_size)) return 0;
    return pxa_build_message(out, capacity, PXA_IPC_SERVICE,
                                PXA_IPC_CALL, token, records,
                                first_size + second_size, written);
}

static inline int32_t pxa_ipc_request_call(
    uint8_t *packet, size_t capacity, uint64_t token,
    const char *endpoint, size_t endpoint_size,
    const uint8_t *payload, size_t payload_size) {
    uint32_t size = 0;
    return pxa_ipc_build_call(packet, capacity, token, endpoint,
                                 endpoint_size, payload, payload_size,
                                 &size)
               ? pxa_submit(packet, size) : -1;
}

static inline int pxa_ipc_build_reply(
    uint8_t *out, size_t capacity, uint64_t token, uint32_t call_id,
    int32_t status, const uint8_t *payload, size_t payload_size,
    uint32_t *written) {
    uint8_t *records;
    uint8_t value[4];
    size_t offset = 0;
    size_t record_size = 0;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 || call_id == 0 ||
        status > 0 || status < PXA_STATUS_LIMIT_EXCEEDED ||
        (payload == NULL && payload_size != 0) ||
        (status != 0 && payload_size != 0) ||
        payload_size > PXA_IPC_MAX_PAYLOAD ||
        capacity < PXA_HEADER_BYTES + 16u +
                       (payload_size == 0 ? 0u : 4u + payload_size)) return 0;
    records = out + PXA_HEADER_BYTES;
    pxa_store_u32(value, call_id);
    if (!pxa_wire_record_encode(records, capacity - PXA_HEADER_BYTES,
                                1, value, sizeof(value), &record_size))
        return 0;
    offset += record_size;
    pxa_store_u32(value, (uint32_t)status);
    if (!pxa_wire_record_encode(records + offset,
                                capacity - PXA_HEADER_BYTES - offset,
                                2, value, sizeof(value), &record_size))
        return 0;
    offset += record_size;
    if (payload_size != 0) {
        if (!pxa_wire_record_encode(records + offset,
                                    capacity - PXA_HEADER_BYTES - offset,
                                    3, payload, payload_size, &record_size))
            return 0;
        offset += record_size;
    }
    return pxa_build_message(out, capacity, PXA_IPC_SERVICE,
                                PXA_IPC_REPLY, token, records, offset,
                                written);
}

static inline int32_t pxa_ipc_request_reply(
    uint8_t *packet, size_t capacity, uint64_t token, uint32_t call_id,
    int32_t status, const uint8_t *payload, size_t payload_size) {
    uint32_t size = 0;
    return pxa_ipc_build_reply(packet, capacity, token, call_id,
                                  status, payload, payload_size, &size)
               ? pxa_submit(packet, size) : -1;
}

static inline int pxa_ipc_parse_call_result(
    const pxa_event_t *event, uint64_t token,
    pxa_ipc_call_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_IPC_SERVICE ||
        event->opcode != PXA_IPC_CALL || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 8) return 0;
    output->call_id = pxa_load_u32(event->payload + 4);
    return output->call_id != 0;
}

static inline int pxa_ipc_parse_reply_result(
    const pxa_event_t *event, uint64_t token, int32_t *status) {
    if (status == NULL || event == NULL || token == 0 ||
        event->service != PXA_IPC_SERVICE ||
        event->opcode != PXA_IPC_REPLY || event->token != token ||
        event->payload == NULL || event->payload_size != 4) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_ipc_parse_request(
    const pxa_event_t *event, pxa_ipc_request_t *output) {
    pxa_wire_record_view_t record;
    size_t offset = 0;
    size_t consumed = 0;
    uint16_t previous = 0;
    int has_endpoint = 0;
    int has_payload = 0;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_IPC_SERVICE ||
        event->opcode != PXA_IPC_REQUEST_EVENT ||
        event->token == 0 || event->token > UINT32_MAX ||
        event->payload == NULL) return 0;
    output->call_id = (uint32_t)event->token;
    while (offset < event->payload_size) {
        if (!pxa_wire_record_decode(event->payload + offset,
                                    event->payload_size - offset,
                                    &record, &consumed) ||
            record.raw_tag < previous) return 0;
        previous = record.raw_tag;
        if (record.tag == 1) {
            if (record.optional || has_endpoint ||
                !pxa_ipc_endpoint_valid((const char *)record.payload,
                                            record.payload_size)) return 0;
            output->endpoint.data = record.payload;
            output->endpoint.size = record.payload_size;
            has_endpoint = 1;
        } else if (record.tag == 2) {
            if (record.optional || has_payload ||
                record.payload_size > PXA_IPC_MAX_PAYLOAD) return 0;
            output->payload.data = record.payload;
            output->payload.size = record.payload_size;
            has_payload = 1;
        } else if (!record.optional) {
            return 0;
        }
        offset += consumed;
    }
    return has_endpoint;
}

static inline int pxa_ipc_parse_result(
    const pxa_event_t *event, pxa_ipc_result_t *output) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t offset = 4;
    uint16_t previous = 0;
    int has_payload = 0;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_IPC_SERVICE ||
        event->opcode != PXA_IPC_RESULT_EVENT ||
        event->token == 0 || event->token > UINT32_MAX ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->call_id = (uint32_t)event->token;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    while (offset < event->payload_size) {
        if (!pxa_wire_record_decode(event->payload + offset,
                                    event->payload_size - offset,
                                    &record, &consumed) ||
            record.raw_tag < previous) return 0;
        previous = record.raw_tag;
        if (record.tag == 3) {
            if (record.optional || has_payload ||
                record.payload_size > PXA_IPC_MAX_PAYLOAD) return 0;
            output->payload.data = record.payload;
            output->payload.size = record.payload_size;
            has_payload = 1;
        } else if (!record.optional) {
            return 0;
        }
        offset += consumed;
    }
    return 1;
}

#endif
