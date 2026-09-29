/* Permission + sensor integration fixture using the current Guest SDK. */
#include "pxa.h"
#include "pxa_permission.h"
#include "pxa_sensor.h"
#include "pxa_storage.h"

#define PERMISSION_TOKEN UINT64_C(1)
#define LIST_TOKEN UINT64_C(2)
#define SUBSCRIBE_TOKEN UINT64_C(3)
#define STORAGE_TOKEN UINT64_C(4)

static uint64_t s_permission_handle;

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    uint8_t packet[128];
    uint32_t size = 0;
    (void)config;
    (void)config_length;
    if (!pxa_permission_build(packet, sizeof(packet), PXA_PERMISSION_ACQUIRE,
                              PERMISSION_TOKEN, "sensor.read", 11,
                              (const uint8_t *)"ambient.temperature", 18,
                              &size)) return PXA_STATUS_INTERNAL;
    return pxa_submit(packet, size);
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_PERMISSION_SERVICE &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE) {
        pxa_permission_acquire_result_t acquired;
        if (!pxa_permission_parse_acquire(&parsed, PERMISSION_TOKEN,
                                          &acquired) ||
            acquired.status != PXA_STATUS_OK) return PXA_EVENT_UNHANDLED;
        s_permission_handle = acquired.handle;
        return pxa_sensor_request_list(LIST_TOKEN) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SENSOR_SERVICE &&
        parsed.opcode == PXA_SENSOR_LIST) {
        pxa_sensor_list_result_t listed;
        if (!pxa_sensor_parse_list(&parsed, LIST_TOKEN, &listed) ||
            listed.status != PXA_STATUS_OK) return PXA_EVENT_UNHANDLED;
        return pxa_sensor_request_subscribe(SUBSCRIBE_TOKEN, 1, 1000,
                                             s_permission_handle) ==
                       PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SENSOR_SERVICE &&
        parsed.opcode == PXA_SENSOR_SUBSCRIBE) {
        pxa_sensor_subscribe_result_t subscribed;
        return pxa_sensor_parse_subscribe(&parsed, SUBSCRIBE_TOKEN,
                                           &subscribed) &&
                       subscribed.status == PXA_STATUS_OK &&
                       subscribed.handle != 0
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SENSOR_SERVICE &&
        parsed.opcode == PXA_SENSOR_SAMPLE) {
        pxa_sensor_sample_t sample;
        uint8_t packet[96];
        if (!pxa_sensor_parse_sample(&parsed, &sample) || sample.count == 0 ||
            sample.dimensions == 0 || sample.values[0] != 2500)
            return PXA_EVENT_UNHANDLED;
        return pxa_storage_request_set(packet, sizeof(packet), STORAGE_TOKEN,
                                       "sensor_ok", 9,
                                       (const uint8_t *)"1", 1) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
