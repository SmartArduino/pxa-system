#include "ui/ui_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t size;
} allocation_header_t;

typedef struct {
    size_t current;
    size_t peak;
    size_t allocations;
    size_t fail_at;
} allocator_state_t;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} bytes_t;

typedef struct {
    unsigned begins;
    unsigned applies;
    unsigned commits;
    unsigned cancels;
    unsigned resets;
    unsigned canvas_presents;
    unsigned surface_opens;
    unsigned surface_closes;
    unsigned environment_changes;
    int reject_commit;
    int reject_canvas;
    pxa_ui_transaction_info_t transaction;
    void *canvas_bytes;
    pxa_ui_release_fn canvas_release;
    void *canvas_release_context;
} backend_state_t;

static uint64_t test_now_us(void *context) {
    uint64_t *clock = (uint64_t *)context;
    *clock += 50;
    return *clock;
}

static void *test_allocate(void *context, size_t size) {
    allocator_state_t *state = (allocator_state_t *)context;
    allocation_header_t *header;
    if (state->fail_at != 0 && state->allocations + 1u == state->fail_at)
        return NULL;
    header = (allocation_header_t *)malloc(sizeof(*header) + size);
    if (header == NULL) return NULL;
    header->size = size;
    ++state->allocations;
    state->current += size;
    if (state->current > state->peak) state->peak = state->current;
    return header + 1;
}

static void test_release(void *context, void *memory) {
    allocator_state_t *state = (allocator_state_t *)context;
    allocation_header_t *header;
    if (memory == NULL) return;
    header = (allocation_header_t *)memory - 1;
    assert(header->size <= state->current);
    state->current -= header->size;
    free(header);
}

