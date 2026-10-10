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
    unsigned input_focus_changes;
    void *focused_handle;
    bool focused;
    int reject_commit;
    int reject_canvas;
    pxa_ui_transaction_info_t transaction;
    void *canvas_bytes;
    pxa_ui_release_fn canvas_release;
    void *canvas_release_context;
} backend_state_t;

static pxa_status_t backend_input_focus(void *context, void *node, bool focused) {
    backend_state_t *state = context;
    ++state->input_focus_changes;
    state->focused_handle = node;
    state->focused = focused;
    return PXA_STATUS_OK;
}

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

static void *test_resize(void *context, void *memory, size_t size) {
    allocator_state_t *state = (allocator_state_t *)context;
    allocation_header_t *old_header =
        (allocation_header_t *)memory - 1;
    allocation_header_t *replacement;
    size_t old_size = old_header->size;
    if (state->fail_at != 0 && state->allocations + 1u == state->fail_at)
        return NULL;
    replacement = (allocation_header_t *)realloc(
        old_header, sizeof(*replacement) + size);
    if (replacement == NULL) return NULL;
    replacement->size = size;
    ++state->allocations;
    state->current = state->current - old_size + size;
    if (state->current > state->peak) state->peak = state->current;
    return replacement + 1;
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

static void test_text_input_focus(pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  backend_state_t *backend) {
    bytes_t stream = {0};
    uint8_t focus[12] = {0};
    uint8_t mask[8] = {0};
    create_node(&stream, 5, 2, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT);
    pxa_write_u64(mask, UINT64_C(1) << (PXA_UI_EVENT_TEXT - 1u));
    set_property(&stream, 5, PXA_UI_PROPERTY_EVENT_MASK, mask, sizeof(mask));
    assert(begin(runtime, component, 8, 2, 0, PXA_UI_PATCH) == PXA_STATUS_OK);
    assert(write_stream(runtime, component, 8, &stream) == PXA_STATUS_OK);
    assert(commit(runtime, component, 8) == PXA_STATUS_OK);
    pxa_write_u32(focus, 1);
    pxa_write_u32(focus + 4, 5);
    focus[8] = 1;
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_OK);
    assert(backend->input_focus_changes == 1 && backend->focused &&
           backend->focused_handle == (void *)(uintptr_t)6);
    focus[8] = 0;
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_OK);
    assert(!backend->focused);
    focus[9] = 1;
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_INVALID_ARGUMENT);
    focus[9] = 0;
    focus[8] = 2;
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_INVALID_ARGUMENT);
    focus[8] = 1;
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 11) ==
           PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(focus + 4, 4); /* A button is not an editor. */
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(focus + 4, 500);
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_NOT_FOUND);
    pxa_write_u32(focus, 500); /* Cannot address another surface/component. */
    pxa_write_u32(focus + 4, 5);
    assert(control(runtime, component, PXA_UI_TEXT_INPUT_FOCUS, focus, 12) ==
           PXA_STATUS_NOT_FOUND);
    assert(backend->input_focus_changes == 2);
    free(stream.data);
}

