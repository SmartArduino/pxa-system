#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/surface.h"
#include "pxa/service.h"

typedef struct {
    uint8_t staged;
    uint8_t pending;
    unsigned creates;
    unsigned writes;
    unsigned queues;
    unsigned configures;
    unsigned overlay_configures;
    unsigned queries;
    unsigned closes;
    pxa_surface_layer_t layer;
    pxa_surface_damage_rect_t overlay;
    pxa_surface_state_t state;
} backend_t;

static pxa_status_t backend_create(
    void *context, const pxa_surface_desc_t *desc,
    uint64_t *surface, uint32_t *stride) {
    backend_t *backend = (backend_t *)context;
    uint32_t bytes_per_pixel;
    if (desc->format == PXA_SURFACE_FORMAT_RGB565) {
        assert(desc->flags == 0);
        bytes_per_pixel = 2;
    } else {
        assert(desc->format == PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED);
        assert(desc->flags == PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA);
        bytes_per_pixel = 4;
    }
    ++backend->creates;
    *surface = 0x1234;
    *stride = (uint32_t)desc->width * bytes_per_pixel;
    backend->state.free_buffers = desc->buffer_count;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_write(void *context, uint64_t surface,
                                  const uint8_t *pixels, size_t size) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234 && pixels != NULL && size == 32);
    if (backend->staged || backend->state.free_buffers == 0)
        return PXA_STATUS_WOULD_BLOCK;
    backend->staged = 1;
    --backend->state.free_buffers;
    ++backend->writes;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_queue(
    void *context, uint64_t surface, uint64_t frame_id,
    const pxa_surface_damage_rect_t *damage, uint8_t damage_count) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234 && frame_id != 0);
    if (!backend->staged) return PXA_STATUS_BAD_STATE;
    if (damage_count != 0) {
        assert(damage != NULL && damage[0].width <= 4 &&
               damage[0].height <= 4);
    }
    if (backend->pending) {
        ++backend->state.dropped_frames;
        ++backend->state.free_buffers;
    }
    backend->staged = 0;
    backend->pending = 1;
    ++backend->queues;
    ++backend->state.submitted_frames;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_configure(
    void *context, uint64_t surface, const pxa_surface_layer_t *layer) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234);
    backend->layer = *layer;
    ++backend->configures;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_configure_opaque_ui_regions(
    void *context, uint64_t surface,
    const pxa_surface_damage_rect_t *regions, uint8_t region_count) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234 && regions != NULL && region_count == 1);
    backend->overlay = regions[0];
    ++backend->overlay_configures;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_query(void *context, uint64_t surface,
                                  pxa_surface_state_t *state) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234);
    *state = backend->state;
    ++backend->queries;
    return PXA_STATUS_OK;
}

static void backend_close(void *context, uint64_t surface) {
    backend_t *backend = (backend_t *)context;
    assert(surface == 0x1234);
    ++backend->closes;
}

static size_t message(uint8_t *packet, size_t capacity, uint16_t opcode,
                      uint32_t request_id, const uint8_t *payload,
                      size_t payload_size) {
    pxa_writer_t writer;
    pxa_writer_init(&writer, packet, capacity);
    assert(pxa_writer_message(&writer, PXA_SURFACE_SERVICE_ID, opcode,
                              request_id, payload, payload_size) ==
           PXA_STATUS_OK);
    return writer.size;
}

static pxa_status_t dispatch(pxa_runtime_t *runtime,
                             pxa_component_t component,
                             const uint8_t *packet, size_t size) {
    pxa_status_t result;
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    result = pxa_runtime_control(runtime, component, packet, size);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    return result;
}

static int32_t dispatch_io(pxa_runtime_t *runtime,
                           pxa_component_t component, pxa_handle_t handle,
                           uint8_t *pixels, size_t size) {
    int32_t result;
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    result = pxa_runtime_io(runtime, component, handle, PXA_IO_WRITE, pixels,
                            size);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    return result;
}

