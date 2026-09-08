#ifndef PXSYS_NATIVE_CLIENT_H
#define PXSYS_NATIVE_CLIENT_H

#include <stdint.h>

#include "pxsys/bound_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    pxsys_app_identity_t app;
    pxsys_string_t component_id;
    pxsys_task_manager_t* tasks;
    pxsys_service_registry_t* services;
    pxsys_event_broker_t* events;
    pxsys_allocator_t allocator;
} pxsys_native_client_config_t;

typedef pxsys_bound_client_t pxsys_native_client_t;

pxsys_status_t pxsys_native_client_create(const pxsys_native_client_config_t* config,
                                          pxsys_native_client_t** output);
void pxsys_native_client_destroy(pxsys_native_client_t* client);
const pxsys_caller_t* pxsys_native_client_caller(const pxsys_native_client_t* client);
pxsys_status_t pxsys_native_client_start(pxsys_native_client_t* client,
                                         const pxsys_intent_t* intent,
                                         pxsys_instance_ref_t* instance);
pxsys_status_t pxsys_native_client_invoke(pxsys_native_client_t* client,
                                          const pxsys_service_request_t* request,
                                          void* completion_context,
                                          pxsys_service_complete_fn complete);
pxsys_status_t pxsys_native_client_subscribe(pxsys_native_client_t* client, pxsys_string_t topic,
                                             pxsys_version_t minimum_version, void* context,
                                             pxsys_topic_event_fn callback);
pxsys_status_t pxsys_native_client_unsubscribe(pxsys_native_client_t* client, pxsys_string_t topic,
                                               void* context, pxsys_topic_event_fn callback);
pxsys_status_t pxsys_native_client_publish(pxsys_native_client_t* client,
                                           const pxsys_topic_event_t* event, size_t* delivered);

#ifdef __cplusplus
}
#endif

#endif
