#include "../../testing/pxa_control_capture.h"
#include "pxa_store_installer.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;
static int32_t control_status;
static unsigned control_calls;

int32_t pxa_test_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    ++control_calls;
    return control_status;
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
    const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const int32_t errors[] = {PXA_STATUS_DENIED, PXA_STATUS_BUSY,
        PXA_STATUS_RESOURCE_LIMIT, PXA_STATUS_UNSUPPORTED, PXA_STATUS_UNAVAILABLE};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        control_status = errors[i];
        assert(pxa_store_request_package_status(PXA_STORE_INSTALL_REQUEST,
            token, "test-app", "/ticket", digest, 123, packet, sizeof(packet)) == errors[i]);
        assert(!pxa_store_install(token, "test-app", "/ticket", digest,
                                   123, packet, sizeof(packet)));
    }
    control_status = PXA_STATUS_OK;
    assert(pxa_store_request_package_status(PXA_STORE_DOWNLOAD_REQUEST,
        token, "test-app", "/ticket", digest, 123, packet, sizeof(packet)) == PXA_STATUS_OK);
    assert(pxa_store_install(token, "test-app", "/ticket", digest,
                              123, packet, sizeof(packet)));
    unsigned calls_before = control_calls;
    assert(pxa_store_request_package_status(PXA_STORE_INSTALL_REQUEST,
        token, "test-app", "/ticket", digest, UINT64_C(0x80000000), packet,
        sizeof(packet)) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(pxa_store_request_package_status(PXA_STORE_INSTALL_REQUEST,
        token, "test-app", "/ticket", "bad digest", 123, packet,
        sizeof(packet)) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_store_request_package_status(PXA_STORE_INSTALL_REQUEST,
        token, "test-app", "/ticket", digest, 123, packet, 4) == PXA_STATUS_INVALID_ARGUMENT);
    assert(control_calls == calls_before);

    assert(pxa_store_request_package_status(PXA_STORE_INSTALL_REQUEST, token,
        "test-app", "/ticket", digest, UINT64_C(0x7fffffff), packet, sizeof(packet)) == PXA_STATUS_OK);
    assert(pxa_store_request_capabilities(token) == PXA_STATUS_OK);
    assert(pxa_parse_event(captured, captured_length, &decoded));
    assert(decoded.opcode == PXA_STORE_CAPABILITIES_REQUEST && decoded.payload_size == 0);
    uint8_t capability_bytes[20] = {0};
    pxa_store_u16(capability_bytes + 6, 8); pxa_store_u32(capability_bytes + 8, 3);
    pxa_store_u64(capability_bytes + 12, UINT64_C(0x7fffffff));
    pxa_store_capabilities_t capabilities;
    pxa_event_t capability_event = {PXA_SERVICE_STORE_INSTALLER, PXA_STORE_CAPABILITIES_REQUEST,
        token, capability_bytes, sizeof(capability_bytes)};
    assert(pxa_store_parse_capabilities(&capability_event, token, &capabilities));
    assert(capabilities.minor == 8 && capabilities.features == 3 && capabilities.max_container_bytes == UINT64_C(0x7fffffff));
    capability_event.payload_size--;
    assert(!pxa_store_parse_capabilities(&capability_event, token, &capabilities));
    capability_event.payload_size = 4; pxa_store_u32(capability_bytes, PXA_STATUS_UNSUPPORTED);
    assert(pxa_store_parse_capabilities(&capability_event, token, &capabilities) && capabilities.status == PXA_STATUS_UNSUPPORTED);
    assert(!pxa_store_parse_capabilities(&capability_event, token + 1, &capabilities));

    assert(pxa_store_manage(PXA_STORE_INSTALLED_LIST_REQUEST, token, NULL,
                            packet, sizeof(packet)));
    assert(captured_length == PXA_HEADER_BYTES);
    assert(pxa_parse_event(captured, captured_length, &decoded));
    assert(decoded.service == PXA_SERVICE_STORE_INSTALLER);
    assert(decoded.opcode == PXA_STORE_INSTALLED_LIST_REQUEST);
    assert(decoded.token == token);
    assert(decoded.payload_size == 0);

    const char *full="17982acd08493944059713aee9a56135dd4a080ecaaf7aa77df11fdf3102d171:pxa-nes";
    assert(pxa_store_manage(PXA_STORE_LAUNCH_IDENTITY_REQUEST,token,full,packet,sizeof(packet)));
    assert(pxa_parse_event(captured,captured_length,&decoded));
    assert(decoded.opcode==PXA_STORE_LAUNCH_IDENTITY_REQUEST && decoded.payload_size==strlen(full));
    calls_before=control_calls;
    assert(!pxa_store_manage(PXA_STORE_LAUNCH_IDENTITY_REQUEST,token,"pxa-nes",packet,sizeof(packet)));
    assert(!pxa_store_manage(PXA_STORE_LAUNCH_REQUEST,token,full,packet,sizeof(packet)));
    assert(control_calls==calls_before);

    assert(pxa_store_install_identity_status(token,full,"/ticket",digest,123,packet,sizeof(packet))==PXA_STATUS_OK);
    assert(pxa_parse_event(captured,captured_length,&decoded));
    assert(decoded.opcode==PXA_STORE_INSTALL_IDENTITY_REQUEST && decoded.payload_size==32+43+7+7);
    assert(decoded.payload[0]==0x17 && decoded.payload[1]==0x98 && decoded.payload[32]==7);
    uint8_t binding[180]={0};
    assert(pxa_store_register_namespace_status(token,binding,sizeof(binding),packet,sizeof(packet))==PXA_STATUS_OK);
    assert(pxa_parse_event(captured,captured_length,&decoded) && decoded.opcode==PXA_STORE_REGISTER_NAMESPACE_REQUEST && decoded.payload_size==180);
    calls_before=control_calls;
    assert(pxa_store_install_identity_status(token,"pxa-nes","/ticket",digest,123,packet,sizeof(packet))==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_store_register_namespace_status(token,binding,179,packet,sizeof(packet))==PXA_STATUS_INVALID_ARGUMENT);
    assert(control_calls==calls_before);
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
