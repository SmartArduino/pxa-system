#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/game_render.h"
#include "pxa/service.h"

typedef struct {
    unsigned creates;
    unsigned uploads;
    unsigned submits;
    unsigned queries;
    unsigned closes;
} backend_t;

static pxa_status_t backend_create(
    void *context, const pxa_game_render_desc_t *desc,
    uint64_t *provider_context, uint32_t *capabilities) {
    backend_t *backend = context;
    assert(desc->width == 148 && desc->height == 120);
    assert(desc->buffer_count == 3);
    assert(desc->flags == PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT);
    ++backend->creates;
    *provider_context = UINT64_C(0x1234);
    *capabilities = PXA_RASTER_CAP_TEXTURED_QUAD |
                    PXA_RASTER_CAP_SPRITE_BATCH |
                    PXA_RASTER_CAP_TRIANGLE_BATCH;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_upload(void *context, uint64_t provider_context,
                                   const uint8_t *bytes, size_t size) {
    backend_t *backend = context;
    assert(provider_context == UINT64_C(0x1234));
    assert(bytes != NULL && size == 4);
    ++backend->uploads;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_submit(void *context, uint64_t provider_context,
                                   const uint8_t *bytes, size_t size) {
    backend_t *backend = context;
    assert(provider_context == UINT64_C(0x1234));
    assert(bytes != NULL && size == 4);
    ++backend->submits;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_query(void *context, uint64_t provider_context,
                                  pxa_raster_telemetry_t *telemetry) {
    backend_t *backend = context;
    assert(provider_context == UINT64_C(0x1234));
    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->submitted_frames = 12;
    telemetry->draw_list_bytes = 4096;
    telemetry->last_host_raster_us = 321;
    ++backend->queries;
    return PXA_STATUS_OK;
}

static void backend_close(void *context, uint64_t provider_context) {
    backend_t *backend = context;
    assert(provider_context == UINT64_C(0x1234));
    ++backend->closes;
}

static size_t make_create(uint8_t *packet, size_t capacity) {
    uint8_t payload[8] = {148, 0, 120, 0, 3,
                          PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT, 0, 0};
    pxa_writer_t writer;
    pxa_writer_init(&writer, packet, capacity);
    assert(pxa_writer_message(&writer, PXA_GAME_RENDER_SERVICE_ID,
                              PXA_GAME_RENDER_CREATE_CONTEXT, 7, payload,
                              sizeof(payload)) == PXA_STATUS_OK);
    return writer.size;
}

int main(void) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_game_render_config_t config = {0};
    pxa_game_render_service_t *service = NULL;
    backend_t backend = {0};
    void *runtime_workspace;
    void *service_workspace;
    uint8_t packet[64];
    uint8_t event_bytes[64];
    uint8_t io[88] = {0};
    pxa_event_view_t view;
    pxa_message_view_t event;
    pxa_handle_t handle;
    size_t event_size = 0;

    pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    limits.max_requests = 2;
    limits.max_requests_per_component = 2;
    limits.max_handles = 2;
    limits.max_events = 2;
    limits.mailbox_capacity = 2;
    limits.reliable_event_reserve = 1;
    limits.max_services = 2;
    limits.event_block_size = 64;
    limits.event_block_count = 4;
    runtime_workspace = malloc(pxa_runtime_workspace_size(&limits));
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace,
                            pxa_runtime_workspace_size(&limits), &limits,
                            &runtime) == PXA_STATUS_OK);

    config.struct_size = sizeof(config);
    config.max_contexts = 1;
    config.max_contexts_per_component = 1;
    config.max_width = 296;
    config.max_height = 240;
    config.min_buffer_count = 2;
    config.max_buffer_count = 3;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = &backend;
    config.backend.create = backend_create;
    config.backend.upload = backend_upload;
    config.backend.submit = backend_submit;
    config.backend.query = backend_query;
    config.backend.close = backend_close;
    service_workspace = malloc(pxa_game_render_service_workspace_size(&config));
    assert(service_workspace != NULL);
    assert(pxa_game_render_service_init(
               service_workspace,
               pxa_game_render_service_workspace_size(&config), runtime,
               &config, &service) == PXA_STATUS_OK);
    assert(pxa_game_render_service_register(service) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 1, &component) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);

    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(runtime, component, packet,
                               make_create(packet, sizeof(packet))) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    assert(pxa_event_peek(runtime, component, &view) == PXA_STATUS_OK);
    assert(pxa_event_read(runtime, view.token, 0, event_bytes,
                          sizeof(event_bytes), &event_size) == PXA_STATUS_OK);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_read_u32(event.payload.data + 8) ==
           (PXA_RASTER_CAP_TEXTURED_QUAD | PXA_RASTER_CAP_SPRITE_BATCH |
            PXA_RASTER_CAP_TRIANGLE_BATCH));
    assert(pxa_read_u32(event.payload.data + 12) == PXA_RASTER_MAX_DRAW_BYTES);
    assert(pxa_event_consume(runtime, component, view.token) == PXA_STATUS_OK);

    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_io(runtime, component, handle,
                          PXA_GAME_RENDER_IO_UPLOAD, io, 4) == 4);
    assert(pxa_runtime_io(runtime, component, handle,
                          PXA_GAME_RENDER_IO_SUBMIT, io, 4) == 4);
    assert(pxa_runtime_io(runtime, component, handle,
                          PXA_GAME_RENDER_IO_TELEMETRY, io, sizeof(io)) ==
           (int32_t)sizeof(io));
    assert(pxa_read_u64(io) == 12 && pxa_read_u64(io + 8) == 4096 &&
           pxa_read_u32(io + 84) == 321);
    assert(pxa_handle_close(runtime, component, handle) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    assert(backend.creates == 1 && backend.uploads == 1 &&
           backend.submits == 1 && backend.queries == 1 && backend.closes == 1);

    pxa_runtime_deinit(runtime);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
