#ifndef PXSYS_PXA_GATEWAY_WIRE_H
#define PXSYS_PXA_GATEWAY_WIRE_H

#include "pxa/wire.h"
#include "pxsys/event_broker.h"
#include "pxsys/service_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_PXA_GATEWAY_SERVICE_ID UINT16_C(17)
#define PXSYS_PXA_GATEWAY_INTENT_START UINT16_C(1)
#define PXSYS_PXA_GATEWAY_SERVICE_INVOKE UINT16_C(2)
#define PXSYS_PXA_GATEWAY_TOPIC_PUBLISH UINT16_C(3)
#define PXSYS_PXA_GATEWAY_TOPIC_SUBSCRIBE UINT16_C(4)
#define PXSYS_PXA_GATEWAY_TOPIC_UNSUBSCRIBE UINT16_C(5)
#define PXSYS_PXA_GATEWAY_SERVICE_REGISTER UINT16_C(6)
#define PXSYS_PXA_GATEWAY_SERVICE_UNREGISTER UINT16_C(7)
#define PXSYS_PXA_GATEWAY_SERVICE_COMPLETE UINT16_C(8)
#define PXSYS_PXA_GATEWAY_TOPIC_EVENT UINT16_C(0x8001)
#define PXSYS_PXA_GATEWAY_SERVICE_REQUEST UINT16_C(0x8002)

typedef struct {
    pxsys_string_t interface_id;
    pxsys_version_t version;
    uint64_t features;
} pxsys_pxa_service_descriptor_t;

typedef struct {
    uint64_t call_id;
    pxa_status_t status;
    pxsys_bytes_t payload;
} pxsys_pxa_service_completion_t;

pxsys_status_t pxsys_pxa_gateway_decode_service(pxa_bytes_t payload,
                                                pxsys_service_request_t* output);
pxsys_status_t pxsys_pxa_gateway_decode_topic(pxa_bytes_t payload, pxsys_topic_event_t* output);
pxsys_status_t pxsys_pxa_gateway_encode_topic(const pxsys_topic_event_t* event, uint8_t* output,
                                              size_t capacity, size_t* output_size);
pxsys_status_t pxsys_pxa_gateway_decode_service_descriptor(pxa_bytes_t payload,
                                                           pxsys_pxa_service_descriptor_t* output);
pxsys_status_t pxsys_pxa_gateway_encode_service_request(const pxsys_service_request_t* request,
                                                        uint64_t call_id, uint8_t* output,
                                                        size_t capacity, size_t* output_size);
pxsys_status_t pxsys_pxa_gateway_decode_service_completion(pxa_bytes_t payload,
                                                           pxsys_pxa_service_completion_t* output);

#ifdef __cplusplus
}
#endif

#endif
