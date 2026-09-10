#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/app_registry.h"

static void* test_allocate(void* context, size_t size) {
    size_t* allocations = (size_t*)context;
    (*allocations)++;
    return malloc(size);
}

static void test_release(void* context, void* memory) {
    size_t* allocations = (size_t*)context;
    if (memory != NULL)
        (*allocations)--;
    free(memory);
}

static pxsys_app_identity_t identity(uint8_t publisher_byte, const char* app_id) {
    pxsys_app_identity_t result;
    memset(&result, publisher_byte, sizeof(result.publisher_root));
    result.app_id = pxsys_string_from_cstr(app_id);
    return result;
}

static pxsys_app_descriptor_t descriptor(uint8_t publisher_byte, const char* app_id,
                                         const char* display_name, const char* runtime_id) {
    pxsys_app_descriptor_t result;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.identity = identity(publisher_byte, app_id);
    result.display_name = pxsys_string_from_cstr(display_name);
    result.version = pxsys_string_from_cstr("1.0.0");
    result.runtime_id = pxsys_string_from_cstr(runtime_id);
    result.flags = PXSYS_APP_FLAG_ENABLED;
    return result;
}

static pxsys_app_registry_t* create_registry(size_t capacity, size_t* allocations) {
    pxsys_app_registry_config_t config;
    pxsys_app_registry_t* registry = NULL;
    pxsys_app_registry_config_init(&config);
    config.max_apps = capacity;
    config.allocator.context = allocations;
    config.allocator.allocate = test_allocate;
    config.allocator.release = test_release;
    assert(pxsys_app_registry_create(&config, &registry) == PXSYS_STATUS_OK);
    assert(registry != NULL);
    return registry;
}

