#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/intent.h"

static void* test_allocate(void* context, size_t size) {
    size_t* allocations = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*allocations)++;
    return memory;
}

static void test_release(void* context, void* memory) {
    size_t* allocations = (size_t*)context;
    if (memory != NULL)
        (*allocations)--;
    free(memory);
}

static pxsys_allocator_t allocator(size_t* allocations) {
    pxsys_allocator_t value = {0};
    value.struct_size = sizeof(value);
    value.context = allocations;
    value.allocate = test_allocate;
    value.release = test_release;
    return value;
}

static pxsys_app_identity_t identity(uint8_t publisher, const char* app_id) {
    pxsys_app_identity_t value;
    memset(&value, publisher, sizeof(value.publisher_root));
    value.app_id = pxsys_string_from_cstr(app_id);
    return value;
}

static pxsys_app_descriptor_t descriptor(uint8_t publisher, const char* app_id, int enabled) {
    pxsys_app_descriptor_t value = {0};
    value.struct_size = sizeof(value);
    value.identity = identity(publisher, app_id);
    value.display_name = pxsys_string_from_cstr(app_id);
    value.version = pxsys_string_from_cstr("1.0.0");
    value.runtime_id = pxsys_string_from_cstr("native-static");
    value.flags = enabled ? PXSYS_APP_FLAG_ENABLED : 0;
    return value;
}

static void test_explicit_and_implicit_resolution(void) {
    size_t allocations = 0;
    pxsys_app_registry_config_t app_config;
    pxsys_intent_resolver_config_t resolver_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_intent_resolver_t* resolver = NULL;
    pxsys_app_descriptor_t viewer = descriptor(2, "viewer", 1);
    pxsys_app_descriptor_t preferred = descriptor(1, "preferred-viewer", 1);
    pxsys_app_descriptor_t disabled = descriptor(3, "disabled", 0);
    pxsys_intent_filter_t viewer_filter = {0};
    pxsys_intent_filter_t preferred_filter = {0};
    pxsys_intent_t intent = {0};
    const pxsys_app_descriptor_t* resolved = NULL;

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&allocations);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &viewer) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &preferred) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &disabled) == PXSYS_STATUS_OK);

    pxsys_intent_resolver_config_init(&resolver_config);
    resolver_config.apps = apps;
    resolver_config.allocator = allocator(&allocations);
    assert(pxsys_intent_resolver_create(&resolver_config, &resolver) == PXSYS_STATUS_OK);

    viewer_filter.struct_size = sizeof(viewer_filter);
    viewer_filter.app = viewer.identity;
    viewer_filter.action = pxsys_string_from_cstr("system.intent.view");
    viewer_filter.uri_scheme = pxsys_string_from_cstr("file");
    viewer_filter.mime_type = pxsys_string_from_cstr("image/png");
    viewer_filter.priority = 10;
    preferred_filter = viewer_filter;
    preferred_filter.app = preferred.identity;
    preferred_filter.priority = 20;
    assert(pxsys_intent_filter_register(resolver, &viewer_filter) == PXSYS_STATUS_OK);
    assert(pxsys_intent_filter_register(resolver, &preferred_filter) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &viewer.identity) == PXSYS_STATUS_BUSY);

    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.view");
    intent.uri = pxsys_string_from_cstr("file:/photo.png");
    intent.mime_type = pxsys_string_from_cstr("image/png");
    assert(pxsys_intent_resolve(resolver, &intent, &resolved) == PXSYS_STATUS_OK);
    assert(pxsys_app_identity_equal(&resolved->identity, &preferred.identity));

    intent.target = &viewer.identity;
    assert(pxsys_intent_resolve(resolver, &intent, &resolved) == PXSYS_STATUS_OK);
    assert(pxsys_app_identity_equal(&resolved->identity, &viewer.identity));
    intent.target = &disabled.identity;
    assert(pxsys_intent_resolve(resolver, &intent, &resolved) == PXSYS_STATUS_DENIED);

    assert(pxsys_intent_filters_unregister(resolver, &viewer.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &viewer.identity) == PXSYS_STATUS_OK);
    assert(pxsys_intent_resolver_destroy(resolver) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &preferred.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &disabled.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

int main(void) {
    test_explicit_and_implicit_resolution();
    puts("intent tests passed");
    return 0;
}
