#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pxsys/pxa_gateway_wire.h"

static void put_version(uint8_t value[4], uint16_t major, uint16_t minor) {
    pxa_write_u16(value, major);
    pxa_write_u16(value + 2, minor);
}

int main(void) {
    uint8_t encoded[160];
    uint8_t value[8];
    const uint8_t body[] = {1, 2, 3};
    pxa_writer_t writer;
    pxsys_service_request_t service;
    pxsys_topic_event_t event;
    pxsys_pxa_service_descriptor_t descriptor;
    pxsys_pxa_service_completion_t completion;

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    assert(pxa_writer_record(&writer, 1, "vendor.example.sensor", 21) == PXA_STATUS_OK);
    put_version(value, 2, 3);
    assert(pxa_writer_record(&writer, 2, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, 9);
    assert(pxa_writer_record(&writer, 3, value, 4) == PXA_STATUS_OK);
    assert(pxa_writer_record(&writer, 5 | PXA_RECORD_OPTIONAL_MASK, body, sizeof(body)) ==
           PXA_STATUS_OK);
    assert(pxsys_pxa_gateway_decode_service((pxa_bytes_t){encoded, writer.size}, &service) ==
           PXSYS_STATUS_OK);
    assert(service.version.major == 2 && service.version.minor == 3 && service.operation == 9);
    assert(service.interface_id.size == 21 && service.payload.size == sizeof(body));

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    assert(pxa_writer_record(&writer, 1, "vendor.example.motion", 21) == PXA_STATUS_OK);
    put_version(value, 1, 0);
    assert(pxa_writer_record(&writer, 2, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, 4);
    assert(pxa_writer_record(&writer, 3, value, 4) == PXA_STATUS_OK);
    pxa_write_u64(value, 55);
    assert(pxa_writer_record(&writer, 4 | PXA_RECORD_OPTIONAL_MASK, value, 8) == PXA_STATUS_OK);
    assert(pxsys_pxa_gateway_decode_topic((pxa_bytes_t){encoded, writer.size}, &event) ==
           PXSYS_STATUS_OK);
    assert(event.event == 4 && event.sequence == 55 && event.topic.size == 21);

    {
        uint8_t round_trip[160];
        size_t round_trip_size = 0;
        pxsys_topic_event_t decoded;
        event.payload = pxsys_bytes(body, sizeof(body));
        assert(pxsys_pxa_gateway_encode_topic(&event, round_trip, sizeof(round_trip),
                                              &round_trip_size) == PXSYS_STATUS_OK);
        assert(pxsys_pxa_gateway_decode_topic((pxa_bytes_t){round_trip, round_trip_size},
                                              &decoded) == PXSYS_STATUS_OK);
        assert(decoded.event == event.event && decoded.sequence == event.sequence &&
               decoded.payload.size == sizeof(body));
    }

    encoded[0] = 2;
    assert(pxsys_pxa_gateway_decode_topic((pxa_bytes_t){encoded, writer.size}, &event) ==
           PXSYS_STATUS_INVALID_ARGUMENT);

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    assert(pxa_writer_record(&writer, 1, "vendor.example.echo", 19) == PXA_STATUS_OK);
    put_version(value, 1, 2);
    assert(pxa_writer_record(&writer, 2, value, 4) == PXA_STATUS_OK);
    pxa_write_u64(value, 7);
    assert(pxa_writer_record(&writer, 3 | PXA_RECORD_OPTIONAL_MASK, value, 8) == PXA_STATUS_OK);
    assert(pxsys_pxa_gateway_decode_service_descriptor((pxa_bytes_t){encoded, writer.size},
                                                       &descriptor) == PXSYS_STATUS_OK);
    assert(descriptor.version.minor == 2 && descriptor.features == 7);

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    pxa_write_u64(value, 44);
    assert(pxa_writer_record(&writer, 1, value, 8) == PXA_STATUS_OK);
    pxa_write_u32(value, (uint32_t)PXA_STATUS_OK);
    assert(pxa_writer_record(&writer, 2, value, 4) == PXA_STATUS_OK);
    assert(pxa_writer_record(&writer, 3 | PXA_RECORD_OPTIONAL_MASK, body, sizeof(body)) ==
           PXA_STATUS_OK);
    assert(pxsys_pxa_gateway_decode_service_completion((pxa_bytes_t){encoded, writer.size},
                                                       &completion) == PXSYS_STATUS_OK);
    assert(completion.call_id == 44 && completion.payload.size == sizeof(body));

    {
        uint8_t request_wire[256];
        size_t request_wire_size = 0;
        pxsys_caller_t caller = {0};
        caller.struct_size = sizeof(caller);
        memset(caller.app.publisher_root, 9, sizeof(caller.app.publisher_root));
        caller.app.app_id = pxsys_string_from_cstr("native.weather");
        caller.component_id = pxsys_string_from_cstr("main");
        memset(&service, 0, sizeof(service));
        service.struct_size = sizeof(service);
        service.interface_id = pxsys_string_from_cstr("vendor.example.echo");
        service.version = (pxsys_version_t){1, 0};
        service.operation = 1;
        service.payload = pxsys_bytes(body, sizeof(body));
        service.caller = &caller;
        assert(pxsys_pxa_gateway_encode_service_request(&service, 88, request_wire,
                                                        sizeof(request_wire),
                                                        &request_wire_size) == PXSYS_STATUS_OK);
        assert(request_wire_size != 0);
    }
    puts("pxa gateway wire tests passed");
    return 0;
}
