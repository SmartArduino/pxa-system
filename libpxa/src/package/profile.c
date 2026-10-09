#include "pxa/profile.h"
#include "pxa/version.h"

#include <inttypes.h>
#include <stdio.h>

static int identifier(pxa_bytes_t value) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > 128) return 0;
    for (index = 0; index < value.size; ++index) {
        uint8_t c = value.data[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}

pxa_status_t pxa_package_profile_record(
    const pxa_package_host_profile_t *host,
    const pxa_package_activation_profile_t *activation,
    int wasm_enabled, size_t record, char *output, size_t capacity) {
    int size;
    if (host == NULL || activation == NULL || output == NULL || capacity == 0 ||
        (activation->service_count != 0 && activation->services == NULL))
        return PXA_STATUS_INVALID_ARGUMENT;
    output[0] = '\0';
    if (record > activation->service_count) return PXA_STATUS_NOT_FOUND;
    if (record == 0) {
        if (!identifier(host->target) || !identifier(host->engine) ||
            !identifier(host->engine_abi)) return PXA_STATUS_INVALID_ARGUMENT;
        size = snprintf(output, capacity,
            "{\"schema\":\"pxa-host-profile-1\",\"pxa_release\":\"%s\","
            "\"core\":[%u,%u],\"target\":\"%.*s\",\"engine\":\"%.*s\","
            "\"engine_abi\":\"%.*s\",\"memory_model\":%u,\"artifact_features\":%" PRIu64 ","
            "\"wasm\":%s,\"service_count\":%u,\"manifest\":[0,7],\"container\":[0,1]}",
            PXA_VERSION_STRING, (unsigned)activation->core_version.major,
            (unsigned)activation->core_version.minor, (int)host->target.size, host->target.data,
            (int)host->engine.size, host->engine.data, (int)host->engine_abi.size,
            host->engine_abi.data, (unsigned)host->memory_model,
            host->supported_features, wasm_enabled ? "true" : "false",
            (unsigned)activation->service_count);
    } else {
        const pxa_package_service_capability_t *service = &activation->services[record - 1];
        size = snprintf(output, capacity,
            "{\"id\":%u,\"version\":[%u,%u],\"features\":%" PRIu64 "}",
            (unsigned)service->service, (unsigned)service->version.major,
            (unsigned)service->version.minor, service->features);
    }
    if (size < 0 || (size_t)size >= capacity) {
        output[0] = '\0';
        return PXA_STATUS_LIMIT_EXCEEDED;
    }
    return PXA_STATUS_OK;
}
