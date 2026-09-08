#include "pxa/sensor.h"
#include "common/bytes_internal.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_SENSOR_MAGIC UINT32_C(0x5058534e)
#define PXA_SENSOR_SLOT_MAGIC UINT32_C(0x50585352)
#define PXA_SENSOR_SLOT_NONE UINT16_MAX

typedef struct pxa_sensor_subscription pxa_sensor_subscription_t;

struct pxa_sensor_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_service_t *permissions;
    pxa_sensor_descriptor_t *descriptors;
    pxa_sensor_subscription_t *subscriptions;
    uint8_t *list_scratch;
    void *provider_context;
    pxa_sensor_subscribe_fn subscribe;
    pxa_sensor_read_fn read;
    pxa_sensor_unsubscribe_fn unsubscribe;
    size_t list_capacity;
    uint16_t descriptor_count;
    uint16_t active_head;
    uint16_t max_subscriptions;
    uint16_t max_subscriptions_per_component;
    uint16_t free_head;
    uint8_t registered;
};

struct pxa_sensor_subscription {
    uint32_t magic;
    pxa_sensor_service_t *service;
    const pxa_sensor_descriptor_t *descriptor;
    void *provider_subscription;
    pxa_component_t component;
    pxa_handle_t handle;
    uint64_t next_due_us;
    uint32_t period_ms;
    uint16_t next;
    uint8_t active;
};

typedef struct {
    uint16_t sensor_id;
    uint32_t period_ms;
    pxa_handle_t permission_handle;
    uint8_t seen;
} pxa_sensor_subscribe_request_t;

static int semantic_valid(pxa_bytes_t semantic) {
    size_t index;
    if (semantic.data == NULL || semantic.size == 0 ||
        semantic.size > PXA_SENSOR_MAX_SEMANTIC_BYTES ||
        semantic.data[0] < 'a' || semantic.data[0] > 'z') {
        return 0;
    }
    for (index = 1; index < semantic.size; ++index) {
        uint8_t value = semantic.data[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return 0;
        }
    }
    return 1;
}

static int descriptor_valid(const pxa_sensor_descriptor_t *descriptor) {
    return descriptor->id != 0 && semantic_valid(descriptor->semantic) &&
           descriptor->unit != 0 && descriptor->dimensions != 0 &&
           descriptor->dimensions <= PXA_SENSOR_MAX_DIMENSIONS &&
           descriptor->min_period_ms != 0 &&
           descriptor->min_period_ms <= descriptor->max_period_ms;
}

static int config_valid(const pxa_sensor_config_t *config,
                        size_t *semantic_bytes, size_t *list_bytes) {
    size_t semantic_total = 0;
    size_t list_total = 0;
    uint16_t index;
    uint16_t other;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        (config->descriptor_count != 0 && config->descriptors == NULL) ||
        config->descriptor_count > PXA_SENSOR_MAX_DESCRIPTORS ||
        config->max_subscriptions == 0 ||
        config->max_subscriptions_per_component == 0 ||
        config->max_subscriptions_per_component > config->max_subscriptions ||
        config->subscribe == NULL || config->read == NULL ||
        config->unsubscribe == NULL || config->permissions == NULL) {
        return 0;
    }
    for (index = 0; index < config->descriptor_count; ++index) {
        const pxa_sensor_descriptor_t *descriptor = &config->descriptors[index];
        if (!descriptor_valid(descriptor) ||
            (index != 0 && config->descriptors[index - 1].id >= descriptor->id)) {
            return 0;
        }
        for (other = 0; other < index; ++other) {
            if (pxa_bytes_equal_internal(
                    config->descriptors[other].semantic,
                    descriptor->semantic)) {
                return 0;
            }
        }
        if (descriptor->semantic.size > SIZE_MAX - semantic_total) return 0;
        semantic_total += descriptor->semantic.size;
        if (descriptor->semantic.size > SIZE_MAX - 41u - list_total) return 0;
        list_total += 41u + descriptor->semantic.size;
    }
    if (list_total >
        PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE - sizeof(uint32_t)) {
        return 0;
    }
    *semantic_bytes = semantic_total;
    *list_bytes = list_total;
    return 1;
}

