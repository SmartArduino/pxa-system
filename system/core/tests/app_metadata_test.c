#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/app_metadata.h"

#define ENTRY(key_value, text_value) \
    {{key_value, sizeof(key_value) - 1u}, {text_value, sizeof(text_value) - 1u}}

static const pxsys_resource_entry_t default_entries[] = {
    ENTRY("metadata.name", "Default catalog name"),
    ENTRY("metadata.icon", "assets/default.png"),
};
static const pxsys_resource_entry_t zh_entries[] = {
    ENTRY("metadata.name", "天气"),
    ENTRY("metadata.description", "查看本地天气"),
};
static const pxsys_resource_catalog_t catalogs[] = {
    {sizeof(pxsys_resource_catalog_t), {"app.weather", 11}, {NULL, 0},
     default_entries, 2, 0},
    {sizeof(pxsys_resource_catalog_t), {"app.weather", 11}, {"zh", 2},
     zh_entries, 2, 0},
};

static void* allocate(void* context, size_t size) {
    size_t* count = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL) (*count)++;
    return memory;
}

static void release(void* context, void* memory) {
    size_t* count = (size_t*)context;
    if (memory != NULL) (*count)--;
    free(memory);
}

static int equals(pxsys_string_t value, const char* expected) {
    return value.size == strlen(expected) &&
           memcmp(value.data, expected, value.size) == 0;
}

int main(void) {
    size_t allocations = 0;
    size_t index;
    pxsys_resource_service_config_t config;
    pxsys_resource_service_t* resources = NULL;
    pxsys_app_descriptor_t app = {0};
    pxsys_app_metadata_t metadata = {0};

    pxsys_resource_service_config_init(&config);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_resource_service_create(&config, &resources) ==
           PXSYS_STATUS_OK);
    for (index = 0; index < sizeof(catalogs) / sizeof(catalogs[0]); ++index) {
        assert(pxsys_resource_catalog_register(resources, &catalogs[index]) ==
               PXSYS_STATUS_OK);
    }

    app.struct_size = sizeof(app);
    app.display_name = pxsys_string_from_cstr("Weather");
    app.description = pxsys_string_from_cstr("Weather near you");
    app.icon_reference = pxsys_string_from_cstr("assets/weather.png");
    app.resource_namespace = pxsys_string_from_cstr("app.weather");
    app.display_name_resource_key = pxsys_string_from_cstr("metadata.name");
    app.description_resource_key =
        pxsys_string_from_cstr("metadata.description");
    app.icon_resource_key = pxsys_string_from_cstr("metadata.icon");

    metadata.struct_size = sizeof(metadata);
    assert(pxsys_app_metadata_resolve(resources, &app,
                                      pxsys_string_from_cstr("zh-Hans-CN"),
                                      &metadata) == PXSYS_STATUS_OK);
    assert(equals(metadata.display_name, "天气"));
    assert(equals(metadata.description, "查看本地天气"));
    assert(equals(metadata.icon_reference, "assets/default.png"));
    assert(metadata.catalog_fields ==
           (PXSYS_APP_METADATA_CATALOG_DISPLAY_NAME |
            PXSYS_APP_METADATA_CATALOG_DESCRIPTION |
            PXSYS_APP_METADATA_CATALOG_ICON));

    metadata.struct_size = sizeof(metadata);
    assert(pxsys_app_metadata_resolve(resources, &app,
                                      pxsys_string_from_cstr("fr-FR"),
                                      &metadata) == PXSYS_STATUS_OK);
    assert(equals(metadata.display_name, "Default catalog name"));
    assert(equals(metadata.description, "Weather near you"));
    assert(equals(metadata.icon_reference, "assets/default.png"));
    assert(metadata.catalog_fields ==
           (PXSYS_APP_METADATA_CATALOG_DISPLAY_NAME |
            PXSYS_APP_METADATA_CATALOG_ICON));

    metadata.struct_size = sizeof(metadata);
    assert(pxsys_app_metadata_resolve(NULL, &app,
                                      pxsys_string_from_cstr("en-US"),
                                      &metadata) == PXSYS_STATUS_OK);
    assert(equals(metadata.display_name, "Weather"));
    assert(equals(metadata.description, "Weather near you"));
    assert(equals(metadata.icon_reference, "assets/weather.png"));
    assert(metadata.catalog_fields == 0);

    metadata.struct_size = sizeof(metadata);
    assert(pxsys_app_metadata_resolve(resources, &app,
                                      pxsys_string_from_cstr("not_a_locale"),
                                      &metadata) ==
           PXSYS_STATUS_INVALID_ARGUMENT);

    assert(pxsys_resource_service_destroy(resources) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("application metadata tests passed");
    return 0;
}
