#include "pxa_store_installer.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_submit(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return 0;
}

int main(void) {
    const uint64_t token = UINT64_C(0x12345678abcdef01);
    uint8_t packet[512];
    uint8_t payload[24] = {0};
    uint8_t result[5] = {0};
    pxa_event_t event = {0};
    pxa_event_t decoded;
    pxa_store_installed_app_t installed[PXA_STORE_INSTALLED_MAX] = {0};
    uint64_t parsed_token = 0, received = 0, total = 0;
    size_t count = 99;

    assert(pxa_store_manage(PXA_STORE_INSTALLED_LIST_REQUEST, token, NULL,
                            packet, sizeof(packet)));
    assert(captured_length == PXA_HEADER_BYTES);
    assert(pxa_parse_event(captured, captured_length, &decoded));
    assert(decoded.service == PXA_SERVICE_STORE_INSTALLER);
    assert(decoded.opcode == PXA_STORE_INSTALLED_LIST_REQUEST);
    assert(decoded.token == token);
    assert(decoded.payload_size == 0);

    event.service = PXA_SERVICE_STORE_INSTALLER;
    event.opcode = PXA_STORE_INSTALLED_LIST_REQUEST;
    event.token = token;
    event.payload = result;
    event.payload_size = sizeof(result);
    assert(pxa_store_parse_installed(&event, installed,
                                     PXA_STORE_INSTALLED_MAX, &count));
    assert(count == 0);

    pxa_store_u64(payload, token);
    pxa_store_u64(payload + 8, 123);
    pxa_store_u64(payload + 16, 456);
    event.opcode = PXA_STORE_DOWNLOAD_PROGRESS;
    event.token = 0;
    event.payload = payload;
    event.payload_size = sizeof(payload);
    assert(pxa_store_parse_download_progress(&event, &parsed_token,
                                              &received, &total));
    assert(parsed_token == token && received == 123 && total == 456);
    event.payload_size = 20;
    assert(!pxa_store_parse_download_progress(&event, &parsed_token,
                                               &received, &total));
    return 0;
}
