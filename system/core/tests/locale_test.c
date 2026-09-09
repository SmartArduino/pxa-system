#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/locale.h"

typedef struct {
    size_t calls;
    char tag[PXSYS_LOCALE_TAG_MAX_BYTES + 1u];
} observer_t;

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

static void changed(void* context, const pxsys_locale_snapshot_t* locale) {
    observer_t* observer = (observer_t*)context;
    observer->calls++;
    memcpy(observer->tag, locale->tag, locale->tag_size + 1u);
}

int main(void) {
    size_t allocations = 0;
    observer_t observer = {0};
    pxsys_locale_snapshot_t initial;
    pxsys_locale_snapshot_t next;
    pxsys_locale_snapshot_t current = {0};
    pxsys_locale_service_config_t config;
    pxsys_locale_service_t* service = NULL;

    assert(pxsys_locale_snapshot_init(
               &initial, pxsys_string_from_cstr("ZH-hans-cn")) ==
           PXSYS_STATUS_OK);
    assert(strcmp(initial.tag, "zh-Hans-CN") == 0);
    assert(initial.direction == PXSYS_TEXT_DIRECTION_LTR);
    assert(pxsys_locale_snapshot_init(
               &next, pxsys_string_from_cstr("ar-eg")) == PXSYS_STATUS_OK);
    assert(strcmp(next.tag, "ar-EG") == 0);
    assert(next.direction == PXSYS_TEXT_DIRECTION_RTL);
    next.direction = PXSYS_TEXT_DIRECTION_LTR;
    assert(pxsys_locale_snapshot_validate(&next) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    next.direction = PXSYS_TEXT_DIRECTION_RTL;
    assert(pxsys_locale_snapshot_init(
               &current, pxsys_string_from_cstr("bad_tag")) ==
           PXSYS_STATUS_INVALID_ARGUMENT);

    pxsys_locale_service_config_init(&config);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_locale_service_create(&config, &initial, &service) ==
           PXSYS_STATUS_OK);
    assert(pxsys_locale_service_subscribe(service, &observer, changed) ==
           PXSYS_STATUS_OK);
    assert(observer.calls == 1 && strcmp(observer.tag, "zh-Hans-CN") == 0);
    assert(pxsys_locale_service_update(service, &next) == PXSYS_STATUS_OK);
    assert(observer.calls == 2 && strcmp(observer.tag, "ar-EG") == 0);
    current.struct_size = sizeof(current);
    assert(pxsys_locale_service_get(service, &current) == PXSYS_STATUS_OK);
    assert(current.generation == 2 && strcmp(current.tag, "ar-EG") == 0);
    assert(pxsys_locale_service_unsubscribe(service, &observer, changed) ==
           PXSYS_STATUS_OK);
    assert(pxsys_locale_service_destroy(service) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("locale tests passed");
    return 0;
}