static pxa_status_t backend_begin(
    void *context, const pxa_ui_transaction_info_t *info,
    void **backend_transaction) {
    backend_state_t *state = (backend_state_t *)context;
    assert(info != NULL && backend_transaction != NULL);
    state->transaction = *info;
    ++state->begins;
    *backend_transaction = state;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_apply(
    void *context, void *backend_transaction,
    const pxa_ui_command_view_t *command, void **created_handle) {
    backend_state_t *state = (backend_state_t *)context;
    assert(backend_transaction == state && command != NULL);
    ++state->applies;
    if (command->command == PXA_UI_COMMAND_CREATE) {
        assert(created_handle != NULL);
        *created_handle = (void *)(uintptr_t)(command->node + 1u);
    }
    return PXA_STATUS_OK;
}

static pxa_status_t backend_commit(void *context, void *backend_transaction) {
    backend_state_t *state = (backend_state_t *)context;
    assert(backend_transaction == state);
    if (state->reject_commit) return PXA_STATUS_DENIED;
    if (state->transaction.kind != PXA_UI_PATCH &&
        state->canvas_bytes != NULL) {
        state->canvas_release(state->canvas_release_context,
                              state->canvas_bytes);
        state->canvas_bytes = NULL;
    }
    ++state->commits;
    return PXA_STATUS_OK;
}

static void backend_cancel(void *context, void *backend_transaction) {
    backend_state_t *state = (backend_state_t *)context;
    assert(backend_transaction == state);
    ++state->cancels;
}

static pxa_status_t backend_canvas(
    void *context, const pxa_ui_canvas_view_t *canvas,
    pxa_ui_release_fn release, void *release_context) {
    backend_state_t *state = (backend_state_t *)context;
    assert(canvas != NULL && canvas->display_list.size != 0);
    if (state->reject_canvas) return PXA_STATUS_DENIED;
    if (state->canvas_bytes != NULL)
        state->canvas_release(state->canvas_release_context,
                              state->canvas_bytes);
    state->canvas_bytes = (void *)canvas->display_list.data;
    state->canvas_release = release;
    state->canvas_release_context = release_context;
    ++state->canvas_presents;
    return PXA_STATUS_OK;
}

static void backend_reset(void *context) {
    backend_state_t *state = (backend_state_t *)context;
    if (state->canvas_bytes != NULL) {
        state->canvas_release(state->canvas_release_context,
                              state->canvas_bytes);
        state->canvas_bytes = NULL;
    }
    ++state->resets;
}

static pxa_status_t backend_surface_open(
    void *context, pxa_ui_surface_role_t role,
    pxa_ui_environment_t *environment) {
    backend_state_t *state = (backend_state_t *)context;
    assert(role == PXA_UI_SURFACE_DIALOG && environment != NULL);
    environment->width = 800;
    environment->height = 480;
    ++state->surface_opens;
    return PXA_STATUS_OK;
}

static void backend_surface_close(void *context, uint32_t surface) {
    backend_state_t *state = (backend_state_t *)context;
    assert(surface > PXA_UI_PRIMARY_SURFACE);
    ++state->surface_closes;
}

static pxa_status_t backend_environment_changed(
    void *context, const pxa_ui_environment_t *environment) {
    backend_state_t *state = (backend_state_t *)context;
    assert(environment != NULL && environment->surface != 0);
    ++state->environment_changes;
    return PXA_STATUS_OK;
}

static void bytes_reserve(bytes_t *bytes, size_t addition) {
    size_t required = bytes->size + addition;
    if (required > bytes->capacity) {
        size_t capacity = bytes->capacity == 0 ? 256u : bytes->capacity;
        while (capacity < required) capacity *= 2u;
        bytes->data = (uint8_t *)realloc(bytes->data, capacity);
        assert(bytes->data != NULL);
        bytes->capacity = capacity;
    }
}

static void bytes_put(bytes_t *bytes, const void *data, size_t size) {
    bytes_reserve(bytes, size);
    if (size != 0) memcpy(bytes->data + bytes->size, data, size);
    bytes->size += size;
}

static void put_u8(bytes_t *bytes, uint8_t value) {
    bytes_put(bytes, &value, 1);
}

static void put_u16(bytes_t *bytes, uint16_t value) {
    uint8_t encoded[2];
    pxa_write_u16(encoded, value);
    bytes_put(bytes, encoded, sizeof(encoded));
}

static void put_u32(bytes_t *bytes, uint32_t value) {
    uint8_t encoded[4];
    pxa_write_u32(encoded, value);
    bytes_put(bytes, encoded, sizeof(encoded));
}

static void command(bytes_t *stream, uint8_t opcode, uint8_t flags,
                    const bytes_t *payload) {
    assert(payload->size <= UINT16_MAX);
    put_u8(stream, opcode);
    put_u8(stream, flags);
    put_u16(stream, (uint16_t)payload->size);
    bytes_put(stream, payload->data, payload->size);
}

static void create_node(bytes_t *stream, uint32_t node, uint32_t parent,
                        uint8_t type, uint8_t subtype) {
    bytes_t payload = {0};
    put_u32(&payload, node);
    put_u32(&payload, parent);
    put_u32(&payload, 0);
    put_u8(&payload, type);
    put_u8(&payload, subtype);
    put_u16(&payload, 0);
    command(stream, PXA_UI_COMMAND_CREATE, 0, &payload);
    free(payload.data);
}

static void move_node(bytes_t *stream, uint32_t node, uint32_t parent) {
    bytes_t payload = {0};
    put_u32(&payload, node);
    put_u32(&payload, parent);
    put_u32(&payload, 0);
    command(stream, PXA_UI_COMMAND_MOVE, 0, &payload);
    free(payload.data);
}

static void remove_node(bytes_t *stream, uint32_t node) {
    bytes_t payload = {0};
    put_u32(&payload, node);
    command(stream, PXA_UI_COMMAND_REMOVE, 0, &payload);
    free(payload.data);
}

static void set_property(bytes_t *stream, uint32_t node, uint16_t property,
                         const void *value, size_t size) {
    bytes_t payload = {0};
    put_u32(&payload, node);
    put_u16(&payload, property);
    bytes_put(&payload, value, size);
    command(stream, PXA_UI_COMMAND_SET_PROPERTY, 0, &payload);
    free(payload.data);
}

static pxa_status_t control(pxa_runtime_t *runtime, pxa_component_t component,
                            uint16_t opcode, const void *payload, size_t size) {
    uint8_t message[PXA_MAX_CONTROL_MESSAGE];
    pxa_writer_t writer;
    assert(size <= sizeof(message) - PXA_ENVELOPE_SIZE);
    pxa_writer_init(&writer, message, sizeof(message));
    assert(pxa_writer_message(&writer, PXA_UI_SERVICE_ID, opcode, 0,
                              payload, size) == PXA_STATUS_OK);
    return pxa_runtime_control(runtime, component, message, writer.size);
}

static pxa_status_t begin(pxa_runtime_t *runtime, pxa_component_t component,
                          uint32_t transaction, uint32_t generation,
                          uint32_t target, uint8_t kind) {
    uint8_t payload[20] = {0};
    pxa_write_u32(payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(payload + 4, transaction);
    pxa_write_u32(payload + 8, generation);
    pxa_write_u32(payload + 12, target);
    payload[16] = kind;
    payload[17] = PXA_UI_PRESERVE_ON_FAILURE;
    return control(runtime, component, PXA_UI_TX_BEGIN,
                   payload, sizeof(payload));
}

static pxa_status_t write_stream(pxa_runtime_t *runtime,
                                 pxa_component_t component,
                                 uint32_t transaction,
                                 const bytes_t *stream) {
    uint8_t payload[67];
    size_t offset = 0;
    size_t stride = 1;
    while (offset < stream->size) {
        size_t chunk = stride;
        pxa_status_t status;
        if (chunk > 63) chunk = 63;
        if (chunk > stream->size - offset) chunk = stream->size - offset;
        pxa_write_u32(payload, transaction);
        memcpy(payload + 4, stream->data + offset, chunk);
        status = control(runtime, component, PXA_UI_TX_WRITE,
                         payload, chunk + 4u);
        if (status != PXA_STATUS_OK) return status;
        offset += chunk;
        stride = stride * 3u + 1u;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t commit(pxa_runtime_t *runtime, pxa_component_t component,
                           uint32_t transaction) {
    uint8_t payload[4];
    pxa_write_u32(payload, transaction);
    return control(runtime, component, PXA_UI_TX_COMMIT,
                   payload, sizeof(payload));
}

static void test_initial_tree(pxa_runtime_t *runtime,
                              pxa_component_t component,
                              pxa_ui_service_t *service,
                              backend_state_t *backend) {
    bytes_t stream = {0};
    uint8_t event_mask[8];
    pxa_ui_node_snapshot_t node;
    pxa_ui_memory_snapshot_t memory;
    pxa_write_u64(event_mask, UINT64_C(1) << (PXA_UI_EVENT_ACTION - 1u));
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    create_node(&stream, 2, 1, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 3, 2, PXA_UI_NODE_CANVAS, 0);
    create_node(&stream, 4, 2, PXA_UI_NODE_CONTROL,
                PXA_UI_CONTROL_BUTTON);
    pxa_write_u64(event_mask, PXA_UI_EVENT_MASK_CONTROLLER_STATE);
    set_property(&stream, 1, PXA_UI_PROPERTY_EVENT_MASK,
                 event_mask, sizeof(event_mask));
    pxa_write_u64(event_mask, UINT64_C(1) << (PXA_UI_EVENT_ACTION - 1u));
    set_property(&stream, 4, PXA_UI_PROPERTY_EVENT_MASK,
                 event_mask, sizeof(event_mask));
    assert(begin(runtime, component, 7, 1, 0,
                 PXA_UI_REPLACE_SURFACE) == PXA_STATUS_OK);
    assert(write_stream(runtime, component, 7, &stream) == PXA_STATUS_OK);
    assert(commit(runtime, component, 7) == PXA_STATUS_OK);
    assert(backend->begins == 1 && backend->commits == 1);
    assert(pxa_ui_find_node(service, component, 1, 4, &node) ==
           PXA_STATUS_OK);
    assert(node.type == PXA_UI_NODE_CONTROL &&
           node.subtype == PXA_UI_CONTROL_BUTTON &&
           node.event_mask != 0);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.node_count == 4 && memory.canvas_bytes == 0 &&
           memory.transaction_bytes == 0 && memory.current_bytes < 48u * 1024u);
    free(stream.data);
}

static void test_atomic_patch(pxa_runtime_t *runtime,
                              pxa_component_t component,
                              pxa_ui_service_t *service,
                              backend_state_t *backend) {
    bytes_t stream = {0};
    pxa_ui_node_snapshot_t node;
    const char text[] = "new";
    create_node(&stream, 5, 2, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 5, PXA_UI_PROPERTY_TEXT,
                 text, sizeof(text) - 1u);
    assert(begin(runtime, component, 8, 2, 0, PXA_UI_PATCH) ==
           PXA_STATUS_OK);
    assert(write_stream(runtime, component, 8, &stream) == PXA_STATUS_OK);
    backend->reject_commit = 1;
    assert(commit(runtime, component, 8) == PXA_STATUS_DENIED);
    backend->reject_commit = 0;
    assert(backend->cancels == 1);
    assert(pxa_ui_find_node(service, component, 1, 5, &node) ==
           PXA_STATUS_NOT_FOUND);
    assert(begin(runtime, component, 9, 2, 0, PXA_UI_PATCH) ==
           PXA_STATUS_OK);
    assert(write_stream(runtime, component, 9, &stream) == PXA_STATUS_OK);
    assert(commit(runtime, component, 9) == PXA_STATUS_OK);
    assert(pxa_ui_find_node(service, component, 1, 5, &node) ==
           PXA_STATUS_OK);
    free(stream.data);
}

static void test_invalid_utf8(pxa_runtime_t *runtime,
                              pxa_component_t component,
                              backend_state_t *backend) {
    bytes_t stream = {0};
    const uint8_t invalid[] = {0xc0, 0xaf};
    unsigned begins = backend->begins;
    set_property(&stream, 5, PXA_UI_PROPERTY_TEXT,
                 invalid, sizeof(invalid));
    assert(begin(runtime, component, 10, 3, 0, PXA_UI_PATCH) ==
           PXA_STATUS_OK);
    assert(write_stream(runtime, component, 10, &stream) == PXA_STATUS_OK);
    assert(commit(runtime, component, 10) == PXA_STATUS_INVALID_ARGUMENT);
    assert(backend->begins == begins);
    free(stream.data);
}

static void test_canvas_bitmap_validation(void) {
    uint8_t bitmap[4 + 20 + 8] = {0};
    bitmap[0] = PXA_UI_CANVAS_BITMAP_RGB565;
    pxa_write_u16(bitmap + 2, 28);
    pxa_write_u32(bitmap + 12, 2);
    pxa_write_u32(bitmap + 16, 2);
    pxa_write_u32(bitmap + 20, 4);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS |
                                  PXA_UI_FEATURE_RGB565_BITMAP,
                                  bitmap, sizeof(bitmap)) == PXA_STATUS_OK);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS, bitmap,
                                  sizeof(bitmap)) == PXA_STATUS_UNSUPPORTED);
    pxa_write_u32(bitmap + 20, 2);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS |
                                  PXA_UI_FEATURE_RGB565_BITMAP,
                                  bitmap, sizeof(bitmap)) ==
           PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(bitmap + 20, 4);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS |
                                  PXA_UI_FEATURE_RGB565_BITMAP,
                                  bitmap, sizeof(bitmap) - 1u) ==
           PXA_STATUS_INVALID_ARGUMENT);
}

