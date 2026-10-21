#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "pxa_audio.h"
#include "pxa_clock.h"
#include "pxa_http.h"
#include "pxa_image_decode.h"
#include "pxa_ime.h"
#include "pxa_window.h"

static uint32_t calls, io_calls, route, operation, sent_size;
static uint64_t sent_token;
static uint8_t sent[4096];
static int32_t io_status;
static const uint64_t token = UINT64_C(0x1122334455667788);
static const uint64_t handle = UINT64_C(0xaabbccdd00000007);

int32_t pxa_call(uint32_t r, uint64_t t, const uint8_t *bytes, uint32_t size) {
    assert(size <= sizeof(sent));
    ++calls; route = r; sent_token = t; sent_size = size;
    if (size) memcpy(sent, bytes, size);
    return 0;
}
int32_t pxa_io(uint64_t h, uint32_t op, uint8_t *bytes, uint32_t size) {
    assert(h == handle && size <= sizeof(sent));
    ++io_calls; operation = op; sent_size = size;
    if (size) memcpy(sent, bytes, size);
    return io_status;
}

static void test_http(void) {
    uint8_t packet[4096], payload[36] = {0};
    uint32_t size = 99;
    pxa_http_request_t request = pxa_http_request_defaults();
    pxa_net_header_t header = {"X-Test", 6, (const uint8_t *)"ok", 2};
    const char *wanted[] = {"ETag", "Content-Type"};
    const uint16_t lengths[] = {4, 12};
    request.url = "https://test.invalid/a"; request.url_size = (uint16_t)strlen(request.url);
    request.permission_handle = handle; request.headers = &header; request.header_count = 1;
    request.wanted_headers = wanted; request.wanted_header_sizes = lengths; request.wanted_header_count = 2;
    request.max_body_bytes = UINT64_C(1) << 40;
    assert(pxa_http_build_open(packet, sizeof(packet), token, &request, &size));
    assert(size > 20 && pxa_load_u64(packet + 4) == token);
    assert(pxa_http_open(token, &request, packet, sizeof(packet)) == 0);
    assert(route == (PXA_NET_SERVICE | (PXA_HTTP_OPEN << 16)) && sent_token == token);
    assert(!pxa_http_build_open(packet, size - 1, token, &request, &size) && !size);
    header.name = "Host"; header.name_size = 4;
    assert(!pxa_http_build_open(packet, sizeof(packet), token, &request, &size));
    header.name = "X-Test"; header.name_size = 6;
    request.method = PXA_NET_POST; request.body = (const uint8_t *)"hi"; request.body_size = 2;
    assert(pxa_http_build_open(packet, sizeof(packet), token, &request, &size));
    request.upload_bytes = 3;
    assert(!pxa_http_build_open(packet, sizeof(packet), token, &request, &size));
    request.body_size = 0; request.upload_bytes = INT32_MAX;
    assert(pxa_http_build_open(packet, sizeof(packet), token, &request, &size));
    request.upload_bytes++;
    assert(!pxa_http_build_open(packet, sizeof(packet), token, &request, &size));

    pxa_http_open_result_t opened;
    pxa_event_t event = {9, PXA_HTTP_OPEN, token, payload, 12};
    pxa_store_u64(payload + 4, handle);
    assert(pxa_http_parse_open(&event, token, &opened) && opened.handle == handle);
    event.payload_size = 13;
    assert(!pxa_http_parse_open(&event, token, &opened) && opened.handle == handle);
    pxa_store_u32(payload, (uint32_t)PXA_STATUS_DENIED); event.payload_size = 4;
    assert(pxa_http_parse_open(&event, token, &opened) && opened.status == PXA_STATUS_DENIED);
    assert(!pxa_http_parse_open(&event, token + 1, &opened));
    memset(payload, 0, sizeof(payload)); event.opcode = PXA_HTTP_CAPABILITIES; event.payload_size = 36;
    pxa_store_u32(payload + 4, 3); pxa_store_u64(payload + 24, UINT64_C(1) << 40);
    pxa_http_capabilities_t capabilities;
    assert(pxa_http_parse_capabilities(&event, token, &capabilities));
    assert(capabilities.features == 3 && capabilities.max_upload_bytes == (UINT64_C(1) << 40));

    uint8_t head[30] = {0};
    pxa_store_u16(head, 200); pxa_store_u32(head + 4, 14); pxa_store_u64(head + 8, UINT64_MAX);
    for (size_t i = 16; i < sizeof(head); i += 7) {
        pxa_store_u16(head + i, 1); pxa_store_u16(head + i + 2, 2);
        memcpy(head + i + 4, "Xok", 3);
    }
    pxa_http_response_t response;
    pxa_net_header_t field;
    size_t offset = 0;
    assert(pxa_http_parse_response(head, sizeof(head), &response) && !response.body_length_known);
    assert(pxa_http_header_next(&response, &offset, &field) && field.name[0] == 'X');
    assert(pxa_http_header_next(&response, &offset, &field) && offset == 14);
    assert(!pxa_http_header_next(&response, &offset, &field));
    for (size_t n = 0; n < sizeof(head); ++n) assert(!pxa_http_parse_response(head, n, &response));
    head[29] = 0x7f; assert(!pxa_http_parse_response(head, sizeof(head), &response));
    io_status = PXA_STATUS_WOULD_BLOCK;
    assert(pxa_http_read(handle, packet, 32) == PXA_STATUS_WOULD_BLOCK && operation == 1);
    assert(pxa_resource_wait(token, handle, PXA_RESOURCE_READY_READ) == 0);
    assert(sent_token == token && pxa_load_u64(sent) == handle && pxa_load_u32(sent + 8) == 1);
    assert(pxa_resource_wait(0, handle, 1) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_resource_wait(token, handle, 16) == PXA_STATUS_INVALID_ARGUMENT);
    io_status = 0; assert(pxa_http_finish_upload(handle) == 0 && operation == PXA_HTTP_IO_FINISH_UPLOAD);
}