static size_t completion(pxa_runtime_t *runtime, pxa_component_t component,
                         uint8_t *bytes, size_t capacity,
                         pxa_message_view_t *event) {
    pxa_event_view_t view;
    size_t size = 0;
    assert(pxa_event_peek(runtime, component, &view) == PXA_STATUS_OK);
    assert(view.size <= capacity);
    assert(pxa_event_read(runtime, view.token, 0, bytes, capacity, &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(bytes, size, PXA_MAX_CONTROL_MESSAGE, event) ==
           PXA_STATUS_OK);
    assert(pxa_event_consume(runtime, component, view.token) == PXA_STATUS_OK);
    return size;
}

int main(void) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_surface_config_t config;
    pxa_surface_service_t *surface = NULL;
    backend_t backend = {0};
    void *runtime_workspace;
    void *surface_workspace;
    size_t runtime_size;
    size_t surface_size;
    uint8_t packet[128];
    uint8_t event_bytes[128];
    uint8_t create[8] = {4, 0, 4, 0, 1, 0, 2, 0};
    uint8_t create_alpha[8] = {4, 0, 4, 0, 2, 0, 2, 1};
    uint8_t configure[20] = {0};
    uint8_t overlay[16] = {0};
    uint8_t queue[24] = {0};
    uint8_t pixels[32] = {0};
    pxa_message_view_t event;
    pxa_handle_t handle;
    size_t size;

    pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    limits.max_requests = 4;
    limits.max_requests_per_component = 4;
    limits.max_handles = 4;
    limits.max_events = 4;
    limits.mailbox_capacity = 4;
    limits.reliable_event_reserve = 1;
    limits.max_services = 2;
    limits.event_block_size = 64;
    limits.event_block_count = 8;
    runtime_size = pxa_runtime_workspace_size(&limits);
    runtime_workspace = malloc(runtime_size);
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace, runtime_size, &limits,
                            &runtime) == PXA_STATUS_OK);

    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.max_surfaces = 1;
    config.max_surfaces_per_component = 1;
    config.max_width = 256;
    config.max_height = 240;
    config.max_frame_bytes = 256u * 240u * 2u;
    config.min_buffer_count = 2;
    config.max_buffer_count = 3;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = &backend;
    config.backend.create = backend_create;
    config.backend.write = backend_write;
    config.backend.queue = backend_queue;
    config.backend.configure = backend_configure;
    config.backend.configure_opaque_ui_regions = backend_configure_opaque_ui_regions;
    config.backend.query = backend_query;
    config.backend.close = backend_close;
    surface_size = pxa_surface_service_workspace_size(&config);
    surface_workspace = malloc(surface_size);
    assert(surface_workspace != NULL);
    assert(pxa_surface_service_init(surface_workspace, surface_size,
                                     runtime, &config, &surface) ==
           PXA_STATUS_OK);
    assert(pxa_surface_service_register(surface) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 1, &component) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_validate_import(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);

    size = message(packet, sizeof(packet), PXA_SURFACE_CREATE, 1,
                   create, sizeof(create));
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    completion(runtime, component, event_bytes, sizeof(event_bytes), &event);
    assert(event.opcode == PXA_SURFACE_CREATE &&
           event.request_id == 1 && event.payload.size == 20 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    handle = pxa_read_u32(event.payload.data + 4);
    assert(handle != PXA_HANDLE_INVALID &&
           pxa_read_u32(event.payload.data + 8) == 8 &&
           pxa_read_u32(event.payload.data + 12) == sizeof(pixels) &&
           event.payload.data[16] == 2 && backend.creates == 1 &&
           pxa_surface_has_active_surfaces(surface));

    assert(dispatch_io(runtime, component, handle, pixels,
                       sizeof(pixels) - 1) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(dispatch_io(runtime, component, handle, pixels,
                       sizeof(pixels)) == (int32_t)sizeof(pixels));
    assert(backend.writes == 1);

    pxa_write_u32(configure, handle);
    pxa_write_u32(configure + 4, 8);
    pxa_write_u32(configure + 8, 12);
    pxa_write_u16(configure + 12, 4);
    pxa_write_u16(configure + 14, 4);
    configure[18] = 1;
    size = message(packet, sizeof(packet), PXA_SURFACE_CONFIGURE_LAYER, 2,
                   configure, sizeof(configure));
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    completion(runtime, component, event_bytes, sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.configures == 1 && backend.layer.x == 8 &&
           backend.layer.y == 12 && backend.layer.visible == 1);

    pxa_write_u32(overlay, handle);
    overlay[4] = 1;
    pxa_write_u16(overlay + 8, 1);
    pxa_write_u16(overlay + 10, 2);
    pxa_write_u16(overlay + 12, 3);
    pxa_write_u16(overlay + 14, 2);
    size = message(packet, sizeof(packet), PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS,
                   3, overlay, sizeof(overlay));
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    completion(runtime, component, event_bytes, sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.overlay_configures == 1 && backend.overlay.x == 1 &&
           backend.overlay.y == 2 && backend.overlay.width == 3 &&
           backend.overlay.height == 2);

    pxa_write_u32(queue, handle);
    pxa_write_u64(queue + 4, 1);
    queue[12] = 1;
    pxa_write_u16(queue + 16, 0);
    pxa_write_u16(queue + 18, 0);
    pxa_write_u16(queue + 20, 4);
    pxa_write_u16(queue + 22, 4);
    size = message(packet, sizeof(packet), PXA_SURFACE_QUEUE_FRAME, 0,
                   queue, sizeof(queue));
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    assert(backend.queues == 1 && backend.state.submitted_frames == 1);
    assert(dispatch_io(runtime, component, handle, pixels,
                       sizeof(pixels)) == (int32_t)sizeof(pixels));
    pxa_write_u64(queue + 4, 2);
    assert(message(packet, sizeof(packet), PXA_SURFACE_QUEUE_FRAME, 0,
                   queue, sizeof(queue)) == size);
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    assert(backend.queues == 2 && backend.state.dropped_frames == 1);

    pxa_write_u32(packet, 0);
    {
        uint8_t query[4];
        pxa_write_u32(query, handle);
        size = message(packet, sizeof(packet), PXA_SURFACE_QUERY_STATE, 4,
                       query, sizeof(query));
    }
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    completion(runtime, component, event_bytes, sizeof(event_bytes), &event);
    assert(event.payload.size == 36 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u64(event.payload.data + 4) == 2 &&
           pxa_read_u64(event.payload.data + 20) == 1 &&
           backend.queries == 1);

    assert(pxa_handle_close(runtime, component, handle) == PXA_STATUS_OK);
    assert(backend.closes == 1 && !pxa_surface_has_active_surfaces(surface));

    size = message(packet, sizeof(packet), PXA_SURFACE_CREATE, 5,
                   create_alpha, sizeof(create_alpha));
    assert(dispatch(runtime, component, packet, size) == PXA_STATUS_OK);
    completion(runtime, component, event_bytes, sizeof(event_bytes), &event);
    assert(event.opcode == PXA_SURFACE_CREATE &&
           event.request_id == 5 && event.payload.size == 20 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u32(event.payload.data + 8) == 16 &&
           pxa_read_u32(event.payload.data + 12) == 64);
    handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_handle_close(runtime, component, handle) == PXA_STATUS_OK);
    assert(backend.closes == 2 && !pxa_surface_has_active_surfaces(surface));
    pxa_runtime_deinit(runtime);
    free(surface_workspace);
    free(runtime_workspace);
    return 0;
}
