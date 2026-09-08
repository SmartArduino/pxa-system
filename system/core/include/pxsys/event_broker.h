#ifndef PXSYS_EVENT_BROKER_H
#define PXSYS_EVENT_BROKER_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    pxsys_string_t topic;
    pxsys_version_t version;
    uint32_t event;
    uint64_t sequence;
    pxsys_bytes_t payload;
    const pxsys_caller_t* publisher;
} pxsys_topic_event_t;

typedef void (*pxsys_topic_event_fn)(void* context, const pxsys_topic_event_t* event);

typedef enum {
    PXSYS_TOPIC_POLICY_SUBSCRIBE = 1,
    PXSYS_TOPIC_POLICY_PUBLISH = 2,
} pxsys_topic_policy_action_t;

typedef pxsys_status_t (*pxsys_topic_policy_fn)(void* context, pxsys_topic_policy_action_t action,
                                                const pxsys_caller_t* actor, pxsys_string_t topic);

typedef struct {
    uint32_t struct_size;
    size_t max_subscriptions;
    size_t max_topic_bytes;
    size_t max_app_id_bytes;
    size_t max_component_id_bytes;
    pxsys_app_registry_t* apps;
    void* policy_context;
    pxsys_topic_policy_fn authorize;
    pxsys_allocator_t allocator;
} pxsys_event_broker_config_t;

typedef struct pxsys_event_broker pxsys_event_broker_t;

void pxsys_event_broker_config_init(pxsys_event_broker_config_t* config);
pxsys_status_t pxsys_event_broker_create(const pxsys_event_broker_config_t* config,
                                         pxsys_event_broker_t** output);
pxsys_status_t pxsys_event_broker_destroy(pxsys_event_broker_t* broker);
pxsys_status_t pxsys_event_subscribe(pxsys_event_broker_t* broker, const pxsys_caller_t* subscriber,
                                     pxsys_string_t topic, pxsys_version_t minimum_version,
                                     void* context, pxsys_topic_event_fn callback);
pxsys_status_t pxsys_event_unsubscribe(pxsys_event_broker_t* broker,
                                       const pxsys_caller_t* subscriber, pxsys_string_t topic,
                                       void* context, pxsys_topic_event_fn callback);
pxsys_status_t pxsys_event_publish(pxsys_event_broker_t* broker, const pxsys_topic_event_t* event,
                                   size_t* delivered);

#ifdef __cplusplus
}
#endif

#endif