static void test_empty_text(pxa_runtime_t *runtime, pxa_component_t component,
                           pxa_ui_service_t *service) {
    assert(pxa_ui_queue_event(service, component, 1, 5, PXA_UI_EVENT_TEXT,
        PXA_UI_EVENT_FLAG_RELIABLE, 123, NULL, 0) == PXA_STATUS_OK);
    uint8_t message[64];
    size_t size = 0;
    pxa_message_view_t decoded;
    assert(pxa_event_pop(runtime,component,message,sizeof(message),&size) == PXA_STATUS_OK);
    assert(pxa_message_decode(message,size,sizeof(message),&decoded) == PXA_STATUS_OK);
    assert(decoded.payload.size == 24 && pxa_read_u16(decoded.payload.data + 12) == PXA_UI_EVENT_TEXT);
    uint8_t long_text[PXA_UI_EVENT_TEXT_MAX_BYTES + 1];
    memset(long_text, 'a', sizeof(long_text));
    for (size_t length = 65; length <= PXA_UI_EVENT_TEXT_MAX_BYTES;
         length = PXA_UI_EVENT_TEXT_MAX_BYTES) {
        assert(pxa_ui_queue_event(service, component, 1, 5, PXA_UI_EVENT_TEXT,
            PXA_UI_EVENT_FLAG_RELIABLE, 124, long_text, length) == PXA_STATUS_OK);
        uint8_t large_message[PXA_MAX_CONTROL_MESSAGE];
        assert(pxa_event_pop(runtime, component, large_message, sizeof(large_message), &size) == PXA_STATUS_OK);
        assert(pxa_message_decode(large_message, size, sizeof(large_message), &decoded) == PXA_STATUS_OK);
        assert(decoded.payload.size == 24 + length);
        assert(!memcmp(decoded.payload.data + 24, long_text, length));
        if (length == PXA_UI_EVENT_TEXT_MAX_BYTES) break;
    }
    assert(pxa_ui_queue_event(service, component, 1, 5, PXA_UI_EVENT_TEXT,
        PXA_UI_EVENT_FLAG_RELIABLE, 124, long_text, sizeof(long_text)) == PXA_STATUS_INVALID_ARGUMENT);
    long_text[0] = 0xe4; long_text[1] = 0xb8;
    assert(pxa_ui_queue_event(service, component, 1, 5, PXA_UI_EVENT_TEXT,
        PXA_UI_EVENT_FLAG_RELIABLE, 124, long_text, 2) == PXA_STATUS_INVALID_ARGUMENT);
    uint8_t limit[4];
    pxa_write_u32(limit, 1024);
    assert(pxa_ui_validate_property(PXA_UI_FEATURE_DYNAMIC_TEXT,
        PXA_UI_PROPERTY_TEXT_MAX_BYTES, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT,
        (pxa_bytes_t){limit,4}) == PXA_STATUS_OK);
    assert(pxa_ui_validate_property(0, PXA_UI_PROPERTY_TEXT_MAX_BYTES,
        PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT, (pxa_bytes_t){limit,4}) == PXA_STATUS_UNSUPPORTED);
    assert(pxa_ui_validate_property(PXA_UI_FEATURE_DYNAMIC_TEXT,
        PXA_UI_PROPERTY_TEXT_MAX_BYTES, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON,
        (pxa_bytes_t){limit,4}) == PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(limit, PXA_UI_EVENT_TEXT_MAX_BYTES+1);
    assert(pxa_ui_validate_property(PXA_UI_FEATURE_DYNAMIC_TEXT,
        PXA_UI_PROPERTY_TEXT_MAX_BYTES, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT,
        (pxa_bytes_t){limit,4}) == PXA_STATUS_INVALID_ARGUMENT);
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

static void test_canvas_image_validation(void) {
    uint8_t image[30] = {PXA_UI_CANVAS_IMAGE_HANDLE};
    pxa_write_u16(image + 2, 26);
    pxa_write_u32(image + 12, 2);
    pxa_write_u32(image + 16, 2);
    pxa_write_u64(image + 22, UINT64_C(0x1234567800000042));
    assert(!pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS, image, sizeof(image)));
    assert(pxa_ui_validate_canvas(0,image,sizeof(image)) == PXA_STATUS_UNSUPPORTED);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS,image,sizeof(image)-1) == PXA_STATUS_INVALID_ARGUMENT);
    image[21] = 3;
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS,image,sizeof(image)) == PXA_STATUS_INVALID_ARGUMENT);
    image[21] = 0;
    pxa_write_u64(image + 22,0);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS,image,sizeof(image)) == PXA_STATUS_INVALID_ARGUMENT);
}

