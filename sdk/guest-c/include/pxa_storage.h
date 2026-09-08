#ifndef PXA_STORAGE_H
#define PXA_STORAGE_H

#include "pxa.h"

#define PXA_STORAGE_GET 1u
#define PXA_STORAGE_SET 2u
#define PXA_STORAGE_REMOVE 3u
#define PXA_STORAGE_LIST 4u

#define PXA_STORAGE_KEY 1u
#define PXA_STORAGE_VALUE 2u

#define PXA_STORAGE_MAX_KEY_BYTES 64u

typedef struct {
    int32_t status;
    const uint8_t* value;
    uint16_t value_length;
} pxa_storage_get_result_t;

typedef struct {
    int32_t status;
    const uint8_t* records;
    uint32_t records_length;
    uint16_t key_count;
} pxa_storage_list_result_t;

static inline int pxa_storage_valid_key(const char* key, size_t key_length) {
    size_t index;
    if (key == NULL || key_length == 0 || key_length > PXA_STORAGE_MAX_KEY_BYTES ||
        !((key[0] >= 'a' && key[0] <= 'z') ||
          (key[0] >= 'A' && key[0] <= 'Z'))) return 0;
    for (index = 1; index < key_length; ++index) {
        const char value = key[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '_' ||
              value == '-')) return 0;
    }
    return 1;
}

static inline int pxa_storage_request(uint16_t opcode, uint32_t request_id,
                                      const char* key, size_t key_length,
                                      const uint8_t* value, size_t value_length,
                                      uint8_t* payload, size_t payload_capacity,
                                      uint8_t* packet, size_t packet_capacity) {
    pxa_writer_t writer;
    pxa_writer_t message;
    if (request_id == 0 || payload == NULL || packet == NULL ||
        packet_capacity < 12 || value_length > 2048 ||
        (value == NULL && value_length != 0) ||
        (key != NULL && !pxa_storage_valid_key(key, key_length)) ||
        (key == NULL && key_length != 0) ||
        ((opcode != PXA_STORAGE_LIST) && !pxa_storage_valid_key(key, key_length)) ||
        (opcode != PXA_STORAGE_GET && opcode != PXA_STORAGE_SET &&
         opcode != PXA_STORAGE_REMOVE && opcode != PXA_STORAGE_LIST) ||
        (opcode == PXA_STORAGE_SET && value == NULL && value_length != 0) ||
        (opcode != PXA_STORAGE_SET && value_length != 0)) return 0;
    pxa_writer_init(&writer, payload, payload_capacity);
    if (key != NULL &&
        !pxa_record(&writer, PXA_STORAGE_KEY, (const uint8_t*)key, key_length)) return 0;
    if (opcode == PXA_STORAGE_SET &&
        !pxa_record(&writer, PXA_STORAGE_VALUE, value, value_length)) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_STORAGE, opcode, request_id,
                       writer.data, writer.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_storage_get(uint32_t request_id, const char* key,
                                  size_t key_length, uint8_t* payload,
                                  size_t payload_capacity, uint8_t* packet,
                                  size_t packet_capacity) {
    return pxa_storage_request(PXA_STORAGE_GET, request_id, key, key_length,
                               NULL, 0, payload, payload_capacity, packet,
                               packet_capacity);
}

static inline int pxa_storage_set(uint32_t request_id, const char* key,
                                  size_t key_length, const uint8_t* value,
                                  size_t value_length, uint8_t* payload,
                                  size_t payload_capacity, uint8_t* packet,
                                  size_t packet_capacity) {
    return pxa_storage_request(PXA_STORAGE_SET, request_id, key, key_length,
                               value, value_length, payload, payload_capacity,
                               packet, packet_capacity);
}

static inline int pxa_storage_remove(uint32_t request_id, const char* key,
                                     size_t key_length, uint8_t* payload,
                                     size_t payload_capacity, uint8_t* packet,
                                     size_t packet_capacity) {
    return pxa_storage_request(PXA_STORAGE_REMOVE, request_id, key, key_length,
                               NULL, 0, payload, payload_capacity, packet,
                               packet_capacity);
}

static inline int pxa_storage_list(uint32_t request_id, const char* after_key,
                                   size_t after_key_length, uint8_t* payload,
                                   size_t payload_capacity, uint8_t* packet,
                                   size_t packet_capacity) {
    return pxa_storage_request(PXA_STORAGE_LIST, request_id, after_key,
                               after_key_length, NULL, 0,
                               payload, payload_capacity, packet, packet_capacity);
}

static inline int pxa_storage_parse_status(const pxa_event_t* event,
                                           uint16_t opcode, int32_t* status) {
    if (event == NULL || status == NULL || event->service != PXA_SERVICE_STORAGE ||
        event->opcode != opcode || event->request_id == 0 ||
        event->payload_length != 4) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}

static inline int pxa_storage_parse_get(const pxa_event_t* event,
                                        pxa_storage_get_result_t* output) {
    uint16_t tag;
    uint16_t length;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_STORAGE ||
        event->opcode != PXA_STORAGE_GET || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->value = NULL;
    output->value_length = 0;
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length < 8) return 0;
    tag = pxa_read_u16(event->payload + 4);
    length = pxa_read_u16(event->payload + 6);
    if (tag != PXA_STORAGE_VALUE || length != event->payload_length - 8) return 0;
    output->value = event->payload + 8;
    output->value_length = length;
    return 1;
}

static inline int pxa_storage_parse_list(const pxa_event_t* event,
                                         pxa_storage_list_result_t* output) {
    size_t offset = 4;
    const uint8_t* previous = NULL;
    uint16_t previous_length = 0;
    uint16_t count = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_STORAGE ||
        event->opcode != PXA_STORAGE_LIST || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->records = NULL;
    output->records_length = 0;
    output->key_count = 0;
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    output->records = event->payload + offset;
    output->records_length = event->payload_length - offset;
    while (offset < event->payload_length) {
        uint16_t tag;
        uint16_t length;
        const uint8_t* key;
        if (event->payload_length - offset < 4) return 0;
        tag = pxa_read_u16(event->payload + offset);
        length = pxa_read_u16(event->payload + offset + 2);
        key = event->payload + offset + 4;
        if (tag != PXA_STORAGE_KEY ||
            length == 0 || length > event->payload_length - offset - 4 ||
            !pxa_storage_valid_key((const char*)key, length)) return 0;
        if (previous != NULL) {
            size_t index = 0;
            const size_t shortest = previous_length < length ? previous_length : length;
            while (index < shortest && previous[index] == key[index]) ++index;
            if (index == shortest ? previous_length >= length : previous[index] >= key[index])
                return 0;
        }
        previous = key;
        previous_length = length;
        offset += 4 + length;
        if (++count > 14) return 0;
    }
    output->key_count = count;
    return 1;
}

#endif