static void test_canvas(pxa_runtime_t *runtime, pxa_component_t component,
                        pxa_ui_service_t *service,
                        backend_state_t *backend) {
    uint8_t begin_payload[16] = {0};
    uint8_t write_payload[12 + 1024] = {0};
    uint8_t present_payload[13] = {0};
    pxa_ui_memory_snapshot_t memory;
    allocator_state_t *allocator = service->config.allocator_context;
    size_t allocations;
    uint32_t frame;
    pxa_write_u32(begin_payload, 1);
    pxa_write_u32(begin_payload + 4, 3);
    pxa_write_u32(begin_payload + 8, 1);
    assert(control(runtime, component, PXA_UI_CANVAS_BEGIN,
                   begin_payload, sizeof(begin_payload)) == PXA_STATUS_OK);
    pxa_write_u32(write_payload, 1);
    pxa_write_u32(write_payload + 4, 3);
    pxa_write_u32(write_payload + 8, 1);
    write_payload[12] = PXA_UI_CANVAS_RECT;
    pxa_write_u16(write_payload + 14, 28);
    pxa_write_u32(write_payload + 24, 10);
    pxa_write_u32(write_payload + 28, 10);
    for (size_t offset = 32; offset < 1024; offset += 32)
        memcpy(write_payload + 12 + offset, write_payload + 12, 32);
    assert(control(runtime, component, PXA_UI_CANVAS_WRITE,
                   write_payload, sizeof(write_payload)) == PXA_STATUS_OK);
    pxa_write_u32(present_payload, 1);
    pxa_write_u32(present_payload + 4, 3);
    pxa_write_u32(present_payload + 8, 1);
    assert(control(runtime, component, PXA_UI_CANVAS_PRESENT,
                   present_payload, sizeof(present_payload)) == PXA_STATUS_OK);
    assert(backend->canvas_presents == 1 && backend->canvas_bytes != NULL);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.canvas_count == 1 && memory.canvas_bytes == 0);

    allocations = 0;
    for (frame = 2; frame <= 8; ++frame) {
        void *previous = backend->canvas_bytes;
        pxa_write_u32(begin_payload + 8, frame);
        pxa_write_u32(write_payload + 8, frame);
        pxa_write_u32(present_payload + 8, frame);
        assert(control(runtime, component, PXA_UI_CANVAS_BEGIN,
                       begin_payload, sizeof(begin_payload)) == PXA_STATUS_OK);
        assert(control(runtime, component, PXA_UI_CANVAS_WRITE,
                       write_payload, sizeof(write_payload)) == PXA_STATUS_OK);
        backend->reject_canvas = 1;
        assert(control(runtime, component, PXA_UI_CANVAS_PRESENT,
                       present_payload, sizeof(present_payload)) == PXA_STATUS_DENIED);
        assert(backend->canvas_bytes == previous);
        backend->reject_canvas = 0;
        assert(control(runtime, component, PXA_UI_CANVAS_PRESENT,
                       present_payload, sizeof(present_payload)) == PXA_STATUS_OK);
        if (frame == 2) allocations = allocator->allocations;
        else assert(allocator->allocations == allocations);
    }

    /* A staged frame is reclaimed when its Canvas leaves the committed tree. */
    pxa_write_u32(begin_payload + 8, 9);
    assert(control(runtime, component, PXA_UI_CANVAS_BEGIN,
                   begin_payload, sizeof(begin_payload)) == PXA_STATUS_OK);
    pxa_write_u32(write_payload + 8, 9);
    assert(control(runtime, component, PXA_UI_CANVAS_WRITE,
                   write_payload, sizeof(write_payload)) == PXA_STATUS_OK);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.canvas_count == 1 && memory.canvas_bytes != 0);
}