static void test_canvas_sized_text_validation(void) {
    uint8_t text[31]={PXA_UI_CANVAS_TEXT_SIZED};
    pxa_write_u16(text+2,27);pxa_write_u32(text+12,80);pxa_write_u32(text+16,32);
    pxa_write_u16(text+24,16);memcpy(text+28,"\xe4\xb9\xa6",3);
    pxa_ui_features_t features=PXA_UI_FEATURE_CANVAS|PXA_UI_FEATURE_SIZED_TEXT;
    assert(pxa_ui_validate_canvas(features,text,sizeof(text))==PXA_STATUS_OK);
    assert(pxa_ui_validate_canvas(PXA_UI_FEATURE_CANVAS,text,sizeof(text))==PXA_STATUS_UNSUPPORTED);
    for(unsigned i=0;i<2;++i){pxa_write_u16(text+24,i?129:7);assert(pxa_ui_validate_canvas(features,text,sizeof(text))==PXA_STATUS_INVALID_ARGUMENT);}
    pxa_write_u16(text+24,128);assert(!pxa_ui_validate_canvas(features,text,sizeof(text)));
    text[26]=3;assert(pxa_ui_validate_canvas(features,text,sizeof(text))==PXA_STATUS_INVALID_ARGUMENT);text[26]=0;
    text[27]=1;assert(pxa_ui_validate_canvas(features,text,sizeof(text))==PXA_STATUS_INVALID_ARGUMENT);text[27]=0;
    text[28]=0xff;assert(pxa_ui_validate_canvas(features,text,sizeof(text))==PXA_STATUS_INVALID_ARGUMENT);
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
    size_t previous_limit = service->config.max_dynamic_bytes;
    uint8_t *memory = pxa_ui_grow(service, NULL, 0, &capacity, 65, 65);
    assert(memory != NULL && capacity == 65);
    memset(memory, 0xa5, 65);
    allocator->fail_at = allocator->allocations + 1;
    assert(pxa_ui_grow(service, memory, 65, &capacity, 4096, 4096) == NULL);
    assert(capacity == 65 && memory[64] == 0xa5);
    allocator->fail_at = 0;
    /* A final-size fit succeeds even when old+new cannot both fit the quota. */
    service->config.max_dynamic_bytes =
        baseline + sizeof(pxa_ui_alloc_header_t) + 4096u;
    memory = pxa_ui_grow(service, memory, 65, &capacity, 4096, 4096);
    assert(memory != NULL && capacity == 4096);
    assert(service->current_bytes == service->config.max_dynamic_bytes);
    for (size_t index = 0; index < 65; ++index) assert(memory[index] == 0xa5);
    pxa_ui_free(service, memory);
    service->config.max_dynamic_bytes = previous_limit;
    assert(allocator->current == baseline);
    service->config.resize = NULL;
    capacity = 0;
    memory = pxa_ui_grow(service, NULL, 0, &capacity, 32, 128);
    assert(memory != NULL);
    memset(memory, 0x5a, 32);
    memory = pxa_ui_grow(service, memory, 32, &capacity, 128, 128);
    assert(memory != NULL && capacity == 128);
    for (size_t index = 0; index < 32; ++index) assert(memory[index] == 0x5a);
    pxa_ui_free(service, memory);
    service->config.resize = test_resize;
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
    environment.display_shape = PXA_UI_DISPLAY_SHAPE_CIRCLE;
    environment.corner_radii[1] = 120;
    assert(pxa_ui_update_environment(service, component, &environment) ==
           PXA_STATUS_OK);
    assert(backend->environment_changes == 1);
    assert(pxa_event_pop(runtime, component, message, sizeof(message), &size) ==
           PXA_STATUS_OK);
    assert(pxa_message_decode(message, size, sizeof(message), &decoded) ==
               PXA_STATUS_OK &&
           decoded.opcode == PXA_UI_ENVIRONMENT_CHANGED &&
           decoded.payload.size == PXA_UI_ENVIRONMENT_WIRE_BYTES);
    assert(pxa_read_u16(decoded.payload.data +
                            PXA_UI_ENVIRONMENT_WIRE_BYTES - 24u) == 12u);
    assert(pxa_read_u32(decoded.payload.data +
                            PXA_UI_ENVIRONMENT_WIRE_BYTES - 20u) ==
           PXA_UI_DISPLAY_SHAPE_CIRCLE);
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

static void test_grid_validation(void) {
    uint8_t tracks[65 * PXA_UI_GRID_TRACK_BYTES] = {0};
    for (uint16_t property = PXA_UI_PROPERTY_GRID_COLUMNS;
         property <= PXA_UI_PROPERTY_GRID_ROWS; ++property) {
        pxa_bytes_t value = {tracks, 64 * PXA_UI_GRID_TRACK_BYTES};
        assert(!pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value));
        value.size = sizeof(tracks);
        assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value) == PXA_STATUS_INVALID_ARGUMENT);
        value.size = 8;
        const uint32_t weights[] = {0, 1, 99, 100, UINT32_MAX};
        tracks[0] = PXA_UI_GRID_FRACTION;
        for (unsigned i = 0; i < sizeof(weights) / sizeof(weights[0]); ++i) {
            pxa_write_u32(tracks + 4, weights[i]);
            pxa_status_t expected = weights[i] >= 1 && weights[i] <= 99 ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
            assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value) == expected);
        }
        tracks[0] = PXA_UI_GRID_FIXED;
        pxa_write_u32(tracks + 4, INT32_MAX);
        assert(!pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value));
        pxa_write_u32(tracks + 4, UINT32_MAX);
        assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value) == PXA_STATUS_INVALID_ARGUMENT);
        memset(tracks, 0, sizeof(tracks));
        for (unsigned reserved = 1; reserved <= 3; ++reserved) {
            tracks[reserved] = 1;
            assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value) == PXA_STATUS_INVALID_ARGUMENT);
            tracks[reserved] = 0;
        }
        tracks[0] = 3;
        assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, property, PXA_UI_NODE_BOX, 0, value) == PXA_STATUS_INVALID_ARGUMENT);
        memset(tracks, 0, sizeof(tracks));
    }
    pxa_bytes_t cell = {tracks, 8};
    assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, PXA_UI_PROPERTY_GRID_CELL, PXA_UI_NODE_BOX, 0, cell) == PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(tracks + 4, 1); pxa_write_u16(tracks + 6, 1);
    assert(!pxa_ui_validate_property(PXA_UI_FEATURE_GRID, PXA_UI_PROPERTY_GRID_CELL, PXA_UI_NODE_BOX, 0, cell));
    pxa_write_u16(tracks + 6, 0);
    assert(pxa_ui_validate_property(PXA_UI_FEATURE_GRID, PXA_UI_PROPERTY_GRID_CELL, PXA_UI_NODE_BOX, 0, cell) == PXA_STATUS_INVALID_ARGUMENT);
}

