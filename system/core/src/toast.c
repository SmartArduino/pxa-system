#include "pxsys/toast.h"

#include <string.h>

#define PXSYS_TOAST_MAGIC UINT32_C(0x50585453)

typedef struct {
    void* context;
    pxsys_toast_posted_fn callback;
} toast_observer_t;

struct pxsys_toast_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    size_t max_message_bytes;
    uint8_t notifying;
    uint64_t next_sequence;
    pxsys_allocator_t allocator;
    toast_observer_t* observers;
};

static int service_valid(const pxsys_toast_service_t* service) {
    return service != NULL && service->magic == PXSYS_TOAST_MAGIC;
}

static int toast_valid(const pxsys_toast_message_t* toast,
                       size_t max_message_bytes) {
    return toast != NULL && toast->struct_size >= sizeof(*toast) &&
           toast->message.size != 0 &&
           pxsys_display_text_validate(toast->message, max_message_bytes) &&
           toast->duration_ms <= 60000 && toast->tone <= PXSYS_TOAST_ERROR;
}

void pxsys_toast_message_init(pxsys_toast_message_t* toast,
                              pxsys_string_t message) {
    if (toast == NULL) return;
    memset(toast, 0, sizeof(*toast));
    toast->struct_size = sizeof(*toast);
    toast->message = message;
    toast->duration_ms = 2500;
    toast->tone = PXSYS_TOAST_DEFAULT;
}

void pxsys_toast_service_config_init(pxsys_toast_service_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 8;
    config->max_message_bytes = 512;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_toast_service_create(
    const pxsys_toast_service_config_t* config,
    pxsys_toast_service_t** output) {
    pxsys_toast_service_t* service;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_observers == 0 || config->max_message_bytes == 0 ||
        config->max_observers > SIZE_MAX / sizeof(toast_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    service = (pxsys_toast_service_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*service));
    if (service == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (toast_observer_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0,
           config->max_observers * sizeof(*service->observers));
    service->observer_capacity = config->max_observers;
    service->max_message_bytes = config->max_message_bytes;
    service->next_sequence = 1;
    service->allocator = config->allocator;
    service->magic = PXSYS_TOAST_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_toast_service_destroy(pxsys_toast_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_toast_service_post(
    pxsys_toast_service_t* service, const pxsys_toast_message_t* toast) {
    pxsys_toast_message_t posted;
    size_t index;
    if (!service_valid(service) ||
        !toast_valid(toast, service != NULL ? service->max_message_bytes : 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    posted = *toast;
    posted.struct_size = sizeof(posted);
    posted.sequence = service->next_sequence++;
    if (service->next_sequence == 0) service->next_sequence = 1;
    if (posted.duration_ms == 0) posted.duration_ms = 2500;
    service->notifying = 1;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback != NULL) {
            service->observers[index].callback(
                service->observers[index].context, &posted);
        }
    }
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_toast_service_subscribe(
    pxsys_toast_service_t* service, void* context,
    pxsys_toast_posted_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context)
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL) break;
    }
    service->observers[index].context = context;
    service->observers[index].callback = callback;
    service->observer_count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_toast_service_unsubscribe(
    pxsys_toast_service_t* service, void* context,
    pxsys_toast_posted_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            memset(&service->observers[index], 0, sizeof(service->observers[index]));
            service->observer_count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}

size_t pxsys_toast_wire_size(const pxsys_toast_message_t* toast) {
    return toast_valid(toast, SIZE_MAX - PXSYS_TOAST_WIRE_HEADER_BYTES)
               ? PXSYS_TOAST_WIRE_HEADER_BYTES + toast->message.size : 0;
}

pxsys_status_t pxsys_toast_wire_encode(const pxsys_toast_message_t* toast,
                                       uint8_t* output, size_t output_size,
                                       size_t* written) {
    size_t required = pxsys_toast_wire_size(toast);
    if (written == NULL || required == 0 || output == NULL ||
        output_size < required)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    output[0] = (uint8_t)toast->tone;
    output[1] = output[2] = output[3] = 0;
    output[4] = (uint8_t)toast->duration_ms;
    output[5] = (uint8_t)(toast->duration_ms >> 8);
    output[6] = (uint8_t)(toast->duration_ms >> 16);
    output[7] = (uint8_t)(toast->duration_ms >> 24);
    memcpy(output + PXSYS_TOAST_WIRE_HEADER_BYTES, toast->message.data,
           toast->message.size);
    *written = required;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_toast_wire_decode(pxsys_bytes_t payload,
                                       pxsys_toast_message_t* toast) {
    uint32_t duration;
    if (toast == NULL || payload.data == NULL ||
        payload.size <= PXSYS_TOAST_WIRE_HEADER_BYTES || payload.data[1] != 0 ||
        payload.data[2] != 0 || payload.data[3] != 0)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    duration = (uint32_t)payload.data[4] |
               ((uint32_t)payload.data[5] << 8) |
               ((uint32_t)payload.data[6] << 16) |
               ((uint32_t)payload.data[7] << 24);
    pxsys_toast_message_init(
        toast, pxsys_string((const char*)payload.data + PXSYS_TOAST_WIRE_HEADER_BYTES,
                            payload.size - PXSYS_TOAST_WIRE_HEADER_BYTES));
    toast->duration_ms = duration;
    toast->tone = (pxsys_toast_tone_t)payload.data[0];
    return toast_valid(toast, SIZE_MAX) ? PXSYS_STATUS_OK
                                        : PXSYS_STATUS_INVALID_ARGUMENT;
}