static void test_canvas_stream(pxa_runtime_t *runtime,
                               pxa_component_t component,
                               backend_state_t *backend) {
    uint8_t open_payload[12];
    uint8_t begin_payload[16] = {0};
    uint8_t present_payload[13] = {0};
    uint8_t display_list[32] = {0};
    uint8_t event[64];
    pxa_message_view_t decoded;
    pxa_handle_t handle;
    size_t event_size;
    pxa_write_u32(open_payload, 77);
    pxa_write_u32(open_payload + 4, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(open_payload + 8, 3);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(control(runtime, component, PXA_UI_CANVAS_STREAM_OPEN,
                   open_payload, sizeof(open_payload)) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_event_pop(runtime, component, event, sizeof(event),
                         &event_size) == PXA_STATUS_OK);
    assert(pxa_message_decode(event, event_size, sizeof(event), &decoded) ==
           PXA_STATUS_OK);
    assert(decoded.opcode == PXA_UI_CANVAS_STREAM_READY &&
           decoded.payload.size == 12 &&
           pxa_read_u32(decoded.payload.data) == 77 &&
           (int32_t)pxa_read_u32(decoded.payload.data + 8) == PXA_STATUS_OK);
    handle = pxa_read_u32(decoded.payload.data + 4);
    assert(handle != PXA_HANDLE_INVALID);

    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    display_list[0] = PXA_UI_CANVAS_RECT;
    pxa_write_u16(display_list + 2, 28);
    pxa_write_u32(display_list + 12, 10);
    pxa_write_u32(display_list + 16, 10);
    pxa_write_u32(begin_payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(begin_payload + 4, 3);
    pxa_write_u32(begin_payload + 8, 10);
    assert(control(runtime, component, PXA_UI_CANVAS_BEGIN,
                   begin_payload, sizeof(begin_payload)) == PXA_STATUS_OK);
    assert(pxa_runtime_io(runtime, component, handle, PXA_IO_WRITE,
                          display_list, sizeof(display_list)) ==
           (int32_t)sizeof(display_list));
    pxa_write_u32(present_payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(present_payload + 4, 3);
    pxa_write_u32(present_payload + 8, 10);
    assert(control(runtime, component, PXA_UI_CANVAS_PRESENT,
                   present_payload, sizeof(present_payload)) == PXA_STATUS_OK);
    assert(backend->canvas_presents != 0);
    assert(pxa_handle_close(runtime, component, handle) == PXA_STATUS_OK);
    assert(pxa_runtime_io(runtime, component, handle, PXA_IO_WRITE,
                          display_list, sizeof(display_list)) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
}

static void test_buffer_growth(pxa_ui_service_t *service,
                                allocator_state_t *allocator) {
    size_t capacity = 0;
    size_t baseline = allocator->current;
    uint8_t *memory = pxa_ui_grow(service, NULL, 0, &capacity, 65, 65);
    assert(memory != NULL && capacity == 65);
    memset(memory, 0xa5, 65);
    allocator->fail_at = allocator->allocations + 1;
    assert(pxa_ui_grow(service, memory, 65, &capacity, 4096, 4096) == NULL);
    assert(capacity == 65 && memory[64] == 0xa5);
    allocator->fail_at = 0;
    memory = pxa_ui_grow(service, memory, 65, &capacity, 4096, 4096);
    assert(memory != NULL && capacity == 4096);
    for (size_t index = 0; index < 65; ++index) assert(memory[index] == 0xa5);
    pxa_ui_free(service, memory);
    assert(allocator->current == baseline);
}

static void test_events(pxa_runtime_t *runtime, pxa_component_t component,
                        pxa_ui_service_t *service) {
    uint8_t message[128];
    uint8_t controller[8] = {0, 1, 0, 0, 0x18, 0, 0, 0};
    uint8_t controller_update[8] = {0, 1, 0, 0, 0x08, 0, 0, 0};
    uint8_t controller_two[8] = {1, 1, 0, 0, 0x10, 0, 0, 0};
    size_t size;
    pxa_message_view_t decoded;
    assert(pxa_ui_accepts_event(service, component, 1, 4,
                                PXA_UI_EVENT_ACTION));
    assert(!pxa_ui_accepts_event(service, component, 1, 4,
                                 PXA_UI_EVENT_KEY));
    assert(!pxa_ui_accepts_event(service, component, 1, 99,
                                 PXA_UI_EVENT_ACTION));
    assert(pxa_ui_accepts_event(service, component, 1, 1,
                                PXA_UI_EVENT_CONTROLLER_STATE));
    assert(pxa_ui_queue_event(service, component, 1, 4,
                                 PXA_UI_EVENT_ACTION,
                                 PXA_UI_EVENT_FLAG_RELIABLE, 123, NULL, 0) ==
           PXA_STATUS_OK);
    assert(pxa_ui_queue_event(service, component, 1, 1,
                              PXA_UI_EVENT_CONTROLLER_STATE,
                              PXA_UI_EVENT_FLAG_COALESCIBLE, 124,
                              controller, sizeof(controller)) ==
           PXA_STATUS_OK);
    assert(pxa_ui_queue_event(service, component, 1, 1,
                              PXA_UI_EVENT_CONTROLLER_STATE,
                              PXA_UI_EVENT_FLAG_COALESCIBLE, 125,
                              controller_two, sizeof(controller_two)) ==
           PXA_STATUS_OK);
    assert(pxa_ui_queue_event(service, component, 1, 1,
                              PXA_UI_EVENT_CONTROLLER_STATE,
                              PXA_UI_EVENT_FLAG_COALESCIBLE, 126,
                              controller_update,
                              sizeof(controller_update)) == PXA_STATUS_OK);
    assert(pxa_ui_set_pressure(service, component,
                               PXA_UI_PRESSURE_CONSTRAINED) ==
           PXA_STATUS_OK);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
           PXA_STATUS_OK && decoded.opcode == PXA_UI_EVENT);
    assert(pxa_read_u32(decoded.payload.data + 8) == 2);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
           PXA_STATUS_OK && decoded.opcode == PXA_UI_EVENT &&
           pxa_read_u16(decoded.payload.data + 12) ==
               PXA_UI_EVENT_CONTROLLER_STATE &&
           pxa_read_u16(decoded.payload.data + 14) ==
               PXA_UI_EVENT_FLAG_COALESCIBLE &&
           pxa_read_u32(decoded.payload.data + 28) == 0x08);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
               PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_EVENT && decoded.payload.data[24] == 1 &&
           pxa_read_u32(decoded.payload.data + 28) == 0x10);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
           PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_RESOURCE_PRESSURE);
}

