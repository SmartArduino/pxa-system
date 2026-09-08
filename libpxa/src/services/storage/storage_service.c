#include "pxa/storage.h"
#include "common/bytes_internal.h"
#include "common/checked_math.h"

#include <stdint.h>
#include <string.h>

#define PXA_STORAGE_MAGIC UINT32_C(0x50585354)
#define PXA_STORAGE_LIST_RESULT_BYTES \
    (PXA_STORAGE_MAX_LIST_ENTRIES * (PXA_RECORD_HEADER_SIZE + \
                                     PXA_STORAGE_MAX_KEY_BYTES))

struct pxa_storage_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_storage_backend_t backend;
    uint8_t *value_scratch;
    uint8_t *result_scratch;
    size_t max_value_bytes;
    size_t result_capacity;
    uint8_t registered;
};

typedef struct {
    pxa_bytes_t key;
    pxa_bytes_t value;
    uint8_t has_key;
    uint8_t has_value;
} pxa_storage_request_t;

typedef struct {
    pxa_writer_t *writer;
    pxa_bytes_t previous;
    size_t count;
} pxa_storage_list_encoder_t;

static int service_valid(const pxa_storage_service_t *service) {
    return service != NULL && service->magic == PXA_STORAGE_MAGIC;
}

static int ascii_letter(uint8_t value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static int ascii_digit(uint8_t value) {
    return value >= '0' && value <= '9';
}

int pxa_storage_key_is_valid(pxa_bytes_t key) {
    size_t index;
    if (key.data == NULL || key.size == 0 ||
        key.size > PXA_STORAGE_MAX_KEY_BYTES || !ascii_letter(key.data[0])) {
        return 0;
    }
    for (index = 1; index < key.size; ++index) {
        uint8_t value = key.data[index];
        if (!ascii_letter(value) && !ascii_digit(value) && value != '.' &&
            value != '_' && value != '-') return 0;
    }
    return 1;
}

static int config_valid(const pxa_storage_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_value_bytes != 0 &&
           config->max_value_bytes <= PXA_STORAGE_MAX_VALUE_BYTES &&
           config->max_value_bytes <=
               PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE - 8u &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.get != NULL && config->backend.set != NULL &&
           config->backend.remove != NULL && config->backend.list != NULL;
}

size_t pxa_storage_service_workspace_size(const pxa_storage_config_t *config) {
    size_t result_capacity;
    size_t size;
    if (!config_valid(config)) return 0;
    result_capacity = config->max_value_bytes + PXA_RECORD_HEADER_SIZE;
    if (result_capacity < PXA_STORAGE_LIST_RESULT_BYTES) {
        result_capacity = PXA_STORAGE_LIST_RESULT_BYTES;
    }
    size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (sizeof(pxa_storage_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_storage_service_t);
    if (config->max_value_bytes > SIZE_MAX - size) return 0;
    size += config->max_value_bytes;
    if (result_capacity > SIZE_MAX - size) return 0;
    return size + result_capacity;
}

pxa_status_t pxa_storage_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_storage_config_t *config, pxa_storage_service_t **output) {
    uintptr_t cursor;
    uintptr_t end;
    pxa_storage_service_t *service;
    size_t required;
    size_t result_capacity;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_storage_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_storage_service_t *)cursor;
    cursor += sizeof(*service);
    result_capacity = config->max_value_bytes + PXA_RECORD_HEADER_SIZE;
    if (result_capacity < PXA_STORAGE_LIST_RESULT_BYTES) {
        result_capacity = PXA_STORAGE_LIST_RESULT_BYTES;
    }
    if (cursor > end || config->max_value_bytes > end - cursor ||
        result_capacity > end - cursor - config->max_value_bytes) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(service, 0, sizeof(*service));
    service->value_scratch = (uint8_t *)cursor;
    cursor += config->max_value_bytes;
    service->result_scratch = (uint8_t *)cursor;
    service->runtime = runtime;
    service->backend = config->backend;
    service->max_value_bytes = config->max_value_bytes;
    service->result_capacity = result_capacity;
    service->magic = PXA_STORAGE_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static pxa_status_t parse_request(pxa_bytes_t payload, int key_required,
                                  int value_required, size_t max_value_bytes,
                                  pxa_storage_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 1) {
            if (record.optional || output->has_key ||
                !pxa_storage_key_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->key = record.payload;
            output->has_key = 1;
        } else if (record.tag == 2) {
            if (record.optional || output->has_value ||
                record.payload.size > max_value_bytes) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->value = record.payload;
            output->has_value = 1;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    if ((key_required && !output->has_key) ||
        output->has_value != (uint8_t)value_required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t emit_list_key(void *context, pxa_bytes_t key) {
    pxa_storage_list_encoder_t *encoder =
        (pxa_storage_list_encoder_t *)context;
    if (!pxa_storage_key_is_valid(key)) return PXA_STATUS_INVALID_ARGUMENT;
    if (encoder->previous.size != 0 &&
        pxa_bytes_compare_internal(encoder->previous, key) >= 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (encoder->count >= PXA_STORAGE_MAX_LIST_ENTRIES) {
        return PXA_STATUS_WOULD_BLOCK;
    }
    if (pxa_writer_record(encoder->writer, 1, key.data, key.size) !=
        PXA_STATUS_OK) {
        return encoder->writer->status;
    }
    encoder->previous = key;
    encoder->count++;
    return PXA_STATUS_OK;
}

static pxa_status_t storage_control(void *context, pxa_runtime_t *runtime,
                                    pxa_component_t component,
                                    const pxa_message_view_t *message) {
    pxa_storage_service_t *service = (pxa_storage_service_t *)context;
    pxa_storage_request_t request;
    pxa_writer_t result_writer;
    pxa_status_t result;
    pxa_status_t complete;
    size_t value_size = 0;
    (void)runtime;
    if (message->request_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode < PXA_STORAGE_GET ||
        message->opcode > PXA_STORAGE_LIST) {
        return PXA_STATUS_UNSUPPORTED;
    }
    result = pxa_request_begin(service->runtime, component,
                               message->request_id,
                               PXA_STORAGE_SERVICE_ID,
                               message->opcode, 0);
    if (result != PXA_STATUS_OK) return result;
    result = parse_request(message->payload,
                           message->opcode != PXA_STORAGE_LIST,
                           message->opcode == PXA_STORAGE_SET,
                           service->max_value_bytes, &request);
    pxa_writer_init(&result_writer, service->result_scratch,
                    service->result_capacity);
    if (result == PXA_STATUS_OK && message->opcode == PXA_STORAGE_GET) {
        result = service->backend.get(
            service->backend.context, request.key, service->value_scratch,
            service->max_value_bytes, &value_size);
        if (result == PXA_STATUS_OK && value_size > service->max_value_bytes) {
            result = PXA_STATUS_INTERNAL;
        }
        if (result == PXA_STATUS_OK) {
            result = pxa_writer_record(&result_writer, 2,
                                       service->value_scratch, value_size);
        }
    } else if (result == PXA_STATUS_OK &&
               message->opcode == PXA_STORAGE_SET) {
        result = service->backend.set(service->backend.context,
                                      request.key, request.value);
    } else if (result == PXA_STATUS_OK &&
               message->opcode == PXA_STORAGE_REMOVE) {
        result = service->backend.remove(service->backend.context, request.key);
    } else if (result == PXA_STATUS_OK) {
        pxa_storage_list_encoder_t encoder;
        memset(&encoder, 0, sizeof(encoder));
        encoder.writer = &result_writer;
        if (request.has_key) encoder.previous = request.key;
        result = service->backend.list(
            service->backend.context,
            request.has_key ? request.key : (pxa_bytes_t){NULL, 0},
            emit_list_key, &encoder);
        if (result == PXA_STATUS_WOULD_BLOCK &&
            encoder.count == PXA_STORAGE_MAX_LIST_ENTRIES) {
            result = PXA_STATUS_OK;
        }
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, result,
        result == PXA_STATUS_OK ? service->result_scratch : NULL,
        result == PXA_STATUS_OK ? result_writer.size : 0);
    if (complete != PXA_STATUS_OK) {
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_storage_service_register(pxa_storage_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_STORAGE_SERVICE_ID;
    operations.major = PXA_STORAGE_SERVICE_MAJOR;
    operations.minor = PXA_STORAGE_SERVICE_MINOR;
    operations.context = service;
    operations.control = storage_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
