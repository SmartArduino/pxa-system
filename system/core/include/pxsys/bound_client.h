#ifndef PXSYS_BOUND_CLIENT_H
#define PXSYS_BOUND_CLIENT_H

#include <stdint.h>

#include "pxsys/event_broker.h"
#include "pxsys/service_registry.h"
#include "pxsys/task_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    pxsys_caller_t caller;
    pxsys_task_manager_t* tasks;
    pxsys_service_registry_t* services;
    pxsys_event_broker_t* events;
    pxsys_allocator_t allocator;
} pxsys_bound_client_config_t;

typedef struct pxsys_bound_client pxsys_bound_client_t;

pxsys_status_t pxsys_bound_client_create(const pxsys_bound_client_config_t* config,
                                         pxsys_bound_client_t** output);
void pxsys_bound_client_destroy(pxsys_bound_client_t* client);
const pxsys_caller_t* pxsys_bound_client_caller(const pxsys_bound_client_t* client);
pxsys_status_t pxsys_bound_client_start(pxsys_bound_client_t* client, const pxsys_intent_t* intent,
                                        pxsys_instance_ref_t* instance);
pxsys_status_t pxsys_bound_client_invoke(pxsys_bound_client_t* client,
                                         const pxsys_service_request_t* request,
                                         void* completion_context,
                                         pxsys_service_complete_fn complete);
pxsys_status_t pxsys_bound_client_subscribe(pxsys_bound_client_t* client, pxsys_string_t topic,
                                            pxsys_version_t minimum_version, void* context,
                                            pxsys_topic_event_fn callback);
pxsys_status_t pxsys_bound_client_unsubscribe(pxsys_bound_client_t* client, pxsys_string_t topic,
                                              void* context, pxsys_topic_event_fn callback);
pxsys_status_t pxsys_bound_client_publish(pxsys_bound_client_t* client,
                                          const pxsys_topic_event_t* event, size_t* delivered);

#ifdef __cplusplus
}
#endif

#endif
