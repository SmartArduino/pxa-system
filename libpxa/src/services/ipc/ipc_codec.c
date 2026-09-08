#include "services/ipc/ipc_internal.h"

#include <string.h>

static int ascii_letter(uint8_t value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static int ascii_digit(uint8_t value) {
    return value >= '0' && value <= '9';
}

int pxa_ipc_endpoint_is_valid(pxa_bytes_t endpoint) {
    size_t index;
    if (endpoint.data == NULL || endpoint.size == 0 ||
        endpoint.size > PXA_IPC_MAX_ENDPOINT_BYTES ||
        !ascii_letter(endpoint.data[0])) {
        return 0;
    }
    for (index = 1; index < endpoint.size; ++index) {
        uint8_t value = endpoint.data[index];
        if (!ascii_letter(value) && !ascii_digit(value) && value != '.' &&
            value != '_' && value != '-') {
            return 0;
        }
    }
    return 1;
}

pxa_status_t pxa_ipc_parse_call(pxa_bytes_t payload,
                                pxa_ipc_call_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t has_endpoint = 0;
    uint8_t has_payload = 0;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 1) {
            if (record.optional || has_endpoint ||
                !pxa_ipc_endpoint_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->endpoint = record.payload;
            has_endpoint = 1;
        } else if (record.tag == 2) {
            if (record.optional || has_payload ||
                record.payload.size > PXA_IPC_MAX_PAYLOAD_BYTES) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->payload = record.payload;
            has_payload = 1;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    return has_endpoint ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

pxa_status_t pxa_ipc_parse_reply(pxa_bytes_t payload,
                                 pxa_ipc_reply_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t has_call = 0;
    uint8_t has_status = 0;
    uint8_t has_payload = 0;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 1) {
            if (record.optional || has_call || record.payload.size != 4) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->call_id = pxa_read_u32(record.payload.data);
            if (output->call_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
            has_call = 1;
        } else if (record.tag == 2) {
            if (record.optional || has_status || record.payload.size != 4) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->status = (int32_t)pxa_read_u32(record.payload.data);
            if (!pxa_status_is_known(output->status)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            has_status = 1;
        } else if (record.tag == 3) {
            if (record.optional || has_payload ||
                record.payload.size > PXA_IPC_MAX_PAYLOAD_BYTES) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->payload = record.payload;
            has_payload = 1;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    return has_call && has_status ? PXA_STATUS_OK
                                  : PXA_STATUS_INVALID_ARGUMENT;
}
