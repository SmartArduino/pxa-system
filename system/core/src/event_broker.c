#include "pxsys/event_broker.h"

#include <string.h>

#define PXSYS_EVENT_BROKER_MAGIC UINT32_C(0x50584542)

typedef struct {
    pxsys_caller_t subscriber;
    pxsys_app_ref_t app_ref;
    pxsys_string_t topic;
    pxsys_version_t minimum_version;
    void* context;
    pxsys_topic_event_fn callback;
    void* strings;
    uint8_t occupied;
} subscription_t;

struct pxsys_event_broker {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_topic_bytes;
    size_t max_app_id_bytes;
    size_t max_component_id_bytes;
    uint8_t dispatching;
    pxsys_app_registry_t* apps;
    void* policy_context;
    pxsys_topic_policy_fn authorize;
    pxsys_allocator_t allocator;
    subscription_t* subscriptions;
};

static int broker_valid(const pxsys_event_broker_t* broker) {
    return broker != NULL && broker->magic == PXSYS_EVENT_BROKER_MAGIC;
}

static int string_equal(pxsys_string_t left, pxsys_string_t right) {
    return left.size == right.size && left.data != NULL && right.data != NULL &&
           memcmp(left.data, right.data, left.size) == 0;
}

static int add_size(size_t* total, size_t value) {
    if (*total > SIZE_MAX - value)
        return 0;
    *total += value;
    return 1;
}

static int caller_valid(const pxsys_event_broker_t* broker, const pxsys_caller_t* caller) {
    return caller != NULL && caller->struct_size >= sizeof(*caller) &&
           pxsys_app_identity_validate(&caller->app, broker->max_app_id_bytes) == PXSYS_STATUS_OK &&
           pxsys_identifier_validate(caller->component_id, broker->max_component_id_bytes);
}

