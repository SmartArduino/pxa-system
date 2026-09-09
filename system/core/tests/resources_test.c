#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/resources.h"

#define ENTRY(key_value, text_value) \
    { {key_value, sizeof(key_value) - 1u}, {text_value, sizeof(text_value) - 1u} }

static const pxsys_resource_entry_t default_entries[] = {
    ENTRY("action.ok", "OK"),
};
static const pxsys_resource_entry_t en_entries[] = {
    ENTRY("screen.title", "Settings"),
};
static const pxsys_resource_entry_t zh_entries[] = {
    ENTRY("screen.title", "设置"),
};
static const pxsys_resource_entry_t override_entries[] = {
    ENTRY("screen.title", "设备设置"),
};
static const pxsys_resource_catalog_t catalogs[] = {
    {sizeof(pxsys_resource_catalog_t), {"system.ui", 9}, {NULL, 0},
     default_entries, 1, 0},
    {sizeof(pxsys_resource_catalog_t), {"system.ui", 9}, {"en", 2},
     en_entries, 1, 0},
    {sizeof(pxsys_resource_catalog_t), {"system.ui", 9}, {"zh", 2},
     zh_entries, 1, 0},
    {sizeof(pxsys_resource_catalog_t), {"system.ui", 9}, {"zh-CN", 5},
     override_entries, 1, 100},
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

static void expect(pxsys_resource_service_t* service, const char* key,
                   const char* locale, const char* expected) {
    pxsys_string_t value = {0};
    assert(pxsys_resource_resolve(
               service, pxsys_string_from_cstr("system.ui"),
               pxsys_string_from_cstr(key), pxsys_string_from_cstr(locale),
               &value) == PXSYS_STATUS_OK);
    assert(value.size == strlen(expected));
    assert(memcmp(value.data, expected, value.size) == 0);
}

int main(void) {
    size_t allocations = 0;
    size_t index;
    pxsys_resource_service_config_t config;
    pxsys_resource_service_t* service = NULL;
    pxsys_string_t value = {0};
    pxsys_resource_service_config_init(&config);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_resource_service_create(&config, &service) == PXSYS_STATUS_OK);
    for (index = 0; index < sizeof(catalogs) / sizeof(catalogs[0]); ++index)
        assert(pxsys_resource_catalog_register(service, &catalogs[index]) ==
               PXSYS_STATUS_OK);
    expect(service, "screen.title", "en-US", "Settings");
    expect(service, "screen.title", "zh-Hans-CN", "设置");
    expect(service, "screen.title", "zh-CN", "设备设置");
    expect(service, "action.ok", "fr-FR", "OK");
    assert(pxsys_resource_resolve(
               service, pxsys_string_from_cstr("system.ui"),
               pxsys_string_from_cstr("missing"),
               pxsys_string_from_cstr("en-US"), &value) ==
           PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_resource_catalog_unregister(service, &catalogs[3]) ==
           PXSYS_STATUS_OK);
    expect(service, "screen.title", "zh-CN", "设置");
    assert(pxsys_resource_service_destroy(service) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("resource tests passed");
    return 0;
}
