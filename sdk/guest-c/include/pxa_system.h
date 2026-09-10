#ifndef PXA_SYSTEM_H
#define PXA_SYSTEM_H

#include "pxa.h"

#define PXA_SYSTEM_INTENT_START 1u
#define PXA_SYSTEM_SERVICE_INVOKE 2u
#define PXA_SYSTEM_TOPIC_PUBLISH 3u
#define PXA_SYSTEM_TOPIC_SUBSCRIBE 4u
#define PXA_SYSTEM_TOPIC_UNSUBSCRIBE 5u
#define PXA_SYSTEM_SERVICE_REGISTER 6u
#define PXA_SYSTEM_SERVICE_UNREGISTER 7u
#define PXA_SYSTEM_SERVICE_COMPLETE 8u
#define PXA_SYSTEM_TOPIC_EVENT UINT16_C(0x8001)
#define PXA_SYSTEM_SERVICE_REQUEST UINT16_C(0x8002)
#define PXA_SYSTEM_INTENT_EVENT UINT16_C(0x8003)
#define PXA_SYSTEM_CONFIGURATION_EVENT UINT16_C(0x8004)
#define PXA_SYSTEM_CONFIGURATION_LOCALE 1u
#define PXA_SYSTEM_CONFIGURATION_TEXT_DIRECTION 2u
#define PXA_SYSTEM_CONFIG_ENVIRONMENT 12u
#define PXA_SYSTEM_TEXT_DIRECTION_LTR 0u
#define PXA_SYSTEM_TEXT_DIRECTION_RTL 1u
#define PXA_SYSTEM_LOCALE_MAX_BYTES 63u
#define PXA_SYSTEM_INTENT_WIRE_MIN_BYTES 40u
#define PXA_SYSTEM_RECORD_OPTIONAL UINT16_C(0x8000)

typedef struct {
    int32_t status;
    const uint8_t* payload;
    uint32_t payload_size;
} pxa_system_result_t;

typedef struct {
    const uint8_t* topic;
    uint16_t topic_size;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t event;
    uint64_t sequence;
    const uint8_t* payload;
    uint16_t payload_size;
} pxa_system_topic_event_t;

typedef struct {
    const uint8_t* interface_id;
    uint16_t interface_id_size;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t operation;
    uint32_t flags;
    const uint8_t* payload;
    uint16_t payload_size;
    uint64_t call_id;
    const uint8_t* caller_publisher_root;
    const uint8_t* caller_app_id;
    uint16_t caller_app_id_size;
    const uint8_t* caller_component_id;
    uint16_t caller_component_id_size;
} pxa_system_service_request_t;

typedef struct {
    const uint8_t* intent;
    uint16_t intent_size;
    const uint8_t* caller_publisher_root;
    const uint8_t* caller_app_id;
    uint16_t caller_app_id_size;
    const uint8_t* caller_component_id;
    uint16_t caller_component_id_size;
} pxa_system_intent_event_t;

typedef struct {
    const uint8_t* locale;
    uint16_t locale_size;
    uint8_t text_direction;
} pxa_system_configuration_event_t;