void pxsys_event_broker_config_init(pxsys_event_broker_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_subscriptions = 64;
    config->max_topic_bytes = 128;
    config->max_app_id_bytes = 64;
    config->max_component_id_bytes = 64;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_event_broker_create(const pxsys_event_broker_config_t* config,
                                         pxsys_event_broker_t** output) {
    pxsys_event_broker_t* broker;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) || config->max_subscriptions == 0 ||
        config->max_subscriptions > SIZE_MAX / sizeof(subscription_t) ||
        config->max_topic_bytes == 0 || config->max_topic_bytes == SIZE_MAX ||
        config->max_app_id_bytes == 0 || config->max_app_id_bytes == SIZE_MAX ||
        config->max_component_id_bytes == 0 || config->max_component_id_bytes == SIZE_MAX ||
        config->apps == NULL || config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    broker = (pxsys_event_broker_t*)config->allocator.allocate(config->allocator.context,
                                                               sizeof(*broker));
    if (broker == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(broker, 0, sizeof(*broker));
    broker->subscriptions = (subscription_t*)config->allocator.allocate(
        config->allocator.context, config->max_subscriptions * sizeof(*broker->subscriptions));
    if (broker->subscriptions == NULL) {
        config->allocator.release(config->allocator.context, broker);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(broker->subscriptions, 0, config->max_subscriptions * sizeof(*broker->subscriptions));
    broker->capacity = config->max_subscriptions;
    broker->max_topic_bytes = config->max_topic_bytes;
    broker->max_app_id_bytes = config->max_app_id_bytes;
    broker->max_component_id_bytes = config->max_component_id_bytes;
    broker->apps = config->apps;
    broker->policy_context = config->policy_context;
    broker->authorize = config->authorize;
    broker->allocator = config->allocator;
    broker->magic = PXSYS_EVENT_BROKER_MAGIC;
    *output = broker;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_event_broker_destroy(pxsys_event_broker_t* broker) {
    pxsys_allocator_t allocator;
    size_t index;
    if (!broker_valid(broker))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (broker->dispatching)
        return PXSYS_STATUS_BUSY;
    allocator = broker->allocator;
    for (index = 0; index < broker->capacity; ++index) {
        subscription_t* entry = &broker->subscriptions[index];
        if (!entry->occupied)
            continue;
        (void)pxsys_app_registry_release(broker->apps, entry->app_ref);
        allocator.release(allocator.context, entry->strings);
    }
    broker->magic = 0;
    allocator.release(allocator.context, broker->subscriptions);
    allocator.release(allocator.context, broker);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_event_subscribe(pxsys_event_broker_t* broker, const pxsys_caller_t* subscriber,
                                     pxsys_string_t topic, pxsys_version_t minimum_version,
                                     void* context, pxsys_topic_event_fn callback) {
    subscription_t* entry;
    const pxsys_app_descriptor_t* app;
    pxsys_app_ref_t app_ref;
    pxsys_status_t status;
    char* cursor;
    size_t index;
    size_t bytes;
    if (!broker_valid(broker) || !caller_valid(broker, subscriber) || callback == NULL ||
        !pxsys_identifier_validate(topic, broker->max_topic_bytes) || broker->dispatching) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (broker->authorize != NULL &&
        (status = broker->authorize(broker->policy_context, PXSYS_TOPIC_POLICY_SUBSCRIBE,
                                    subscriber, topic)) != PXSYS_STATUS_OK) {
        return status;
    }
    for (index = 0; index < broker->capacity; ++index) {
        entry = &broker->subscriptions[index];
        if (entry->occupied && pxsys_app_identity_equal(&entry->subscriber.app, &subscriber->app) &&
            string_equal(entry->subscriber.component_id, subscriber->component_id) &&
            string_equal(entry->topic, topic) && entry->context == context &&
            entry->callback == callback) {
            return PXSYS_STATUS_ALREADY_EXISTS;
        }
    }
    if (broker->count == broker->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    status = pxsys_app_registry_acquire(broker->apps, &subscriber->app, &app_ref, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    if ((app->flags & PXSYS_APP_FLAG_ENABLED) == 0) {
        (void)pxsys_app_registry_release(broker->apps, app_ref);
        return PXSYS_STATUS_DENIED;
    }
    bytes = 0;
    if (!add_size(&bytes, subscriber->app.app_id.size) ||
        !add_size(&bytes, subscriber->component_id.size) || !add_size(&bytes, topic.size) ||
        !add_size(&bytes, 3u)) {
        (void)pxsys_app_registry_release(broker->apps, app_ref);
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    for (index = 0; index < broker->capacity; ++index) {
        if (!broker->subscriptions[index].occupied)
            break;
    }
    entry = &broker->subscriptions[index];
    entry->strings = broker->allocator.allocate(broker->allocator.context, bytes);
    if (entry->strings == NULL) {
        (void)pxsys_app_registry_release(broker->apps, app_ref);
        return PXSYS_STATUS_NO_MEMORY;
    }
    entry->subscriber = *subscriber;
    entry->subscriber.struct_size = sizeof(entry->subscriber);
    cursor = (char*)entry->strings;
#define COPY_FIELD(target, source)                      \
    do {                                                \
        memcpy(cursor, (source).data, (source).size);   \
        cursor[(source).size] = '\0';                   \
        (target) = pxsys_string(cursor, (source).size); \
        cursor += (source).size + 1u;                   \
    } while (0)
    COPY_FIELD(entry->subscriber.app.app_id, subscriber->app.app_id);
    COPY_FIELD(entry->subscriber.component_id, subscriber->component_id);
    COPY_FIELD(entry->topic, topic);
#undef COPY_FIELD
    entry->minimum_version = minimum_version;
    entry->context = context;
    entry->callback = callback;
    entry->app_ref = app_ref;
    entry->occupied = 1;
    broker->count++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_event_unsubscribe(pxsys_event_broker_t* broker,
                                       const pxsys_caller_t* subscriber, pxsys_string_t topic,
                                       void* context, pxsys_topic_event_fn callback) {
    size_t index;
    if (!broker_valid(broker) || !caller_valid(broker, subscriber) || callback == NULL ||
        broker->dispatching) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < broker->capacity; ++index) {
        subscription_t* entry = &broker->subscriptions[index];
        if (!entry->occupied ||
            !pxsys_app_identity_equal(&entry->subscriber.app, &subscriber->app) ||
            !string_equal(entry->subscriber.component_id, subscriber->component_id) ||
            !string_equal(entry->topic, topic) || entry->context != context ||
            entry->callback != callback) {
            continue;
        }
        (void)pxsys_app_registry_release(broker->apps, entry->app_ref);
        broker->allocator.release(broker->allocator.context, entry->strings);
        memset(entry, 0, sizeof(*entry));
        broker->count--;
        return PXSYS_STATUS_OK;
    }
    return PXSYS_STATUS_NOT_FOUND;
}

pxsys_status_t pxsys_event_publish(pxsys_event_broker_t* broker, const pxsys_topic_event_t* event,
                                   size_t* delivered) {
    pxsys_status_t status;
    size_t index;
    if (delivered == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *delivered = 0;
    if (!broker_valid(broker) || event == NULL || event->struct_size < sizeof(*event) ||
        !caller_valid(broker, event->publisher) ||
        !pxsys_identifier_validate(event->topic, broker->max_topic_bytes) ||
        (event->payload.size != 0 && event->payload.data == NULL) || broker->dispatching ||
        pxsys_app_registry_find(broker->apps, &event->publisher->app) == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (broker->authorize != NULL &&
        (status = broker->authorize(broker->policy_context, PXSYS_TOPIC_POLICY_PUBLISH,
                                    event->publisher, event->topic)) != PXSYS_STATUS_OK) {
        return status;
    }
    broker->dispatching = 1;
    for (index = 0; index < broker->capacity; ++index) {
        subscription_t* entry = &broker->subscriptions[index];
        if (entry->occupied && string_equal(entry->topic, event->topic) &&
            entry->minimum_version.major == event->version.major &&
            entry->minimum_version.minor <= event->version.minor) {
            entry->callback(entry->context, event);
            (*delivered)++;
        }
    }
    broker->dispatching = 0;
    return PXSYS_STATUS_OK;
}