static void test_ime(void) {
    uint8_t payload[26] = {0}, storage[64];
    pxa_store_u64(payload, handle); pxa_store_u32(payload + 8, 7);
    pxa_store_u16(payload + 12, PXA_IME_ACTION_TEXT); memcpy(payload + 24, "ni", 2);
    pxa_event_t event = {22, PXA_IME_INPUT, 0, payload, sizeof(payload)};
    pxa_ime_input_t input;
    assert(pxa_ime_parse_input(&event, &input) && input.session == handle && input.text_size == 2);
    payload[24] = 0; assert(!pxa_ime_parse_input(&event, &input)); payload[24] = 'n';
    assert(pxa_ime_parse_input(&event, &input));
    pxa_ime_frame_t frame = {0};
    assert(pxa_ime_frame_candidate(&frame, "x", 1) == PXA_STATUS_BAD_STATE);
    assert(pxa_ime_frame_begin(&frame, storage, 31, &input, "", 0, 0, NULL, 0) < 0);
    assert(pxa_ime_frame_begin(&frame, storage, sizeof(storage), &input, "ni", 2, 0, NULL, 0) == 0);
    assert(pxa_ime_frame_candidate(&frame, "你", 3) == 0);
    assert(pxa_ime_frame_spelling(&frame, "ni", 2) == 0);
    assert(pxa_ime_frame_candidate(&frame, "x", 1) == PXA_STATUS_BAD_STATE);
    assert(pxa_ime_frame_present(&frame, UINT32_MAX, 0, 0) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_ime_frame_present(&frame, 3, 1, 0) == 0);
    assert(sent_size == 43 && pxa_load_u64(sent) == handle && pxa_load_u16(sent + 24) == 1);
    assert(pxa_load_u16(sent + 26) == 1 && pxa_load_u32(sent + 16) == 3);
}