static inline int pxa_system_send_records(uint16_t opcode, uint32_t request_id,
                                          pxa_writer_t* records, uint8_t* packet,
                                          size_t packet_capacity) {
    pxa_writer_t message;
    if (request_id == 0 || records == NULL || records->failed || packet == NULL ||
        records->data != packet + 12 || packet_capacity < 12 ||
        records->length > packet_capacity - 12) {
        return 0;
    }
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_SYSTEM, opcode, request_id, records->data,
                       records->length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

/* intent is the backend-neutral PXIN record defined by PXA System. */
static inline int pxa_system_start_intent(uint32_t request_id, const uint8_t* intent,
                                          size_t intent_size, uint8_t* packet,
                                          size_t packet_capacity) {
    pxa_writer_t message;
    if (request_id == 0 || intent == NULL || intent_size < PXA_SYSTEM_INTENT_WIRE_MIN_BYTES ||
        intent_size > UINT32_MAX || packet == NULL) {
        return 0;
    }
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_SYSTEM, PXA_SYSTEM_INTENT_START, request_id, intent,
                       intent_size) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_system_invoke(uint32_t request_id, const uint8_t* interface_id,
                                    size_t interface_id_size, uint16_t version_major,
                                    uint16_t version_minor, uint32_t operation, uint32_t flags,
                                    const uint8_t* payload, size_t payload_size, uint8_t* packet,
                                    size_t packet_capacity) {
    uint8_t value[4];
    pxa_writer_t records;
    if (interface_id == NULL || interface_id_size == 0 || interface_id_size > UINT16_MAX ||
        operation == 0 || (payload == NULL && payload_size != 0) || payload_size > UINT16_MAX ||
        packet == NULL || packet_capacity < 12) {
        return 0;
    }
    pxa_writer_init(&records, packet + 12, packet_capacity - 12);
    value[0] = (uint8_t)version_major;
    value[1] = (uint8_t)(version_major >> 8);
    value[2] = (uint8_t)version_minor;
    value[3] = (uint8_t)(version_minor >> 8);
    if (!pxa_record(&records, 1, interface_id, interface_id_size) ||
        !pxa_record(&records, 2, value, sizeof(value))) {
        return 0;
    }
    value[0] = (uint8_t)operation;
    value[1] = (uint8_t)(operation >> 8);
    value[2] = (uint8_t)(operation >> 16);
    value[3] = (uint8_t)(operation >> 24);
    if (!pxa_record(&records, 3, value, sizeof(value)))
        return 0;
    value[0] = (uint8_t)flags;
    value[1] = (uint8_t)(flags >> 8);
    value[2] = (uint8_t)(flags >> 16);
    value[3] = (uint8_t)(flags >> 24);
    if (!pxa_record(&records, 4 | PXA_SYSTEM_RECORD_OPTIONAL, value, sizeof(value)) ||
        (payload_size != 0 &&
         !pxa_record(&records, 5 | PXA_SYSTEM_RECORD_OPTIONAL, payload, payload_size))) {
        return 0;
    }
    return pxa_system_send_records(PXA_SYSTEM_SERVICE_INVOKE, request_id, &records, packet,
                                   packet_capacity);
}

static inline int pxa_system_topic_request(uint16_t opcode, uint32_t request_id,
                                           const uint8_t* topic, size_t topic_size,
                                           uint16_t version_major, uint16_t version_minor,
                                           uint32_t event, uint64_t sequence,
                                           const uint8_t* payload, size_t payload_size,
                                           uint8_t* packet, size_t packet_capacity) {
    uint8_t value[8];
    pxa_writer_t records;
    size_t index;
    if (topic == NULL || topic_size == 0 || topic_size > UINT16_MAX ||
        (payload == NULL && payload_size != 0) || payload_size > UINT16_MAX || packet == NULL ||
        packet_capacity < 12) {
        return 0;
    }
    pxa_writer_init(&records, packet + 12, packet_capacity - 12);
    value[0] = (uint8_t)version_major;
    value[1] = (uint8_t)(version_major >> 8);
    value[2] = (uint8_t)version_minor;
    value[3] = (uint8_t)(version_minor >> 8);
    if (!pxa_record(&records, 1, topic, topic_size) || !pxa_record(&records, 2, value, 4))
        return 0;
    value[0] = (uint8_t)event;
    value[1] = (uint8_t)(event >> 8);
    value[2] = (uint8_t)(event >> 16);
    value[3] = (uint8_t)(event >> 24);
    if (!pxa_record(&records, 3, value, 4))
        return 0;
    for (index = 0; index < 8; ++index)
        value[index] = (uint8_t)(sequence >> (index * 8));
    if (!pxa_record(&records, 4 | PXA_SYSTEM_RECORD_OPTIONAL, value, 8) ||
        (payload_size != 0 &&
         !pxa_record(&records, 5 | PXA_SYSTEM_RECORD_OPTIONAL, payload, payload_size))) {
        return 0;
    }
    return pxa_system_send_records(opcode, request_id, &records, packet, packet_capacity);
}

static inline int pxa_system_publish(uint32_t request_id, const uint8_t* topic, size_t topic_size,
                                     uint16_t version_major, uint16_t version_minor, uint32_t event,
                                     uint64_t sequence, const uint8_t* payload, size_t payload_size,
                                     uint8_t* packet, size_t packet_capacity) {
    return pxa_system_topic_request(PXA_SYSTEM_TOPIC_PUBLISH, request_id, topic, topic_size,
                                    version_major, version_minor, event, sequence, payload,
                                    payload_size, packet, packet_capacity);
}

static inline int pxa_system_subscribe(uint32_t request_id, const uint8_t* topic, size_t topic_size,
                                       uint16_t version_major, uint16_t version_minor,
                                       uint8_t* packet, size_t packet_capacity) {
    return pxa_system_topic_request(PXA_SYSTEM_TOPIC_SUBSCRIBE, request_id, topic, topic_size,
                                    version_major, version_minor, 0, 0, NULL, 0, packet,
                                    packet_capacity);
}

static inline int pxa_system_unsubscribe(uint32_t request_id, const uint8_t* topic,
                                         size_t topic_size, uint16_t version_major,
                                         uint16_t version_minor, uint8_t* packet,
                                         size_t packet_capacity) {
    return pxa_system_topic_request(PXA_SYSTEM_TOPIC_UNSUBSCRIBE, request_id, topic, topic_size,
                                    version_major, version_minor, 0, 0, NULL, 0, packet,
                                    packet_capacity);
}

static inline int pxa_system_service_registration(uint16_t opcode, uint32_t request_id,
                                                  const uint8_t* interface_id,
                                                  size_t interface_id_size, uint16_t version_major,
                                                  uint16_t version_minor, uint64_t features,
                                                  uint8_t* packet, size_t packet_capacity) {
    uint8_t value[8];
    pxa_writer_t records;
    size_t index;
    if (interface_id == NULL || interface_id_size == 0 || interface_id_size > UINT16_MAX ||
        packet == NULL || packet_capacity < 12)
        return 0;
    pxa_writer_init(&records, packet + 12, packet_capacity - 12);
    value[0] = (uint8_t)version_major;
    value[1] = (uint8_t)(version_major >> 8);
    value[2] = (uint8_t)version_minor;
    value[3] = (uint8_t)(version_minor >> 8);
    if (!pxa_record(&records, 1, interface_id, interface_id_size) ||
        !pxa_record(&records, 2, value, 4))
        return 0;
    for (index = 0; index < 8; ++index)
        value[index] = (uint8_t)(features >> (index * 8));
    if (!pxa_record(&records, 3 | PXA_SYSTEM_RECORD_OPTIONAL, value, 8))
        return 0;
    return pxa_system_send_records(opcode, request_id, &records, packet, packet_capacity);
}

static inline int pxa_system_register_service(uint32_t request_id, const uint8_t* interface_id,
                                              size_t interface_id_size, uint16_t version_major,
                                              uint16_t version_minor, uint64_t features,
                                              uint8_t* packet, size_t packet_capacity) {
    return pxa_system_service_registration(PXA_SYSTEM_SERVICE_REGISTER, request_id, interface_id,
                                           interface_id_size, version_major, version_minor,
                                           features, packet, packet_capacity);
}

static inline int pxa_system_unregister_service(uint32_t request_id, const uint8_t* interface_id,
                                                size_t interface_id_size, uint16_t version_major,
                                                uint8_t* packet, size_t packet_capacity) {
    return pxa_system_service_registration(PXA_SYSTEM_SERVICE_UNREGISTER, request_id, interface_id,
                                           interface_id_size, version_major, 0, 0, packet,
                                           packet_capacity);
}

static inline int pxa_system_complete_service(uint32_t request_id, uint64_t call_id, int32_t status,
                                              const uint8_t* payload, size_t payload_size,
                                              uint8_t* packet, size_t packet_capacity) {
    uint8_t value[8];
    pxa_writer_t records;
    size_t index;
    if (request_id == 0 || call_id == 0 || status > PXA_STATUS_OK ||
        status < PXA_STATUS_LIMIT_EXCEEDED || status == PXA_STATUS_WOULD_BLOCK ||
        (payload == NULL && payload_size != 0) || payload_size > UINT16_MAX || packet == NULL ||
        packet_capacity < 12) {
        return 0;
    }
    pxa_writer_init(&records, packet + 12, packet_capacity - 12);
    for (index = 0; index < 8; ++index)
        value[index] = (uint8_t)(call_id >> (index * 8));
    if (!pxa_record(&records, 1, value, 8))
        return 0;
    value[0] = (uint8_t)status;
    value[1] = (uint8_t)((uint32_t)status >> 8);
    value[2] = (uint8_t)((uint32_t)status >> 16);
    value[3] = (uint8_t)((uint32_t)status >> 24);
    if (!pxa_record(&records, 2, value, 4) ||
        (payload_size != 0 &&
         !pxa_record(&records, 3 | PXA_SYSTEM_RECORD_OPTIONAL, payload, payload_size))) {
        return 0;
    }
    return pxa_system_send_records(PXA_SYSTEM_SERVICE_COMPLETE, request_id, &records, packet,
                                   packet_capacity);
}

static inline int pxa_system_parse_result(const pxa_event_t* event, pxa_system_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SYSTEM ||
        event->opcode < PXA_SYSTEM_INTENT_START || event->opcode > PXA_SYSTEM_SERVICE_COMPLETE ||
        event->request_id == 0 || event->payload == NULL || event->payload_length < 4) {
        return 0;
    }
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->payload = event->payload + 4;
    output->payload_size = event->payload_length - 4;
    return output->status <= PXA_STATUS_OK && output->status >= PXA_STATUS_LIMIT_EXCEEDED;
}

