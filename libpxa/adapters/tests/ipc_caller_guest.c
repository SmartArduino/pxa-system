#include <stdint.h>

#include "pxa_ipc.h"
#include "pxa_storage.h"

static uint32_t s_call_id;

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    uint8_t packet[80];
    (void)config;
    (void)size;
    return pxa_ipc_request_call(
        packet, sizeof(packet), UINT64_C(0x1234567800000001),
        "com.example.echo", sizeof("com.example.echo") - 1u,
        (const uint8_t *)"hi", 2);
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t size) {
    pxa_event_t event;
    if (!pxa_parse_event(bytes, size, &event)) return -1;
    if (event.service == PXA_IPC_SERVICE &&
        event.opcode == PXA_IPC_CALL) {
        pxa_ipc_call_result_t call;
        if (!pxa_ipc_parse_call_result(
                &event, UINT64_C(0x1234567800000001), &call) ||
            call.status != 0 || call.call_id == 0) return -1;
        s_call_id = call.call_id;
        return 1;
    }
    if (event.service == PXA_IPC_SERVICE &&
        event.opcode == PXA_IPC_RESULT_EVENT) {
        pxa_ipc_result_t result;
        uint8_t packet[80];
        static const uint8_t expected[] = {'o', 'k', ':', 'h', 'i'};
        if (!pxa_ipc_parse_result(&event, &result) ||
            result.call_id != s_call_id || result.status != 0 ||
            result.payload.size != sizeof(expected)) return -1;
        for (uint32_t i = 0; i < sizeof(expected); ++i)
            if (result.payload.data[i] != expected[i]) return -1;
        return pxa_storage_request_set(
                   packet, sizeof(packet), UINT64_C(0x1234567800000003),
                   "ipc_verified", sizeof("ipc_verified") - 1u,
                   (const uint8_t *)"1", 1) == 0 ? 1 : -1;
    }
    if (event.service == PXA_STORAGE_SERVICE &&
        event.opcode == PXA_STORAGE_SET) {
        int32_t status;
        return pxa_storage_parse_status(
                   &event, UINT64_C(0x1234567800000003),
                   PXA_STORAGE_SET, &status) && status == 0 ? 1 : -1;
    }
    return 0;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
