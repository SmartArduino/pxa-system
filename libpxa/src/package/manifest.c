#include "pxa/package.h"

#include "common/bytes_internal.h"
#include "common/checked_math.h"
#include "package/manifest_internal.h"
#include "package/package_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_MANIFEST_HEADER_SIZE ((size_t)12)

int pxa_package_path_is_valid(pxa_bytes_t value) {
    static const uint8_t manifest_name[] = "manifest.pxm";
    static const uint8_t signature_name[] = "signature.pxs";
    size_t segment_start = 0;
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > 255 ||
        value.data[0] == '/' || value.data[value.size - 1] == '/' ||
        pxa_bytes_equal_internal(
            value,
            (pxa_bytes_t){manifest_name, sizeof(manifest_name) - 1}) ||
        pxa_bytes_equal_internal(
            value,
            (pxa_bytes_t){signature_name, sizeof(signature_name) - 1})) {
        return 0;
    }
    for (index = 0; index <= value.size; ++index) {
        size_t length;
        if (index != value.size && value.data[index] != '/') {
            uint8_t c = value.data[index];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-')) {
                return 0;
            }
            continue;
        }
        length = index - segment_start;
        if (length == 0 ||
            (length == 1 && value.data[segment_start] == '.') ||
            (length == 2 && value.data[segment_start] == '.' &&
             value.data[segment_start + 1] == '.')) {
            return 0;
        }
        segment_start = index + 1;
    }
    return 1;
}

void pxa_package_limits_init(pxa_package_limits_t *limits) {
    if (limits == NULL) return;
    memset(limits, 0, sizeof(*limits));
    limits->struct_size = sizeof(*limits);
    limits->max_components = PXA_PACKAGE_MAX_COMPONENTS;
    limits->max_artifacts = UINT16_MAX;
    limits->max_services = UINT16_MAX;
    limits->max_files = PXA_PACKAGE_MAX_FILES;
    limits->max_permissions = PXA_PACKAGE_MAX_PERMISSIONS;
    limits->max_ipc_endpoints = PXA_PACKAGE_MAX_IPC_ENDPOINTS;
    limits->max_localizations = PXA_PACKAGE_MAX_LOCALIZATIONS;
}

static uint16_t max_localizations(const pxa_package_limits_t *limits) {
    return limits->struct_size >= sizeof(*limits)
               ? limits->max_localizations
               : 0;
}

static int limits_valid(const pxa_package_limits_t *limits) {
    return limits != NULL && limits->struct_size >= sizeof(*limits) &&
           limits->max_components != 0 &&
           limits->max_artifacts != 0 &&
           limits->max_files != 0;
}

static int manifest_header_valid(pxa_bytes_t encoded) {
    static const uint8_t magic[] = {'P', 'X', 'A', 'M'};
    return encoded.data != NULL && encoded.size >= PXA_MANIFEST_HEADER_SIZE &&
           memcmp(encoded.data, magic, sizeof(magic)) == 0 &&
           pxa_read_u16(encoded.data + 4) ==
               PXA_PACKAGE_MANIFEST_FORMAT_MAJOR &&
           pxa_read_u16(encoded.data + 6) >= 1 &&
           pxa_read_u16(encoded.data + 6) <=
               PXA_PACKAGE_MANIFEST_FORMAT_MINOR &&
           pxa_read_u32(encoded.data + 8) ==
               encoded.size - PXA_MANIFEST_HEADER_SIZE;
}

static pxa_status_t count_component_records(
    pxa_bytes_t encoded, const pxa_package_limits_t *limits,
    uint16_t *artifact_count, uint16_t *service_count) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint16_t component_artifacts = 0;
    uint16_t component_services = 0;
    pxa_status_t status;
    if (artifact_count == NULL || service_count == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    pxa_record_iterator_init(&iterator, encoded);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 4) {
            if (component_artifacts >= PXA_PACKAGE_MAX_ARTIFACTS_PER_COMPONENT ||
                *artifact_count >= limits->max_artifacts) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++component_artifacts;
            ++*artifact_count;
        } else if (record.tag == 5) {
            if (component_services >= PXA_PACKAGE_MAX_SERVICES_PER_COMPONENT ||
                *service_count >= limits->max_services) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++component_services;
            ++*service_count;
        }
    }
    return PXA_STATUS_OK;
}

