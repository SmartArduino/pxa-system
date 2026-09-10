#include <string.h>

#include "pxa_ipc.h"
#include "view.h"

#define CALL_REQUEST UINT32_C(1)

static uint8_t ipc_payload[128];
static uint8_t packet[192];
static uint8_t call_started;

static int call_responder(void) {
    static const char endpoint[] = "cmake.echo";
    static const uint8_t ping[] = {'p', 'i', 'n', 'g'};
    return pxa_ipc_call(CALL_REQUEST, endpoint, sizeof(endpoint) - 1u, ping, sizeof(ping),
                        ipc_payload, sizeof(ipc_payload), packet, sizeof(packet));
}


int32_t pxa_app_start(const uint8_t* config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return pxa_window_fullscreen() && component_view_render("waiting", 0) &&
                   pxa_clock_set_period(100)
               ? PXA_STATUS_OK
               : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t* event, uint32_t length) {
    pxa_event_t parsed;
    pxa_ipc_result_t result;
    int passed;

    if (!pxa_parse_event(event, length, &parsed))
        return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_SERVICE_CLOCK && parsed.opcode == PXA_CLOCK_TICK && !call_started) {
        call_started = 1;
        (void)pxa_clock_set_period(0);
        if (!call_responder()) {
            return component_view_render("failed", 0) ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service != PXA_SERVICE_IPC)
        return PXA_EVENT_UNHANDLED;
    if (parsed.opcode == PXA_IPC_RESULT && pxa_ipc_parse_result(&parsed, &result)) {
        passed = result.status == PXA_STATUS_OK && result.payload_length == 4 &&
                 memcmp(result.payload, "pong", 4) == 0;
        return component_view_render(passed ? "passed" : "failed", passed) ? PXA_EVENT_HANDLED
                                                                           : PXA_STATUS_INTERNAL;
    }
    if (parsed.opcode == PXA_IPC_CALL || parsed.opcode == PXA_IPC_REPLY ||
        parsed.opcode == PXA_IPC_REQUEST) {
        return PXA_EVENT_HANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