static inline int pxa_system_parse_topic_event(const pxa_event_t* event,
                                               pxa_system_topic_event_t* output) {
    const uint8_t* cursor;
    size_t remaining;
    uint16_t previous = 0;
    uint8_t seen = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SYSTEM ||
        event->opcode != PXA_SYSTEM_TOPIC_EVENT || event->request_id != 0 ||
        (event->payload == NULL && event->payload_length != 0)) {
        return 0;
    }
    output->topic = NULL;
    output->topic_size = 0;
    output->version_major = 0;
    output->version_minor = 0;
    output->event = 0;
    output->sequence = 0;
    output->payload = NULL;
    output->payload_size = 0;
    cursor = event->payload;
    remaining = event->payload_length;
    while (remaining != 0) {
        uint16_t raw_tag;
        uint16_t tag;
        uint16_t size;
        int optional;
        if (remaining < 4)
            return 0;
        raw_tag = pxa_read_u16(cursor);
        size = pxa_read_u16(cursor + 2);
        if (raw_tag == 0 || raw_tag < previous || size > remaining - 4)
            return 0;
        previous = raw_tag;
        tag = raw_tag & UINT16_C(0x7fff);
        optional = (raw_tag & PXA_SYSTEM_RECORD_OPTIONAL) != 0;
        cursor += 4;
        remaining -= 4;
        if (tag == 1 && !optional && !(seen & 1u)) {
            output->topic = cursor;
            output->topic_size = size;
            seen |= 1u;
        } else if (tag == 2 && !optional && !(seen & 2u) && size == 4) {
            output->version_major = pxa_read_u16(cursor);
            output->version_minor = pxa_read_u16(cursor + 2);
            seen |= 2u;
        } else if (tag == 3 && !optional && !(seen & 4u) && size == 4) {
            output->event = pxa_read_u32(cursor);
            seen |= 4u;
        } else if (tag == 4 && !(seen & 8u) && size == 8) {
            output->sequence = pxa_read_u64(cursor);
            seen |= 8u;
        } else if (tag == 5 && !(seen & 16u)) {
            output->payload = cursor;
            output->payload_size = size;
            seen |= 16u;
        } else if (!(optional && tag > 5)) {
            return 0;
        }
        cursor += size;
        remaining -= size;
    }
    return (seen & 7u) == 7u && output->topic_size != 0;
}

