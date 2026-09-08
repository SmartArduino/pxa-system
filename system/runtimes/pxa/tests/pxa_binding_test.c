#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/pxa_binding.h"
#include "pxsys/pxa_client.h"

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
    static const uint8_t version[] = "2.1.0";
    uint8_t management_key[PXA_PACKAGE_DIGEST_BYTES];
    pxa_package_manifest_t manifest;
    pxa_package_component_t component;
    pxsys_app_descriptor_t descriptor;
    pxsys_caller_t caller;
    pxsys_pxa_client_config_t client_config = {0};
    pxsys_pxa_client_t* client = NULL;
    size_t allocations = 0;

    memset(management_key, 0x42, sizeof(management_key));
    memset(&manifest, 0, sizeof(manifest));
    manifest.management_key_id = management_key;
    manifest.app_id = (pxa_bytes_t){app_id, sizeof(app_id) - 1u};
    manifest.name = (pxa_bytes_t){name, sizeof(name) - 1u};
    manifest.version = (pxa_bytes_t){version, sizeof(version) - 1u};
    memset(&component, 0, sizeof(component));
    component.id = (pxa_bytes_t){(const uint8_t*)"main", 4};
    manifest.components = &component;
    manifest.component_count = 1;
    assert(pxsys_pxa_manifest_descriptor(&manifest, pxsys_string_from_cstr(PXSYS_PXA_RUNTIME_AOT),
                                         PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_REMOVABLE,
                                         &descriptor) == PXSYS_STATUS_OK);
    assert(memcmp(descriptor.identity.publisher_root, management_key, PXSYS_PUBLISHER_ROOT_BYTES) ==
           0);
    assert(descriptor.identity.app_id.size == sizeof(app_id) - 1u);
    assert(descriptor.runtime_id.size == strlen(PXSYS_PXA_RUNTIME_AOT));
    assert(pxsys_pxa_manifest_caller(&manifest, component.id, &caller) == PXSYS_STATUS_OK);
    assert(caller.component_id.size == 4 &&
           memcmp(caller.app.publisher_root, management_key, PXSYS_PUBLISHER_ROOT_BYTES) == 0);
    assert(pxsys_pxa_manifest_caller(&manifest, (pxa_bytes_t){(const uint8_t*)"missing", 7},
                                     &caller) == PXSYS_STATUS_NOT_FOUND);
    client_config.struct_size = sizeof(client_config);
    client_config.manifest = &manifest;
    client_config.component_id = component.id;
    client_config.tasks = (pxsys_task_manager_t*)&manifest;
    client_config.services = (pxsys_service_registry_t*)&manifest;
    client_config.allocator.struct_size = sizeof(client_config.allocator);
    client_config.allocator.context = &allocations;
    client_config.allocator.allocate = allocate;
    client_config.allocator.release = release;
    assert(pxsys_pxa_client_create(&client_config, &client) == PXSYS_STATUS_OK);
    assert(pxsys_pxa_client_caller(client)->component_id.size == 4);
    pxsys_pxa_client_destroy(client);
    assert(allocations == 0);
    assert(pxsys_status_from_pxa(PXA_STATUS_DENIED) == PXSYS_STATUS_DENIED);
    assert(pxsys_status_to_pxa(PXSYS_STATUS_TIMEOUT) == PXA_STATUS_TIMED_OUT);
    puts("pxa binding tests passed");
    return 0;
}