static void test_identity_namespace_and_deep_copy(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(4, &allocations);
    char app_id[] = "camera";
    char name[] = "Camera";
    char description[] = "Take photos";
    char icon[] = "assets/camera.png";
    pxsys_app_descriptor_t native = descriptor(0x11, app_id, name, "native-static");
    pxsys_app_descriptor_t pxa = descriptor(0x22, app_id, "Camera PXA", "wamr-aot");
    const pxsys_app_descriptor_t* stored;

    native.description = pxsys_string_from_cstr(description);
    native.icon_reference = pxsys_string_from_cstr(icon);
    native.resource_namespace = pxsys_string_from_cstr("app.camera");
    native.display_name_resource_key = pxsys_string_from_cstr("metadata.name");
    native.description_resource_key =
        pxsys_string_from_cstr("metadata.description");
    native.icon_resource_key = pxsys_string_from_cstr("metadata.icon");

    assert(pxsys_app_registry_register(registry, &native) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_generation(registry) == 2);
    assert(pxsys_app_registry_register(registry, &pxa) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_generation(registry) == 3);
    app_id[0] = 'x';
    name[0] = 'X';
    description[0] = 'X';
    icon[0] = 'X';

    stored = pxsys_app_registry_find(registry, &native.identity);
    assert(stored == NULL);
    {
        pxsys_app_identity_t original = identity(0x11, "camera");
        stored = pxsys_app_registry_find(registry, &original);
    }
    assert(stored != NULL);
    assert(stored->identity.app_id.size == 6);
    assert(memcmp(stored->identity.app_id.data, "camera", 6) == 0);
    assert(memcmp(stored->display_name.data, "Camera", 6) == 0);
    assert(stored->description.size == strlen("Take photos") &&
           memcmp(stored->description.data, "Take photos",
                  stored->description.size) == 0);
    assert(stored->icon_reference.size == strlen("assets/camera.png") &&
           memcmp(stored->icon_reference.data, "assets/camera.png",
                  stored->icon_reference.size) == 0);
    assert(stored->resource_namespace.size == strlen("app.camera") &&
           memcmp(stored->resource_namespace.data, "app.camera",
                  stored->resource_namespace.size) == 0);
    assert(memcmp(stored->runtime_id.data, "native-static", 13) == 0);
    assert(pxsys_app_registry_count(registry) == 2);

    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_duplicate_capacity_and_remove(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(2, &allocations);
    pxsys_app_descriptor_t first = descriptor(1, "settings", "Settings", "native-static");
    pxsys_app_descriptor_t same = descriptor(1, "settings", "Other", "wamr-aot");
    pxsys_app_descriptor_t second = descriptor(1, "home", "Home", "wamr-aot");
    pxsys_app_descriptor_t third = descriptor(1, "clock", "Clock", "wamr-aot");

    assert(pxsys_app_registry_register(registry, &first) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(registry, &same) == PXSYS_STATUS_ALREADY_EXISTS);
    assert(pxsys_app_registry_register(registry, &second) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(registry, &third) == PXSYS_STATUS_RESOURCE_LIMIT);
    assert(pxsys_app_registry_unregister(registry, &first.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_generation(registry) == 4);
    assert(pxsys_app_registry_find(registry, &second.identity) != NULL);
    assert(pxsys_app_registry_register(registry, &third) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(registry, &first.identity) == PXSYS_STATUS_NOT_FOUND);

    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_validation(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(2, &allocations);
    pxsys_app_descriptor_t invalid_id = descriptor(1, "Bad/App", "Bad", "native-static");
    pxsys_app_descriptor_t invalid_name = descriptor(1, "bad", "\xC0\xAF", "native-static");
    pxsys_app_descriptor_t valid_utf8 =
        descriptor(1, "weather", "\xE5\xA4\xA9\xE6\xB0\x94", "wamr-aot");
    pxsys_app_descriptor_t key_without_namespace =
        descriptor(1, "files", "Files", "native-static");
    pxsys_app_descriptor_t invalid_description =
        descriptor(1, "about", "About", "native-static");

    key_without_namespace.display_name_resource_key =
        pxsys_string_from_cstr("metadata.name");
    invalid_description.description = pxsys_string("bad\ntext", 8);

    assert(pxsys_app_registry_register(registry, &invalid_id) == PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_app_registry_register(registry, &invalid_name) == PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_app_registry_register(registry, &key_without_namespace) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_app_registry_register(registry, &invalid_description) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_app_registry_register(registry, &valid_utf8) == PXSYS_STATUS_OK);

    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_v1_descriptor_compatibility(void) {
    typedef struct {
        uint32_t struct_size;
        pxsys_app_identity_t identity;
        pxsys_string_t display_name;
        pxsys_string_t version;
        pxsys_string_t runtime_id;
        uint32_t flags;
    } v1_descriptor_t;
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(1, &allocations);
    v1_descriptor_t old = {0};
    const pxsys_app_descriptor_t* stored;

    assert(sizeof(old) == PXSYS_APP_DESCRIPTOR_V1_SIZE);
    old.struct_size = sizeof(old);
    old.identity = identity(9, "legacy");
    old.display_name = pxsys_string_from_cstr("Legacy");
    old.version = pxsys_string_from_cstr("1.0.0");
    old.runtime_id = pxsys_string_from_cstr("native-static");
    old.flags = PXSYS_APP_FLAG_ENABLED;
    assert(pxsys_app_registry_register(
               registry, (const pxsys_app_descriptor_t*)&old) ==
           PXSYS_STATUS_OK);
    stored = pxsys_app_registry_at(registry, 0);
    assert(stored != NULL && stored->struct_size == sizeof(*stored));
    assert(stored->description.size == 0);
    assert(stored->icon_reference.size == 0);
    assert(stored->resource_namespace.size == 0);
    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_acquired_entry_blocks_removal(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(2, &allocations);
    pxsys_app_descriptor_t app = descriptor(3, "viewer", "Viewer", "wamr-aot");
    pxsys_app_ref_t reference;
    const pxsys_app_descriptor_t* acquired = NULL;

    assert(pxsys_app_registry_register(registry, &app) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_acquire(registry, &app.identity, &reference, &acquired) ==
           PXSYS_STATUS_OK);
    assert(acquired != NULL);
    assert(pxsys_app_registry_get(registry, reference) == acquired);
    assert(pxsys_app_registry_unregister(registry, &app.identity) == PXSYS_STATUS_BUSY);
    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_BUSY);
    assert(pxsys_app_registry_release(registry, reference) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(registry, &app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_get(registry, reference) == NULL);
    assert(pxsys_app_registry_release(registry, reference) == PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_atomic_metadata_update(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* registry = create_registry(2, &allocations);
    pxsys_app_descriptor_t initial = descriptor(7, "weather", "Weather", "wamr-aot");
    pxsys_app_descriptor_t update = descriptor(7, "weather", "Weather 2", "wamr-wasm");
    pxsys_app_descriptor_t missing = descriptor(7, "clock", "Clock", "wamr-aot");
    pxsys_app_ref_t reference;
    const pxsys_app_descriptor_t* acquired = NULL;
    const pxsys_app_descriptor_t* stored;

    update.version = pxsys_string_from_cstr("2.0.0");
    assert(pxsys_app_registry_register(registry, &initial) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_acquire(registry, &initial.identity, &reference, &acquired) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_registry_update(registry, &update) == PXSYS_STATUS_BUSY);
    assert(pxsys_app_registry_release(registry, reference) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_update(registry, &update) == PXSYS_STATUS_OK);
    stored = pxsys_app_registry_find(registry, &initial.identity);
    assert(stored != NULL && stored->version.size == 5 &&
           memcmp(stored->version.data, "2.0.0", 5) == 0);
    assert(stored->runtime_id.size == 9 && memcmp(stored->runtime_id.data, "wamr-wasm", 9) == 0);
    assert(pxsys_app_registry_update(registry, &missing) == PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_app_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

int main(void) {
    test_identity_namespace_and_deep_copy();
    test_duplicate_capacity_and_remove();
    test_validation();
    test_v1_descriptor_compatibility();
    test_acquired_entry_blocks_removal();
    test_atomic_metadata_update();
    puts("pxa_system_core app registry tests passed");
    return 0;
}