static inline int pxa_system_parse_service_request(const pxa_event_t* event,
                                                   pxa_system_service_request_t* output) {
    const uint8_t* cursor;
    size_t remaining;
    uint16_t previous = 0;
    uint16_t seen = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SYSTEM ||
        event->opcode != PXA_SYSTEM_SERVICE_REQUEST || event->request_id != 0 ||
        (event->payload == NULL && event->payload_length != 0))
        return 0;
    output->interface_id = NULL;
    output->interface_id_size = 0;
    output->version_major = 0;
    output->version_minor = 0;
    output->operation = 0;
    output->flags = 0;
    output->payload = NULL;
    output->payload_size = 0;
    output->call_id = 0;
    output->caller_publisher_root = NULL;
    output->caller_app_id = NULL;
    output->caller_app_id_size = 0;
    output->caller_component_id = NULL;
    output->caller_component_id_size = 0;
    cursor = event->payload;
    remaining = event->payload_length;
    while (remaining != 0) {
        uint16_t raw_tag;
        uint16_t tag;
        uint16_t size;
        int optional;
        if (remaining < 4)
            return 0;
        raw_tag = pxa_read_u16(cursor);
        size = pxa_read_u16(cursor + 2);
        if (raw_tag == 0 || raw_tag < previous || size > remaining - 4)
            return 0;
        previous = raw_tag;
        tag = raw_tag & UINT16_C(0x7fff);
        optional = (raw_tag & PXA_SYSTEM_RECORD_OPTIONAL) != 0;
        cursor += 4;
        remaining -= 4;
        if (tag == 1 && !optional && !(seen & 1u)) {
            output->interface_id = cursor;
            output->interface_id_size = size;
            seen |= 1u;
        } else if (tag == 2 && !optional && !(seen & 2u) && size == 4) {
            output->version_major = pxa_read_u16(cursor);
            output->version_minor = pxa_read_u16(cursor + 2);
            seen |= 2u;
        } else if (tag == 3 && !optional && !(seen & 4u) && size == 4) {
            output->operation = pxa_read_u32(cursor);
            seen |= 4u;
        } else if (tag == 4 && !(seen & 8u) && size == 4) {
            output->flags = pxa_read_u32(cursor);
            seen |= 8u;
        } else if (tag == 5 && !(seen & 16u)) {
            output->payload = cursor;
            output->payload_size = size;
            seen |= 16u;
        } else if (tag == 6 && !(seen & 32u) && size == 8) {
            output->call_id = pxa_read_u64(cursor);
            seen |= 32u;
        } else if (tag == 7 && !(seen & 64u) && size == 32) {
            output->caller_publisher_root = cursor;
            seen |= 64u;
        } else if (tag == 8 && !(seen & 128u)) {
            output->caller_app_id = cursor;
            output->caller_app_id_size = size;
            seen |= 128u;
        } else if (tag == 9 && !(seen & 256u)) {
            output->caller_component_id = cursor;
            output->caller_component_id_size = size;
            seen |= 256u;
        } else if (!(optional && tag > 9)) {
            return 0;
        }
        cursor += size;
        remaining -= size;
    }
    return (seen & UINT16_C(0x1e7)) == UINT16_C(0x1e7) && output->interface_id_size != 0 &&
           output->operation != 0 && output->call_id != 0 && output->caller_app_id_size != 0 &&
           output->caller_component_id_size != 0;
}

