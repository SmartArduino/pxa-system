#include "pxa/lvgl/pxa_lvgl_ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "pxa/runtime.h"
#include "pxa/ui.h"
#include "pxa/wire.h"

#include "test_lvgl_host.h"

typedef struct {
    size_t size;
} allocation_header_t;

typedef struct {
    size_t current;
    size_t peak;
    size_t allocations;
} allocator_state_t;

typedef struct {
    uint8_t data[4096];
    size_t size;
} bytes_t;

typedef struct {
    lv_point_t point;
    uint32_t timestamp_ms;
    int pressed;
} pointer_input_t;

static pxa_runtime_t *g_runtime;
static pxa_component_t g_component;
static pxa_lvgl_ui_t *g_adapter;
static uint64_t g_now_us;
static uint64_t g_event_timestamp_us;
static unsigned g_events;
static unsigned g_asset_resolves;
static unsigned g_asset_releases;
static unsigned g_execute_depth;
static int g_expect_unlocked_assets;
static int g_missing_asset;
static uint32_t g_event_surface;
static uint32_t g_event_node;
static pxa_ui_event_kind_t g_event_kind;
static uint32_t g_visible_first;
static uint32_t g_visible_count;

static void *test_allocate(void *context, size_t size) {
    allocator_state_t *state = (allocator_state_t *)context;
    allocation_header_t *header =
        (allocation_header_t *)malloc(sizeof(*header) + size);
    if (header == NULL) return NULL;
    header->size = size;
    state->current += size;
    ++state->allocations;
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

static pxa_status_t sync_execute(
    pxa_lvgl_ui_execute_callback_fn callback, void *callback_data,
    void *user_data) {
    (void)user_data;
    ++g_execute_depth;
    callback(callback_data);
    --g_execute_depth;
    return PXA_STATUS_OK;
}

static uint64_t test_now_us(void *user_data) {
    (void)user_data;
    return g_now_us;
}

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data) {
    pointer_input_t *state = (pointer_input_t *)lv_indev_get_user_data(input);
    assert(state != NULL && data != NULL);
    data->point = state->point;
    data->state = state->pressed ? LV_INDEV_STATE_PRESSED
                                 : LV_INDEV_STATE_RELEASED;
    data->timestamp = state->timestamp_ms;
}

static void on_event(uint32_t surface, uint32_t node,
                     pxa_ui_event_kind_t kind, uint16_t flags,
                     const void *value, size_t value_size, void *user_data) {
    (void)flags;
    (void)user_data;
    ++g_events;
    g_event_timestamp_us = pxa_lvgl_ui_event_timestamp_us(g_adapter);
    g_event_surface = surface;
    g_event_node = node;
    g_event_kind = kind;
    if (kind == PXA_UI_EVENT_VISIBLE_RANGE) {
        assert(value != NULL && value_size == 8);
        g_visible_first = pxa_read_u32((const uint8_t *)value);
        g_visible_count = pxa_read_u32((const uint8_t *)value + 4);
    }
}

static const void *resolve_asset(const uint8_t *path, size_t path_size,
                                 void *user_data) {
    static const char expected[] = "assets/test.png";
    static const uint8_t pixel[] = {0xff, 0xff, 0xff, 0xff};
    static lv_image_dsc_t image;
    (void)user_data;
    if (g_expect_unlocked_assets) assert(g_execute_depth == 0);
    if (g_missing_asset) return NULL;
    assert(path_size == sizeof(expected) - 1u);
    assert(memcmp(path, expected, path_size) == 0);
    ++g_asset_resolves;
    if (image.header.magic == 0) {
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_ARGB8888;
        image.header.w = 1;
        image.header.h = 1;
        image.data_size = sizeof(pixel);
        image.data = pixel;
    }
    return &image;
}

static void release_asset(const void *source, void *user_data) {
    (void)source;
    (void)user_data;
    assert(source != NULL);
    ++g_asset_releases;
}

static void put_data(bytes_t *bytes, const void *data, size_t size) {
    assert(size <= sizeof(bytes->data) - bytes->size);
    if (size != 0) memcpy(bytes->data + bytes->size, data, size);
    bytes->size += size;
}