static void test_move_and_remove(pxa_runtime_t *runtime,
                                 pxa_component_t component,
                                 pxa_ui_service_t *service) {
    bytes_t stream = {0};
    pxa_ui_node_snapshot_t node;
    pxa_status_t status;
    create_node(&stream, 6, 5, PXA_UI_NODE_TEXT, 0);
    move_node(&stream, 5, 1);
    remove_node(&stream, 2);
    assert(begin(runtime, component, 12, 3, 0, PXA_UI_PATCH) ==
           PXA_STATUS_OK);
    assert(write_stream(runtime, component, 12, &stream) == PXA_STATUS_OK);
    status = commit(runtime, component, 12);
    assert(status == PXA_STATUS_OK);
    assert(pxa_ui_find_node(service, component, 1, 2, &node) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_ui_find_node(service, component, 1, 4, &node) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_ui_find_node(service, component, 1, 5, &node) ==
               PXA_STATUS_OK &&
           node.parent == 1);
    assert(pxa_ui_find_node(service, component, 1, 6, &node) ==
               PXA_STATUS_OK &&
           node.parent == 5);
    free(stream.data);
}

static void test_large_deep_tree(pxa_runtime_t *runtime,
                                 pxa_component_t component,
                                 pxa_ui_service_t *service) {
    bytes_t stream = {0};
    pxa_ui_node_snapshot_t node;
    pxa_ui_memory_snapshot_t memory;
    uint32_t id;
    create_node(&stream, 10000, 0, PXA_UI_NODE_ROOT, 0);
    for (id = 10001; id < 15000; ++id)
        create_node(&stream, id, id - 1u, PXA_UI_NODE_BOX, 0);
    assert(begin(runtime, component, 11, 4, 0,
                 PXA_UI_REPLACE_SURFACE) == PXA_STATUS_OK);
    assert(write_stream(runtime, component, 11, &stream) == PXA_STATUS_OK);
    assert(commit(runtime, component, 11) == PXA_STATUS_OK);
    assert(pxa_ui_find_node(service, component, 1, 14999, &node) ==
           PXA_STATUS_OK && node.parent == 14998);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.node_count == 5000 && memory.transaction_bytes == 0 &&
           memory.canvas_count == 0 && memory.canvas_bytes == 0);
    assert(memory.current_bytes <= memory.node_count * 64u + 4096u);
    free(stream.data);
}