static int add_workspace(size_t *size, size_t count, size_t element_size) {
    size_t bytes;
    if (count != 0 && element_size > SIZE_MAX / count) return 0;
    bytes = count * element_size;
    if (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u > SIZE_MAX - *size) return 0;
    *size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (bytes > SIZE_MAX - *size) return 0;
    *size += bytes;
    return 1;
}

size_t pxa_package_manifest_workspace_size(
    const pxa_package_limits_t *limits) {
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!limits_valid(limits) ||
        sizeof(pxa_package_manifest_t) > SIZE_MAX - size) {
        return 0;
    }
    size += sizeof(pxa_package_manifest_t);
    if (!add_workspace(&size, limits->max_components,
                       sizeof(pxa_package_component_t)) ||
        !add_workspace(&size, limits->max_artifacts,
                       sizeof(pxa_package_artifact_t)) ||
        !add_workspace(&size, limits->max_services,
                       sizeof(pxa_package_service_requirement_t)) ||
        !add_workspace(&size, limits->max_files,
                       sizeof(pxa_package_file_t)) ||
        !add_workspace(&size, limits->max_permissions,
                       sizeof(pxa_package_permission_t)) ||
        !add_workspace(&size, limits->max_ipc_endpoints,
                       sizeof(pxa_package_ipc_endpoint_t)) ||
        !add_workspace(&size, max_localizations(limits),
                       sizeof(pxa_package_localization_t))) {
        return 0;
    }
    return size;
}

pxa_status_t pxa_package_manifest_measure(
    pxa_bytes_t encoded, const pxa_package_limits_t *limits,
    pxa_package_limits_t *required_limits) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_package_limits_t measured;
    uint16_t previous = 0;
    pxa_status_t status;
    if (!limits_valid(limits) || required_limits == NULL ||
        !manifest_header_valid(encoded)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    measured = *limits;
    measured.max_components = 0;
    measured.max_artifacts = 0;
    measured.max_services = 0;
    measured.max_files = 0;
    measured.max_permissions = 0;
    measured.max_ipc_endpoints = 0;
    measured.max_localizations = 0;
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){encoded.data + PXA_MANIFEST_HEADER_SIZE,
                      encoded.size - PXA_MANIFEST_HEADER_SIZE});
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 16) {
            if (measured.max_components >= limits->max_components) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++measured.max_components;
            status = count_component_records(record.payload, limits,
                                             &measured.max_artifacts,
                                             &measured.max_services);
            if (status != PXA_STATUS_OK) return status;
        } else if (record.tag == 17) {
            if (measured.max_files >= limits->max_files) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++measured.max_files;
        } else if (record.tag == 18) {
            if (measured.max_permissions >= limits->max_permissions) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++measured.max_permissions;
        } else if (record.tag == 19) {
            if (measured.max_ipc_endpoints >= limits->max_ipc_endpoints) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++measured.max_ipc_endpoints;
        } else if (record.tag == 20) {
            if (measured.max_localizations >= max_localizations(limits)) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            ++measured.max_localizations;
        }
    }
    /* A malformed manifest with no components, artifacts, or files still
     * reaches the normal parser and receives its semantic validation error. */
    if (measured.max_components == 0) measured.max_components = 1;
    if (measured.max_artifacts == 0) measured.max_artifacts = 1;
    if (measured.max_files == 0) measured.max_files = 1;
    *required_limits = measured;
    return PXA_STATUS_OK;
}

