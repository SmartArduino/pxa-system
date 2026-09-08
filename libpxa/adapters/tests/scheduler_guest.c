/* Freestanding PXA guest exercising lease + Work: as the UI component it
 * acquires a foreground lease and enqueues work; as the job component it
 * receives the Work context and records activation. Host clock
 * advancement produces a lease revocation the guest observes. No libc. */

#include "pxa.h"
#include "pxa_lease.h"
#include "pxa_work.h"

#define PXA_STORAGE_SET 2u
static uint32_t s_request_id = 1;

static uint32_t pxa_length(const uint8_t *value) {
    uint32_t length = 0;
    while (value[length] != 0) ++length;
    return length;
}

static int32_t storage_set(const char *key, const char *value) {
    uint8_t payload[80];
    uint8_t packet[96];
    pxa_writer_t writer;
    pxa_writer_t message;
    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, 1, (const uint8_t *)key,
                    (uint32_t)pxa_length((const uint8_t *)key)) ||
        !pxa_record(&writer, 2, (const uint8_t *)value,
                    (uint32_t)pxa_length((const uint8_t *)value))) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_SET,
                     s_request_id++, writer.data,
                     (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

uint32_t pxa_app_api_version(void) { return PXA_CORE_VERSION; }

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    pxa_work_context_t work;
    if (pxa_work_parse_context(config, config_length, &work)) {
        /* Work component activation: record delivery for the host test. */
        return storage_set("job_done", "1");
    }
    {
        uint8_t payload[64];
        uint8_t packet[128];
        if (!pxa_lease_acquire(s_request_id++, PXA_LEASE_FOREGROUND, 5000,
                               payload, sizeof(payload), packet,
                               sizeof(packet))) {
            return PXA_STATUS_INTERNAL;
        }
    }
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_SERVICE_CORE &&
        parsed.opcode == PXA_CORE_ACQUIRE_LEASE) {
        pxa_lease_result_t result;
        uint8_t payload[64];
        uint8_t packet[128];
        if (!pxa_lease_parse_result(&parsed, &result) ||
            result.status != PXA_STATUS_OK || result.handle == 0) {
            return PXA_EVENT_UNHANDLED;
        }
        pxa_work_request_t work = {0};
        work.worker = "job";
        work.worker_length = 3;
        work.initial_delay_ms = 2000;
        work.execution_hint_ms = 10000;
        work.retry_delay_ms = 1000;
        work.max_attempts = 1;
        if (!pxa_work_enqueue(s_request_id++, &work, payload,
                              sizeof(payload), packet, sizeof(packet))) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_WORK &&
        parsed.opcode == PXA_WORK_ENQUEUE) {
        pxa_work_enqueue_result_t result;
        if (!pxa_work_parse_enqueue(&parsed, &result) ||
            result.status != PXA_STATUS_OK || result.id == 0) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_CORE &&
        parsed.opcode == PXA_CORE_LEASE_REVOKED) {
        pxa_lease_revoked_t revoked;
        if (!pxa_lease_parse_revoked(&parsed, &revoked)) {
            return PXA_EVENT_UNHANDLED;
        }
        return storage_set("lease_revoked", "1") == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED
                   : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