int main(void) {
    test_grid_validation();
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
                      PXA_UI_FEATURE_CANVAS_STREAM_IO |
                      PXA_UI_FEATURE_TEXT_INPUT_CONTROL | PXA_UI_FEATURE_DYNAMIC_TEXT;
    config.allocator_context = &allocator;
    config.allocate = test_allocate;
    config.release = test_release;
    config.resize = test_resize;
    config.clock_context = &clock_us;
    config.now_us = test_now_us;
    config.color_scheme = PXA_UI_COLOR_SCHEME_DARK;
    config.primary_width = 296;
    config.primary_height = 240;
    config.safe_insets[0] = 8;
    config.safe_insets[1] = 10;
    config.safe_insets[2] = 8;
    config.safe_insets[3] = 10;
    config.display_shape = 1;
    config.corner_radii[0] = 48;
    config.corner_radii[1] = 48;
    config.corner_radii[2] = 48;
    config.corner_radii[3] = 48;
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
    backend.text_input_focus = backend_input_focus;
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
    assert(primary_environment.display_shape == 1 &&
           primary_environment.corner_radii[1] == 48);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    test_initial_tree(runtime, component, service, &backend_state);
    test_registry_reserve_rollback(service, component, &allocator);
    test_atomic_patch(runtime, component, service, &backend_state);
    test_invalid_utf8(runtime, component, &backend_state);
    test_canvas_image_validation();
    test_canvas_sized_text_validation();
    test_canvas_bitmap_validation();
    test_canvas(runtime, component, service, &backend_state);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);
    {
        pxa_ui_theme_snapshot_t theme;
        pxa_message_view_t decoded;
        uint8_t event[128];
        size_t event_size = 0;
        assert(pxa_ui_get_theme(service, &theme) == PXA_STATUS_OK);
        assert(theme.generation == 1 && theme.color_scheme == PXA_UI_COLOR_SCHEME_DARK);
        assert(theme.typography_px[0] == 12 && theme.typography_px[5] == 28);
        theme.generation = 2;
        theme.rgba[PXA_UI_THEME_PRIMARY] = UINT32_C(0x69d8c4ff);
        assert(pxa_ui_update_theme(service, &theme) == PXA_STATUS_OK);
        assert(pxa_event_pop(runtime, component, event, sizeof(event), &event_size) ==
               PXA_STATUS_OK);
        assert(pxa_message_decode(event, event_size, sizeof(event), &decoded) ==
               PXA_STATUS_OK);
        assert(decoded.service == PXA_UI_SERVICE_ID &&
               decoded.opcode == PXA_UI_THEME_CHANGED && decoded.payload.size == 60);
        assert(pxa_read_u32(decoded.payload.data) == 2);
        assert(pxa_read_u32(decoded.payload.data + 8 +
                            4u * PXA_UI_THEME_PRIMARY) == UINT32_C(0x69d8c4ff));
        {
            uint8_t request[32];
            pxa_writer_t writer;
            pxa_writer_init(&writer, request, sizeof(request));
            assert(pxa_writer_message(&writer, PXA_UI_SERVICE_ID,
                                      PXA_UI_THEME_GET, 71, NULL, 0) == PXA_STATUS_OK);
            assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
            assert(pxa_runtime_control(runtime, component, request, writer.size) ==
                   PXA_STATUS_OK);
            assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);
            assert(pxa_event_pop(runtime, component, event, sizeof(event), &event_size) ==
                   PXA_STATUS_OK);
            assert(pxa_message_decode(event, event_size, sizeof(event), &decoded) ==
                   PXA_STATUS_OK);
            assert(decoded.request_id == 71 && decoded.opcode == PXA_UI_THEME_GET &&
                   decoded.payload.size == 4u + PXA_UI_THEME_WIRE_BYTES &&
                   (int32_t)pxa_read_u32(decoded.payload.data) == PXA_STATUS_OK &&
                   pxa_read_u32(decoded.payload.data + 4) == 2);
        }
    }
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
    {
        pxa_component_t editor_component = PXA_COMPONENT_INVALID;
        memset(&backend_state, 0, sizeof(backend_state));
        assert(pxa_component_create(runtime, 3, &editor_component) == PXA_STATUS_OK);
        assert(pxa_ui_bind(service, editor_component, &backend) == PXA_STATUS_OK);
        assert(pxa_component_begin_start(runtime, editor_component) == PXA_STATUS_OK);
        test_initial_tree(runtime, editor_component, service, &backend_state);
        test_text_input_focus(runtime, editor_component, &backend_state);
        assert(pxa_component_finish_start(runtime, editor_component, PXA_STATUS_OK) == PXA_STATUS_OK);
        test_empty_text(runtime, editor_component, service);
        assert(pxa_component_request_stop(runtime, editor_component, PXA_STOP_NORMAL) == PXA_STATUS_OK);
        assert(pxa_component_begin_stop(runtime, editor_component) == PXA_STATUS_OK);
        assert(pxa_component_finish_stop(runtime, editor_component) == PXA_STATUS_OK);
        assert(allocator.current == 0);
    }
    {
        pxa_component_t v1_component = PXA_COMPONENT_INVALID;
        uint8_t event[128];
        uint8_t request[32];
        uint8_t open_payload[12];
        uint8_t frame_payload[16] = {0};
        uint8_t draw_list[32] = {0};
        size_t event_size = 0;
        pxa_message_view_t decoded;
        pxa_writer_t writer;
        pxa_handle64_t stream_handle;
        memset(&backend_state, 0, sizeof(backend_state));
        assert(pxa_component_create(runtime, 2, &v1_component) ==
               PXA_STATUS_OK);
        assert(pxa_ui_bind(service, v1_component, &backend) == PXA_STATUS_OK);
        assert(pxa_component_begin_start(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(pxa_component_set_core_major(runtime, v1_component, 1) ==
               PXA_STATUS_OK);
        assert(pxa_component_validate_import(runtime, v1_component) ==
               PXA_STATUS_OK);
        test_initial_tree(runtime, v1_component, service, &backend_state);
        assert(pxa_component_finish_start(runtime, v1_component,
                                           PXA_STATUS_OK) == PXA_STATUS_OK);
        pxa_writer_init(&writer, request, sizeof(request));
        assert(pxa_writer_message(&writer, PXA_UI_SERVICE_ID,
                                  PXA_UI_THEME_GET, 81, NULL, 0) ==
               PXA_STATUS_OK);
        assert(pxa_component_begin_event(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(pxa_runtime_control(runtime, v1_component,
                                   request, writer.size) == PXA_STATUS_OK);
        assert(pxa_component_finish_event(runtime, v1_component, 1) ==
               PXA_STATUS_OK);
        assert(pxa_event_pop(runtime, v1_component, event, sizeof(event),
                             &event_size) == PXA_STATUS_OK);
        assert(pxa_message_decode(event, event_size, sizeof(event),
                                  &decoded) == PXA_STATUS_OK &&
               decoded.opcode == PXA_UI_THEME_GET &&
               decoded.request_id == 81 && decoded.payload.size == 64);

        pxa_write_u32(frame_payload, PXA_UI_PRIMARY_SURFACE);
        pxa_write_u32(frame_payload + 4, 3);
        pxa_write_u32(frame_payload + 8, 10);
        assert(pxa_component_begin_event(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(control(runtime, v1_component, PXA_UI_CANVAS_BEGIN,
                       frame_payload, sizeof(frame_payload)) == PXA_STATUS_OK);
        assert(pxa_component_finish_event(runtime, v1_component, 1) ==
               PXA_STATUS_OK);
        pxa_write_u32(open_payload, 82);
        pxa_write_u32(open_payload + 4, PXA_UI_PRIMARY_SURFACE);
        pxa_write_u32(open_payload + 8, 3);
        assert(pxa_component_begin_event(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(control(runtime, v1_component, PXA_UI_CANVAS_STREAM_OPEN,
                       open_payload, sizeof(open_payload)) == PXA_STATUS_OK);
        assert(pxa_component_finish_event(runtime, v1_component, 1) ==
               PXA_STATUS_OK);
        assert(pxa_event_pop(runtime, v1_component, event, sizeof(event),
                             &event_size) == PXA_STATUS_OK);
        assert(pxa_message_decode(event, event_size, sizeof(event),
                                  &decoded) == PXA_STATUS_OK &&
               decoded.opcode == PXA_UI_CANVAS_STREAM_READY &&
               decoded.payload.size == 16 &&
               pxa_read_u32(decoded.payload.data) == 82 &&
               (int32_t)pxa_read_u32(decoded.payload.data + 12) ==
                   PXA_STATUS_OK);
        stream_handle = pxa_read_u64(decoded.payload.data + 4);
        assert(stream_handle > UINT32_MAX);
        draw_list[0] = PXA_UI_CANVAS_RECT;
        pxa_write_u16(draw_list + 2, 28);
        pxa_write_u32(draw_list + 12, 10);
        pxa_write_u32(draw_list + 16, 10);
        assert(pxa_component_begin_event(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(pxa_runtime_io64(runtime, v1_component, stream_handle,
                                PXA_IO_WRITE, draw_list,
                                sizeof(draw_list)) ==
               (int32_t)sizeof(draw_list));
        assert(pxa_handle_close64(runtime, v1_component, stream_handle) ==
               PXA_STATUS_OK);
        assert(pxa_runtime_io64(runtime, v1_component, stream_handle,
                                PXA_IO_WRITE, draw_list, 1) ==
               PXA_STATUS_NOT_FOUND);
        assert(pxa_component_finish_event(runtime, v1_component, 1) ==
               PXA_STATUS_OK);
        assert(pxa_component_request_stop(runtime, v1_component,
                                           PXA_STOP_NORMAL) == PXA_STATUS_OK);
        assert(pxa_component_begin_stop(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(pxa_component_finish_stop(runtime, v1_component) ==
               PXA_STATUS_OK);
        assert(allocator.current == 0);
    }
    pxa_ui_service_deinit(service);
    pxa_runtime_deinit(runtime);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