static void *take_array(uintptr_t *cursor, uintptr_t end, size_t count,
                        size_t element_size) {
    size_t bytes;
    *cursor = pxa_internal_align_pointer(
        *cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (count != 0 && element_size > SIZE_MAX / count) return NULL;
    bytes = count * element_size;
    if (*cursor > end || bytes > end - *cursor) return NULL;
    {
        void *output = (void *)*cursor;
        *cursor += bytes;
        return output;
    }
}

static pxa_status_t initialize_manifest_workspace(
    void *workspace, size_t workspace_size,
    const pxa_package_limits_t *limits, pxa_package_manifest_t **output) {
    uintptr_t cursor;
    uintptr_t end;
    pxa_package_manifest_t *manifest;
    if ((uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    cursor = pxa_internal_align_pointer(
        (uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    end = (uintptr_t)workspace + workspace_size;
    if (cursor > end || sizeof(*manifest) > end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    manifest = (pxa_package_manifest_t *)cursor;
    cursor += sizeof(*manifest);
    memset(manifest, 0, sizeof(*manifest));
    manifest->components = (pxa_package_component_t *)take_array(
        &cursor, end, limits->max_components, sizeof(manifest->components[0]));
    manifest->artifacts = (pxa_package_artifact_t *)take_array(
        &cursor, end, limits->max_artifacts, sizeof(manifest->artifacts[0]));
    manifest->services = (pxa_package_service_requirement_t *)take_array(
        &cursor, end, limits->max_services, sizeof(manifest->services[0]));
    manifest->files = (pxa_package_file_t *)take_array(
        &cursor, end, limits->max_files, sizeof(manifest->files[0]));
    manifest->permissions = (pxa_package_permission_t *)take_array(
        &cursor, end, limits->max_permissions,
        sizeof(manifest->permissions[0]));
    manifest->ipc_endpoints = (pxa_package_ipc_endpoint_t *)take_array(
        &cursor, end, limits->max_ipc_endpoints,
        sizeof(manifest->ipc_endpoints[0]));
    manifest->localizations = (pxa_package_localization_t *)take_array(
        &cursor, end, max_localizations(limits),
        sizeof(manifest->localizations[0]));
    if (manifest->components == NULL || manifest->artifacts == NULL ||
        (limits->max_services != 0 && manifest->services == NULL) ||
        manifest->files == NULL ||
        (limits->max_permissions != 0 && manifest->permissions == NULL) ||
        (limits->max_ipc_endpoints != 0 &&
         manifest->ipc_endpoints == NULL) ||
        (max_localizations(limits) != 0 &&
         manifest->localizations == NULL)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = manifest;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_package_manifest_parse(
    void *workspace, size_t workspace_size, pxa_bytes_t encoded,
    const pxa_package_limits_t *limits, pxa_package_manifest_t **output) {
    pxa_package_manifest_t *manifest;
    pxa_manifest_parser_t parser;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    size_t required;
    uint16_t previous = 0;
    uint32_t singleton_seen = 0;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_package_manifest_workspace_size(limits);
    if (workspace == NULL || required == 0 || workspace_size < required ||
        !manifest_header_valid(encoded)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = initialize_manifest_workspace(workspace, workspace_size, limits,
                                           &manifest);
    if (status != PXA_STATUS_OK) return status;
    manifest->encoded = encoded;
    manifest->format_minor = pxa_read_u16(encoded.data + 6);
    parser.manifest = manifest;
    parser.limits = limits;
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){encoded.data + PXA_MANIFEST_HEADER_SIZE,
                      encoded.size - PXA_MANIFEST_HEADER_SIZE});
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        status = pxa_manifest_decode_record(&parser, &record,
                                            &singleton_seen);
        if (status != PXA_STATUS_OK) return status;
    }
    status = pxa_manifest_decode_finish(&parser, singleton_seen);
    if (status != PXA_STATUS_OK) return status;
    *output = manifest;
    return PXA_STATUS_OK;
}
