#include "pxa/device.h"

#include "common/checked_math.h"

#include <string.h>

#define PXA_DEVICE_MAGIC UINT32_C(0x50584456)

struct pxa_device_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_service_t *permissions;
    void *provider_context;
    pxa_device_get_mac_fn get_mac;
    const char *target;
    const char *architecture;
    const char *engine;
    const char *engine_abi;
    uint32_t formats;
    uint8_t registered;
};

typedef struct {
    uint16_t kind;
    pxa_handle_t permission_handle;
    uint8_t seen;
} pxa_device_get_mac_request_t;

static int kind_valid(uint16_t kind) {
    return kind >= PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE &&
           kind <= PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT;
}

static pxa_bytes_t scope_for_kind(uint16_t kind) {
    static const uint8_t wifi_station_hardware[] =
        "mac.wifi.station.hardware";
    static const uint8_t wifi_softap_hardware[] = "mac.wifi.softap.hardware";
    static const uint8_t bluetooth_hardware[] = "mac.bluetooth.hardware";
    static const uint8_t ethernet_hardware[] = "mac.ethernet.hardware";
    static const uint8_t wifi_station_current[] = "mac.wifi.station.current";
    switch (kind) {
        case PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE:
            return (pxa_bytes_t){wifi_station_hardware,
                                 sizeof(wifi_station_hardware) - 1u};
        case PXA_DEVICE_MAC_KIND_WIFI_SOFTAP_HARDWARE:
            return (pxa_bytes_t){wifi_softap_hardware,
                                 sizeof(wifi_softap_hardware) - 1u};
        case PXA_DEVICE_MAC_KIND_BLUETOOTH_HARDWARE:
            return (pxa_bytes_t){bluetooth_hardware,
                                 sizeof(bluetooth_hardware) - 1u};
        case PXA_DEVICE_MAC_KIND_ETHERNET_HARDWARE:
            return (pxa_bytes_t){ethernet_hardware,
                                 sizeof(ethernet_hardware) - 1u};
        case PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT:
            return (pxa_bytes_t){wifi_station_current,
                                 sizeof(wifi_station_current) - 1u};
        default:
            return (pxa_bytes_t){NULL, 0};
    }
}

static int config_valid(const pxa_device_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->get_mac != NULL && config->permissions != NULL;
}

size_t pxa_device_service_workspace_size(const pxa_device_config_t *config) {
    return config_valid(config) ? sizeof(pxa_device_service_t) +
                                    PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u
                              : 0;
}

pxa_status_t pxa_device_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_device_config_t *config, pxa_device_service_t **output) {
    size_t required;
    pxa_device_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_device_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    service = (pxa_device_service_t *)pxa_internal_align_pointer(
        (uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(service, 0, sizeof(*service));
    service->runtime = runtime;
    service->permissions = config->permissions;
    service->provider_context = config->provider_context;
    service->get_mac = config->get_mac;
    service->target = config->target;
    service->architecture = config->architecture;
    service->engine = config->engine;
    service->engine_abi = config->engine_abi;
    service->formats = config->formats;
    service->magic = PXA_DEVICE_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_device_service_t *service) {
    return service != NULL && service->magic == PXA_DEVICE_MAGIC;
}

static pxa_status_t parse_get_mac(pxa_bytes_t payload,
                                  pxa_device_get_mac_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK || record.raw_tag < previous) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == PXA_DEVICE_TAG_MAC_KIND &&
            (output->seen & 1u) == 0 && record.payload.size == 2) {
            output->kind = pxa_read_u16(record.payload.data);
            output->seen |= 1u;
        } else if (record.tag == PXA_DEVICE_TAG_PERMISSION_HANDLE &&
                   (output->seen & 2u) == 0 && record.payload.size == 4) {
            output->permission_handle = pxa_read_u32(record.payload.data);
            output->seen |= 2u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return output->seen == 3u && kind_valid(output->kind) &&
                   output->permission_handle != PXA_HANDLE_INVALID
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t get_mac(pxa_device_service_t *service,
                            pxa_component_t component,
                            const pxa_message_view_t *message,
                            uint8_t result[24], size_t *result_size) {
    static const uint8_t permission_name[] = "device.identity";
    pxa_device_get_mac_request_t request;
    pxa_authority_t authority;
    pxa_writer_t writer;
    pxa_status_t status;
    uint8_t mac[6];
    uint8_t value[4];
    uint32_t flags = 0;
    status = parse_get_mac(message->payload, &request);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_permission_resolve(
        service->permissions, component, request.permission_handle,
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1u},
        scope_for_kind(request.kind), &authority);
    if (status != PXA_STATUS_OK) return status;
    status = service->get_mac(service->provider_context, request.kind, mac,
                              &flags);
    if (status != PXA_STATUS_OK) return status;
    pxa_writer_init(&writer, result, 24);
    pxa_write_u16(value, request.kind);
    status = pxa_writer_record(&writer, PXA_DEVICE_TAG_MAC_KIND, value, 2);
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&writer, PXA_DEVICE_TAG_MAC, mac, sizeof(mac));
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, flags);
        status = pxa_writer_record(&writer, PXA_DEVICE_TAG_FLAGS, value, 4);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t device_control(void *context, pxa_runtime_t *runtime,
                                   pxa_component_t component,
                                   const pxa_message_view_t *message) {
    pxa_device_service_t *service = (pxa_device_service_t *)context;
    uint8_t result[256];
    size_t result_size = 0;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || message == NULL || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_DEVICE_GET_MAC &&
        message->opcode != PXA_DEVICE_GET_RUNTIME_INFO)
        return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin(service->runtime, component, message->request_id,
                               PXA_DEVICE_SERVICE_ID, message->opcode, 0);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_DEVICE_GET_MAC) {
        status = get_mac(service, component, message, result, &result_size);
    } else {
        pxa_writer_t writer;
        uint8_t formats[4];
        const char *values[] = {service->target, service->architecture,
                                service->engine, service->engine_abi};
        size_t index;
        status = message->payload.size == 0 ? PXA_STATUS_OK
                                            : PXA_STATUS_INVALID_ARGUMENT;
        for (index = 0; status == PXA_STATUS_OK && index < 4; ++index) {
            if (values[index] == NULL || values[index][0] == '\0')
                status = PXA_STATUS_UNSUPPORTED;
        }
        if (status == PXA_STATUS_OK) {
            pxa_writer_init(&writer, result, sizeof(result));
            for (index = 0; status == PXA_STATUS_OK && index < 4; ++index)
                status = pxa_writer_record(&writer, (uint16_t)(index + 1u),
                                            (const uint8_t *)values[index],
                                            strlen(values[index]));
            pxa_write_u32(formats, service->formats);
            if (status == PXA_STATUS_OK)
                status = pxa_writer_record(&writer, PXA_DEVICE_TAG_FORMATS,
                                            formats, sizeof(formats));
            if (status == PXA_STATUS_OK) result_size = writer.size;
        }
    }
    complete = pxa_request_complete(service->runtime, component,
                                    message->request_id, status,
                                    status == PXA_STATUS_OK ? result : NULL,
                                    status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_device_service_register(pxa_device_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_DEVICE_SERVICE_ID;
    operations.major = PXA_DEVICE_SERVICE_MAJOR;
    operations.minor = PXA_DEVICE_SERVICE_MINOR;
    operations.context = service;
    operations.control = device_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
