#ifndef PXA_IPC_H
#define PXA_IPC_H

#include "pxa.h"

#define PXA_IPC_CALL 1u
#define PXA_IPC_REPLY 2u
#define PXA_IPC_REQUEST 0x8001u
#define PXA_IPC_RESULT 0x8002u

#define PXA_IPC_ENDPOINT 1u
#define PXA_IPC_CALL_ID 1u
#define PXA_IPC_PAYLOAD 2u
#define PXA_IPC_STATUS 2u
#define PXA_IPC_REPLY_PAYLOAD 3u

typedef struct {
    int32_t status;
    uint32_t call_id;
} pxa_ipc_call_result_t;

typedef struct {
    uint32_t call_id;
    const char* endpoint;
    size_t endpoint_length;
    const uint8_t* payload;
    size_t payload_length;
} pxa_ipc_request_t;

typedef struct {
    uint32_t call_id;
    int32_t status;
    const uint8_t* payload;
    size_t payload_length;
} pxa_ipc_result_t;

static inline int pxa_ipc_valid_endpoint(const char* endpoint, size_t length) {
    if (endpoint == NULL || length == 0 || length > 64 ||
        !((endpoint[0] >= 'A' && endpoint[0] <= 'Z') ||
          (endpoint[0] >= 'a' && endpoint[0] <= 'z'))) return 0;
    for (size_t index = 0; index < length; ++index) {
        const char value = endpoint[index];
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '_' ||
              value == '-')) return 0;
    }
    return 1;
}

static inline int pxa_ipc_call(uint32_t request_id, const char* endpoint,
                               size_t endpoint_length, const uint8_t* body,
                               size_t body_length, uint8_t* payload,
                               size_t payload_capacity, uint8_t* packet,
                               size_t packet_capacity) {
    pxa_writer_t payload_writer;
    pxa_writer_t packet_writer;
    if (request_id == 0 || !pxa_ipc_valid_endpoint(endpoint, endpoint_length) ||
        (body == NULL && body_length != 0) || body_length > 1024 ||
        payload == NULL || packet == NULL || packet_capacity < 12) return 0;
    pxa_writer_init(&payload_writer, payload, payload_capacity);
    if (!pxa_record(&payload_writer, PXA_IPC_ENDPOINT,
                    (const uint8_t*)endpoint, endpoint_length) ||
        (body_length != 0 && !pxa_record(&payload_writer, PXA_IPC_PAYLOAD,
                                          body, body_length)) ||
        payload_writer.failed) return 0;
    pxa_writer_init(&packet_writer, packet, packet_capacity);
    return pxa_message(&packet_writer, PXA_SERVICE_IPC, PXA_IPC_CALL, request_id,
                       payload_writer.data, payload_writer.length) &&
           pxa_control(packet_writer.data, (uint32_t)packet_writer.length) == PXA_STATUS_OK;
}

static inline int pxa_ipc_reply(uint32_t request_id, uint32_t call_id,
                                int32_t status, const uint8_t* body,
                                size_t body_length, uint8_t* payload,
                                size_t payload_capacity, uint8_t* packet,
                                size_t packet_capacity) {
    uint8_t encoded_call[4];
    uint8_t encoded_status[4];
    pxa_writer_t payload_writer;
    pxa_writer_t packet_writer;
    if (request_id == 0 || call_id == 0 || status > PXA_STATUS_OK ||
        status < PXA_STATUS_INTERNAL || (body == NULL && body_length != 0) ||
        body_length > 1024 || payload == NULL || packet == NULL ||
        packet_capacity < 12) return 0;
    encoded_call[0] = (uint8_t)call_id;
    encoded_call[1] = (uint8_t)(call_id >> 8);
    encoded_call[2] = (uint8_t)(call_id >> 16);
    encoded_call[3] = (uint8_t)(call_id >> 24);
    encoded_status[0] = (uint8_t)status;
    encoded_status[1] = (uint8_t)((uint32_t)status >> 8);
    encoded_status[2] = (uint8_t)((uint32_t)status >> 16);
    encoded_status[3] = (uint8_t)((uint32_t)status >> 24);
    pxa_writer_init(&payload_writer, payload, payload_capacity);
    if (!pxa_record(&payload_writer, PXA_IPC_CALL_ID, encoded_call,
                    sizeof(encoded_call)) ||
        !pxa_record(&payload_writer, PXA_IPC_STATUS, encoded_status,
                    sizeof(encoded_status)) ||
        (body_length != 0 && !pxa_record(&payload_writer, PXA_IPC_REPLY_PAYLOAD,
                                          body, body_length)) ||
        payload_writer.failed) return 0;
    pxa_writer_init(&packet_writer, packet, packet_capacity);
    return pxa_message(&packet_writer, PXA_SERVICE_IPC, PXA_IPC_REPLY, request_id,
                       payload_writer.data, payload_writer.length) &&
           pxa_control(packet_writer.data, (uint32_t)packet_writer.length) == PXA_STATUS_OK;
}

static inline int pxa_ipc_parse_call_result(const pxa_event_t* event,
                                            pxa_ipc_call_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_IPC ||
        event->opcode != PXA_IPC_CALL || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 8) return 0;
    output->call_id = pxa_read_u32(event->payload + 4);
    return output->call_id != 0;
}

static inline int pxa_ipc_parse_request(const pxa_event_t* event,
                                        pxa_ipc_request_t* output) {
    size_t offset = 0;
    uint16_t previous = 0;
    int has_endpoint = 0;
    int has_payload = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_IPC ||
        event->opcode != PXA_IPC_REQUEST || event->request_id == 0) return 0;
    *output = (pxa_ipc_request_t){.call_id = event->request_id};
    while (offset < event->payload_length) {
        if (event->payload_length - offset < 4) return 0;
        const uint16_t tag = pxa_read_u16(event->payload + offset);
        const uint16_t length = pxa_read_u16(event->payload + offset + 2);
        offset += 4;
        if (tag < previous || length > event->payload_length - offset) return 0;
        previous = tag;
        if (tag == PXA_IPC_ENDPOINT && !has_endpoint &&
            pxa_ipc_valid_endpoint((const char*)(event->payload + offset), length)) {
            output->endpoint = (const char*)(event->payload + offset);
            output->endpoint_length = length;
            has_endpoint = 1;
        } else if (tag == PXA_IPC_PAYLOAD && !has_payload && length <= 1024) {
            output->payload = event->payload + offset;
            output->payload_length = length;
            has_payload = 1;
        } else {
            return 0;
        }
        offset += length;
    }
    return has_endpoint;
}

static inline int pxa_ipc_parse_result(const pxa_event_t* event,
                                       pxa_ipc_result_t* output) {
    size_t offset = 4;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_IPC ||
        event->opcode != PXA_IPC_RESULT || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    *output = (pxa_ipc_result_t){.call_id = event->request_id,
                                  .status = (int32_t)pxa_read_u32(event->payload)};
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (offset == event->payload_length) return 1;
    if (event->payload_length - offset < 4 ||
        pxa_read_u16(event->payload + offset) != PXA_IPC_REPLY_PAYLOAD) return 0;
    const uint16_t length = pxa_read_u16(event->payload + offset + 2);
    offset += 4;
    if (length != event->payload_length - offset) return 0;
    output->payload = event->payload + offset;
    output->payload_length = length;
    return 1;
}

#endif