size_t pxa_sensor_service_workspace_size(const pxa_sensor_config_t *config) {
    size_t semantic_bytes;
    size_t list_bytes;
    size_t descriptors_size;
    size_t subscriptions_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config, &semantic_bytes, &list_bytes)) return 0;
    if ((config->descriptor_count != 0 &&
         sizeof(pxa_sensor_descriptor_t) >
             SIZE_MAX / (size_t)config->descriptor_count) ||
        sizeof(pxa_sensor_subscription_t) >
            SIZE_MAX / (size_t)config->max_subscriptions) {
        return 0;
    }
    descriptors_size = (size_t)config->descriptor_count *
                       sizeof(pxa_sensor_descriptor_t);
    subscriptions_size = (size_t)config->max_subscriptions *
                         sizeof(pxa_sensor_subscription_t);
    if (sizeof(pxa_sensor_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_sensor_service_t);
    if (descriptors_size > SIZE_MAX - size) return 0;
    size += descriptors_size;
    if (subscriptions_size > SIZE_MAX - size) return 0;
    size += subscriptions_size;
    if (semantic_bytes > SIZE_MAX - size) return 0;
    size += semantic_bytes;
    if (list_bytes > SIZE_MAX - size) return 0;
    return size + list_bytes;
}

pxa_status_t pxa_sensor_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_sensor_config_t *config, pxa_sensor_service_t **output) {
    size_t semantic_bytes;
    size_t list_bytes;
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint8_t *semantic_cursor;
    uint16_t index;
    pxa_sensor_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_sensor_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        !config_valid(config, &semantic_bytes, &list_bytes) ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_sensor_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->descriptors = (pxa_sensor_descriptor_t *)cursor;
    cursor += (size_t)config->descriptor_count * sizeof(service->descriptors[0]);
    service->subscriptions = (pxa_sensor_subscription_t *)cursor;
    cursor += (size_t)config->max_subscriptions *
              sizeof(service->subscriptions[0]);
    semantic_cursor = (uint8_t *)cursor;
    cursor += semantic_bytes;
    service->list_scratch = (uint8_t *)cursor;
    cursor += list_bytes;
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->permissions = config->permissions;
    service->provider_context = config->provider_context;
    service->subscribe = config->subscribe;
    service->read = config->read;
    service->unsubscribe = config->unsubscribe;
    service->descriptor_count = config->descriptor_count;
    service->max_subscriptions = config->max_subscriptions;
    service->max_subscriptions_per_component =
        config->max_subscriptions_per_component;
    service->free_head = 0;
    service->active_head = PXA_SENSOR_SLOT_NONE;
    service->list_capacity = list_bytes;
    for (index = 0; index < service->descriptor_count; ++index) {
        service->descriptors[index] = config->descriptors[index];
        memcpy(semantic_cursor, config->descriptors[index].semantic.data,
               config->descriptors[index].semantic.size);
        service->descriptors[index].semantic.data = semantic_cursor;
        semantic_cursor += config->descriptors[index].semantic.size;
    }
    memset(service->subscriptions, 0,
           (size_t)service->max_subscriptions *
               sizeof(service->subscriptions[0]));
    for (index = 0; index < service->max_subscriptions; ++index) {
        service->subscriptions[index].magic = PXA_SENSOR_SLOT_MAGIC;
        service->subscriptions[index].service = service;
        service->subscriptions[index].next =
            index + 1u < service->max_subscriptions
                ? (uint16_t)(index + 1u)
                : PXA_SENSOR_SLOT_NONE;
    }
    service->magic = PXA_SENSOR_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_sensor_service_t *service) {
    return service != NULL && service->magic == PXA_SENSOR_MAGIC;
}

static const pxa_sensor_descriptor_t *find_descriptor(
    const pxa_sensor_service_t *service, uint16_t id) {
    uint16_t begin = 0;
    uint16_t end = service->descriptor_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        if (service->descriptors[middle].id < id) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin < service->descriptor_count &&
                   service->descriptors[begin].id == id
               ? &service->descriptors[begin]
               : NULL;
}