static void put_u8(bytes_t *bytes, uint8_t value) {
    put_data(bytes, &value, sizeof(value));
}

static void put_u16(bytes_t *bytes, uint16_t value) {
    uint8_t encoded[2];
    pxa_write_u16(encoded, value);
    put_data(bytes, encoded, sizeof(encoded));
}

static void put_u32(bytes_t *bytes, uint32_t value) {
    uint8_t encoded[4];
    pxa_write_u32(encoded, value);
    put_data(bytes, encoded, sizeof(encoded));
}

static void command(bytes_t *stream, uint8_t opcode, const bytes_t *payload) {
    assert(payload->size <= UINT16_MAX);
    put_u8(stream, opcode);
    put_u8(stream, 0);
    put_u16(stream, (uint16_t)payload->size);
    put_data(stream, payload->data, payload->size);
}

static void create_node(bytes_t *stream, uint32_t node, uint32_t parent,
                        uint8_t type, uint8_t subtype) {
    bytes_t payload = {{0}, 0};
    put_u32(&payload, node);
    put_u32(&payload, parent);
    put_u32(&payload, 0);
    put_u8(&payload, type);
    put_u8(&payload, subtype);
    put_u16(&payload, 0);
    command(stream, PXA_UI_COMMAND_CREATE, &payload);
}

static void set_property(bytes_t *stream, uint32_t node, uint16_t property,
                         const void *value, size_t size) {
    bytes_t payload = {{0}, 0};
    put_u32(&payload, node);
    put_u16(&payload, property);
    put_data(&payload, value, size);
    command(stream, PXA_UI_COMMAND_SET_PROPERTY, &payload);
}

static pxa_status_t control(uint16_t opcode, const void *payload, size_t size) {
    uint8_t message[PXA_MAX_CONTROL_MESSAGE];
    pxa_writer_t writer;
    pxa_writer_init(&writer, message, sizeof(message));
    assert(pxa_writer_message(&writer, PXA_UI_SERVICE_ID, opcode, 0,
                              payload, size) == PXA_STATUS_OK);
    return pxa_runtime_control(g_runtime, g_component, message, writer.size);
}

