#include "pxsys/native_client.h"

pxsys_status_t pxsys_native_client_create(const pxsys_native_client_config_t* config,
                                          pxsys_native_client_t** output) {
    pxsys_bound_client_config_t bound = {0};
    if (config == NULL || config->struct_size < sizeof(*config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    bound.struct_size = sizeof(bound);
    bound.caller.struct_size = sizeof(bound.caller);
    bound.caller.app = config->app;
    bound.caller.component_id = config->component_id;
    bound.tasks = config->tasks;
    bound.services = config->services;
    bound.events = config->events;
    bound.allocator = config->allocator;
    return pxsys_bound_client_create(&bound, output);
}

void pxsys_native_client_destroy(pxsys_native_client_t* client) {
    pxsys_bound_client_destroy(client);
}

const pxsys_caller_t* pxsys_native_client_caller(const pxsys_native_client_t* client) {
    return pxsys_bound_client_caller(client);
}

pxsys_status_t pxsys_native_client_start(pxsys_native_client_t* client,
                                         const pxsys_intent_t* intent,
                                         pxsys_instance_ref_t* instance) {
    return pxsys_bound_client_start(client, intent, instance);
}

pxsys_status_t pxsys_native_client_invoke(pxsys_native_client_t* client,
                                          const pxsys_service_request_t* request,
                                          void* completion_context,
                                          pxsys_service_complete_fn complete) {
    return pxsys_bound_client_invoke(client, request, completion_context, complete);
}

pxsys_status_t pxsys_native_client_subscribe(pxsys_native_client_t* client, pxsys_string_t topic,
                                             pxsys_version_t minimum_version, void* context,
                                             pxsys_topic_event_fn callback) {
    return pxsys_bound_client_subscribe(client, topic, minimum_version, context, callback);
}

pxsys_status_t pxsys_native_client_unsubscribe(pxsys_native_client_t* client, pxsys_string_t topic,
                                               void* context, pxsys_topic_event_fn callback) {
    return pxsys_bound_client_unsubscribe(client, topic, context, callback);
}

pxsys_status_t pxsys_native_client_publish(pxsys_native_client_t* client,
                                           const pxsys_topic_event_t* event, size_t* delivered) {
    return pxsys_bound_client_publish(client, event, delivered);
}
