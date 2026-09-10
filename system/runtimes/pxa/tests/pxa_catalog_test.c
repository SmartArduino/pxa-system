#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/pxa_catalog.h"

static void* allocate(void* context, size_t size) {
    size_t* count = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*count)++;
    return memory;
}

static void release(void* context, void* memory) {
    size_t* count = (size_t*)context;
    if (memory != NULL)
        (*count)--;
    free(memory);
}

int main(void) {
    static const uint8_t app_id[] = "weather";
    static const uint8_t name[] = "Weather";
    static const uint8_t description[] = "Local weather";
    static const uint8_t icon[] = "assets/icon.png";
    static const uint8_t version_one[] = "1.0.0";
    static const uint8_t version_two[] = "2.0.0";
    size_t allocations = 0;
    uint8_t management_key[PXA_PACKAGE_DIGEST_BYTES];
    pxa_package_manifest_t manifest = {0};
    pxsys_app_registry_config_t config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_pxa_catalog_change_t change = 0;
    pxsys_app_identity_t identity = {0};
    const pxsys_app_descriptor_t* stored;

    memset(management_key, 0x31, sizeof(management_key));
    manifest.management_key_id = management_key;
    manifest.app_id = (pxa_bytes_t){app_id, sizeof(app_id) - 1u};
    manifest.name = (pxa_bytes_t){name, sizeof(name) - 1u};
    manifest.description =
        (pxa_bytes_t){description, sizeof(description) - 1u};
    manifest.icon_path = (pxa_bytes_t){icon, sizeof(icon) - 1u};
    manifest.version = (pxa_bytes_t){version_one, sizeof(version_one) - 1u};
    pxsys_app_registry_config_init(&config);
    config.allocator.struct_size = sizeof(config.allocator);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_app_registry_create(&config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_pxa_catalog_publish(apps, &manifest, pxsys_string_from_cstr(PXSYS_PXA_RUNTIME_AOT),
                                     PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_REMOVABLE,
                                     &change) == PXSYS_STATUS_OK);
    assert(change == PXSYS_PXA_CATALOG_ADDED);
    memcpy(identity.publisher_root, management_key, sizeof(identity.publisher_root));
    identity.app_id = pxsys_string_from_cstr("weather");
    stored = pxsys_app_registry_find(apps, &identity);
    assert(stored != NULL && stored->version.size == 5);
    assert(stored->description.size == sizeof(description) - 1u &&
           memcmp(stored->description.data, description,
                  stored->description.size) == 0);
    assert(stored->icon_reference.size == sizeof(icon) - 1u &&
           memcmp(stored->icon_reference.data, icon,
                  stored->icon_reference.size) == 0);

    manifest.version = (pxa_bytes_t){version_two, sizeof(version_two) - 1u};
    assert(pxsys_pxa_catalog_publish(
               apps, &manifest, pxsys_string_from_cstr(PXSYS_PXA_RUNTIME_WASM),
               PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_REMOVABLE, &change) == PXSYS_STATUS_OK);
    assert(change == PXSYS_PXA_CATALOG_UPDATED);
    stored = pxsys_app_registry_find(apps, &identity);
    assert(stored != NULL && memcmp(stored->version.data, "2.0.0", 5) == 0);
    assert(pxsys_pxa_catalog_unpublish(apps, &manifest) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_count(apps) == 0);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("pxa catalog tests passed");
    return 0;
}
