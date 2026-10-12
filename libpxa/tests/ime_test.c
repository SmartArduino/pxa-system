#include "pxa/ime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned frames, lost;
static pxa_status_t present(void *context, const pxa_ime_frame_t *frame) {
    (void)context;
    if (frame) ++frames; else ++lost;
    return PXA_STATUS_OK;
}
static pxa_component_t start(pxa_runtime_t *runtime, uint64_t instance) {
    pxa_component_t component;
    assert(pxa_component_create(runtime, instance, &component) == 0);
    assert(pxa_component_begin_start(runtime, component) == 0);
    assert(pxa_component_finish_start(runtime, component, 0) == 0);
    return component;
}
static pxa_status_t control(pxa_runtime_t *runtime, pxa_component_t component,
                            const pxa_message_view_t *message) {
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    pxa_status_t status = pxa_runtime_control_view(runtime, component, message);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    return status;
}
int main(void) {
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    void *memory = malloc(pxa_runtime_workspace_size(&limits));
    pxa_runtime_t *runtime;
    assert(pxa_runtime_init(memory, pxa_runtime_workspace_size(&limits), &limits, &runtime) == 0);
    pxa_ime_config_t config = {sizeof(config), NULL, present};
    void *workspace = malloc(pxa_ime_service_workspace_size());
    pxa_ime_service_t *service;
    assert(pxa_ime_service_init(workspace, pxa_ime_service_workspace_size(), runtime, &config, &service) == 0);
    assert(pxa_ime_service_register(service) == 0);
    pxa_component_t provider = start(runtime, 1), other = start(runtime, 2);
    uint64_t session;
    assert(pxa_ime_begin(service, provider, 0, &session) == 0);
    assert(pxa_ime_send(service, session, PXA_IME_TEXT, 0, 0, 0,
        (pxa_bytes_t){(const uint8_t *)"ni", 2}) == 0);
    uint8_t event[128]; size_t size;
    assert(pxa_event_pop(runtime, provider, event, sizeof(event), &size) == 0);
    pxa_ime_input_t input;
    assert(pxa_ime_decode_input((pxa_bytes_t){event + PXA_WIRE_SIZE, size - PXA_WIRE_SIZE}, &input) == 0);
    assert(input.session == session && input.sequence == 1 && input.text.size == 2);
    uint8_t response[64] = {0};
    pxa_write_u64(response, session); pxa_write_u32(response + 8, 1);
    pxa_ime_frame_t frame;
    assert(pxa_ime_decode_frame((pxa_bytes_t){response, 32}, &frame) == 0);
    pxa_message_view_t message = {0};
    message.service = PXA_IME_SERVICE_ID; message.opcode = PXA_IME_PRESENT;
    message.payload = (pxa_bytes_t){response, 32};
    assert(control(runtime, other, &message) == PXA_STATUS_DENIED);
    assert(control(runtime, provider, &message) == 0 && frames == 1);
    assert(control(runtime, provider, &message) == PXA_STATUS_BAD_STATE);
    assert(pxa_ime_pending(service) == 0);
    pxa_ime_end(service, session);
    uint64_t next;
    assert(pxa_ime_begin(service, provider, PXA_IME_SENSITIVE, &next) == 0 && next != session);
    assert(control(runtime, provider, &message) == PXA_STATUS_BAD_STATE);
    assert(pxa_ime_send(service, next, PXA_IME_TEXT, 0, 0, 0,
        (pxa_bytes_t){(const uint8_t *)"\xf0\x9f\x8c\xb2", 4}) == 0);
    assert(pxa_ime_send(service, next, PXA_IME_TEXT, 0, 0, 0,
        (pxa_bytes_t){(const uint8_t *)"\xf0\x9f", 2}) != 0);
    assert(pxa_component_abort(runtime, provider, PXA_STOP_FAULT) == 0 && lost == 1);
    assert(pxa_ime_send(service, next, PXA_IME_RESET, 0, 0, 0, (pxa_bytes_t){0}) == PXA_STATUS_BAD_STATE);
    /* Truncated, invalid UTF-8 and inconsistent candidate counts are rejected. */
    pxa_write_u16(response + 24, 1); response[32] = 3; response[34] = 0xe4;
    for (size_t n = 0; n < 37; ++n)
        assert(pxa_ime_decode_frame((pxa_bytes_t){response, n}, &frame) != 0);
    memcpy(response + 34, "你", 3);
    assert(pxa_ime_decode_frame((pxa_bytes_t){response, 37}, &frame) == 0);
    pxa_bytes_t item;
    assert(pxa_ime_frame_item(&frame, 0, &item) == 0 && item.size == 3);
    assert(pxa_ime_decode_frame((pxa_bytes_t){response, 38}, &frame) != 0);
    pxa_runtime_deinit(runtime); free(workspace); free(memory);
    puts("IME: ordered edits, focus generations, provider isolation, fault and UTF-8 checks passed");
}