static void test_allocation_failures(pxa_runtime_t *runtime,
                                     pxa_component_t component,
                                     pxa_ui_service_t *service,
                                     allocator_state_t *allocator) {
    bytes_t stream = {0};
    pxa_ui_node_snapshot_t node;
    uint8_t cancel_payload[4];
    uint32_t attempt;
    int committed = 0;
    static const char text[] = "allocation-failure-atomicity";
    create_node(&stream, 20000, 14999, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 20000, PXA_UI_PROPERTY_TEXT,
                 text, sizeof(text) - 1u);
    for (attempt = 1; attempt <= 64 && !committed; ++attempt) {
        uint32_t transaction = 100u + attempt;
        pxa_status_t status;
        assert(begin(runtime, component, transaction, 5, 0,
                     PXA_UI_PATCH) == PXA_STATUS_OK);
        allocator->fail_at = allocator->allocations + attempt;
        status = write_stream(runtime, component, transaction, &stream);
        if (status == PXA_STATUS_OK)
            status = commit(runtime, component, transaction);
        else {
            pxa_write_u32(cancel_payload, transaction);
            assert(control(runtime, component, PXA_UI_TX_CANCEL,
                           cancel_payload, sizeof(cancel_payload)) ==
                   PXA_STATUS_OK);
        }
        allocator->fail_at = 0;
        if (status == PXA_STATUS_OK) {
            committed = 1;
        } else {
            assert(status == PXA_STATUS_RESOURCE_LIMIT);
            assert(pxa_ui_find_node(service, component, 1, 20000, &node) ==
                   PXA_STATUS_NOT_FOUND);
        }
    }
    assert(committed && attempt > 2);
    assert(pxa_ui_find_node(service, component, 1, 20000, &node) ==
           PXA_STATUS_OK);
    free(stream.data);
}

