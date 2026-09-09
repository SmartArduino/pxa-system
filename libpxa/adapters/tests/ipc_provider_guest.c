/* Freestanding PXA IPC provider guest: registers nothing itself; the host
 * binds the endpoint. On an IPC request it replies with the payload echoed
 * prefixed by "ok:". No libc dependency. */

#include "pxa.h"
#include "pxa_ipc.h"

static uint32_t pxa_copy(uint8_t *destination, const uint8_t *source,
                         uint32_t length) {
    uint32_t index;
    for (index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
    return length;
}


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    pxa_ipc_request_t request;
    uint8_t reply_payload[128];
    uint8_t reply_packet[256];
    uint8_t reply_message[256];
    uint32_t payload_length;
    if (!pxa_parse_event(event, length, &parsed) ||
        parsed.service != PXA_SERVICE_IPC ||
        parsed.opcode != PXA_IPC_REQUEST ||
        !pxa_ipc_parse_request(&parsed, &request)) {
        return PXA_EVENT_UNHANDLED;
    }
    reply_payload[0] = 'o';
    reply_payload[1] = 'k';
    reply_payload[2] = ':';
    if (request.payload_length > sizeof(reply_payload) - 3) {
        return PXA_EVENT_UNHANDLED;
    }
    payload_length =
        pxa_copy(reply_payload + 3, request.payload, request.payload_length);
    if (!pxa_ipc_reply(1, request.call_id, PXA_STATUS_OK, reply_payload,
                       3 + payload_length, reply_packet,
                       sizeof(reply_packet), reply_message,
                       sizeof(reply_message))) {
        return PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_HANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
