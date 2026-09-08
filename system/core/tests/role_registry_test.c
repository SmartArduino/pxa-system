#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/role_registry.h"

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

static pxsys_allocator_t allocator(size_t* count) {
    pxsys_allocator_t value = {0};
    value.struct_size = sizeof(value);
    value.context = count;
    value.allocate = allocate;
    value.release = release;
    return value;
}

static pxsys_app_descriptor_t app(uint8_t publisher, const char* id) {
    pxsys_app_descriptor_t value = {0};
    value.struct_size = sizeof(value);
    memset(value.identity.publisher_root, publisher, PXSYS_PUBLISHER_ROOT_BYTES);
    value.identity.app_id = pxsys_string_from_cstr(id);
    value.display_name = pxsys_string_from_cstr(id);
    value.version = pxsys_string_from_cstr("1");
    value.runtime_id = pxsys_string_from_cstr("native-static");
    value.flags = PXSYS_APP_FLAG_ENABLED;
    return value;
}

int main(void) {
    size_t allocations = 0;
    pxsys_app_registry_config_t app_config;
    pxsys_role_registry_config_t role_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_role_registry_t* roles = NULL;
    pxsys_app_descriptor_t standard = app(1, "standard-home");
    pxsys_app_descriptor_t product = app(2, "product-home");
    pxsys_role_candidate_t candidate = {0};
    const pxsys_app_descriptor_t* resolved;

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&allocations);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &standard) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &product) == PXSYS_STATUS_OK);
    pxsys_role_registry_config_init(&role_config);
    role_config.apps = apps;
    role_config.allocator = allocator(&allocations);
    assert(pxsys_role_registry_create(&role_config, &roles) == PXSYS_STATUS_OK);
    candidate.struct_size = sizeof(candidate);
    candidate.role_id = pxsys_string_from_cstr(PXSYS_ROLE_HOME);
    candidate.app = standard.identity;
    candidate.priority = 0;
    assert(pxsys_role_candidate_register(roles, &candidate) == PXSYS_STATUS_OK);
    candidate.app = product.identity;
    candidate.priority = 100;
    assert(pxsys_role_candidate_register(roles, &candidate) == PXSYS_STATUS_OK);
    assert(pxsys_role_resolve(roles, pxsys_string_from_cstr(PXSYS_ROLE_HOME), &resolved) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_identity_equal(&resolved->identity, &product.identity));
    assert(pxsys_app_registry_unregister(apps, &product.identity) == PXSYS_STATUS_BUSY);
    assert(pxsys_role_candidates_unregister(roles, &product.identity) == PXSYS_STATUS_OK);
    assert(pxsys_role_resolve(roles, pxsys_string_from_cstr(PXSYS_ROLE_HOME), &resolved) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_identity_equal(&resolved->identity, &standard.identity));
    assert(pxsys_role_registry_destroy(roles) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &standard.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &product.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("role registry tests passed");
    return 0;
}
