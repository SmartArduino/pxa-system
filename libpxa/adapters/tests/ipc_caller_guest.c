/* Freestanding PXA IPC caller guest: calls the provider endpoint at start,
 * verifies the reply content, then records the result through Storage so the
 * host can observe it. No libc dependency. */

#include "pxa.h"
#include "pxa_ipc.h"

#define PXA_STORAGE_SET 2u

static uint8_t s_verified;

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
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_SET, 9,
                     writer.data, (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

static int payload_is_ok_echo(const uint8_t *payload, uint32_t length) {
    static const uint8_t expected[] = {'o', 'k', ':', 'h', 'i'};
    uint32_t index;
    if (length != sizeof(expected)) return 0;
    for (index = 0; index < sizeof(expected); ++index) {
        if (payload[index] != expected[index]) return 0;
    }
    return 1;
}


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    uint8_t payload[128];
    uint8_t packet[256];
    pxa_writer_t payload_writer;
    pxa_writer_t packet_writer;
    (void)config;
    (void)config_length;
    pxa_writer_init(&payload_writer, payload, sizeof(payload));
    if (!pxa_record(&payload_writer, 1, (const uint8_t *)"com.example.echo",
                    sizeof("com.example.echo") - 1) ||
        !pxa_record(&payload_writer, 2, (const uint8_t *)"hi", 2)) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&packet_writer, packet, sizeof(packet));
    if (!pxa_message(&packet_writer, PXA_SERVICE_IPC, PXA_IPC_CALL, 1,
                     payload_writer.data, (uint32_t)payload_writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(packet_writer.data, (uint32_t)packet_writer.length);
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    pxa_ipc_result_t result;
    if (!pxa_parse_event(event, length, &parsed) ||
        parsed.service != PXA_SERVICE_IPC ||
        parsed.opcode != PXA_IPC_RESULT ||
        !pxa_ipc_parse_result(&parsed, &result) ||
        result.status != PXA_STATUS_OK) {
        return PXA_EVENT_UNHANDLED;
    }
    if (payload_is_ok_echo(result.payload, result.payload_length)) {
        s_verified = 1;
        return storage_set("ipc_verified", "1") == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED
                   : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
