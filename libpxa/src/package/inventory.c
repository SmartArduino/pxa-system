#include "pxa/package.h"
#include "pxa/log.h"
#include "pxa/device.h"
#include "pxa/window.h"
#include "pxa/permission.h"
#include "pxa/storage.h"
#include "pxa/fs.h"
#include "pxa/ipc.h"
#include "pxa/net.h"
#include "pxa/audio.h"
#include "pxa/sensor.h"
#include "pxa/scheduler.h"
#include "pxa/surface.h"
#include "pxa/clock.h"
#include "pxa/ui.h"
#include "pxa/wasi.h"
#include "pxa/game_render.h"
#include "pxa/assets.h"
#include "pxa/service.h"

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
    if (manifest->min_sdk.major != 1) return PXA_STATUS_UNSUPPORTED;
    if (host->core_version.major != 1 ||
        host->core_version.minor < manifest->min_sdk.minor) {
        return PXA_STATUS_UNSUPPORTED;
    }
    /* Admit only services with a current Core request path. */
    for (requirement_index = 0;
         requirement_index < component->service_count;
         ++requirement_index) {
        if (component->services[requirement_index].service !=
                    PXA_LOG_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_DEVICE_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_WINDOW_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_PERMISSION_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_STORAGE_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_FS_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_IPC_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_NET_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_AUDIO_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_SENSOR_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_WORK_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_SURFACE_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_CLOCK_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_WASI_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_UI_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_GAME_RENDER_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_ASSETS_SERVICE_ID &&
                component->services[requirement_index].service !=
                    PXA_STORE_INSTALLER_SERVICE_ID)
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