static inline int pxa_system_parse_intent_event(
    const pxa_event_t* event, pxa_system_intent_event_t* output) {
    const uint8_t* cursor;
    size_t remaining;
    uint16_t previous = 0;
    uint8_t seen = 0;
    if (event == NULL || output == NULL ||
        event->service != PXA_SERVICE_SYSTEM ||
        event->opcode != PXA_SYSTEM_INTENT_EVENT || event->request_id != 0 ||
        event->payload == NULL) {
        return 0;
    }
    output->intent = NULL;
    output->intent_size = 0;
    output->caller_publisher_root = NULL;
    output->caller_app_id = NULL;
    output->caller_app_id_size = 0;
    output->caller_component_id = NULL;
    output->caller_component_id_size = 0;
    cursor = event->payload;
    remaining = event->payload_length;
    while (remaining != 0) {
        uint16_t raw_tag;
        uint16_t tag;
        uint16_t size;
        int optional;
        if (remaining < 4) return 0;
        raw_tag = pxa_read_u16(cursor);
        size = pxa_read_u16(cursor + 2);
        if (raw_tag == 0 || raw_tag < previous || size > remaining - 4)
            return 0;
        previous = raw_tag;
        tag = raw_tag & UINT16_C(0x7fff);
        optional = (raw_tag & PXA_SYSTEM_RECORD_OPTIONAL) != 0;
        cursor += 4;
        remaining -= 4;
        if (tag == 1 && !optional && !(seen & 1u) &&
            size >= PXA_SYSTEM_INTENT_WIRE_MIN_BYTES) {
            output->intent = cursor;
            output->intent_size = size;
            seen |= 1u;
        } else if (tag == 2 && optional && !(seen & 2u) && size == 32) {
            output->caller_publisher_root = cursor;
            seen |= 2u;
        } else if (tag == 3 && optional && !(seen & 4u) && size != 0) {
            output->caller_app_id = cursor;
            output->caller_app_id_size = size;
            seen |= 4u;
        } else if (tag == 4 && optional && !(seen & 8u) && size != 0) {
            output->caller_component_id = cursor;
            output->caller_component_id_size = size;
            seen |= 8u;
        } else if (!(optional && tag > 4)) {
            return 0;
        }
        cursor += size;
        remaining -= size;
    }
    return (seen & 1u) != 0 &&
           (((seen & 14u) == 0) || ((seen & 14u) == 14u));
}

