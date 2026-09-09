#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/standard_system.h"

typedef struct {
    size_t outstanding;
    size_t calls;
    size_t fail_at;
} failing_allocator_t;

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

static void* failing_allocate(void* context, size_t size) {
    failing_allocator_t* allocator = (failing_allocator_t*)context;
    void* memory;
    if (allocator->calls++ == allocator->fail_at)
        return NULL;
    memory = malloc(size);
    if (memory != NULL)
        allocator->outstanding++;
    return memory;
}

static void failing_release(void* context, void* memory) {
    failing_allocator_t* allocator = (failing_allocator_t*)context;
    if (memory != NULL)
        allocator->outstanding--;
    free(memory);
}

static void service_completed(void* context, uint64_t request_id,
                              pxsys_status_t status, pxsys_bytes_t payload) {
    unsigned* completed = (unsigned*)context;
    assert(request_id == 7);
    assert(status == PXSYS_STATUS_OK);
    assert(payload.size == 0);
    (*completed)++;
}

static void test_allocation_rollback(void) {
    size_t fail_at;
    for (fail_at = 0; fail_at < 32; ++fail_at) {
        failing_allocator_t allocator = {0};
        pxsys_standard_system_config_t config;
        pxsys_standard_system_t* system = NULL;
        pxsys_status_t status;
        allocator.fail_at = fail_at;
        pxsys_standard_system_config_init(&config);
        config.allocator.context = &allocator;
        config.allocator.allocate = failing_allocate;
        config.allocator.release = failing_release;
        status = pxsys_standard_system_create(&config, &system);
        if (status == PXSYS_STATUS_OK) {
            assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
        } else {
            assert(status == PXSYS_STATUS_NO_MEMORY);
            assert(system == NULL);
        }
        assert(allocator.outstanding == 0);
    }
}

int main(void) {
    size_t allocations = 0;
    pxsys_standard_system_config_t config;
    pxsys_standard_system_t* system = NULL;
    pxsys_theme_snapshot_t theme = {0};
    pxsys_display_profile_t display = {0};
    pxsys_system_status_snapshot_t status_snapshot = {0};
    pxsys_toast_message_t toast;
    pxsys_service_request_t request = {0};
    pxsys_caller_t caller = {0};
    pxsys_app_identity_t caller_app = {0};
    uint8_t toast_wire[64];
    size_t toast_wire_size = 0;
    unsigned service_completions = 0;

    pxsys_standard_system_config_init(&config);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_standard_system_create(&config, &system) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_apps(system) != NULL);
    assert(pxsys_standard_system_runtime(system) != NULL);
    assert(pxsys_standard_system_native_runtime(system) != NULL);
    assert(pxsys_standard_system_intents(system) != NULL);
    assert(pxsys_standard_system_tasks(system) != NULL);
    assert(pxsys_standard_system_roles(system) != NULL);
    assert(pxsys_standard_system_role_host(system) != NULL);
    assert(pxsys_standard_system_renderer(system) != NULL);
    assert(pxsys_standard_system_services(system) != NULL);
    assert(pxsys_standard_system_events(system) != NULL);
    assert(pxsys_standard_system_toasts(system) != NULL);
    assert(pxsys_standard_system_locale(system) != NULL);
    assert(pxsys_standard_system_resources(system) != NULL);
    display.struct_size = sizeof(display);
    assert(pxsys_display_service_get(pxsys_standard_system_display(system),
                                     &display) == PXSYS_STATUS_OK);
    assert(display.width == 320 && display.height == 240);
    status_snapshot.struct_size = sizeof(status_snapshot);
    assert(pxsys_system_status_service_get(pxsys_standard_system_status(system),
                                           &status_snapshot) == PXSYS_STATUS_OK);
    pxsys_toast_message_init(&toast, pxsys_string_from_cstr("Standard toast"));
    assert(pxsys_toast_wire_encode(&toast, toast_wire, sizeof(toast_wire),
                                   &toast_wire_size) == PXSYS_STATUS_OK);
    memset(caller_app.publisher_root, 0x31, sizeof(caller_app.publisher_root));
    caller_app.app_id = pxsys_string_from_cstr("test.client");
    caller.struct_size = sizeof(caller);
    caller.app = caller_app;
    caller.component_id = pxsys_string_from_cstr("main");
    request.struct_size = sizeof(request);
    request.interface_id = pxsys_string_from_cstr(PXSYS_TOAST_INTERFACE_ID);
    request.version = (pxsys_version_t){1, 0};
    request.operation = PXSYS_TOAST_OPERATION_POST;
    request.request_id = 7;
    request.payload = (pxsys_bytes_t){toast_wire, toast_wire_size};
    request.caller = &caller;
    assert(pxsys_service_invoke(pxsys_standard_system_services(system), &request,
                                &service_completions, service_completed) ==
           PXSYS_STATUS_OK);
    assert(service_completions == 1);
    theme.struct_size = sizeof(theme);
    assert(pxsys_theme_service_get(pxsys_standard_system_theme(system), &theme) == PXSYS_STATUS_OK);
    assert(theme.effective_scheme == PXSYS_COLOR_SCHEME_LIGHT);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    test_allocation_rollback();
    puts("standard system tests passed");
    return 0;
}