static pxa_status_t encode_descriptor(
    pxa_writer_t *output, const pxa_sensor_descriptor_t *descriptor) {
    uint8_t nested_bytes[101];
    uint8_t value[4];
    pxa_writer_t nested;
    pxa_status_t status;
    pxa_writer_init(&nested, nested_bytes, sizeof(nested_bytes));
    pxa_write_u16(value, descriptor->id);
    status = pxa_writer_record(&nested, 1, value, 2);
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&nested, 2, descriptor->semantic.data,
                                   descriptor->semantic.size);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u16(value, descriptor->unit);
        status = pxa_writer_record(&nested, 3, value, 2);
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&nested, 4, &descriptor->dimensions, 1);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, descriptor->min_period_ms);
        status = pxa_writer_record(&nested, 5, value, 4);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, descriptor->max_period_ms);
        status = pxa_writer_record(&nested, 6, value, 4);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    return pxa_writer_record(output, 1, nested.data, nested.size);
}

static pxa_status_t encode_list(pxa_sensor_service_t *service,
                                size_t *result_size) {
    pxa_writer_t writer;
    uint16_t index;
    pxa_writer_init(&writer, service->list_scratch, service->list_capacity);
    for (index = 0; index < service->descriptor_count; ++index) {
        pxa_status_t status = encode_descriptor(&writer,
                                                &service->descriptors[index]);
        if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    }
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t parse_subscribe(
    pxa_bytes_t payload, pxa_sensor_subscribe_request_t *output) {
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
        if (record.optional) continue;
        if (record.tag == 1 && (output->seen & 1u) == 0 &&
            record.payload.size == 2) {
            output->sensor_id = pxa_read_u16(record.payload.data);
            output->seen |= 1u;
        } else if (record.tag == 2 && (output->seen & 2u) == 0 &&
                   record.payload.size == 4) {
            output->period_ms = pxa_read_u32(record.payload.data);
            output->seen |= 2u;
        } else if (record.tag == 3 && (output->seen & 4u) == 0 &&
                   record.payload.size == 4) {
            output->permission_handle = pxa_read_u32(record.payload.data);
            output->seen |= 4u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return output->seen == 7u && output->sensor_id != 0 &&
                   output->period_ms != 0 &&
                   output->permission_handle != PXA_HANDLE_INVALID
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_sensor_subscription_t *allocate_subscription(
    pxa_sensor_service_t *service) {
    uint16_t index = service->free_head;
    pxa_sensor_subscription_t *subscription;
    if (index == PXA_SENSOR_SLOT_NONE) return NULL;
    subscription = &service->subscriptions[index];
    service->free_head = subscription->next;
    subscription->next = service->active_head;
    service->active_head = index;
    subscription->active = 1;
    return subscription;
}

static void release_subscription(pxa_sensor_subscription_t *subscription) {
    pxa_sensor_service_t *service = subscription->service;
    uint16_t index = (uint16_t)(subscription - service->subscriptions);
    uint16_t current = service->active_head;
    uint16_t previous = PXA_SENSOR_SLOT_NONE;
    while (current != PXA_SENSOR_SLOT_NONE && current != index) {
        previous = current;
        current = service->subscriptions[current].next;
    }
    if (current == PXA_SENSOR_SLOT_NONE) return;
    if (previous == PXA_SENSOR_SLOT_NONE) {
        service->active_head = subscription->next;
    } else {
        service->subscriptions[previous].next = subscription->next;
    }
    subscription->descriptor = NULL;
    subscription->provider_subscription = NULL;
    subscription->component = PXA_COMPONENT_INVALID;
    subscription->handle = PXA_HANDLE_INVALID;
    subscription->next_due_us = 0;
    subscription->period_ms = 0;
    subscription->active = 0;
    subscription->next = service->free_head;
    service->free_head = index;
}

static void close_subscription(void *context) {
    pxa_sensor_subscription_t *subscription =
        (pxa_sensor_subscription_t *)context;
    if (subscription == NULL || subscription->magic != PXA_SENSOR_SLOT_MAGIC ||
        !subscription->active) {
        return;
    }
    subscription->service->unsubscribe(
        subscription->service->provider_context,
        subscription->provider_subscription, subscription->descriptor->id);
    release_subscription(subscription);
}

static int32_t unsupported_io(void *context, uint32_t operation, uint8_t *data,
                              size_t size) {
    (void)context;
    (void)operation;
    (void)data;
    (void)size;
    return PXA_STATUS_UNSUPPORTED;
}

static const pxa_resource_ops_t k_sensor_resource_ops = {
    sizeof(pxa_resource_ops_t), unsupported_io,
};

static uint16_t component_subscription_count(
    const pxa_sensor_service_t *service, pxa_component_t component) {
    uint16_t count = 0;
    uint16_t index = service->active_head;
    while (index != PXA_SENSOR_SLOT_NONE) {
        const pxa_sensor_subscription_t *subscription =
            &service->subscriptions[index];
        if (subscription->component == component) {
            count++;
        }
        index = subscription->next;
    }
    return count;
}

static pxa_status_t subscribe_component(
    pxa_sensor_service_t *service, pxa_component_t component,
    const pxa_message_view_t *message, uint8_t result[4],
    pxa_handle_t *opened_handle) {
    static const uint8_t permission_name[] = "sensor.read";
    pxa_sensor_subscribe_request_t request;
    const pxa_sensor_descriptor_t *descriptor;
    pxa_sensor_subscription_t *subscription;
    pxa_authority_t authority = 0;
    pxa_resource_t resource;
    pxa_status_t status = parse_subscribe(message->payload, &request);
    if (status != PXA_STATUS_OK) return status;
    descriptor = find_descriptor(service, request.sensor_id);
    if (descriptor == NULL) return PXA_STATUS_NOT_FOUND;
    if (request.period_ms < descriptor->min_period_ms ||
        request.period_ms > descriptor->max_period_ms) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = pxa_permission_resolve(
        service->permissions, component, request.permission_handle,
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
        descriptor->semantic, &authority);
    if (status != PXA_STATUS_OK) return status;
    if (component_subscription_count(service, component) >=
        service->max_subscriptions_per_component) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    subscription = allocate_subscription(service);
    if (subscription == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    subscription->descriptor = descriptor;
    subscription->component = component;
    subscription->period_ms = request.period_ms;
    status = pxa_status_normalize(service->subscribe(
        service->provider_context, descriptor->id, request.period_ms,
        &subscription->provider_subscription));
    if (status != PXA_STATUS_OK) {
        release_subscription(subscription);
        return status;
    }
    if (subscription->provider_subscription == NULL) {
        release_subscription(subscription);
        return PXA_STATUS_INTERNAL;
    }
    resource.context = subscription;
    resource.operations = &k_sensor_resource_ops;
    resource.close = close_subscription;
    status = pxa_handle_open(service->runtime, component, PXA_RESOURCE_SENSOR,
                             authority, &resource, opened_handle);
    if (status != PXA_STATUS_OK) {
        close_subscription(subscription);
        return status;
    }
    subscription->handle = *opened_handle;
    pxa_write_u32(result, *opened_handle);
    return PXA_STATUS_OK;
}

static pxa_status_t sensor_control(void *context, pxa_runtime_t *runtime,
                                   pxa_component_t component,
                                   const pxa_message_view_t *message) {
    pxa_sensor_service_t *service = (pxa_sensor_service_t *)context;
    uint8_t handle_result[4];
    const void *result = NULL;
    size_t result_size = 0;
    pxa_handle_t opened_handle = PXA_HANDLE_INVALID;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_SENSOR_LIST &&
        message->opcode != PXA_SENSOR_SUBSCRIBE) {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = pxa_request_begin(service->runtime, component,
                               message->request_id, PXA_SENSOR_SERVICE_ID,
                               message->opcode, 0);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_SENSOR_LIST) {
        status = message->payload.size == 0
                     ? encode_list(service, &result_size)
                     : PXA_STATUS_INVALID_ARGUMENT;
        result = service->list_scratch;
    } else {
        status = subscribe_component(service, component, message,
                                     handle_result, &opened_handle);
        result = handle_result;
        result_size = 4;
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE_INVALID) {
            (void)pxa_handle_close(service->runtime, component, opened_handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_sensor_service_register(pxa_sensor_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_SENSOR_SERVICE_ID;
    operations.major = PXA_SENSOR_SERVICE_MAJOR;
    operations.minor = PXA_SENSOR_SERVICE_MINOR;
    operations.context = service;
    operations.control = sensor_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

int pxa_sensor_has_active_subscriptions(const pxa_sensor_service_t *service) {
    return service_valid(service) &&
           service->active_head != PXA_SENSOR_SLOT_NONE;
}

static void append_affected(pxa_component_t component,
                            pxa_component_t *affected, size_t capacity,
                            size_t *count) {
    size_t index;
    for (index = 0; index < *count && index < capacity; ++index) {
        if (affected[index] == component) return;
    }
    if (*count < capacity && affected != NULL) affected[*count] = component;
    (*count)++;
}

pxa_status_t pxa_sensor_poll(
    pxa_sensor_service_t *service, uint64_t timestamp_us,
    pxa_component_t *affected, size_t capacity, size_t *count) {
    uint16_t index;
    if (count == NULL || !service_valid(service) ||
        (affected == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    index = service->active_head;
    while (index != PXA_SENSOR_SLOT_NONE) {
        pxa_sensor_subscription_t *subscription =
            &service->subscriptions[index];
        uint16_t next = subscription->next;
        uint64_t period_us;
        int32_t values[PXA_SENSOR_MAX_DIMENSIONS] = {0, 0, 0};
        uint8_t payload_bytes[42];
        uint8_t encoded[12];
        pxa_writer_t payload;
        pxa_status_t status;
        uint8_t dimension;
        if (!subscription->active ||
            (subscription->next_due_us != 0 &&
             timestamp_us < subscription->next_due_us)) {
            index = next;
            continue;
        }
        period_us = (uint64_t)subscription->period_ms * UINT64_C(1000);
        subscription->next_due_us =
            timestamp_us > UINT64_MAX - period_us
                ? UINT64_MAX
                : timestamp_us + period_us;
        status = pxa_status_normalize(service->read(
            service->provider_context, subscription->provider_subscription,
            subscription->descriptor->id, values));
        if (status != PXA_STATUS_OK) {
            index = next;
            continue;
        }
        pxa_writer_init(&payload, payload_bytes, sizeof(payload_bytes));
        pxa_write_u32(encoded, subscription->handle);
        status = pxa_writer_record(&payload, 4, encoded, 4);
        if (status == PXA_STATUS_OK) {
            pxa_write_u64(encoded, timestamp_us);
            status = pxa_writer_record(&payload, 2, encoded, 8);
        }
        if (status == PXA_STATUS_OK) {
            pxa_write_u16(encoded, 1);
            status = pxa_writer_record(&payload, 3, encoded, 2);
        }
        for (dimension = 0;
             status == PXA_STATUS_OK &&
             dimension < subscription->descriptor->dimensions;
             ++dimension) {
            uint32_t bits;
            memcpy(&bits, &values[dimension], sizeof(bits));
            pxa_write_u32(encoded + (size_t)dimension * 4u, bits);
        }
        if (status == PXA_STATUS_OK) {
            status = pxa_writer_record(
                &payload, 4, encoded,
                (size_t)subscription->descriptor->dimensions * 4u);
        }
        if (status == PXA_STATUS_OK) {
            uint64_t coalesce_key =
                ((uint64_t)PXA_SENSOR_SERVICE_ID << 48) |
                ((uint64_t)PXA_SENSOR_SAMPLE << 32) | subscription->handle;
            status = pxa_event_post_message(
                service->runtime, subscription->component,
                PXA_SENSOR_SERVICE_ID, PXA_SENSOR_SAMPLE, 0,
                (pxa_bytes_t){payload.data, payload.size}, 0, coalesce_key);
        }
        if (status == PXA_STATUS_OK) {
            append_affected(subscription->component, affected, capacity, count);
        }
        index = next;
    }
    return *count > capacity ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_OK;
}