static inline int pxa_system_parse_configuration_records(
    const uint8_t* payload, size_t payload_length,
    pxa_system_configuration_event_t* output) {
    const uint8_t* cursor;
    size_t remaining;
    uint16_t previous = 0;
    uint8_t seen = 0;
    if (output == NULL || payload == NULL || payload_length == 0) {
        return 0;
    }
    output->locale = NULL;
    output->locale_size = 0;
    output->text_direction = PXA_SYSTEM_TEXT_DIRECTION_LTR;
    cursor = payload;
    remaining = payload_length;
    while (remaining != 0) {
        uint16_t raw_tag;
        uint16_t tag;
        uint16_t size;
        int optional;
        if (remaining < 4) return 0;
        raw_tag = pxa_read_u16(cursor);
        size = pxa_read_u16(cursor + 2);
        if (raw_tag == 0 || raw_tag < previous || size > remaining - 4)
            return 0;
        previous = raw_tag;
        tag = raw_tag & UINT16_C(0x7fff);
        optional = (raw_tag & PXA_SYSTEM_RECORD_OPTIONAL) != 0;
        cursor += 4;
        remaining -= 4;
        if (tag == PXA_SYSTEM_CONFIGURATION_LOCALE && !optional &&
            !(seen & 1u) && size >= 2 && size <= PXA_SYSTEM_LOCALE_MAX_BYTES) {
            output->locale = cursor;
            output->locale_size = size;
            seen |= 1u;
        } else if (tag == PXA_SYSTEM_CONFIGURATION_TEXT_DIRECTION &&
                   optional && !(seen & 2u) && size == 1 &&
                   cursor[0] <= PXA_SYSTEM_TEXT_DIRECTION_RTL) {
            output->text_direction = cursor[0];
            seen |= 2u;
        } else if (!(optional && tag > PXA_SYSTEM_CONFIGURATION_TEXT_DIRECTION)) {
            return 0;
        }
        cursor += size;
        remaining -= size;
    }
    return (seen & 1u) != 0;
}

static inline int pxa_system_parse_configuration_event(
    const pxa_event_t* event, pxa_system_configuration_event_t* output) {
    return event != NULL && event->service == PXA_SERVICE_SYSTEM &&
           event->opcode == PXA_SYSTEM_CONFIGURATION_EVENT &&
           event->request_id == 0 &&
           pxa_system_parse_configuration_records(
               event->payload, event->payload_length, output);
}

static inline int pxa_system_parse_start_configuration(
    const uint8_t* config, size_t config_size,
    pxa_system_configuration_event_t* output) {
    size_t offset = 0;
    int found = 0;
    if (output == NULL || (config == NULL && config_size != 0)) return 0;
    while (offset < config_size) {
        uint16_t tag;
        uint16_t length;
        if (config_size - offset < 4) return 0;
        tag = pxa_read_u16(config + offset);
        length = pxa_read_u16(config + offset + 2);
        offset += 4;
        if (length > config_size - offset) return 0;
        if (tag == PXA_SYSTEM_CONFIG_ENVIRONMENT) {
            if (found || !pxa_system_parse_configuration_records(
                             config + offset, length, output)) {
                return 0;
            }
            found = 1;
        }
        offset += length;
    }
    return found;
}

#endif