static void test_image(void) {
    uint8_t packet[64], payload[52] = {0}; uint32_t size;
    pxa_image_decode_options_t options = pxa_image_decode_defaults();
    pxa_image_animation_limits_t animation = {4096, 65536};
    assert(pxa_image_decode_build(packet, sizeof(packet), token, handle, 3, options, &animation, &size));
    assert(size == 52 && pxa_load_u16(packet + 30) == PXA_IMAGE_WITH_FRAMES);
    assert(pxa_image_decode_build(packet, sizeof(packet), token, handle, 2, options, NULL, &size) && size == 44);
    assert(!pxa_image_decode_build(packet, sizeof(packet), token, handle, 4, options, NULL, &size));
    pxa_store_u64(payload + 4, handle); payload[12] = PXA_ASSET_IMAGE;
    pxa_store_u16(payload + 16, 16); pxa_store_u16(payload + 18, 16);
    pxa_store_u16(payload + 32, 100); pxa_store_u16(payload + 34, 100);
    pxa_store_u32(payload + 36, 4096); pxa_store_u32(payload + 40, 50);
    pxa_store_u32(payload + 48, PXA_IMAGE_FRAME_LAST);
    pxa_event_t event = {21, PXA_ASSETS_DECODE_IMAGE, token, payload, sizeof(payload)};
    pxa_image_decode_result_t result;
    assert(pxa_image_decode_parse(&event, token, 6, 1, options, &result));
    assert(result.handle == handle && result.source_width == 100 && result.delay_ms == 50);
    payload[48] = 2;
    assert(!pxa_image_decode_parse(&event, token, 6, 1, options, &result) && result.handle == handle);
    payload[48] = 1; event.opcode = 7;
    assert(pxa_image_decode_parse(&event, token, 7, 1, options, &result));
    assert(pxa_image_next_frame(token, handle) == 0 && pxa_load_u64(sent) == handle);
}

static void test_clock_audio(void) {
    uint8_t payload[28] = {0};
    pxa_clock_wall_time_result_t result;
    pxa_event_t event = {4, PXA_CLOCK_WALL_TIME, token, payload, sizeof(payload)};
    pxa_store_u64(payload + 4, (uint64_t)-1000000);
    pxa_store_u64(payload + 12, handle); pxa_store_u32(payload + 20, (uint32_t)-3600);
    assert(pxa_clock_parse_wall_time(&event, token, &result));
    assert(result.unix_us == -1000000 && result.utc_offset_seconds == -3600 && result.monotonic_us == handle);
    pxa_store_u64(payload + 4, (uint64_t)INT64_MIN);
    assert(!pxa_clock_parse_wall_time(&event, token, &result));
    pxa_store_u32(payload + 20, 0);
    assert(pxa_clock_parse_wall_time(&event, token, &result) && result.unix_us == INT64_MIN);
    pxa_audio_tone_t tones[] = {{440, 80, -256, 0, 2, 5, 0}, {880, 40, -512, 1, 1, 3, 20}};
    io_status = 32;
    assert(pxa_audio_play_effect(handle, tones, 2, PXA_AUDIO_EFFECT_INDEPENDENT) == 32);
    assert(operation == PXA_AUDIO_IO_PLAY_EFFECT && sent_size == 32 && sent[1] == 255);
    uint32_t previous = io_calls; tones[1].duration_ms = 0;
    assert(pxa_audio_play_effect(handle, tones, 2, 1) < 0 && io_calls == previous);
    assert(pxa_audio_stop_effect(handle, 6) < 0 && io_calls == previous);
    io_status = 4; assert(pxa_audio_stop_effect(handle, 5) == 4 && sent[0] == 5);
    assert(pxa_window_request_back() == 0 && route == (2u | (4u << 16)) && !sent_token);
}

int main(void) {
    test_http(); test_ime(); test_image(); test_clock_audio();
    assert(calls && io_calls);
    return 0;
}
