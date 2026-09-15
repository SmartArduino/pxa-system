#include "builtin_catalog.h"

#include <string.h>

#include "builtin_catalog_data.h"

#define PXSYS_DESKTOP_RUNTIME_ID "pxa-sim"

size_t pxsys_desktop_builtin_app_count(void) {
    return sizeof(k_builtin_apps) / sizeof(k_builtin_apps[0]);
}

const pxsys_desktop_builtin_app_t* pxsys_desktop_builtin_app(size_t index) {
    return index < pxsys_desktop_builtin_app_count() ? &k_builtin_apps[index]
                                                     : NULL;
}

pxsys_status_t pxsys_desktop_register_builtin_apps(
    pxsys_app_registry_t* registry,
    const uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES]) {
    size_t index;
    if (registry == NULL || publisher_root == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < pxsys_desktop_builtin_app_count(); ++index) {
        const pxsys_desktop_builtin_app_t* app = &k_builtin_apps[index];
        pxsys_app_descriptor_t descriptor = {0};
        pxsys_status_t status;
        descriptor.struct_size = sizeof(descriptor);
        memcpy(descriptor.identity.publisher_root, publisher_root,
               PXSYS_PUBLISHER_ROOT_BYTES);
        descriptor.identity.app_id = pxsys_string_from_cstr(app->id);
        descriptor.display_name = pxsys_string_from_cstr(app->name);
        descriptor.version = pxsys_string_from_cstr(app->version);
        descriptor.runtime_id =
            pxsys_string_from_cstr(PXSYS_DESKTOP_RUNTIME_ID);
        descriptor.flags = PXSYS_APP_FLAG_REMOVABLE | PXSYS_APP_FLAG_ENABLED |
                           PXSYS_APP_FLAG_LAUNCHER;
        descriptor.description = pxsys_string_from_cstr(app->description);
        descriptor.icon_reference = pxsys_string_from_cstr(app->icon_reference);
        status = pxsys_app_registry_register(registry, &descriptor);
        if (status != PXSYS_STATUS_OK) return status;
    }
    return PXSYS_STATUS_OK;
}
