#include "pxa_ipc.h"

#define REPLY_REQUEST UINT32_C(2)

static uint8_t ipc_payload[128];
static uint8_t packet[192];


int32_t pxa_app_start(const uint8_t* config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t* event, uint32_t length) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    pxa_event_t parsed;
    pxa_ipc_request_t request;

    if (!pxa_parse_event(event, length, &parsed) || !pxa_ipc_parse_request(&parsed, &request)) {
        return PXA_EVENT_UNHANDLED;
    }
    return pxa_ipc_reply(REPLY_REQUEST, request.call_id, PXA_STATUS_OK, pong, sizeof(pong),
                         ipc_payload, sizeof(ipc_payload), packet, sizeof(packet))
               ? PXA_EVENT_HANDLED
               : PXA_STATUS_INTERNAL;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
