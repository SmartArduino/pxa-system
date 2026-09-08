#include "pxsys/pxa_client.h"

pxsys_status_t pxsys_pxa_client_create(const pxsys_pxa_client_config_t* config,
                                       pxsys_pxa_client_t** output) {
    pxsys_bound_client_config_t bound = {0};
    pxsys_status_t status;
    if (config == NULL || config->struct_size < sizeof(*config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    bound.struct_size = sizeof(bound);
    status = pxsys_pxa_manifest_caller(config->manifest, config->component_id, &bound.caller);
    if (status != PXSYS_STATUS_OK)
        return status;
    bound.tasks = config->tasks;
    bound.services = config->services;
    bound.events = config->events;
    bound.allocator = config->allocator;
    return pxsys_bound_client_create(&bound, output);
}

void pxsys_pxa_client_destroy(pxsys_pxa_client_t* client) { pxsys_bound_client_destroy(client); }

const pxsys_caller_t* pxsys_pxa_client_caller(const pxsys_pxa_client_t* client) {
    return pxsys_bound_client_caller(client);
}

pxsys_status_t pxsys_pxa_client_start(pxsys_pxa_client_t* client, const pxsys_intent_t* intent,
                                      pxsys_instance_ref_t* instance) {
    return pxsys_bound_client_start(client, intent, instance);
}

pxsys_status_t pxsys_pxa_client_invoke(pxsys_pxa_client_t* client,
                                       const pxsys_service_request_t* request,
                                       void* completion_context,
                                       pxsys_service_complete_fn complete) {
    return pxsys_bound_client_invoke(client, request, completion_context, complete);
}

pxsys_status_t pxsys_pxa_client_subscribe(pxsys_pxa_client_t* client, pxsys_string_t topic,
                                          pxsys_version_t minimum_version, void* context,
                                          pxsys_topic_event_fn callback) {
    return pxsys_bound_client_subscribe(client, topic, minimum_version, context, callback);
}

pxsys_status_t pxsys_pxa_client_unsubscribe(pxsys_pxa_client_t* client, pxsys_string_t topic,
                                            void* context, pxsys_topic_event_fn callback) {
    return pxsys_bound_client_unsubscribe(client, topic, context, callback);
}

pxsys_status_t pxsys_pxa_client_publish(pxsys_pxa_client_t* client,
                                        const pxsys_topic_event_t* event, size_t* delivered) {
    return pxsys_bound_client_publish(client, event, delivered);
}
