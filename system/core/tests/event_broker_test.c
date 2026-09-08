#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/event_broker.h"

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

static pxsys_caller_t caller(pxsys_app_identity_t app_identity) {
    pxsys_caller_t value = {0};
    value.struct_size = sizeof(value);
    value.app = app_identity;
    value.component_id = pxsys_string_from_cstr("main");
    return value;
}

static void receive(void* context, const pxsys_topic_event_t* event) {
    size_t* count = (size_t*)context;
    assert(event->publisher != NULL && event->event == 3);
    (*count)++;
}

int main(void) {
    size_t allocations = 0;
    size_t received = 0;
    size_t delivered = 0;
    pxsys_app_registry_config_t app_config;
    pxsys_event_broker_config_t broker_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_event_broker_t* broker = NULL;
    pxsys_app_descriptor_t publisher_app = app(1, "sensor-host");
    pxsys_app_descriptor_t subscriber_app = app(2, "dashboard");
    pxsys_caller_t publisher = caller(publisher_app.identity);
    pxsys_caller_t subscriber = caller(subscriber_app.identity);
    pxsys_topic_event_t event = {0};

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&allocations);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &publisher_app) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &subscriber_app) == PXSYS_STATUS_OK);
    pxsys_event_broker_config_init(&broker_config);
    broker_config.apps = apps;
    broker_config.allocator = allocator(&allocations);
    assert(pxsys_event_broker_create(&broker_config, &broker) == PXSYS_STATUS_OK);
    assert(pxsys_event_subscribe(broker, &subscriber,
                                 pxsys_string_from_cstr("vendor.example.sensor.changed"),
                                 (pxsys_version_t){1, 0}, &received, receive) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &subscriber_app.identity) == PXSYS_STATUS_BUSY);
    event.struct_size = sizeof(event);
    event.topic = pxsys_string_from_cstr("vendor.example.sensor.changed");
    event.version = (pxsys_version_t){1, 1};
    event.event = 3;
    event.publisher = &publisher;
    assert(pxsys_event_publish(broker, &event, &delivered) == PXSYS_STATUS_OK);
    assert(received == 1 && delivered == 1);
    assert(pxsys_event_unsubscribe(broker, &subscriber, event.topic, &received, receive) ==
           PXSYS_STATUS_OK);
    assert(pxsys_event_broker_destroy(broker) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &subscriber_app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &publisher_app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("event broker tests passed");
    return 0;
}
