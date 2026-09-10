#include "pxsys/app_metadata.h"

#include <stddef.h>
#include <string.h>

#include "pxsys/locale.h"

static int descriptor_has(const pxsys_app_descriptor_t* app, size_t end) {
    return app != NULL && app->struct_size >= end;
}

#define DESCRIPTOR_HAS(app, member)                                      \
    descriptor_has((app), offsetof(pxsys_app_descriptor_t, member) +     \
                               sizeof((app)->member))

static pxsys_status_t resolve_field(
    const pxsys_resource_service_t* resources, pxsys_string_t resource_namespace,
    pxsys_string_t key, pxsys_string_t locale, pxsys_string_t fallback,
    uint32_t catalog_bit, pxsys_string_t* output, uint32_t* catalog_fields) {
    pxsys_status_t status;
    *output = fallback;
    if (resources == NULL || resource_namespace.size == 0 || key.size == 0)
        return PXSYS_STATUS_OK;
    status = pxsys_resource_resolve(resources, resource_namespace, key, locale,
                                    output);
    if (status == PXSYS_STATUS_NOT_FOUND) {
        *output = fallback;
        return PXSYS_STATUS_OK;
    }
    if (status == PXSYS_STATUS_OK) *catalog_fields |= catalog_bit;
    return status;
}

pxsys_status_t pxsys_app_metadata_resolve(
    const pxsys_resource_service_t* resources,
    const pxsys_app_descriptor_t* app, pxsys_string_t locale,
    pxsys_app_metadata_t* metadata) {
    pxsys_locale_snapshot_t canonical;
    pxsys_string_t resource_namespace;
    pxsys_string_t description;
    pxsys_string_t icon_reference;
    pxsys_string_t name_key;
    pxsys_string_t description_key;
    pxsys_string_t icon_key;
    pxsys_status_t status;
    if (app == NULL || app->struct_size < PXSYS_APP_DESCRIPTOR_V1_SIZE ||
        metadata == NULL || metadata->struct_size < sizeof(*metadata) ||
        !pxsys_display_text_validate(app->display_name, SIZE_MAX) ||
        pxsys_locale_snapshot_init(&canonical, locale) != PXSYS_STATUS_OK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    description = pxsys_string(NULL, 0);
    icon_reference = pxsys_string(NULL, 0);
    resource_namespace = pxsys_string(NULL, 0);
    name_key = pxsys_string(NULL, 0);
    description_key = pxsys_string(NULL, 0);
    icon_key = pxsys_string(NULL, 0);
    if (DESCRIPTOR_HAS(app, description)) description = app->description;
    if (DESCRIPTOR_HAS(app, icon_reference))
        icon_reference = app->icon_reference;
    if (DESCRIPTOR_HAS(app, resource_namespace))
        resource_namespace = app->resource_namespace;
    if (DESCRIPTOR_HAS(app, display_name_resource_key))
        name_key = app->display_name_resource_key;
    if (DESCRIPTOR_HAS(app, description_resource_key))
        description_key = app->description_resource_key;
    if (DESCRIPTOR_HAS(app, icon_resource_key))
        icon_key = app->icon_resource_key;
    memset(metadata, 0, sizeof(*metadata));
    metadata->struct_size = sizeof(*metadata);
    locale = pxsys_string(canonical.tag, canonical.tag_size);
    status = resolve_field(resources, resource_namespace, name_key, locale,
                           app->display_name,
                           PXSYS_APP_METADATA_CATALOG_DISPLAY_NAME,
                           &metadata->display_name,
                           &metadata->catalog_fields);
    if (status != PXSYS_STATUS_OK) return status;
    status = resolve_field(resources, resource_namespace, description_key,
                           locale, description,
                           PXSYS_APP_METADATA_CATALOG_DESCRIPTION,
                           &metadata->description,
                           &metadata->catalog_fields);
    if (status != PXSYS_STATUS_OK) return status;
    return resolve_field(resources, resource_namespace, icon_key, locale,
                         icon_reference, PXSYS_APP_METADATA_CATALOG_ICON,
                         &metadata->icon_reference,
                         &metadata->catalog_fields);
}
