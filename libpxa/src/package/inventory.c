#include "pxa/package.h"

#include "common/bytes_internal.h"
#include "package/package_internal.h"

#include <string.h>

static int version_in_range(pxa_package_version_t value,
                            pxa_package_version_t minimum,
                            pxa_package_version_t maximum) {
    return value.major == minimum.major && minimum.major == maximum.major &&
           value.minor >= minimum.minor && value.minor <= maximum.minor;
}

pxa_status_t pxa_package_inventory_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_inventory_entry_t *inventory, size_t count) {
    size_t index;
    if (manifest == NULL || (inventory == NULL && count != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (count != manifest->file_count) return PXA_STATUS_DENIED;
    for (index = 0; index < count; ++index) {
        const pxa_package_file_t *expected;
        size_t prior;
        if (!pxa_package_path_is_valid(inventory[index].path) ||
            inventory[index].sha256 == NULL) {
            return PXA_STATUS_DENIED;
        }
        for (prior = 0; prior < index; ++prior) {
            if (pxa_bytes_equal_internal(inventory[prior].path,
                                         inventory[index].path)) {
                return PXA_STATUS_DENIED;
            }
        }
        expected = pxa_package_file_find(manifest, inventory[index].path);
        if (expected == NULL || expected->size != inventory[index].size ||
            memcmp(expected->sha256, inventory[index].sha256,
                   PXA_PACKAGE_DIGEST_BYTES) != 0) {
            return PXA_STATUS_DENIED;
        }
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_package_requirements_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_component_t *component,
    const pxa_package_activation_profile_t *host) {
    size_t capability_index;
    uint16_t requirement_index;
    if (manifest == NULL || component == NULL || host == NULL ||
        (host->services == NULL && host->service_count != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (!version_in_range(host->core_version, manifest->core_min,
                          manifest->core_max)) {
        return PXA_STATUS_UNSUPPORTED;
    }
    for (capability_index = 0; capability_index < host->service_count;
         ++capability_index) {
        size_t prior;
        if (host->services[capability_index].service == 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        for (prior = 0; prior < capability_index; ++prior) {
            if (host->services[prior].service ==
                host->services[capability_index].service) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    for (requirement_index = 0;
         requirement_index < component->service_count; ++requirement_index) {
        const pxa_package_service_requirement_t *requirement =
            &component->services[requirement_index];
        const pxa_package_service_capability_t *capability = NULL;
        for (capability_index = 0; capability_index < host->service_count;
             ++capability_index) {
            if (host->services[capability_index].service ==
                requirement->service) {
                capability = &host->services[capability_index];
                break;
            }
        }
        if (capability == NULL ||
            !version_in_range(capability->version, requirement->min_version,
                              requirement->max_version) ||
            (requirement->required_features & ~capability->features) != 0) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    return PXA_STATUS_OK;
}
