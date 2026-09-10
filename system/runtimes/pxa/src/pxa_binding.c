#include "pxsys/pxa_binding.h"

#include <string.h>

pxsys_status_t pxsys_pxa_manifest_identity(const pxa_package_manifest_t* manifest,
                                           pxsys_app_identity_t* identity) {
    if (manifest == NULL || identity == NULL || manifest->management_key_id == NULL ||
        manifest->app_id.data == NULL || manifest->app_id.size == 0) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    memset(identity, 0, sizeof(*identity));
    memcpy(identity->publisher_root, manifest->management_key_id, PXSYS_PUBLISHER_ROOT_BYTES);
    identity->app_id = pxsys_string((const char*)manifest->app_id.data, manifest->app_id.size);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_manifest_descriptor(const pxa_package_manifest_t* manifest,
                                             pxsys_string_t runtime_id, uint32_t app_flags,
                                             pxsys_app_descriptor_t* descriptor) {
    if (manifest == NULL || descriptor == NULL || manifest->name.data == NULL ||
        manifest->name.size == 0 || manifest->version.data == NULL || manifest->version.size == 0 ||
        runtime_id.data == NULL || runtime_id.size == 0) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->struct_size = sizeof(*descriptor);
    if (pxsys_pxa_manifest_identity(manifest, &descriptor->identity) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    descriptor->display_name = pxsys_string((const char*)manifest->name.data, manifest->name.size);
    descriptor->description =
        pxsys_string((const char*)manifest->description.data,
                     manifest->description.size);
    descriptor->icon_reference =
        pxsys_string((const char*)manifest->icon_path.data,
                     manifest->icon_path.size);
    descriptor->version = pxsys_string((const char*)manifest->version.data, manifest->version.size);
    descriptor->runtime_id = runtime_id;
    descriptor->flags = app_flags;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_manifest_caller(const pxa_package_manifest_t* manifest,
                                         pxa_bytes_t component_id, pxsys_caller_t* caller) {
    size_t index;
    if (manifest == NULL || caller == NULL || component_id.data == NULL || component_id.size == 0) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < manifest->component_count; ++index) {
        pxa_bytes_t declared = manifest->components[index].id;
        if (declared.size == component_id.size && declared.data != NULL &&
            memcmp(declared.data, component_id.data, component_id.size) == 0) {
            memset(caller, 0, sizeof(*caller));
            caller->struct_size = sizeof(*caller);
            if (pxsys_pxa_manifest_identity(manifest, &caller->app) != PXSYS_STATUS_OK)
                return PXSYS_STATUS_INVALID_ARGUMENT;
            caller->component_id = pxsys_string((const char*)declared.data, declared.size);
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}

pxsys_status_t pxsys_status_from_pxa(pxa_status_t status) {
    switch (status) {
        case PXA_STATUS_OK:
            return PXSYS_STATUS_OK;
        case PXA_STATUS_INVALID_ARGUMENT:
        case PXA_STATUS_PROTOCOL_ERROR:
        case PXA_STATUS_LIMIT_EXCEEDED:
            return PXSYS_STATUS_INVALID_ARGUMENT;
        case PXA_STATUS_BAD_STATE:
            return PXSYS_STATUS_BAD_STATE;
        case PXA_STATUS_UNSUPPORTED:
            return PXSYS_STATUS_UNSUPPORTED;
        case PXA_STATUS_DENIED:
            return PXSYS_STATUS_DENIED;
        case PXA_STATUS_NOT_FOUND:
            return PXSYS_STATUS_NOT_FOUND;
        case PXA_STATUS_BUSY:
        case PXA_STATUS_WOULD_BLOCK:
            return PXSYS_STATUS_BUSY;
        case PXA_STATUS_QUOTA_EXCEEDED:
        case PXA_STATUS_RESOURCE_LIMIT:
            return PXSYS_STATUS_RESOURCE_LIMIT;
        case PXA_STATUS_CANCELLED:
            return PXSYS_STATUS_CANCELLED;
        case PXA_STATUS_TIMED_OUT:
            return PXSYS_STATUS_TIMEOUT;
        case PXA_STATUS_UNAVAILABLE:
            return PXSYS_STATUS_UNAVAILABLE;
        case PXA_STATUS_IO_ERROR:
            return PXSYS_STATUS_INTERNAL;
        case PXA_STATUS_INTERNAL:
        default:
            return PXSYS_STATUS_INTERNAL;
    }
}

pxa_status_t pxsys_status_to_pxa(pxsys_status_t status) {
    switch (status) {
        case PXSYS_STATUS_OK:
            return PXA_STATUS_OK;
        case PXSYS_STATUS_INVALID_ARGUMENT:
            return PXA_STATUS_INVALID_ARGUMENT;
        case PXSYS_STATUS_NOT_FOUND:
            return PXA_STATUS_NOT_FOUND;
        case PXSYS_STATUS_ALREADY_EXISTS:
            return PXA_STATUS_BUSY;
        case PXSYS_STATUS_RESOURCE_LIMIT:
        case PXSYS_STATUS_NO_MEMORY:
            return PXA_STATUS_RESOURCE_LIMIT;
        case PXSYS_STATUS_UNSUPPORTED:
            return PXA_STATUS_UNSUPPORTED;
        case PXSYS_STATUS_DENIED:
            return PXA_STATUS_DENIED;
        case PXSYS_STATUS_BUSY:
            return PXA_STATUS_BUSY;
        case PXSYS_STATUS_BAD_STATE:
            return PXA_STATUS_BAD_STATE;
        case PXSYS_STATUS_CANCELLED:
            return PXA_STATUS_CANCELLED;
        case PXSYS_STATUS_TIMEOUT:
            return PXA_STATUS_TIMED_OUT;
        case PXSYS_STATUS_UNAVAILABLE:
            return PXA_STATUS_UNAVAILABLE;
        case PXSYS_STATUS_CONFLICT:
            return PXA_STATUS_BUSY;
        case PXSYS_STATUS_PENDING:
            return PXA_STATUS_WOULD_BLOCK;
        case PXSYS_STATUS_INTERNAL:
        default:
            return PXA_STATUS_INTERNAL;
    }
}
