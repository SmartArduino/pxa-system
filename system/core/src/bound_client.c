#include "pxsys/bound_client.h"

#include <string.h>

#define PXSYS_BOUND_CLIENT_MAGIC UINT32_C(0x50584243)

struct pxsys_bound_client {
    uint32_t magic;
    pxsys_caller_t caller;
    pxsys_task_manager_t* tasks;
    pxsys_service_registry_t* services;
    pxsys_event_broker_t* events;
    pxsys_allocator_t allocator;
    void* strings;
};

static int client_valid(const pxsys_bound_client_t* client) {
    return client != NULL && client->magic == PXSYS_BOUND_CLIENT_MAGIC;
}

static int add_size(size_t* total, size_t value) {
    if (*total > SIZE_MAX - value)
        return 0;
    *total += value;
    return 1;
}

pxsys_status_t pxsys_bound_client_create(const pxsys_bound_client_config_t* config,
                                         pxsys_bound_client_t** output) {
    pxsys_bound_client_t* client;
    size_t bytes = 0;
    char* cursor;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->caller.struct_size < sizeof(config->caller) ||
        pxsys_app_identity_validate(&config->caller.app, 64) != PXSYS_STATUS_OK ||
        !pxsys_identifier_validate(config->caller.component_id, 64) || config->tasks == NULL ||
        config->services == NULL || config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        !add_size(&bytes, config->caller.app.app_id.size) ||
        !add_size(&bytes, config->caller.component_id.size) || !add_size(&bytes, 2u)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    client = (pxsys_bound_client_t*)config->allocator.allocate(config->allocator.context,
                                                               sizeof(*client));
    if (client == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(client, 0, sizeof(*client));
    client->strings = config->allocator.allocate(config->allocator.context, bytes);
    if (client->strings == NULL) {
        config->allocator.release(config->allocator.context, client);
        return PXSYS_STATUS_NO_MEMORY;
    }
    client->caller = config->caller;
    client->caller.struct_size = sizeof(client->caller);
    cursor = (char*)client->strings;
    memcpy(cursor, config->caller.app.app_id.data, config->caller.app.app_id.size);
    cursor[config->caller.app.app_id.size] = '\0';
    client->caller.app.app_id = pxsys_string(cursor, config->caller.app.app_id.size);
    cursor += config->caller.app.app_id.size + 1u;
    memcpy(cursor, config->caller.component_id.data, config->caller.component_id.size);
    cursor[config->caller.component_id.size] = '\0';
    client->caller.component_id = pxsys_string(cursor, config->caller.component_id.size);
    client->tasks = config->tasks;
    client->services = config->services;
    client->events = config->events;
    client->allocator = config->allocator;
    client->magic = PXSYS_BOUND_CLIENT_MAGIC;
    *output = client;
    return PXSYS_STATUS_OK;
}

void pxsys_bound_client_destroy(pxsys_bound_client_t* client) {
    pxsys_allocator_t allocator;
    if (!client_valid(client))
        return;
    allocator = client->allocator;
    client->magic = 0;
    allocator.release(allocator.context, client->strings);
    allocator.release(allocator.context, client);
}

const pxsys_caller_t* pxsys_bound_client_caller(const pxsys_bound_client_t* client) {
    return client_valid(client) ? &client->caller : NULL;
}

pxsys_status_t pxsys_bound_client_start(pxsys_bound_client_t* client, const pxsys_intent_t* intent,
                                        pxsys_instance_ref_t* instance) {
    if (!client_valid(client))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    return pxsys_task_manager_start_as(client->tasks, &client->caller, intent, instance);
}

pxsys_status_t pxsys_bound_client_invoke(pxsys_bound_client_t* client,
                                         const pxsys_service_request_t* request,
                                         void* completion_context,
                                         pxsys_service_complete_fn complete) {
    pxsys_service_request_t bound;
    if (!client_valid(client) || request == NULL || request->struct_size < sizeof(*request))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    bound = *request;
    bound.struct_size = sizeof(bound);
    bound.caller = &client->caller;
    return pxsys_service_invoke(client->services, &bound, completion_context, complete);
}

pxsys_status_t pxsys_bound_client_subscribe(pxsys_bound_client_t* client, pxsys_string_t topic,
                                            pxsys_version_t minimum_version, void* context,
                                            pxsys_topic_event_fn callback) {
    if (!client_valid(client))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (client->events == NULL)
        return PXSYS_STATUS_UNSUPPORTED;
    return pxsys_event_subscribe(client->events, &client->caller, topic, minimum_version, context,
                                 callback);
}

pxsys_status_t pxsys_bound_client_unsubscribe(pxsys_bound_client_t* client, pxsys_string_t topic,
                                              void* context, pxsys_topic_event_fn callback) {
    if (!client_valid(client))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (client->events == NULL)
        return PXSYS_STATUS_UNSUPPORTED;
    return pxsys_event_unsubscribe(client->events, &client->caller, topic, context, callback);
}

pxsys_status_t pxsys_bound_client_publish(pxsys_bound_client_t* client,
                                          const pxsys_topic_event_t* event, size_t* delivered) {
    pxsys_topic_event_t bound;
    if (!client_valid(client) || event == NULL || event->struct_size < sizeof(*event))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (client->events == NULL)
        return PXSYS_STATUS_UNSUPPORTED;
    bound = *event;
    bound.struct_size = sizeof(bound);
    bound.publisher = &client->caller;
    return pxsys_event_publish(client->events, &bound, delivered);
}
