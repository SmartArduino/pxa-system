#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/toast.h"

static void* allocate(void* context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void posted(void* context, const pxsys_toast_message_t* toast) {
    unsigned* count = (unsigned*)context;
    assert(toast->sequence == *count + 1u);
    assert(toast->message.size == 5);
    (*count)++;
}

int main(void) {
    pxsys_toast_service_config_t config;
    pxsys_toast_service_t* service = NULL;
    pxsys_toast_message_t toast;
    pxsys_toast_message_t decoded;
    uint8_t wire[32];
    size_t written = 0;
    unsigned notifications = 0;
    pxsys_toast_service_config_init(&config);
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_toast_service_create(&config, &service) == PXSYS_STATUS_OK);
    assert(pxsys_toast_service_subscribe(service, &notifications, posted) ==
           PXSYS_STATUS_OK);
    pxsys_toast_message_init(&toast, pxsys_string_from_cstr("Hello"));
    toast.tone = PXSYS_TOAST_SUCCESS;
    assert(pxsys_toast_service_post(service, &toast) == PXSYS_STATUS_OK);
    assert(notifications == 1);
    assert(pxsys_toast_wire_encode(&toast, wire, sizeof(wire), &written) ==
           PXSYS_STATUS_OK);
    assert(pxsys_toast_wire_decode((pxsys_bytes_t){wire, written}, &decoded) ==
           PXSYS_STATUS_OK);
    assert(decoded.tone == PXSYS_TOAST_SUCCESS &&
           decoded.duration_ms == toast.duration_ms &&
           decoded.message.size == toast.message.size &&
           memcmp(decoded.message.data, toast.message.data, toast.message.size) == 0);
    assert(pxsys_toast_service_unsubscribe(service, &notifications, posted) ==
           PXSYS_STATUS_OK);
    assert(pxsys_toast_service_destroy(service) == PXSYS_STATUS_OK);
    return 0;
}
