#include <stdint.h>

#include "pxa_core.h"
#include "pxa_device.h"
#include "pxa_net.h"
#include "pxa_audio.h"
#include "pxa_sensor.h"
#include "pxa_lease.h"
#include "pxa_work.h"
#include "pxa_surface.h"
#include "pxa_clock.h"
#include "pxa_ui_wire.h"
#include "pxa_store_installer.h"

#define STORE_TOKEN UINT64_C(0x12345678abcdeee7)

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    uint8_t invalid[PXA_HEADER_BYTES] = {19, 0, 1, 0};
    uint8_t bad_cancel[PXA_HEADER_BYTES + 8u];
    uint32_t cancel_size = 0;
    uint8_t net_packet[128];
    uint32_t net_size = 0;
    uint8_t audio_packet[PXA_HEADER_BYTES + 18u];
    uint32_t audio_size = 0;
    uint8_t surface_packet[PXA_HEADER_BYTES + 8u];
    uint32_t surface_size = 0;
    uint8_t ui_packet[PXA_HEADER_BYTES];
    uint8_t focus_packet[PXA_HEADER_BYTES + 12u];
    uint8_t focus_payload[12]={1,0,0,0,2,0,0,0,1,0,0,0};
    uint32_t ui_size = 0;
    uint8_t store_packet[64];
    pxa_net_request_t net_request = {0};
    (void)config;
    (void)size;
    invalid[16] = 1;
    if (pxa_submit(invalid, sizeof(invalid)) != -1) return -1;
    if (pxa_io(0, 1, 0, 0) != -1) return -1;
    net_request.url = "https://example.test/hello";
    net_request.url_size = sizeof("https://example.test/hello") - 1;
    net_request.method = PXA_NET_GET;
    net_request.permission_handle = UINT64_C(0x1234567800000001);
    net_request.max_response_bytes = 2;
    if (!pxa_net_build(net_packet, sizeof(net_packet),
                          PXA_NET_FETCH, UINT64_C(0x123456789abcdeff),
                          &net_request, &net_size) ||
        pxa_submit(net_packet, net_size) != -3) return -1;
    if (!pxa_audio_build_open(audio_packet, sizeof(audio_packet),
                                 UINT64_C(0x123456789abcdeee),
                                 UINT64_C(0x1234567800000001),
                                 &audio_size) ||
        pxa_submit(audio_packet, audio_size) != -3) return -1;
    if (pxa_sensor_request_list(UINT64_C(0x123456789abcdeed)) != -3)
        return -1;
    if (pxa_lease_acquire(UINT64_C(0x123456789abcdeec), 1, 10) != -3)
        return -1;
    if (pxa_work_cancel(UINT64_C(0x123456789abcdeeb), 4) != -3)
        return -1;
    if (!pxa_surface_build_query_state(
            surface_packet, sizeof(surface_packet),
            UINT64_C(0x123456789abcdeea),
            UINT64_C(0x1234567800000001), &surface_size) ||
        pxa_submit(surface_packet, surface_size) != -3) return -1;
    if (pxa_clock_now(UINT64_C(0x123456789abcdee9)) != -3)
        return -1;
    if (!pxa_ui_wire_build_theme_get(ui_packet, sizeof(ui_packet),
                                    UINT64_C(0x123456789abcdee8),
                                    &ui_size) ||
        pxa_submit(ui_packet, ui_size) != -3) return -1;
    /* The mock rejects this, but the v1 adapter must route it to UI. */
    if (!pxa_build_message(focus_packet,sizeof(focus_packet),PXA_UI_SERVICE,
            12,0,focus_payload,sizeof(focus_payload),&ui_size) ||
        pxa_submit(focus_packet,ui_size) != -3) return -1;
    if (!pxa_store_send(PXA_STORE_DOWNLOAD_REQUEST, STORE_TOKEN,
                        NULL, 0, store_packet, sizeof(store_packet)))
        return -1;
    /* The fixture rejects twice, then retains a request until cancellation. */
    if (pxa_device_request_runtime_info(
            UINT64_C(0x123456789abcdef0)) != -3 ||
        pxa_device_request_runtime_info(
            UINT64_C(0x123456789abcdef0)) != -3)
        return -1;
    if (pxa_device_request_runtime_info(
            UINT64_C(0x123456789abcdef0)) != 0 ||
        !pxa_build_cancel(bad_cancel, sizeof(bad_cancel),
                             UINT64_C(0x123456789abcdef0), &cancel_size) ||
        cancel_size != sizeof(bad_cancel))
        return -1;
    bad_cancel[4] = 1;
    if (pxa_submit(bad_cancel, cancel_size) != -1 ||
        pxa_cancel(0) != -1 ||
        pxa_cancel(UINT64_C(0x123456789abcdef0)) != 0 ||
        pxa_cancel(UINT64_C(0x123456789abcdef0)) != 0 ||
        pxa_cancel(UINT64_C(0x123456789abcdef1)) != 0)
        return -1;
    return 0;
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t size) {
    pxa_event_t event;
    if (!pxa_parse_event(bytes, size, &event))
        return -1;
    if (event.service == 15 && event.opcode == 2 &&
        event.token == UINT64_C(0x123456789abcdef0) &&
        event.payload_size == 4 &&
        (int32_t)pxa_load_u32(event.payload) == PXA_STATUS_CANCELLED)
        return 0;
    if (event.service == PXA_SENSOR_SERVICE &&
        event.opcode == PXA_SENSOR_SAMPLE && event.token == 0) {
        pxa_sensor_sample_t sample;
        return pxa_sensor_parse_sample(&event, &sample) &&
               sample.handle == UINT64_C(0x1234567800000001) &&
               sample.timestamp_us == 1000 && sample.count == 1 &&
               sample.dimensions == 1 && sample.values[0] == 42
                   ? 1 : -1;
    }
    if (event.service == PXA_SURFACE_SERVICE &&
        event.opcode == PXA_SURFACE_RELEASED && event.token == 0) {
        pxa_surface_released_t released;
        return pxa_surface_parse_released(&event, &released) &&
               released.handle == UINT64_C(0x1234567800000001) &&
               released.buffer_index == 2 && released.frame_id == 77
                   ? 1 : -1;
    }
    if (event.service == PXA_UI_SERVICE &&
        event.opcode == PXA_UI_CANVAS_STREAM_READY && event.token == 0) {
        pxa_ui_wire_canvas_stream_t stream;
        return pxa_ui_wire_parse_canvas_stream_ready(&event, &stream) &&
               stream.request == 19 &&
               stream.handle == UINT64_C(0x1234567800000001) &&
               stream.status == 0 ? 1 : -1;
    }
    if (event.service == PXA_SERVICE_STORE_INSTALLER &&
        event.opcode == PXA_STORE_DOWNLOAD_PROGRESS) {
        pxa_store_token_t token;
        uint64_t received, total;
        return pxa_store_parse_download_progress(&event, &token,
                                                  &received, &total) &&
               token == STORE_TOKEN && received == 123 && total == 456
                   ? 1 : -1;
    }
    if (event.service != 19 || event.opcode != 2 || event.token != 0 ||
        event.payload_size != 1 || event.payload[0] != 0x5a)
        return -1;
    return 1;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