static void test_registry_reserve_rollback(pxa_ui_service_t *service,
                                           pxa_component_t component,
                                           allocator_state_t *allocator) {
    pxa_ui_entry_t *entry = pxa_ui_find_entry(service, component);
    pxa_ui_node_chunk_t *chunk;
    size_t free_nodes = 0;
    size_t chunks_needed = 0;
    size_t current = allocator->current;
    const size_t additional_nodes = 100;
    assert(entry != NULL);
    for (chunk = entry->chunks; chunk != NULL; chunk = chunk->next)
        free_nodes += PXA_UI_NODE_CHUNK_COUNT - chunk->used;
    if (additional_nodes > free_nodes) {
        chunks_needed = (additional_nodes - free_nodes +
                         PXA_UI_NODE_CHUNK_COUNT - 1u) /
                        PXA_UI_NODE_CHUNK_COUNT;
    }
    assert(chunks_needed != 0);
    allocator->fail_at = allocator->allocations + chunks_needed + 1u;
    assert(pxa_ui_registry_reserve(service, entry, additional_nodes) ==
           PXA_STATUS_RESOURCE_LIMIT);
    allocator->fail_at = 0;
    assert(allocator->current == current);
}

static void test_surfaces(pxa_runtime_t *runtime, pxa_component_t component,
                          pxa_ui_service_t *service,
                          backend_state_t *backend) {
    uint8_t open_payload[8] = {0};
    uint8_t close_payload[4];
    uint8_t encoded_environment[PXA_UI_ENVIRONMENT_WIRE_BYTES];
    uint8_t message[256];
    size_t size;
    size_t encoded_size = 0;
    pxa_message_view_t decoded;
    pxa_ui_environment_t environment;
    pxa_ui_memory_snapshot_t memory;
    uint32_t surface;
    pxa_write_u32(open_payload, 42);
    open_payload[4] = PXA_UI_SURFACE_DIALOG;
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(control(runtime, component, PXA_UI_SURFACE_OPEN,
                   open_payload, sizeof(open_payload)) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
               PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_SURFACE_READY &&
           decoded.payload.size == PXA_UI_SURFACE_READY_PAYLOAD_BYTES &&
           pxa_read_u32(decoded.payload.data) == 42);
    surface = pxa_read_u32(decoded.payload.data + 4);
    assert(surface > 1 && backend->surface_opens == 1);
    assert(pxa_ui_get_environment(service, component, surface,
                                     &environment) == PXA_STATUS_OK);
    assert(environment.width == 800 && environment.height == 480);
    assert(pxa_ui_encode_environment(
               &environment, encoded_environment,
               sizeof(encoded_environment), &encoded_size) == PXA_STATUS_OK &&
           encoded_size == PXA_UI_ENVIRONMENT_WIRE_BYTES);
    assert(pxa_ui_encode_environment(
               &environment, encoded_environment,
               sizeof(encoded_environment) - 1u, &encoded_size) ==
           PXA_STATUS_RESOURCE_LIMIT);
    environment.width = 640;
    assert(pxa_ui_update_environment(service, component, &environment) ==
           PXA_STATUS_OK);
    assert(backend->environment_changes == 1);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
               PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_ENVIRONMENT_CHANGED &&
           decoded.payload.size == PXA_UI_ENVIRONMENT_WIRE_BYTES);
    pxa_write_u32(close_payload, surface);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(control(runtime, component, PXA_UI_SURFACE_CLOSE,
                   close_payload, sizeof(close_payload)) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    assert(backend->surface_closes == 1);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK && memory.surface_count == 1);

    /* An app may stop without explicitly closing every secondary Surface. */
    pxa_ui_find_entry(service, component)->next_surface = UINT32_MAX;
    pxa_write_u32(open_payload, 43);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(control(runtime, component, PXA_UI_SURFACE_OPEN,
                   open_payload, sizeof(open_payload)) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
               PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_SURFACE_READY);
    assert(pxa_read_u32(decoded.payload.data + 4) != PXA_UI_PRIMARY_SURFACE);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
               PXA_STATUS_OK &&
           memory.surface_count == 2);
}

