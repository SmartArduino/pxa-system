#include "pxa_system.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[256];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data, uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    uint8_t intent[PXA_SYSTEM_INTENT_WIRE_MIN_BYTES] = {
        'P', 'X', 'I', 'N', 0, 1, 0, 40,
    };
    uint8_t packet[128];
    const uint8_t result_packet[16] = {17, 0, 1, 0, 9, 0, 0, 0, 4, 0, 0, 0, 252, 255, 255, 255};
    pxa_event_t event;
    pxa_system_result_t result;
    const uint8_t body[] = {1, 2, 3};

    assert(pxa_system_start_intent(9, intent, sizeof(intent), packet, sizeof(packet)));
    assert(captured_length == 12 + sizeof(intent));
    assert(pxa_read_u16(captured) == PXA_SERVICE_SYSTEM);
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_INTENT_START);
    assert(pxa_read_u32(captured + 4) == 9);
    assert(memcmp(captured + 12, intent, sizeof(intent)) == 0);

    assert(pxa_system_invoke(10, (const uint8_t*)"vendor.example.sensor", 21, 2, 3, 9, 4, body,
                             sizeof(body), packet, sizeof(packet)));
    assert(pxa_read_u16(captured) == PXA_SERVICE_SYSTEM);
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_SERVICE_INVOKE);
    assert(pxa_read_u32(captured + 4) == 10);

    assert(pxa_system_publish(11, (const uint8_t*)"vendor.example.motion", 21, 1, 0, 7, 55, body,
                              sizeof(body), packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_TOPIC_PUBLISH);
    assert(pxa_read_u32(captured + 4) == 11);

    assert(pxa_system_subscribe(12, (const uint8_t*)"vendor.example.motion", 21, 1, 0, packet,
                                sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_TOPIC_SUBSCRIBE);
    assert(pxa_system_unsubscribe(13, (const uint8_t*)"vendor.example.motion", 21, 1, 0, packet,
                                  sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_TOPIC_UNSUBSCRIBE);

    assert(pxa_system_register_service(14, (const uint8_t*)"vendor.example.echo", 19, 1, 0, 3,
                                       packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_SERVICE_REGISTER);
    assert(pxa_system_unregister_service(15, (const uint8_t*)"vendor.example.echo", 19, 1, packet,
                                         sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_SERVICE_UNREGISTER);
    assert(pxa_system_complete_service(16, 99, PXA_STATUS_OK, body, sizeof(body), packet,
                                       sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_SYSTEM_SERVICE_COMPLETE);

    assert(pxa_parse_event(result_packet, sizeof(result_packet), &event));
    assert(pxa_system_parse_result(&event, &result));
    assert(result.status == PXA_STATUS_DENIED);
    assert(result.payload_size == 0);

    {
        uint8_t intent_packet[256];
        uint8_t publisher_root[32];
        pxa_writer_t records;
        pxa_writer_t envelope;
        pxa_system_intent_event_t intent_event;
        memset(publisher_root, 7, sizeof(publisher_root));
        pxa_writer_init(&records, intent_packet + 12,
                        sizeof(intent_packet) - 12);
        assert(pxa_record(&records, 1, intent, sizeof(intent)));
        assert(pxa_record(&records, 2 | PXA_SYSTEM_RECORD_OPTIONAL,
                          publisher_root, sizeof(publisher_root)));
        assert(pxa_record(&records, 3 | PXA_SYSTEM_RECORD_OPTIONAL,
                          (const uint8_t*)"native.launcher", 15));
        assert(pxa_record(&records, 4 | PXA_SYSTEM_RECORD_OPTIONAL,
                          (const uint8_t*)"main", 4));
        pxa_writer_init(&envelope, intent_packet, sizeof(intent_packet));
        assert(pxa_message(&envelope, PXA_SERVICE_SYSTEM,
                           PXA_SYSTEM_INTENT_EVENT, 0, records.data,
                           records.length));
        assert(pxa_parse_event(intent_packet, (uint32_t)envelope.length,
                               &event));
        assert(pxa_system_parse_intent_event(&event, &intent_event));
        assert(intent_event.intent_size == sizeof(intent) &&
               intent_event.caller_app_id_size == 15 &&
               intent_event.caller_component_id_size == 4 &&
               memcmp(intent_event.caller_publisher_root, publisher_root,
                      sizeof(publisher_root)) == 0);
    }
    {
        uint8_t topic_packet[80];
        pxa_writer_t records;
        pxa_writer_t envelope;
        uint8_t value[8] = {1, 0, 0, 0};
        pxa_system_topic_event_t topic_event;
        pxa_writer_init(&records, topic_packet + 12, sizeof(topic_packet) - 12);
        assert(pxa_record(&records, 1, (const uint8_t*)"weather.changed", 15));
        assert(pxa_record(&records, 2, value, 4));
        value[0] = 7;
        assert(pxa_record(&records, 3, value, 4));
        pxa_writer_init(&envelope, topic_packet, sizeof(topic_packet));
        assert(pxa_message(&envelope, PXA_SERVICE_SYSTEM, PXA_SYSTEM_TOPIC_EVENT, 0, records.data,
                           records.length));
        assert(pxa_parse_event(topic_packet, (uint32_t)envelope.length, &event));
        assert(pxa_system_parse_topic_event(&event, &topic_event));
        assert(topic_event.topic_size == 15 && topic_event.event == 7);
    }
    {
        uint8_t service_packet[256];
        uint8_t value[32] = {1, 0, 0, 0};
        pxa_writer_t records;
        pxa_writer_t envelope;
        pxa_system_service_request_t service_request;
        pxa_writer_init(&records, service_packet + 12, sizeof(service_packet) - 12);
        assert(pxa_record(&records, 1, (const uint8_t*)"vendor.example.echo", 19));
        assert(pxa_record(&records, 2, value, 4));
        assert(pxa_record(&records, 3, value, 4));
        value[0] = 77;
        assert(pxa_record(&records, 6 | PXA_SYSTEM_RECORD_OPTIONAL, value, 8));
        memset(value, 9, 32);
        assert(pxa_record(&records, 7 | PXA_SYSTEM_RECORD_OPTIONAL, value, 32));
        assert(pxa_record(&records, 8 | PXA_SYSTEM_RECORD_OPTIONAL,
                          (const uint8_t*)"native.weather", 14));
        assert(pxa_record(&records, 9 | PXA_SYSTEM_RECORD_OPTIONAL, (const uint8_t*)"main", 4));
        pxa_writer_init(&envelope, service_packet, sizeof(service_packet));
        assert(pxa_message(&envelope, PXA_SERVICE_SYSTEM, PXA_SYSTEM_SERVICE_REQUEST, 0,
                           records.data, records.length));
        assert(pxa_parse_event(service_packet, (uint32_t)envelope.length, &event));
        assert(pxa_system_parse_service_request(&event, &service_request));
        assert(service_request.call_id == 77 && service_request.operation == 1 &&
               service_request.caller_app_id_size == 14);
    }
    return 0;
}
