/* Freestanding PXA guest exercising the permission + sensor path:
 * acquire a sensor.read permission, list sensor descriptors, subscribe to
 * the temperature channel, then verify a sample and persist the result
 * through Storage. No libc dependency. */

#include "pxa.h"
#include "pxa_permission.h"
#include "pxa_sensor.h"

#define PXA_STORAGE_SET 2u

static uint32_t s_request_id = 1;
static uint32_t s_permission_handle;
static uint8_t s_done;

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
    uint8_t payload[64];
    uint8_t packet[128];
    (void)config;
    (void)config_length;
    if (!pxa_permission_acquire(
            s_request_id++, "sensor.read", 11,
            (const uint8_t *)"ambient.temperature", 18, payload,
            sizeof(payload), packet, sizeof(packet))) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_SERVICE_PERMISSION &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE) {
        pxa_permission_acquire_result_t acquired;
        uint8_t packet[128];
        if (!pxa_permission_parse_acquire(&parsed, &acquired) ||
            acquired.status != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        s_permission_handle = acquired.handle;
        if (!pxa_sensor_list(s_request_id++, packet, sizeof(packet))) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_SENSOR &&
        parsed.opcode == PXA_SENSOR_LIST) {
        /* LIST response carries the status, then descriptor records. */
        if (parsed.payload_length >= 4 &&
            (int32_t)pxa_read_u32(parsed.payload) != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        {
            uint8_t payload[128];
            uint8_t packet[256];
            if (!pxa_sensor_subscribe(s_request_id++, 1, 1000,
                                      s_permission_handle, payload,
                                      sizeof(payload), packet,
                                      sizeof(packet))) {
                return PXA_EVENT_UNHANDLED;
            }
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_SENSOR &&
        parsed.opcode == PXA_SENSOR_SUBSCRIBE) {
        pxa_sensor_subscribe_result_t subscribed;
        if (!pxa_sensor_parse_subscribe(&parsed, &subscribed) ||
            subscribed.status != PXA_STATUS_OK || subscribed.handle == 0) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_SENSOR &&
        parsed.opcode == PXA_SENSOR_SAMPLE) {
        pxa_sensor_sample_t sample;
        if (!pxa_sensor_parse_sample(&parsed, &sample) ||
            sample.count == 0 || sample.values_length == 0) {
            return PXA_EVENT_UNHANDLED;
        }
        /* The test backend reports 2500 milli-celsius (25.0 C). */
        if (pxa_read_u16(sample.values) == 2500 &&
            storage_set("sensor_ok", "1") == PXA_STATUS_OK) {
            s_done = 1;
            return PXA_EVENT_HANDLED;
        }
        return PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