int main(void) {
    pxa_runtime_limits_t runtime_limits;
    pxa_runtime_t *runtime = NULL;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_ui_config_t config;
    pxa_ui_service_t *service = NULL;
    pxa_ui_backend_t backend;
    pxa_ui_environment_t primary_environment;
    backend_state_t backend_state;
    allocator_state_t allocator;
    pxa_ui_memory_snapshot_t memory;
    uint64_t clock_us = 0;
    void *runtime_workspace;
    void *service_workspace;
    size_t size;

    memset(&allocator, 0, sizeof(allocator));
    memset(&backend_state, 0, sizeof(backend_state));
    pxa_runtime_limits_init(&runtime_limits);
    size = pxa_runtime_workspace_size(&runtime_limits);
    runtime_workspace = malloc(size);
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace, size, &runtime_limits, &runtime) ==
           PXA_STATUS_OK);
    pxa_ui_config_init(&config);
    config.features = PXA_UI_FEATURE_CANVAS |
                      PXA_UI_FEATURE_RGB565_BITMAP |
                      PXA_UI_FEATURE_CONTROLLER_INPUT |
                      PXA_UI_FEATURE_MULTIPLE_SURFACES |
                      PXA_UI_FEATURE_CANVAS_STREAM_IO;
    config.allocator_context = &allocator;
    config.allocate = test_allocate;
    config.release = test_release;
    config.clock_context = &clock_us;
    config.now_us = test_now_us;
    config.color_scheme = PXA_UI_COLOR_SCHEME_DARK;
    config.primary_width = 296;
    config.primary_height = 240;
    config.safe_insets[0] = 8;
    config.safe_insets[1] = 10;
    config.safe_insets[2] = 8;
    config.safe_insets[3] = 10;
    service_workspace = malloc(pxa_ui_service_workspace_size());
    assert(service_workspace != NULL);
    assert(pxa_ui_service_init(service_workspace,
                                  pxa_ui_service_workspace_size(), runtime,
                                  &config, &service) == PXA_STATUS_OK);
    assert(pxa_ui_service_register(service) == PXA_STATUS_OK);
    test_buffer_growth(service, &allocator);
    assert(pxa_component_create(runtime, 1, &component) == PXA_STATUS_OK);
    memset(&backend, 0, sizeof(backend));
    backend.struct_size = sizeof(backend);
    backend.context = &backend_state;
    backend.begin = backend_begin;
    backend.apply = backend_apply;
    backend.commit = backend_commit;
    backend.cancel = backend_cancel;
    backend.present_canvas = backend_canvas;
    backend.reset = backend_reset;
    backend.surface_open = backend_surface_open;
    backend.surface_close = backend_surface_close;
    backend.environment_changed = backend_environment_changed;
    assert(pxa_ui_bind(service, component, &backend) == PXA_STATUS_OK);
    assert(pxa_ui_get_environment(service, component, PXA_UI_PRIMARY_SURFACE,
                                  &primary_environment) == PXA_STATUS_OK);
    assert(primary_environment.color_scheme == PXA_UI_COLOR_SCHEME_DARK);
    assert(primary_environment.width == 296 &&
           primary_environment.height == 240);
    assert(primary_environment.safe_insets[0] == 8 &&
           primary_environment.safe_insets[1] == 10 &&
           primary_environment.safe_insets[2] == 8 &&
           primary_environment.safe_insets[3] == 10);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    test_initial_tree(runtime, component, service, &backend_state);
    test_registry_reserve_rollback(service, component, &allocator);
    test_atomic_patch(runtime, component, service, &backend_state);
    test_invalid_utf8(runtime, component, &backend_state);
    test_canvas_bitmap_validation();
    test_canvas(runtime, component, service, &backend_state);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);
    test_canvas_stream(runtime, component, &backend_state);
    test_events(runtime, component, service);
    test_surfaces(runtime, component, service, &backend_state);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    test_move_and_remove(runtime, component, service);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    test_large_deep_tree(runtime, component, service);
    test_allocation_failures(runtime, component, service, &allocator);
    assert(pxa_ui_memory_snapshot(service, component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.commit_count != 0 && memory.last_commit_us == 50 &&
           memory.max_commit_us == 50);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_component_request_stop(runtime, component, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(pxa_component_begin_stop(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_stop(runtime, component) == PXA_STATUS_OK);
    assert(backend_state.resets == 1);
    assert(backend_state.surface_closes == 2);
    assert(allocator.current == 0);
    pxa_ui_service_deinit(service);
    pxa_runtime_deinit(runtime);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
