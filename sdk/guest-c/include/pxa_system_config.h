#ifndef PXA_GUEST_SYSTEM_CONFIG_H
#define PXA_GUEST_SYSTEM_CONFIG_H

#include "pxa_writer.h"

#define PXA_SYSTEM_CONFIGURATION_EVENT UINT16_C(0x8004)
#define PXA_SYSTEM_CONFIGURATION_LOCALE 1u
#define PXA_SYSTEM_CONFIGURATION_TEXT_DIRECTION 2u
#define PXA_SYSTEM_CONFIG_ENVIRONMENT 12u
#define PXA_SYSTEM_TEXT_DIRECTION_LTR 0u
#define PXA_SYSTEM_TEXT_DIRECTION_RTL 1u
#define PXA_SYSTEM_LOCALE_MAX_BYTES 63u
#define PXA_SYSTEM_RECORD_OPTIONAL UINT16_C(0x8000)

typedef struct {
    const uint8_t *locale;
    uint16_t locale_size;
    uint8_t text_direction;
} pxa_system_configuration_event_t;

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
