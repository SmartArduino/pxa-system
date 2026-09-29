/* Foreground lease and scheduled Work integration fixture. */
#include "pxa.h"
#include "pxa_lease.h"
#include "pxa_work.h"
#include "pxa_storage.h"

#define LEASE_TOKEN UINT64_C(1)
#define WORK_TOKEN UINT64_C(2)
#define STORAGE_TOKEN UINT64_C(3)

static int32_t storage_set(const char *key, size_t key_size) {
    uint8_t packet[96];
    return pxa_storage_request_set(packet, sizeof(packet), STORAGE_TOKEN,
                                   key, key_size, (const uint8_t *)"1", 1);
}

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    pxa_work_context_t work;
    if (pxa_work_parse_context(config, config_length, &work))
        return storage_set("job_done", 8);
    return pxa_lease_acquire(LEASE_TOKEN, 1, 5000);
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_CORE_SERVICE &&
        parsed.opcode == PXA_LEASE_ACQUIRE) {
        pxa_lease_result_t result;
        pxa_work_request_t work = {0};
        if (!pxa_lease_parse_result(&parsed, LEASE_TOKEN, &result) ||
            result.status != PXA_STATUS_OK || result.handle == 0)
            return PXA_EVENT_UNHANDLED;
        work.worker = "job";
        work.worker_size = 3;
        work.initial_delay_ms = 2000;
        work.execution_hint_ms = 10000;
        work.retry_delay_ms = 1000;
        work.max_attempts = 1;
        return pxa_work_enqueue(WORK_TOKEN, &work) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_WORK_SERVICE &&
        parsed.opcode == PXA_WORK_ENQUEUE) {
        pxa_work_enqueue_result_t result;
        return pxa_work_parse_enqueue(&parsed, WORK_TOKEN, &result) &&
                       result.status == PXA_STATUS_OK && result.id != 0
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_CORE_SERVICE &&
        parsed.opcode == PXA_LEASE_REVOKED) {
        pxa_lease_revoked_t revoked;
        if (!pxa_lease_parse_revoked(&parsed, &revoked))
            return PXA_EVENT_UNHANDLED;
        return storage_set("lease_revoked", 13) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