static void transact(uint32_t transaction, uint32_t generation,
                     uint32_t target, uint8_t kind, const bytes_t *stream) {
    uint8_t begin_payload[20] = {0};
    uint8_t write_payload[68];
    uint8_t commit_payload[4];
    size_t offset = 0;
    pxa_write_u32(begin_payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(begin_payload + 4, transaction);
    pxa_write_u32(begin_payload + 8, generation);
    pxa_write_u32(begin_payload + 12, target);
    begin_payload[16] = kind;
    begin_payload[17] = PXA_UI_PRESERVE_ON_FAILURE;
    assert(control(PXA_UI_TX_BEGIN, begin_payload,
                   sizeof(begin_payload)) == PXA_STATUS_OK);
    while (offset < stream->size) {
        size_t chunk = stream->size - offset;
        if (chunk > sizeof(write_payload) - 4u)
            chunk = sizeof(write_payload) - 4u;
        pxa_write_u32(write_payload, transaction);
        memcpy(write_payload + 4, stream->data + offset, chunk);
        assert(control(PXA_UI_TX_WRITE, write_payload, chunk + 4u) ==
               PXA_STATUS_OK);
        offset += chunk;
    }
    pxa_write_u32(commit_payload, transaction);
    assert(control(PXA_UI_TX_COMMIT, commit_payload,
                   sizeof(commit_payload)) == PXA_STATUS_OK);
}

static lv_obj_t *find_type(lv_obj_t *object, const lv_obj_class_t *class_p) {
    uint32_t index;
    if (lv_obj_check_type(object, class_p)) return object;
    for (index = 0; index < lv_obj_get_child_count(object); ++index) {
        lv_obj_t *found = find_type(lv_obj_get_child(object, (int32_t)index),
                                    class_p);
        if (found != NULL) return found;
    }
    return NULL;
}

static lv_obj_t *find_label(lv_obj_t *object, const char *text) {
    uint32_t index;
    if (lv_obj_check_type(object, &lv_label_class) &&
        strcmp(lv_label_get_text(object), text) == 0)
        return object;
    for (index = 0; index < lv_obj_get_child_count(object); ++index) {
        lv_obj_t *found = find_label(
            lv_obj_get_child(object, (int32_t)index), text);
        if (found != NULL) return found;
    }
    return NULL;
}

static void build_initial_surface(void) {
    bytes_t stream = {{0}, 0};
    uint8_t event_mask[8];
    uint8_t pointer_mask[8];
    uint8_t range_mask[8];
    uint8_t item_count[4];
    uint8_t item_extent[4];
    uint8_t icon[4];
    uint8_t fill[8] = {PXA_UI_LENGTH_FILL, 0, 0, 0, 0, 0, 0, 0};
    uint8_t canvas_size[8] = {
        PXA_UI_LENGTH_LOGICAL_PX, 0, 0, 0, 0, 20, 0, 0};
    uint8_t viewport_half[8] = {
        PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16, 0, 0, 0, 0, 0, 0, 0};
    static const char title[] = "UI ABI 0.3";
    static const char button[] = "Apply";
    static const char direct_button[] = "Direct";
    pxa_write_u64(event_mask,
                  UINT64_C(1) << (PXA_UI_EVENT_ACTION - 1u));
    pxa_write_u64(pointer_mask,
                  UINT64_C(1) << (PXA_UI_EVENT_POINTER - 1u));
    pxa_write_u64(range_mask,
                  UINT64_C(1) << (PXA_UI_EVENT_VISIBLE_RANGE - 1u));
    pxa_write_u32(item_count, 100);
    pxa_write_u32(item_extent, 10u * 64u);
    pxa_write_u32(icon, 5);
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    create_node(&stream, 2, 1, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 3, 2, PXA_UI_NODE_TEXT, 0);
    create_node(&stream, 4, 2, PXA_UI_NODE_CONTROL,
                PXA_UI_CONTROL_BUTTON);
    create_node(&stream, 5, 2, PXA_UI_NODE_CANVAS, 0);
    create_node(&stream, 6, 2, PXA_UI_NODE_VIRTUAL_LIST, 0);
    create_node(&stream, 7, 4, PXA_UI_NODE_TEXT, 0);
    create_node(&stream, 8, 2, PXA_UI_NODE_CONTROL,
                PXA_UI_CONTROL_BUTTON);
    create_node(&stream, 9, 2, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 1, PXA_UI_PROPERTY_WIDTH, fill, sizeof(fill));
    set_property(&stream, 1, PXA_UI_PROPERTY_HEIGHT, fill, sizeof(fill));
    pxa_write_u32(viewport_half + 4, UINT32_C(1) << 15);
    set_property(&stream, 2, PXA_UI_PROPERTY_WIDTH,
                 viewport_half, sizeof(viewport_half));
    set_property(&stream, 3, PXA_UI_PROPERTY_TEXT,
                 title, sizeof(title) - 1u);
    set_property(&stream, 7, PXA_UI_PROPERTY_TEXT,
                 button, sizeof(button) - 1u);
    set_property(&stream, 8, PXA_UI_PROPERTY_TEXT,
                 direct_button, sizeof(direct_button) - 1u);
    set_property(&stream, 9, PXA_UI_PROPERTY_ICON, icon, sizeof(icon));
    set_property(&stream, 4, PXA_UI_PROPERTY_EVENT_MASK,
                 event_mask, sizeof(event_mask));
    set_property(&stream, 5, PXA_UI_PROPERTY_WIDTH,
                 canvas_size, sizeof(canvas_size));
    set_property(&stream, 5, PXA_UI_PROPERTY_HEIGHT,
                 canvas_size, sizeof(canvas_size));
    set_property(&stream, 5, PXA_UI_PROPERTY_EVENT_MASK,
                 pointer_mask, sizeof(pointer_mask));
    set_property(&stream, 6, PXA_UI_PROPERTY_WIDTH, fill, sizeof(fill));
    set_property(&stream, 6, PXA_UI_PROPERTY_HEIGHT,
                 canvas_size, sizeof(canvas_size));
    set_property(&stream, 6, PXA_UI_PROPERTY_ITEM_COUNT,
                 item_count, sizeof(item_count));
    set_property(&stream, 6, PXA_UI_PROPERTY_ITEM_EXTENT,
                 item_extent, sizeof(item_extent));
    set_property(&stream, 6, PXA_UI_PROPERTY_EVENT_MASK,
                 range_mask, sizeof(range_mask));
    transact(1, 1, 0, PXA_UI_REPLACE_SURFACE, &stream);
}

static void patch_title(void) {
    bytes_t stream = {{0}, 0};
    static const char title[] = "Patched without rebuilding";
    set_property(&stream, 3, PXA_UI_PROPERTY_TEXT,
                 title, sizeof(title) - 1u);
    transact(2, 2, 0, PXA_UI_PATCH, &stream);
}

static void present_canvas(uint32_t generation, uint8_t dirty_count) {
    uint8_t begin_payload[16] = {0};
    uint8_t write_payload[12 + 64] = {0};
    uint8_t present_payload[13 + 16] = {0};
    bytes_t frame = {{0}, 0};
    bytes_t payload = {{0}, 0};
    size_t offset = 0;
    unsigned depth;
    static const char text[] = "Canvas v2";
    static const uint8_t bitmap[] = {
        0x00, 0xf8, 0xe0, 0x07,
        0x1f, 0x00, 0xff, 0xff,
    };
    const char *asset = g_missing_asset ? "assets/missing.png" : "assets/test.png";
    pxa_write_u32(begin_payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(begin_payload + 4, 5);
    pxa_write_u32(begin_payload + 8, generation);
    assert(control(PXA_UI_CANVAS_BEGIN, begin_payload,
                   sizeof(begin_payload)) == PXA_STATUS_OK);
    for (depth = 0; depth < 12; ++depth) {
        payload.size = 0;
        put_u32(&payload, depth);
        put_u32(&payload, depth);
        put_u32(&payload, 80u - depth * 2u);
        put_u32(&payload, 80u - depth * 2u);
        command(&frame, PXA_UI_CANVAS_CLIP_PUSH, &payload);
    }
    payload.size = 0;
    put_u32(&payload, 2); put_u32(&payload, 2);
    put_u32(&payload, 40); put_u32(&payload, 30);
    put_u32(&payload, UINT32_C(0x34c785ff));
    put_u16(&payload, 4); put_u16(&payload, 1);
    put_u32(&payload, UINT32_C(0xffffffff));
    command(&frame, PXA_UI_CANVAS_RECT, &payload);
    payload.size = 0;
    put_u32(&payload, 8); put_u32(&payload, 8);
    put_u32(&payload, 24); put_u32(&payload, 18);
    put_u32(&payload, UINT32_C(0xf5bd4fff));
    put_u16(&payload, 1); put_u32(&payload, UINT32_C(0xffffffff));
    command(&frame, PXA_UI_CANVAS_ELLIPSE, &payload);
    payload.size = 0;
    put_u32(&payload, 4); put_u32(&payload, 40);
    put_u32(&payload, 50); put_u32(&payload, 40);
    put_u32(&payload, UINT32_C(0xffffffff)); put_u16(&payload, 2);
    command(&frame, PXA_UI_CANVAS_LINE, &payload);
    payload.size = 0;
    put_u32(&payload, 40); put_u32(&payload, 40); put_u32(&payload, 16);
    put_u16(&payload, 0); put_u16(&payload, 180);
    put_u32(&payload, UINT32_C(0xef5d67ff)); put_u16(&payload, 2);
    command(&frame, PXA_UI_CANVAS_ARC, &payload);
    payload.size = 0;
    put_u32(&payload, 2); put_u32(&payload, 52); put_u32(&payload, 72);
    put_u32(&payload, UINT32_C(0xffffffff));
    put_u8(&payload, 1); put_u8(&payload, 1);
    put_data(&payload, text, sizeof(text) - 1u);
    command(&frame, PXA_UI_CANVAS_TEXT, &payload);
    payload.size = 0;
    put_u32(&payload, 2); put_u32(&payload, 2);
    put_u32(&payload, 16); put_u32(&payload, 16);
    put_u8(&payload, 255); put_u8(&payload, 0);
    put_data(&payload, asset, strlen(asset));
    command(&frame, PXA_UI_CANVAS_IMAGE, &payload);
    payload.size = 0;
    put_u32(&payload, 50); put_u32(&payload, 20);
    put_u32(&payload, 2); put_u32(&payload, 2); put_u32(&payload, 4);
    put_data(&payload, bitmap, sizeof(bitmap));
    command(&frame, PXA_UI_CANVAS_BITMAP_RGB565, &payload);
    for (depth = 0; depth < 12; ++depth) {
        payload.size = 0;
        command(&frame, PXA_UI_CANVAS_CLIP_POP, &payload);
    }
    while (offset < frame.size) {
        size_t chunk = frame.size - offset;
        if (chunk > 64) chunk = 64;
        pxa_write_u32(write_payload, PXA_UI_PRIMARY_SURFACE);
        pxa_write_u32(write_payload + 4, 5);
        pxa_write_u32(write_payload + 8, generation);
        memcpy(write_payload + 12, frame.data + offset, chunk);
        assert(control(PXA_UI_CANVAS_WRITE, write_payload,
                       chunk + 12u) == PXA_STATUS_OK);
        offset += chunk;
    }
    pxa_write_u32(present_payload, PXA_UI_PRIMARY_SURFACE);
    pxa_write_u32(present_payload + 4, 5);
    pxa_write_u32(present_payload + 8, generation);
    present_payload[12] = dirty_count;
    pxa_write_u32(present_payload + 13, 2);
    pxa_write_u32(present_payload + 17, 2);
    pxa_write_u32(present_payload + 21, 4);
    pxa_write_u32(present_payload + 25, 4);
    g_expect_unlocked_assets = 1;
    assert(control(PXA_UI_CANVAS_PRESENT, present_payload,
                   13u + (size_t)dirty_count * 16u) ==
           (g_missing_asset ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_OK));
    g_expect_unlocked_assets = 0;
}

static void replace_content_subtree(void) {
    bytes_t stream = {{0}, 0};
    static const char title[] = "Replacement subtree";
    create_node(&stream, 2, 1, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 6, 2, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 6, PXA_UI_PROPERTY_TEXT,
                 title, sizeof(title) - 1u);
    transact(3, 3, 2, PXA_UI_REPLACE_SUBTREE, &stream);
}

int main(void) {
    pxa_runtime_limits_t runtime_limits;
    pxa_ui_config_t service_config;
    pxa_lvgl_ui_config_t adapter_config;
    pxa_ui_service_t *service = NULL;
    pxa_lvgl_ui_t *adapter = NULL;
    pxa_ui_backend_t backend;
    pxa_ui_node_snapshot_t snapshot;
    pxa_ui_memory_snapshot_t memory;
    allocator_state_t allocator = {0};
    void *runtime_workspace;
    void *service_workspace;
    void *adapter_workspace;
    lv_obj_t *root;
    lv_obj_t *button;
    lv_obj_t *box;
    lv_obj_t *direct_button;
    lv_obj_t *icon;
    lv_obj_t *list;
    lv_obj_t *canvas;
    lv_indev_t *pointer_input;
    pointer_input_t pointer_state = {{0, 0}, 0, 0};
    lv_area_t canvas_area;
    size_t size;
    size_t canvas_resident;
    uint32_t flushes;

    test_lvgl_init();
    pxa_runtime_limits_init(&runtime_limits);
    runtime_limits.max_components = 1;
    size = pxa_runtime_workspace_size(&runtime_limits);
    runtime_workspace = malloc(size);
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace, size, &runtime_limits,
                            &g_runtime) == PXA_STATUS_OK);

    pxa_ui_config_init(&service_config);
    service_config.features = PXA_UI_FEATURE_CANVAS |
                              PXA_UI_FEATURE_RGB565_BITMAP |
                              PXA_UI_FEATURE_VIRTUAL_LIST;
    service_config.allocator_context = &allocator;
    service_config.allocate = test_allocate;
    service_config.release = test_release;
    service_workspace = malloc(pxa_ui_service_workspace_size());
    assert(service_workspace != NULL);
    assert(pxa_ui_service_init(service_workspace,
                                  pxa_ui_service_workspace_size(),
                                  g_runtime, &service_config,
                                  &service) == PXA_STATUS_OK);
    assert(pxa_ui_service_register(service) == PXA_STATUS_OK);

    memset(&adapter_config, 0, sizeof(adapter_config));
    adapter_config.struct_size = sizeof(adapter_config);
    adapter_config.allocator_context = &allocator;
    adapter_config.allocate = test_allocate;
    adapter_config.release = test_release;
    adapter_config.execute = sync_execute;
    adapter_config.resolve_asset = resolve_asset;
    adapter_config.release_asset = release_asset;
    adapter_config.event_callback = on_event;
    adapter_config.now_us = test_now_us;
    adapter_config.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    adapter_config.primary_environment.width = 320;
    adapter_config.primary_environment.height = 240;
    adapter_config.primary_environment.density_q16 = UINT32_C(1) << 16;
    adapter_config.primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    pxa_lvgl_ui_theme_init(&adapter_config.theme);
    adapter_config.theme.body_font = &lv_font_montserrat_14;
    adapter_config.theme.icon_font = &lv_font_montserrat_20;
    adapter_workspace = malloc(pxa_lvgl_ui_workspace_size());
    assert(adapter_workspace != NULL);
    assert(pxa_lvgl_ui_init(adapter_workspace,
                               pxa_lvgl_ui_workspace_size(),
                               &adapter_config, &adapter,
                               &backend) == PXA_STATUS_OK);
    g_adapter = adapter;

    assert(pxa_component_create(g_runtime, 1, &g_component) == PXA_STATUS_OK);
    assert(pxa_ui_bind(service, g_component, &backend) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(g_runtime, g_component) == PXA_STATUS_OK);

    build_initial_surface();
    assert(lv_obj_get_child_count(lv_screen_active()) == 1);
    root = lv_obj_get_child(lv_screen_active(), 0);
    assert(root != NULL && find_label(root, "UI ABI 0.3") != NULL);
    assert(!lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE));
    assert(lv_obj_has_flag(root, LV_OBJ_FLAG_CLICKABLE));
    box = lv_obj_get_child(root, 0);
    assert(lv_obj_get_width(box) == 160);
    button = find_type(root, &lv_button_class);
    assert(button != NULL);
    assert(lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE));
    assert(lv_obj_get_child_count(button) == 1);
    assert(find_label(button, "Apply") != NULL);
    direct_button = lv_obj_get_child(box, 4);
    assert(lv_obj_check_type(direct_button, &lv_button_class));
    assert(lv_obj_get_child_count(direct_button) == 1);
    assert(find_label(direct_button, "Direct") != NULL);
    icon = lv_obj_get_child(box, 5);
    assert(lv_obj_get_style_text_font(icon, LV_PART_MAIN) ==
           &lv_font_montserrat_20);
    lv_obj_send_event(root, LV_EVENT_CLICKED, NULL);
    assert(g_events == 0);
    g_now_us = UINT64_C(123456000);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    assert(g_events == 1 && g_event_surface == PXA_UI_PRIMARY_SURFACE &&
           g_event_node == 4 && g_event_kind == PXA_UI_EVENT_ACTION &&
           g_event_timestamp_us == g_now_us);
    assert(pxa_lvgl_ui_event_timestamp_us(adapter) == 0);
    list = lv_obj_get_child(lv_obj_get_child(root, 0), 3);
    assert(list != NULL);
    lv_obj_scroll_to_y(list, 100, LV_ANIM_OFF);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
    assert(g_events == 2 && g_event_node == 6 &&
           g_event_kind == PXA_UI_EVENT_VISIBLE_RANGE &&
           g_visible_count != 0 && g_visible_first < 100);

    assert(pxa_ui_memory_snapshot(service, g_component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.node_count == 9 && memory.canvas_bytes == 0);
    patch_title();
    assert(lv_obj_get_child(lv_screen_active(), 0) == root);
    assert(find_label(root, "Patched without rebuilding") != NULL);

    lv_display_set_render_mode(g_test_display, LV_DISPLAY_RENDER_MODE_PARTIAL);
    present_canvas(1, 0);
    assert(g_asset_resolves == 1 && g_asset_releases == 0);
    canvas_resident = allocator.current;
    canvas = lv_obj_get_child(box, 2);
    assert(canvas != NULL && lv_obj_check_type(canvas, &lv_obj_class));
    lv_obj_update_layout(root);
    lv_obj_get_coords(canvas, &canvas_area);
    lv_obj_move_foreground(canvas);
    pointer_input = lv_indev_create();
    assert(pointer_input != NULL);
    lv_indev_set_type(pointer_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer_input, g_test_display);
    lv_indev_set_user_data(pointer_input, &pointer_state);
    lv_indev_set_read_cb(pointer_input, read_pointer);
    pointer_state.point.x = canvas_area.x1 + 2;
    pointer_state.point.y = canvas_area.y1 + 2;
    pointer_state.pressed = 1;
    g_now_us = UINT64_C(200000000);
    pointer_state.timestamp_ms = (uint32_t)(g_now_us / 1000u) - 7u;
    lv_indev_read(pointer_input);
    assert(g_event_kind == PXA_UI_EVENT_POINTER &&
           g_event_timestamp_us == g_now_us - 7000u);
    pointer_state.pressed = 0;
    pointer_state.timestamp_ms = (uint32_t)(g_now_us / 1000u);
    lv_indev_read(pointer_input);
    lv_indev_delete(pointer_input);
    g_test_rgb565_x = canvas_area.x1 + 50;
    g_test_rgb565_y = canvas_area.y1 + 20;
    g_test_rgb565_pixels = 0;
    flushes = g_test_flush_count;
    test_lvgl_tick();
    assert(g_test_flush_count > flushes);
    assert(g_test_rgb565_pixels == UINT8_C(0x0f));
    {
        size_t allocations;
        uint64_t pixels = g_test_flushed_pixels;
        present_canvas(2, 1);
        allocations = allocator.allocations;
        test_lvgl_tick();
        assert(g_test_flushed_pixels > pixels);
        assert(g_test_flushed_pixels - pixels < 80u * 80u);
        present_canvas(3, 1);
        assert(allocator.allocations == allocations);
        test_lvgl_tick();
        assert(g_asset_resolves == 1 && g_asset_releases == 0);
        {
            size_t resident = allocator.current;
            g_missing_asset = 1;
            present_canvas(4, 1);
            g_missing_asset = 0;
            assert(allocator.current == resident);
            assert(g_asset_resolves == 1 && g_asset_releases == 0);
            present_canvas(5, 1);
            test_lvgl_tick();
        }
    }
    assert(g_asset_resolves == 1 && g_asset_releases == 0);

    replace_content_subtree();
    assert(lv_obj_get_child(lv_screen_active(), 0) == root);
    assert(find_label(root, "Replacement subtree") != NULL);
    assert(find_type(root, &lv_button_class) == NULL);
    assert(pxa_ui_find_node(service, g_component,
                               PXA_UI_PRIMARY_SURFACE, 4,
                               &snapshot) == PXA_STATUS_NOT_FOUND);
    assert(pxa_ui_find_node(service, g_component,
                               PXA_UI_PRIMARY_SURFACE, 6,
                               &snapshot) == PXA_STATUS_OK);
    assert(allocator.current < canvas_resident);
    assert(g_asset_resolves == 1 && g_asset_releases == 1);

    assert(pxa_component_finish_start(g_runtime, g_component,
                                      PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_component_request_stop(g_runtime, g_component,
                                      PXA_STOP_NORMAL) == PXA_STATUS_OK);
    assert(pxa_component_begin_stop(g_runtime, g_component) == PXA_STATUS_OK);
    assert(pxa_component_finish_stop(g_runtime, g_component) == PXA_STATUS_OK);
    assert(lv_obj_get_child_count(lv_screen_active()) == 0);
    assert(allocator.current == 0);

    pxa_lvgl_ui_deinit(adapter);
    pxa_ui_service_deinit(service);
    pxa_runtime_deinit(g_runtime);
    free(adapter_workspace);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
