/* Assertions also execute fixture setup; keep them in Release test builds. */
#undef NDEBUG
#include "pxa/lvgl/pxa_lvgl_ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "src/image/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "pxa/runtime.h"
#include "pxa/ui.h"
#include "pxa/wire.h"

#include "test_lvgl_host.h"

static unsigned g_native_snapshot_calls;
#ifdef PXA_TEST_WRAP_SNAPSHOT
static unsigned g_snapshot_draw_calls, g_snapshot_fail_at;
lv_result_t __real_lv_snapshot_take_to_draw_buf(lv_obj_t *, lv_color_format_t, lv_draw_buf_t *);
lv_result_t __wrap_lv_snapshot_take_to_draw_buf(lv_obj_t *object, lv_color_format_t format,
                                                lv_draw_buf_t *draw) {
    if (++g_snapshot_draw_calls == g_snapshot_fail_at) return LV_RESULT_INVALID;
    return __real_lv_snapshot_take_to_draw_buf(object,format,draw);
}
lv_draw_buf_t *__real_lv_snapshot_take(lv_obj_t *object, lv_color_format_t format);
lv_draw_buf_t *__wrap_lv_snapshot_take(lv_obj_t *object, lv_color_format_t format) {
    ++g_native_snapshot_calls;
    return __real_lv_snapshot_take(object, format);
}
#endif

typedef struct {
    size_t size;
} allocation_header_t;

typedef struct {
    size_t current;
    size_t peak;
    size_t allocations;
    size_t fail_at;
    size_t fail_size;
    unsigned size_failures;
    unsigned fail_during_execute;
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
static uint32_t g_input_test_tick;
static uint32_t input_test_tick(void) { return g_input_test_tick; }
static unsigned g_events;
static unsigned g_asset_resolves;
static unsigned g_asset_releases;
static unsigned g_execute_depth;
static int g_expect_unlocked_assets;
static int g_missing_asset;
static uint32_t g_event_surface;
static uint32_t g_event_node;
static pxa_ui_event_kind_t g_event_kind;
static uint8_t g_event_value[PXA_UI_EVENT_TEXT_MAX_BYTES];
static size_t g_event_value_size;
static uint32_t g_visible_first;
static uint32_t g_visible_count;

static pxa_asset_object_t *g_prepared_image;
static pxa_asset_object_t *g_alternate_image;
static unsigned g_image_acquires, g_image_frees;
static const uint64_t image_handle=UINT64_C(0x1234567800000042);
static uint64_t monotonic_ns(void) {
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC,&now));
    return (uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
}
static uint64_t snapshot_hash(lv_obj_t *object) {
    lv_draw_buf_t *snapshot=lv_snapshot_take(object,LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot);
    uint64_t hash=UINT64_C(14695981039346656037);
    for (unsigned y=0;y<snapshot->header.h;++y)
        for (unsigned x=0;x<snapshot->header.w*4u;++x)
            hash=(hash^snapshot->data[y*snapshot->header.stride+x])*UINT64_C(1099511628211);
    lv_draw_buf_destroy(snapshot);
    return hash;
}
static void *image_allocate(void *ctx,size_t size) { (void)ctx; return malloc(size); }
static void image_release(void *ctx,void *memory) { (void)ctx; ++g_image_frees; free(memory); }
static pxa_status_t acquire_image(pxa_component_t component,pxa_handle64_t handle,pxa_asset_object_t **out,void *ctx) {
    (void)ctx; assert(!g_execute_depth); *out=NULL;
    if (component!=g_component) return PXA_STATUS_DENIED;
    pxa_asset_object_t *image=handle==image_handle ? g_prepared_image :
        handle==image_handle+1 ? g_alternate_image : NULL;
    if (!image) return PXA_STATUS_NOT_FOUND;
    ++g_image_acquires; pxa_asset_object_retain(image); *out=image;
    return PXA_STATUS_OK;
}

static void *test_allocate(void *context, size_t size) {
    allocator_state_t *state = (allocator_state_t *)context;
    if (state->fail_size && state->fail_size == size) {
        ++state->size_failures;
        return NULL;
    }
    if (state->fail_at && state->allocations + 1 == state->fail_at) return NULL;
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
    allocator_state_t *allocator = user_data;
    size_t saved_failure = allocator ? allocator->fail_at : 0;
    if (allocator && allocator->fail_during_execute)
        allocator->fail_at = allocator->allocations + allocator->fail_during_execute;
    ++g_execute_depth;
    callback(callback_data);
    --g_execute_depth;
    if (allocator) allocator->fail_at = saved_failure;
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
    g_event_value_size = value_size;
    if (value_size != 0 && value != NULL) {
        assert(value_size <= sizeof(g_event_value));
        memcpy(g_event_value, value, value_size);
    }
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

static pxa_status_t transact_status(uint32_t transaction, uint32_t generation,
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
    return control(PXA_UI_TX_COMMIT, commit_payload, sizeof(commit_payload));
}

static void transact(uint32_t transaction, uint32_t generation,
                     uint32_t target, uint8_t kind, const bytes_t *stream) {
    pxa_status_t result = transact_status(transaction, generation, target, kind, stream);
    if (result != PXA_STATUS_OK) fprintf(stderr, "Transaction %u generation %u failed: %d\n", transaction, generation, result);
    assert(result == PXA_STATUS_OK);
}

static void create_overlay(bytes_t *commands, uint32_t node, uint32_t parent,
                           unsigned width, unsigned height) {
    uint8_t length[8] = {PXA_UI_LENGTH_LOGICAL_PX};
    uint8_t composition = PXA_UI_COMPOSITION_ALPHA_OVERLAY;
    uint8_t background[8] = {1};
    pxa_write_u32(background + 4, UINT32_C(0xd05030ff));
    create_node(commands, node, parent, PXA_UI_NODE_BOX, 0);
    pxa_write_u32(length + 4, width * 64);
    set_property(commands, node, PXA_UI_PROPERTY_WIDTH, length, sizeof(length));
    pxa_write_u32(length + 4, height * 64);
    set_property(commands, node, PXA_UI_PROPERTY_HEIGHT, length, sizeof(length));
    set_property(commands, node, PXA_UI_PROPERTY_COMPOSITION, &composition, 1);
    set_property(commands, node, PXA_UI_PROPERTY_BACKGROUND, background, sizeof(background));
}

/* Writes one 8-byte grid track: kind:u8 | reserved:u8[3] | value:u32. */
static void put_grid_track(bytes_t *bytes, uint8_t kind, uint32_t value) {
    put_u8(bytes, kind);
    put_u8(bytes, 0);
    put_u8(bytes, 0);
    put_u8(bytes, 0);
    put_u32(bytes, value);
}

/* Writes one u16[4] grid cell: column | row | column-span | row-span. */
static void put_grid_cell(bytes_t *bytes, uint16_t column, uint16_t row,
                          uint16_t column_span, uint16_t row_span) {
    put_u16(bytes, column);
    put_u16(bytes, row);
    put_u16(bytes, column_span);
    put_u16(bytes, row_span);
}

static void build_text_surface(void) {
    bytes_t stream = {{0}, 0};
    uint8_t size[8] = {PXA_UI_LENGTH_LOGICAL_PX, 0, 0, 0, 0, 0, 0, 0};
    uint8_t mask[8];
    pxa_write_u64(mask, PXA_UI_EVENT_MASK_TEXT | PXA_UI_EVENT_MASK_ACTION);
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    create_node(&stream, 2, 1, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT);
    pxa_write_u32(size + 4, 152u * 64u);
    set_property(&stream, 2, PXA_UI_PROPERTY_WIDTH, size, sizeof(size));
    pxa_write_u32(size + 4, 36u * 64u);
    set_property(&stream, 2, PXA_UI_PROPERTY_HEIGHT, size, sizeof(size));
    set_property(&stream, 2, PXA_UI_PROPERTY_EVENT_MASK, mask, sizeof(mask));
    {
        uint8_t padding[16];
        uint8_t radius[4];
        pxa_write_u32(padding, 14u * 64u);
        pxa_write_u32(padding + 4, 6u * 64u);
        pxa_write_u32(padding + 8, 14u * 64u);
        pxa_write_u32(padding + 12, 6u * 64u);
        set_property(&stream, 2, PXA_UI_PROPERTY_PADDING, padding,
                     sizeof(padding));
        pxa_write_u32(radius, 12u * 64u);
        set_property(&stream, 2, PXA_UI_PROPERTY_RADIUS, radius,
                     sizeof(radius));
    }
    {
        /* The store search field: fill width, fixed height, padding, radius,
         * border and a theme token background. */
        uint8_t fill[8] = {PXA_UI_LENGTH_FILL, 0, 0, 0, 0, 0, 0, 0};
        uint8_t token[8] = {0, 1, 0, 0, 0, 0, 0, 0};
        uint8_t border[4];
        uint8_t mask2[8];
        pxa_write_u64(mask2, PXA_UI_EVENT_MASK_TEXT | PXA_UI_EVENT_MASK_ACTION);
        set_property(&stream, 2, PXA_UI_PROPERTY_WIDTH, fill, sizeof(fill));
        pxa_write_u32(size + 4, 32u * 64u);
        set_property(&stream, 2, PXA_UI_PROPERTY_HEIGHT, size, sizeof(size));
        set_property(&stream, 2, PXA_UI_PROPERTY_BACKGROUND, token,
                     sizeof(token));
        pxa_write_u32(border, 1u * 64u);
        set_property(&stream, 2, PXA_UI_PROPERTY_BORDER_WIDTH, border,
                     sizeof(border));
        set_property(&stream, 2, PXA_UI_PROPERTY_BORDER_COLOR, token,
                     sizeof(token));
        set_property(&stream, 2, PXA_UI_PROPERTY_EVENT_MASK, mask2,
                     sizeof(mask2));
        /* the store writes the current draft, empty at first */
        set_property(&stream, 2, PXA_UI_PROPERTY_TEXT, "x", 1);
    }
    create_node(&stream, 3, 2, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 3, PXA_UI_PROPERTY_TEXT, "hint", 4);
    transact(11, 11, 0, PXA_UI_REPLACE_SURFACE, &stream);
}

static void build_font_surface(void) {
    static const char *const names[] = {
        "caption", "label", "body", "title", "headline", "display"
    };
    static const uint16_t roles[] = {
        PXA_UI_FONT_ROLE_CAPTION, PXA_UI_FONT_ROLE_LABEL,
        PXA_UI_FONT_ROLE_BODY, PXA_UI_FONT_ROLE_TITLE,
        PXA_UI_FONT_ROLE_HEADLINE, PXA_UI_FONT_ROLE_DISPLAY
    };
    bytes_t stream = {{0}, 0};
    uint8_t role[2];
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    for (size_t index = 0; index < 6u; ++index) {
        create_node(&stream, (uint32_t)index + 2u, 1, PXA_UI_NODE_TEXT, 0);
        set_property(&stream, (uint32_t)index + 2u, PXA_UI_PROPERTY_TEXT,
                     names[index], strlen(names[index]));
        pxa_write_u16(role, roles[index]);
        set_property(&stream, (uint32_t)index + 2u,
                     PXA_UI_PROPERTY_FONT_ROLE, role, sizeof(role));
    }
    transact(12, 12, 0, PXA_UI_REPLACE_SURFACE, &stream);
}

static void build_image_surface(void) {
    bytes_t stream = {{0}, 0};
    uint8_t primary[8] = {0};
    primary[1] = 2;
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    create_node(&stream, 2, 1, PXA_UI_NODE_IMAGE, 0);
    create_node(&stream, 3, 1, PXA_UI_NODE_IMAGE, 0);
    set_property(&stream, 2, PXA_UI_PROPERTY_ASSET,
                 "assets/test.png", sizeof("assets/test.png") - 1u);
    set_property(&stream, 3, PXA_UI_PROPERTY_ASSET,
                 "assets/test.png", sizeof("assets/test.png") - 1u);
    set_property(&stream, 2, PXA_UI_PROPERTY_FOREGROUND,
                 primary, sizeof(primary));
    transact(13, 13, 0, PXA_UI_REPLACE_SURFACE, &stream);
}

static void build_grid_surface(void) {
    bytes_t stream = {{0}, 0};
    bytes_t columns = {{0}, 0};
    bytes_t rows = {{0}, 0};
    bytes_t first_cell = {{0}, 0};
    bytes_t second_cell = {{0}, 0};
    /* LOGICAL_PX values are 1/64 dp multiples. */
    uint8_t size[8] = {PXA_UI_LENGTH_LOGICAL_PX, 0, 0, 0, 0, 0, 0, 0};
    pxa_write_u32(size + 4, 180u * 64u);
    create_node(&stream, 1, 0, PXA_UI_NODE_ROOT, 0);
    create_node(&stream, 2, 1, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 3, 2, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 4, 2, PXA_UI_NODE_BOX, 0);
    put_grid_track(&columns, PXA_UI_GRID_FRACTION, 1);
    put_grid_track(&columns, PXA_UI_GRID_FRACTION, 2);
    put_grid_track(&rows, PXA_UI_GRID_CONTENT, 0);
    put_grid_cell(&first_cell, 0, 0, 1, 1);
    put_grid_cell(&second_cell, 1, 0, 1, 1);
    set_property(&stream, 2, PXA_UI_PROPERTY_WIDTH, size, sizeof(size));
    pxa_write_u32(size + 4, 60u * 64u);
    set_property(&stream, 2, PXA_UI_PROPERTY_HEIGHT, size, sizeof(size));
    set_property(&stream, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data,
                 columns.size);
    set_property(&stream, 2, PXA_UI_PROPERTY_GRID_ROWS, rows.data, rows.size);
    {
        uint8_t stretch = 3;
        set_property(&stream, 3, PXA_UI_PROPERTY_ALIGN, &stretch, 1);
        set_property(&stream, 4, PXA_UI_PROPERTY_ALIGN, &stretch, 1);
    }
    set_property(&stream, 3, PXA_UI_PROPERTY_GRID_CELL, first_cell.data,
                 first_cell.size);
    set_property(&stream, 4, PXA_UI_PROPERTY_GRID_CELL, second_cell.data,
                 second_cell.size);
    transact(10, 10, 0, PXA_UI_REPLACE_SURFACE, &stream);
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
    set_property(&stream, 2, PXA_UI_PROPERTY_EVENT_MASK,
                 pointer_mask, sizeof(pointer_mask));
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

static void patch_visibility(uint32_t transaction, uint32_t generation,
                             uint8_t visible) {
    bytes_t stream = {{0}, 0};
    set_property(&stream, 3, PXA_UI_PROPERTY_VISIBLE, &visible, sizeof(visible));
    transact(transaction, generation, 0, PXA_UI_PATCH, &stream);
}

static void patch_canvas_alpha(void) {
    bytes_t stream = {{0}, 0};
    const uint8_t composition = PXA_UI_COMPOSITION_ALPHA_OVERLAY;
    set_property(&stream, 5, PXA_UI_PROPERTY_COMPOSITION,
                 &composition, sizeof(composition));
    transact(5, 5, 0, PXA_UI_PATCH, &stream);
}

static uint64_t alpha_plane_hash(const pxa_lvgl_ui_alpha_plane_t *plane,
                                 size_t *visible_pixels) {
    const size_t color_stride = plane->pixel_stride_bytes /
                                sizeof(*plane->pixels);
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t visible = 0;
    uint16_t y;
    for (y = 0; y < plane->height; ++y) {
        uint16_t x;
        for (x = 0; x < plane->width; ++x) {
            const uint8_t alpha =
                plane->alpha[(size_t)y * plane->alpha_stride_bytes + x];
            if (alpha == 0) continue;
            hash ^= plane->pixels[(size_t)y * color_stride + x];
            hash *= UINT64_C(1099511628211);
            hash ^= alpha;
            hash *= UINT64_C(1099511628211);
            ++visible;
        }
    }
    *visible_pixels = visible;
    return hash;
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
    put_u32(&payload, generation == 6 ? UINT32_C(0xe25656ff)
                                     : UINT32_C(0x34c785ff));
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

static bytes_t native_canvas_frame(uint64_t handle, int bad_second) {
    bytes_t frame = {{0},0};
    for (unsigned i=0; i<3; ++i) {
        bytes_t value = {{0},0};
        put_u32(&value, i == 0 ? 5000 : 0); put_u32(&value,0);
        put_u32(&value,2); put_u32(&value,2);
        put_u8(&value,255); put_u8(&value,PXA_UI_IMAGE_FIT_STRETCH);
        uint8_t wire[8]; pxa_write_u64(wire, handle + (bad_second && i==1));
        put_data(&value,wire,sizeof(wire));
        command(&frame,PXA_UI_CANVAS_IMAGE_HANDLE,&value);
    }
    return frame;
}

static pxa_status_t submit_native_canvas(uint32_t generation, const bytes_t *frame) {
    uint8_t begin[16]={0}, write[12+256]={0}, present[13]={0};
    pxa_write_u32(begin,PXA_UI_PRIMARY_SURFACE); pxa_write_u32(begin+4,4);
    pxa_write_u32(begin+8,generation);
    assert(!control(PXA_UI_CANVAS_BEGIN,begin,sizeof(begin)));
    memcpy(write,begin,12);
    for (size_t offset=0;offset<frame->size;) {
        size_t bytes=frame->size-offset;
        if (bytes>256) bytes=256;
        memcpy(write+12,frame->data+offset,bytes);
        assert(!control(PXA_UI_CANVAS_WRITE,write,12+bytes));
        offset+=bytes;
    }
    memcpy(present,begin,12);
    return control(PXA_UI_CANVAS_PRESENT,present,sizeof(present));
}

static void unexpected_frame_release(void *context, void *memory) {
    (void)context; (void)memory; assert(0 && "rejected frame ownership transferred");
}

static void replace_content_subtree(void) {
    bytes_t stream = {{0}, 0};
    static const char title[] = "Replacement subtree";
    create_node(&stream, 2, 1, PXA_UI_NODE_BOX, 0);
    create_node(&stream, 6, 2, PXA_UI_NODE_TEXT, 0);
    set_property(&stream, 6, PXA_UI_PROPERTY_TEXT,
                 title, sizeof(title) - 1u);
    transact(6, 6, 2, PXA_UI_REPLACE_SUBTREE, &stream);
}

static pxa_status_t bounded_overlay_length(pxa_ui_backend_t *backend, void **old_root,
                    int32_t width, int32_t height, uint8_t unit, int overlay) {
    pxa_ui_transaction_info_t info = {.kind=PXA_UI_REPLACE_SURFACE,
        .surface=PXA_UI_PRIMARY_SURFACE, .target_handle=*old_root};
    void *transaction=NULL, *root=NULL, *box=NULL;
    assert(!backend->begin(backend->context,&info,&transaction));
    pxa_ui_command_view_t view={.command=PXA_UI_COMMAND_CREATE,.node=1,.type=PXA_UI_NODE_ROOT};
    assert(!backend->apply(backend->context,transaction,&view,&root));
    view.node=2;view.parent=1;view.parent_handle=root;view.type=PXA_UI_NODE_BOX;
    assert(!backend->apply(backend->context,transaction,&view,&box));
    uint8_t value[8]={0};value[0]=unit;pxa_write_u32(value+4,(uint32_t)width);
    view.command=PXA_UI_COMMAND_SET_PROPERTY;view.node_handle=box;
    view.property=PXA_UI_PROPERTY_WIDTH;view.value=(pxa_bytes_t){value,8};
    assert(!backend->apply(backend->context,transaction,&view,NULL));
    value[0]=unit==PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16?PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16:unit;
    pxa_write_u32(value+4,(uint32_t)height);
    view.property=PXA_UI_PROPERTY_HEIGHT;
    assert(!backend->apply(backend->context,transaction,&view,NULL));
    value[0]=1;pxa_write_u32(value+4,UINT32_C(0xd05030ff));
    view.property=PXA_UI_PROPERTY_BACKGROUND;
    assert(!backend->apply(backend->context,transaction,&view,NULL));
    value[0]=overlay ? PXA_UI_COMPOSITION_ALPHA_OVERLAY : PXA_UI_COMPOSITION_BASE;
    view.property=PXA_UI_PROPERTY_COMPOSITION;view.value.size=1;
    assert(!backend->apply(backend->context,transaction,&view,NULL));
    pxa_status_t status=backend->commit(backend->context,transaction);
    if(status) backend->cancel(backend->context,transaction);
    else *old_root=root;
    return status;
}

static pxa_status_t bounded_overlay(pxa_ui_backend_t *backend, void **root,
                                    unsigned side, int overlay) {
    return bounded_overlay_length(backend,root,(int32_t)side*64,(int32_t)side*64,
                                   PXA_UI_LENGTH_LOGICAL_PX,overlay);
}

static void test_viewport_density(void) {
    allocator_state_t allocator={0};
    pxa_lvgl_ui_config_t config={0};
    config.struct_size=sizeof(config);config.allocate=test_allocate;config.release=test_release;
    config.allocator_context=&allocator;config.execute=sync_execute;config.execute_user_data=&allocator;
    config.primary_environment.width=320;config.primary_environment.height=240;
    config.primary_environment.surface=PXA_UI_PRIMARY_SURFACE;
    config.primary_environment.density_q16=65536;config.primary_environment.font_scale_q16=65536;
    pxa_lvgl_ui_theme_init(&config.theme);
    void *workspace=malloc(pxa_lvgl_ui_workspace_size()),*root=NULL;
    pxa_lvgl_ui_t *adapter=NULL;pxa_ui_backend_t backend;
    assert(workspace&&!pxa_lvgl_ui_init(workspace,pxa_lvgl_ui_workspace_size(),&config,&adapter,&backend));
    const uint32_t densities[]={65536,98304,124928,196608};
    for(unsigned i=0;i<4;++i){
        config.primary_environment.density_q16=densities[i];
        assert(!backend.environment_changed(backend.context,&config.primary_environment));
        assert(!bounded_overlay_length(&backend,&root,32768,32768,PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16,1));
        lv_obj_t *box=lv_obj_get_child(lv_obj_get_child(lv_screen_active(),0),0);
        assert(lv_obj_get_width(box)==160&&lv_obj_get_height(box)==120);
    }
    pxa_lvgl_ui_deinit(adapter);free(workspace);assert(allocator.current==0);
    puts("LVGL viewport lengths: 50vw/50vh stay 160x120 at 160/240/305/480 DPI OK");
}

static void test_text_input_focus(void) {
    allocator_state_t allocator={0};
    pxa_lvgl_ui_config_t config={0};
    config.struct_size=sizeof(config);config.allocate=test_allocate;config.release=test_release;
    config.allocator_context=&allocator;config.execute=sync_execute;config.execute_user_data=&allocator;
    config.primary_environment.width=320;config.primary_environment.height=240;
    config.primary_environment.surface=PXA_UI_PRIMARY_SURFACE;
    config.primary_environment.density_q16=65536;
    pxa_lvgl_ui_theme_init(&config.theme);
    void *workspace=malloc(pxa_lvgl_ui_workspace_size()), *handles[3]={0}, *transaction=NULL;
    pxa_lvgl_ui_t *adapter=NULL;pxa_ui_backend_t backend;
    assert(workspace&&!pxa_lvgl_ui_init(workspace,pxa_lvgl_ui_workspace_size(),&config,&adapter,&backend));
    pxa_ui_transaction_info_t info={.surface=1,.generation=1,.kind=PXA_UI_REPLACE_SURFACE};
    assert(!backend.begin(backend.context,&info,&transaction));
    for(unsigned i=0;i<3;++i) {
        pxa_ui_command_view_t command={.command=PXA_UI_COMMAND_CREATE,.node=i+1,
            .parent=i?1:0,.parent_handle=i?handles[0]:NULL,.type=i?PXA_UI_NODE_CONTROL:PXA_UI_NODE_ROOT,
            .subtype=i?PXA_UI_CONTROL_TEXT_INPUT:0};
        assert(!backend.apply(backend.context,transaction,&command,&handles[i]));
        uint8_t size[8]={PXA_UI_LENGTH_LOGICAL_PX};
        command.command=PXA_UI_COMMAND_SET_PROPERTY;
        command.node_handle=handles[i];command.value=(pxa_bytes_t){size,8};
        command.property=PXA_UI_PROPERTY_WIDTH;
        pxa_write_u32(size+4,(i?100:320)*64);
        assert(!backend.apply(backend.context,transaction,&command,NULL));
        command.property=PXA_UI_PROPERTY_HEIGHT;
        pxa_write_u32(size+4,(i?32:240)*64);
        assert(!backend.apply(backend.context,transaction,&command,NULL));
    }
    assert(!backend.commit(backend.context,transaction));
    lv_obj_t *root=lv_obj_get_child(lv_screen_active(),0);
    lv_obj_t *a=lv_obj_get_child(root,0), *b=lv_obj_get_child(root,1);
    size_t resident=allocator.current;
    assert(!backend.text_input_focus(backend.context,handles[1],true));
    assert(lv_obj_has_state(a,LV_STATE_FOCUSED));
    assert(!backend.text_input_focus(backend.context,handles[2],true));
    assert(!lv_obj_has_state(a,LV_STATE_FOCUSED) && lv_obj_has_state(b,LV_STATE_FOCUSED));
    assert(!backend.text_input_focus(backend.context,handles[2],false));
    assert(!lv_obj_has_state(b,LV_STATE_FOCUSED));
    lv_obj_set_hidden(root,true);
    assert(backend.text_input_focus(backend.context,handles[1],true)==PXA_STATUS_DENIED);
    lv_obj_set_hidden(root,false);
    lv_obj_add_state(a,LV_STATE_DISABLED);
    assert(backend.text_input_focus(backend.context,handles[1],true)==PXA_STATUS_DENIED);
    assert(backend.text_input_focus(backend.context,&allocator,true)==PXA_STATUS_NOT_FOUND);
    assert(backend.text_input_focus(backend.context,handles[0],true)==PXA_STATUS_INVALID_ARGUMENT);
    assert(allocator.current==resident);
    pxa_lvgl_ui_deinit(adapter);free(workspace);assert(allocator.current==0);
    puts("LVGL input focus: ownership, single focus, hide, hidden/disabled denial, no adapter allocation OK");
}

static void test_enabled_inheritance(void) {
    allocator_state_t allocator={0}; pxa_lvgl_ui_config_t config={0};
    config.struct_size=sizeof(config); config.allocate=test_allocate; config.release=test_release;
    config.allocator_context=&allocator; config.execute=sync_execute; config.execute_user_data=&allocator;
    config.event_callback=on_event; config.primary_environment=(pxa_ui_environment_t){
        .surface=1,.width=320,.height=240,.density_q16=65536,.font_scale_q16=65536};
    pxa_lvgl_ui_theme_init(&config.theme);
    void *workspace=malloc(pxa_lvgl_ui_workspace_size()), *transaction=NULL, *handles[5]={0};
    pxa_lvgl_ui_t *adapter=NULL; pxa_ui_backend_t backend;
    assert(workspace && !pxa_lvgl_ui_init(workspace,pxa_lvgl_ui_workspace_size(),&config,&adapter,&backend));
    g_adapter=adapter;
    pxa_ui_transaction_info_t info={.surface=1,.generation=1,.kind=PXA_UI_REPLACE_SURFACE};
    assert(!backend.begin(backend.context,&info,&transaction));
    for(unsigned i=0;i<5;++i) {
        unsigned parent=i==0?0:i==1||i==3?1:2;
        pxa_ui_command_view_t c={.command=PXA_UI_COMMAND_CREATE,.node=i+1,.parent=parent,
            .parent_handle=parent?handles[parent-1]:NULL,
            .type=i==0?PXA_UI_NODE_ROOT:i==2?PXA_UI_NODE_CONTROL:i==4?PXA_UI_NODE_CONTROL:PXA_UI_NODE_BOX,
            .subtype=i==2?PXA_UI_CONTROL_TOGGLE:i==4?PXA_UI_CONTROL_TEXT_INPUT:0};
        assert(!backend.apply(backend.context,transaction,&c,&handles[i]));
        uint8_t length[8]={PXA_UI_LENGTH_LOGICAL_PX};
        c.command=PXA_UI_COMMAND_SET_PROPERTY; c.node_handle=handles[i]; c.property=PXA_UI_PROPERTY_WIDTH;
        pxa_write_u32(length+4,(i==0?320:i==2?36:160)*64); c.value=(pxa_bytes_t){length,8};
        assert(!backend.apply(backend.context,transaction,&c,NULL));
        c.property=PXA_UI_PROPERTY_HEIGHT; pxa_write_u32(length+4,(i==0?240:i==2?20:40)*64);
        assert(!backend.apply(backend.context,transaction,&c,NULL));
    }
    uint8_t enabled=0, mask[8]; pxa_write_u64(mask,PXA_UI_EVENT_MASK_VALUE_CHANGED);
    pxa_ui_command_view_t property={.command=PXA_UI_COMMAND_SET_PROPERTY,.node_handle=handles[1],
        .property=PXA_UI_PROPERTY_ENABLED,.value={&enabled,1}};
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    property.node_handle=handles[2]; property.property=PXA_UI_PROPERTY_EVENT_MASK; property.value=(pxa_bytes_t){mask,8};
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    assert(!backend.commit(backend.context,transaction));
    lv_obj_t *root=lv_obj_get_child(lv_screen_active(),0), *row=lv_obj_get_child(root,0);
    lv_obj_t *toggle=lv_obj_get_child(row,0), *input=lv_obj_get_child(row,1);
    lv_obj_update_layout(root); test_lvgl_tick();
    assert(lv_obj_has_state(toggle,LV_STATE_DISABLED));
    assert(backend.text_input_focus(backend.context,handles[4],true)==PXA_STATUS_DENIED);
    lv_area_t area; lv_obj_get_coords(toggle,&area);
    pointer_input_t pointer_state={{(area.x1+area.x2)/2,(area.y1+area.y2)/2},lv_tick_get(),1};
    lv_indev_t *pointer=lv_indev_create(); assert(pointer);
    lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER); lv_indev_set_display(pointer,g_test_display);
    lv_indev_set_user_data(pointer,&pointer_state); lv_indev_set_read_cb(pointer,read_pointer);
    g_events=0; lv_indev_read(pointer); lv_tick_inc(20);
    pointer_state.timestamp_ms=lv_tick_get(); pointer_state.pressed=0; lv_indev_read(pointer);
    assert(!lv_obj_has_state(toggle,LV_STATE_CHECKED) && g_events==0);
    lv_indev_delete(pointer);
    size_t resident=allocator.current;
    // Parent enable restores eligible children, but preserves a child's own disable.
    info.kind=PXA_UI_PATCH; info.generation++;
    assert(!backend.begin(backend.context,&info,&transaction));
    property.node_handle=handles[4]; property.property=PXA_UI_PROPERTY_ENABLED; property.value=(pxa_bytes_t){&enabled,1};
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    enabled=1; property.node_handle=handles[1];
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    assert(!backend.commit(backend.context,transaction));
    assert(!lv_obj_has_state(toggle,LV_STATE_DISABLED) && lv_obj_has_state(input,LV_STATE_DISABLED));
    assert(allocator.current==resident);
    // A failed alpha preparation must restore inherited AND requested states.
    info.generation++; assert(!backend.begin(backend.context,&info,&transaction));
    enabled=0; property.node_handle=handles[1];
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    enabled=1; property.node_handle=handles[0]; property.property=PXA_UI_PROPERTY_COMPOSITION;
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    allocator.fail_during_execute=1;
    assert(backend.commit(backend.context,transaction)==PXA_STATUS_RESOURCE_LIMIT);
    allocator.fail_during_execute=0; backend.cancel(backend.context,transaction);
    assert(!lv_obj_has_state(toggle,LV_STATE_DISABLED) && lv_obj_has_state(input,LV_STATE_DISABLED));
    assert(allocator.current==resident);
    // Reparenting into a disabled container recomputes inherited state.
    info.generation++; assert(!backend.begin(backend.context,&info,&transaction));
    enabled=0; property.node_handle=handles[3]; property.property=PXA_UI_PROPERTY_ENABLED;
    assert(!backend.apply(backend.context,transaction,&property,NULL));
    pxa_ui_command_view_t move={.command=PXA_UI_COMMAND_MOVE,.node_handle=handles[2],.parent_handle=handles[3]};
    assert(!backend.apply(backend.context,transaction,&move,NULL));
    assert(!backend.commit(backend.context,transaction));
    assert(lv_obj_has_state(toggle,LV_STATE_DISABLED));
    pxa_lvgl_ui_deinit(adapter); free(workspace); assert(allocator.current==0);
    puts("Enabled inheritance: real pointer denied, focus denied, own state preserved, MOVE, failed commit rollback, stable adapter bytes OK");
}

static void test_snapshot_limits(void) {
    allocator_state_t allocator={0};
    pxa_lvgl_ui_config_t config={0};
    config.struct_size=sizeof(config);config.allocate=test_allocate;config.release=test_release;
    config.allocator_context=&allocator;config.execute=sync_execute;config.execute_user_data=&allocator;
    config.primary_environment.width=320;config.primary_environment.height=240;
    config.primary_environment.density_q16=1u<<16;
    config.snapshot_limit_bytes=8192;config.alpha_limit_bytes=2048;
    pxa_lvgl_ui_theme_init(&config.theme);
    void *workspace=malloc(pxa_lvgl_ui_workspace_size()), *root=NULL;
    pxa_lvgl_ui_t *adapter=NULL;
    pxa_ui_backend_t backend;
    assert(workspace && !pxa_lvgl_ui_init(workspace,pxa_lvgl_ui_workspace_size(),&config,&adapter,&backend));
    assert(!bounded_overlay(&backend,&root,16,1));
    pxa_lvgl_ui_alpha_plane_t before,after;
    assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
    size_t visible;
    uint64_t hash=alpha_plane_hash(&before,&visible);
    size_t resident=allocator.current;
    void *original_root=root;
    for(unsigned mode=0;mode<3;++mode) {
        /* 32px fits snapshot but exceeds plane quota; 48px exceeds snapshot.
         * 24px fits both quotas but its replacement scratch allocation fails. */
        if(mode==2) allocator.fail_size=24*24*4+LV_DRAW_BUF_ALIGN-1u;
        assert(bounded_overlay(&backend,&root,mode==0?32:mode==1?48:24,1)==PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_size=0;
        assert(root==original_root && allocator.current==resident);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&after) && after.pixels==before.pixels &&
            after.revision==before.revision && alpha_plane_hash(&after,&visible)==hash);
    }
    assert(allocator.size_failures==1);
    assert(!bounded_overlay(&backend,&root,24,1));
    assert(pxa_lvgl_ui_alpha_plane(adapter,&after) && after.width==24 && after.height==24);
    size_t alpha_resident=allocator.current;
    assert(!bounded_overlay(&backend,&root,24,0));
    assert(!pxa_lvgl_ui_alpha_plane(adapter,&after) && allocator.current<alpha_resident);
    printf("Alpha limits: snapshot=8192 plane=2048 rejected=3 recovered=1 opaque_released=%zu\n",
        alpha_resident-allocator.current);
    pxa_lvgl_ui_deinit(adapter);
    assert(allocator.current==0);
    free(workspace);
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
    pxa_lvgl_ui_alpha_plane_t alpha_plane;
    uint64_t alpha_before;
    size_t alpha_visible;

    test_lvgl_init();
    pxa_runtime_limits_init(&runtime_limits);
    runtime_limits.max_components = 1;
    size = pxa_runtime_workspace_size(&runtime_limits);
    runtime_workspace = malloc(size);
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace, size, &runtime_limits,
                            &g_runtime) == PXA_STATUS_OK);

    pxa_ui_config_init(&service_config);
    service_config.features = PXA_UI_FEATURE_CANVAS | PXA_UI_FEATURE_GRID |
                              PXA_UI_FEATURE_RGB565_BITMAP |
                              PXA_UI_FEATURE_VIRTUAL_LIST | PXA_UI_FEATURE_DYNAMIC_TEXT;
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
    adapter_config.execute_user_data = &allocator;
    adapter_config.resolve_asset = resolve_asset;
    adapter_config.acquire_image = acquire_image;
    adapter_config.release_asset = release_asset;
    adapter_config.event_callback = on_event;
    adapter_config.now_us = test_now_us;
    adapter_config.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    adapter_config.primary_environment.width = 320;
    adapter_config.primary_environment.height = 240;
    adapter_config.primary_environment.density_q16 = UINT32_C(1) << 16;
    adapter_config.primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    pxa_lvgl_ui_theme_init(&adapter_config.theme);
    adapter_config.theme.caption_font = &lv_font_montserrat_14;
    adapter_config.theme.label_font = &lv_font_montserrat_20;
    adapter_config.theme.body_font = &lv_font_montserrat_14;
    adapter_config.theme.title_font = &lv_font_montserrat_20;
    adapter_config.theme.headline_font = &lv_font_montserrat_14;
    adapter_config.theme.display_font = &lv_font_montserrat_20;
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
    {
        unsigned before = g_events;
        lv_timer_handler();
        assert(g_events == before + 1 && g_event_node == 6 &&
               g_event_kind == PXA_UI_EVENT_VISIBLE_RANGE && g_visible_count > 0);
        before = g_events;
        lv_timer_handler();
        assert(g_events == before);
        g_events = 0;
    }
    assert(lv_obj_get_child_count(lv_screen_active()) == 1);
    root = lv_obj_get_child(lv_screen_active(), 0);
    assert(root != NULL && find_label(root, "UI ABI 0.3") != NULL);
    assert(!lv_obj_is_scrollable(root));
    assert(lv_obj_is_clickable(root));
    box = lv_obj_get_child(root, 0);
    assert(lv_obj_get_width(box) == 160);
    button = find_type(root, &lv_button_class);
    assert(button != NULL);
    assert(lv_obj_is_clickable(button));
    /* A label inside an interactive node must not swallow the tap: only nodes
     * the Guest subscribes to take pointer input. */
    {
        lv_obj_t *button_label = find_label(button, "Apply");
        assert(button_label != NULL);
        assert(!lv_obj_is_clickable(button_label));
    }
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
    /* A gesture that starts on a child bubbles to the ancestor that
     * subscribes, so a container observes a drag over its content. */
    {
        lv_indev_t *press_input = lv_indev_create();
        pointer_input_t press_state = {{0, 0}, 0, 0};
        lv_area_t button_area;
        unsigned events_before;
        assert(press_input != NULL);
        lv_indev_set_type(press_input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(press_input, g_test_display);
        lv_indev_set_user_data(press_input, &press_state);
        lv_indev_set_read_cb(press_input, read_pointer);
        lv_obj_update_layout(root);
        lv_obj_get_coords(button, &button_area);
        press_state.point.x = button_area.x1 + 4;
        press_state.point.y = button_area.y1 + 4;
        press_state.pressed = 1;
        g_now_us = UINT64_C(124000000);
        press_state.timestamp_ms = lv_tick_get() - 2u;
        g_event_kind = (pxa_ui_event_kind_t)0;
        g_event_node = 0;
        events_before = g_events;
        lv_indev_read(press_input);
        assert(g_event_kind == PXA_UI_EVENT_POINTER && g_event_node == 2);
        press_state.pressed = 0;
        press_state.timestamp_ms = lv_tick_get();
        lv_indev_read(press_input);
        /* The container saw the gesture, the child emitted nothing. */
        assert(g_events > events_before);
        g_events = events_before;
        lv_indev_delete(press_input);
    }
    list = lv_obj_get_child(lv_obj_get_child(root, 0), 3);
    assert(list != NULL);
    lv_obj_scroll_to_y(list, 100, LV_ANIM_OFF);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
    lv_timer_handler();
    assert(g_events == 2 && g_event_node == 6 &&
           g_event_kind == PXA_UI_EVENT_VISIBLE_RANGE &&
           g_visible_count != 0 && g_visible_first < 100);

    assert(pxa_ui_memory_snapshot(service, g_component, &memory) ==
           PXA_STATUS_OK);
    assert(memory.node_count == 9 && memory.canvas_bytes == 0);
    patch_title();
    assert(lv_obj_get_child(lv_screen_active(), 0) == root);
    assert(find_label(root, "Patched without rebuilding") != NULL);

    /* A node hidden by a patch must be able to come back: the hidden flag is
     * removed when the node is shown again. */
    {
        lv_obj_t *title = find_label(root, "Patched without rebuilding");
        assert(title != NULL);
        patch_visibility(3, 3, 0);
        assert(lv_obj_is_hidden(title));
        patch_visibility(4, 4, 1);
        assert(!lv_obj_is_hidden(title));
    }
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
    pointer_state.timestamp_ms = lv_tick_get() - 7u;
    lv_indev_read(pointer_input);
    assert(g_event_kind == PXA_UI_EVENT_POINTER &&
           g_event_timestamp_us == g_now_us - 7000u);
    /* Host and LVGL clocks deliberately have different epochs above. Also
     * preserve sample age across the 32-bit LVGL tick rollover. */
    lv_tick_set_cb(input_test_tick);
    g_input_test_tick = 3;
    pointer_state.pressed = 0;
    pointer_state.timestamp_ms = UINT32_MAX - 3u;
    lv_indev_read(pointer_input);
    assert(g_event_timestamp_us == g_now_us - 7000u);
    pointer_state.pressed = 1;
    pointer_state.timestamp_ms = g_input_test_tick - 1001u;
    lv_indev_read(pointer_input);
    assert(g_event_timestamp_us == g_now_us);
    pointer_state.pressed = 0;
    pointer_state.timestamp_ms = g_input_test_tick + 1u;
    lv_indev_read(pointer_input);
    assert(g_event_timestamp_us == g_now_us);
    lv_tick_set_cb(NULL);
    lv_indev_delete(pointer_input);
    g_test_rgb565_x = canvas_area.x1 + 50;
    g_test_rgb565_y = canvas_area.y1 + 20;
    g_test_rgb565_pixels = 0;
    flushes = g_test_flush_count;
    test_lvgl_tick();
    assert(g_test_flush_count > flushes);
    assert(g_test_rgb565_pixels == UINT8_C(0x0f));
    /* Finish unrelated pointer/state animations before measuring only the
     * dirty rectangle submitted by the next Canvas frame. */
    for (unsigned tick = 0; tick < 10; ++tick) test_lvgl_tick();
    {
        size_t allocations;
        uint64_t pixels = g_test_flushed_pixels;
        present_canvas(2, 1);
        allocations = allocator.allocations;
        test_lvgl_tick();
        assert(g_test_flushed_pixels > pixels);
        if (g_test_flushed_pixels - pixels >= 80u * 80u)
            fprintf(stderr, "Dirty canvas flush: %llu pixels\n",
                (unsigned long long)(g_test_flushed_pixels - pixels));
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

    patch_canvas_alpha();
    assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
    assert(alpha_plane.x >= canvas_area.x1 &&
           alpha_plane.y >= canvas_area.y1);
    assert(alpha_plane.width < 80 && alpha_plane.height < 80);
    assert(alpha_plane.pixel_stride_bytes == 80u * sizeof(uint16_t));
    assert(alpha_plane.alpha_stride_bytes == 80);
    assert(alpha_plane.revision != 0);
    alpha_before = alpha_plane_hash(&alpha_plane, &alpha_visible);
    assert(alpha_visible != 0);
    present_canvas(6, 0);
    assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
    assert(alpha_plane.revision > 1);
    assert(alpha_plane_hash(&alpha_plane, &alpha_visible) != alpha_before);
    assert(alpha_visible != 0);

    /* Focus/press/release on a Canvas only affect hit testing. A visible
     * command list, like an empty input layer, is independent of that state:
     * do not snapshot or allocate before forwarding input. */
    {
        const uint64_t revision = alpha_plane.revision;
        const uint64_t hash = alpha_plane_hash(&alpha_plane, &alpha_visible);
        const size_t allocations = allocator.allocations;
#ifdef PXA_TEST_WRAP_SNAPSHOT
        const unsigned snapshots = g_snapshot_draw_calls;
#endif
        lv_obj_add_state(canvas, LV_STATE_FOCUSED);
        lv_obj_add_state(canvas, LV_STATE_PRESSED);
        lv_obj_remove_state(canvas, LV_STATE_PRESSED);
        lv_obj_remove_state(canvas, LV_STATE_FOCUSED);
        assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
        assert(alpha_plane.revision == revision);
        assert(alpha_plane_hash(&alpha_plane, &alpha_visible) == hash);
        assert(allocator.allocations == allocations);
#ifdef PXA_TEST_WRAP_SNAPSHOT
        assert(g_snapshot_draw_calls == snapshots);
#endif
    }

    if (!getenv("PXA_CANVAS_MEASURE")) {
        present_canvas(7,0);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&alpha_plane));
        unsigned native_before=g_native_snapshot_calls;
        size_t allocations_before=allocator.allocations;
        uint64_t hash=alpha_plane_hash(&alpha_plane,&alpha_visible);
        for(unsigned i=8;i<108;++i) present_canvas(i,0);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&alpha_plane));
        assert(alpha_plane_hash(&alpha_plane,&alpha_visible)==hash);
#ifdef PXA_TEST_WRAP_SNAPSHOT
        printf("Alpha warm path: frames=100 native_snapshot_calls=%u adapter_allocations=%zu hash=%llu\n",
            g_native_snapshot_calls-native_before,allocator.allocations-allocations_before,(unsigned long long)hash);
#else
        printf("Alpha warm path: frames=100 native_snapshot_calls=unobserved adapter_allocations=%zu\n",
            allocator.allocations-allocations_before);
#endif
        fflush(stdout);
        assert(g_native_snapshot_calls==native_before);
        assert(allocator.allocations==allocations_before);
    }

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

    /* Grid layout: two fractional columns with stretched cells. */
    build_grid_surface();
    {
        lv_obj_t *grid_root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *grid = lv_obj_get_child(grid_root, 0);
        lv_obj_t *first;
        lv_obj_t *second;
        assert(grid != NULL && lv_obj_get_child_count(grid) == 2);
        assert(lv_obj_get_style_layout(grid, 0) == LV_LAYOUT_GRID);
        first = lv_obj_get_child(grid, 0);
        second = lv_obj_get_child(grid, 1);
        assert(first != NULL && second != NULL);
        assert(lv_obj_get_style_grid_cell_column_pos(first, 0) == 0);
        assert(lv_obj_get_style_grid_cell_column_pos(second, 0) == 1);
        assert(lv_obj_get_style_grid_cell_column_span(second, 0) == 1);
        lv_obj_update_layout(lv_screen_active());
        assert(lv_obj_get_x(second) > lv_obj_get_x(first));
    }

    /* Text input: editing reports the text and tapping does not act. */
    build_text_surface();
    {
        lv_obj_t *text_root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *input = lv_obj_get_child(text_root, 0);
        assert(input != NULL);
        /* A text input that receives its text while the transaction applies
         * must still be revealed by the commit. */
        assert(!lv_obj_is_hidden(input));
        assert(lv_obj_get_width(input) == lv_obj_get_width(text_root));
        assert(lv_obj_get_height(input) == 32);
        assert(lv_obj_get_style_bg_opa(input, 0) == LV_OPA_COVER);
        assert(lv_obj_get_style_border_width(input, 0) == 1);
        g_events = 0;
        g_event_value_size = 0;
        lv_textarea_set_text(input, "hello");
        assert(g_events == 1);
        assert(g_event_kind == PXA_UI_EVENT_TEXT);
        assert(g_event_node == 2);
        assert(g_event_value_size == 5);
        assert(memcmp(g_event_value, "hello", 5) == 0);
        lv_textarea_set_text(input, "");
        assert(g_event_kind == PXA_UI_EVENT_TEXT && g_event_value_size == 0);
        lv_textarea_set_text(input,
            "123456789012345678901234567890123456789012345678901234567890123西遊記");
        assert(g_event_value_size == 63); /* Never split the next UTF-8 rune. */
        g_events = 0;
        lv_obj_send_event(input, LV_EVENT_CLICKED, NULL);
        assert(g_events == 0);
        assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
        lv_obj_remove_state(input, LV_STATE_FOCUSED);
        {
            lv_indev_t *press_input = lv_indev_create();
            pointer_input_t press_state = {{0, 0}, 0, 0};
            lv_area_t input_area;
            assert(press_input != NULL);
            lv_indev_set_type(press_input, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(press_input, g_test_display);
            lv_indev_set_user_data(press_input, &press_state);
            lv_indev_set_read_cb(press_input, read_pointer);
            lv_obj_update_layout(text_root);
            lv_obj_get_coords(input, &input_area);
            press_state.point.x = input_area.x1 + 20;
            press_state.point.y = input_area.y1 + 15;
            press_state.pressed = 1;
            lv_indev_read(press_input);
            press_state.pressed = 0;
            lv_indev_read(press_input);
            assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
            lv_indev_delete(press_input);
        }
        g_events = 0;
        lv_obj_send_event(input, LV_EVENT_READY, NULL);
        assert(g_events == 1 && g_event_kind == PXA_UI_EVENT_ACTION);
        g_events = 0;
    }

    build_font_surface();
    {
        static const char *const names[] = {
            "caption", "label", "body", "title", "headline", "display"
        };
        const lv_font_t *expected[] = {
            &lv_font_montserrat_14, &lv_font_montserrat_20,
            &lv_font_montserrat_14, &lv_font_montserrat_20,
            &lv_font_montserrat_14, &lv_font_montserrat_20
        };
        lv_obj_t *font_root = lv_obj_get_child(lv_screen_active(), 0);
        for (size_t index = 0; index < 6u; ++index) {
            lv_obj_t *label = find_label(font_root, names[index]);
            assert(label != NULL);
            assert(lv_obj_get_style_text_font(label, LV_PART_MAIN) == expected[index]);
        }
    }

    build_image_surface();
    {
        bytes_t stream = {{0}, 0};
        bytes_t property = {{0}, 0};
        pxa_lvgl_ui_theme_t theme = adapter_config.theme;
        lv_obj_t *image_root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *tinted = lv_obj_get_child(image_root, 0);
        lv_obj_t *original = lv_obj_get_child(image_root, 1);
        assert(lv_obj_get_style_image_recolor_opa(tinted, 0) == LV_OPA_COVER);
        assert(lv_obj_get_style_image_recolor_opa(original, 0) == LV_OPA_TRANSP);
        theme.rgba[2] = UINT32_C(0xcd3467ff);
        assert(pxa_lvgl_ui_set_theme(adapter, &theme) == PXA_STATUS_OK);
        assert(lv_color_eq(lv_obj_get_style_image_recolor(tinted, 0),
                           lv_color_hex(0xcd3467)));
        assert(lv_obj_get_style_image_recolor_opa(original, 0) == LV_OPA_TRANSP);
        put_u32(&property, 2);
        put_u16(&property, PXA_UI_PROPERTY_FOREGROUND);
        command(&stream, PXA_UI_COMMAND_CLEAR_PROPERTY, &property);
        transact(14, 14, 0, PXA_UI_PATCH, &stream);
        assert(lv_obj_get_style_image_recolor_opa(tinted, 0) == LV_OPA_TRANSP);
    }

    /* A prepared handle is resolved before execute and shares its exact pixels.
     * Failed/cancelled transactions preserve the committed image. */
    {
        pxa_asset_info_t info={.kind=PXA_ASSET_IMAGE,.encoding=PXA_ASSET_ENCODING_RGB565,
            .width=2,.height=2,.format_version=1,.payload_offset=32,.stored_bytes=40,.decoded_bytes=8};
        uint8_t *pixels;
        assert(!pxa_asset_object_create(&info,image_allocate,image_release,NULL,&g_prepared_image,&pixels));
        const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
        for(unsigned i=0;i<4;++i) pxa_write_u16(pixels+2*i,colors[i]);
        pxa_asset_object_finish_loading(g_prepared_image);
        bytes_t stream={{0},0}; uint8_t handle[8]; pxa_write_u64(handle,image_handle);
        set_property(&stream,3,PXA_UI_PROPERTY_IMAGE_HANDLE,handle,8);
        transact(15,15,0,PXA_UI_PATCH,&stream);
        lv_obj_t *image_root=lv_obj_get_child(lv_screen_active(),0);
        lv_obj_t *object=lv_obj_get_child(image_root,1);
        const lv_image_dsc_t *source=lv_image_get_src(object);
        assert(source && source->data==pixels && source->header.cf==LV_COLOR_FORMAT_RGB565);
        lv_image_decoder_dsc_t decoded;
        assert(lv_image_decoder_open(&decoded,source,NULL)==LV_RESULT_OK);
        assert(decoded.decoded->data==pixels && !decoded.user_data && !decoded.cache_entry);
        lv_image_decoder_close(&decoded);
        assert(pxa_asset_object_reference_count(g_prepared_image)==2 && g_image_acquires==1);
        pxa_asset_object_release(g_prepared_image); /* Guest close. UI still owns it. */
        assert(!g_image_frees && pxa_asset_object_reference_count(g_prepared_image)==1);
        assert(!pxa_ui_find_node(service,g_component,PXA_UI_PRIMARY_SURFACE,3,&snapshot));
        pxa_ui_transaction_info_t tx={0}; tx.surface=PXA_UI_PRIMARY_SURFACE; tx.kind=PXA_UI_PATCH; tx.component=g_component;
        void *pending=NULL;
        pxa_ui_command_view_t view={0}; view.command=PXA_UI_COMMAND_SET_PROPERTY;
        view.node_handle=snapshot.backend_handle; view.property=PXA_UI_PROPERTY_IMAGE_HANDLE;
        view.value=(pxa_bytes_t){handle,8};
        assert(!backend.begin(backend.context,&tx,&pending));
        assert(!backend.apply(backend.context,pending,&view,NULL));
        assert(pxa_asset_object_reference_count(g_prepared_image)==2);
        backend.cancel(backend.context,pending);
        assert(pxa_asset_object_reference_count(g_prepared_image)==1 && lv_image_get_src(object)==source);
        pxa_write_u64(handle,image_handle+1);
        assert(!backend.begin(backend.context,&tx,&pending));
        assert(backend.apply(backend.context,pending,&view,NULL)==PXA_STATUS_NOT_FOUND);
        backend.cancel(backend.context,pending);
        assert(lv_image_get_src(object)==source && !g_image_frees);
        pxa_write_u64(handle,image_handle); tx.component=g_component+1;
        assert(!backend.begin(backend.context,&tx,&pending));
        assert(backend.apply(backend.context,pending,&view,NULL)==PXA_STATUS_DENIED);
        backend.cancel(backend.context,pending);
        assert(lv_image_get_src(object)==source && pxa_asset_object_reference_count(g_prepared_image)==1);
        lv_obj_update_layout(image_root);
        lv_area_t location; lv_obj_get_coords(object,&location);
        g_test_rgb565_x=location.x1; g_test_rgb565_y=location.y1; g_test_rgb565_pixels=0;
        size_t allocations=allocator.allocations; unsigned acquires=g_image_acquires;
        for(unsigned frame=0;frame<4;++frame) { lv_obj_invalidate(object); test_lvgl_tick(); }
        assert(g_test_rgb565_pixels==15 && allocator.allocations==allocations && g_image_acquires==acquires);
        g_test_rgb565_x=g_test_rgb565_y=-1;
        /* A native alpha image also aliases the original byte storage. */
        info.encoding=PXA_ASSET_ENCODING_BGRA8888; info.decoded_bytes=16; info.stored_bytes=48;
        assert(!pxa_asset_object_create(&info,image_allocate,image_release,NULL,&g_prepared_image,&pixels));
        for(unsigned i=0;i<4;++i) { pixels[4*i]=0; pixels[4*i+1]=0; pixels[4*i+2]=255; pixels[4*i+3]=128; }
        pxa_asset_object_finish_loading(g_prepared_image);
        stream=(bytes_t){{0},0}; pxa_write_u64(handle,image_handle);
        set_property(&stream,3,PXA_UI_PROPERTY_IMAGE_HANDLE,handle,8);
        transact(16,16,0,PXA_UI_PATCH,&stream);
        assert(g_image_frees==1);
        source=lv_image_get_src(object);
        assert(source->data==pixels && source->header.cf==LV_COLOR_FORMAT_ARGB8888 && source->data[3]==128);
        assert(lv_image_decoder_open(&decoded,source,NULL)==LV_RESULT_OK);
        assert(decoded.decoded->data==pixels && !decoded.user_data && !decoded.cache_entry);
        lv_image_decoder_close(&decoded);
        /* Compare actual alpha composition with LVGL's standard memory-image
         * decoder, using the same straight-alpha pixels and object geometry. */
        lv_draw_buf_t *native_snapshot=lv_snapshot_take(object,LV_COLOR_FORMAT_ARGB8888);
        assert(native_snapshot);
        const lv_color32_t *native_pixel=(const lv_color32_t *)native_snapshot->data;
        assert(native_pixel->alpha > 0 && native_pixel->red > 0);
        lv_image_dsc_t reference=*source; reference.header.flags=0;
        lv_image_set_src(object,&reference);
        lv_draw_buf_t *reference_snapshot=lv_snapshot_take(object,LV_COLOR_FORMAT_ARGB8888);
        assert(reference_snapshot && reference_snapshot->header.w==native_snapshot->header.w &&
            reference_snapshot->header.h==native_snapshot->header.h);
        for(unsigned y=0;y<native_snapshot->header.h;++y)
            assert(!memcmp((uint8_t *)native_snapshot->data+y*native_snapshot->header.stride,
                (uint8_t *)reference_snapshot->data+y*reference_snapshot->header.stride,
                native_snapshot->header.w*4));
        lv_image_set_src(object,source);
        lv_image_cache_drop(&reference);
        lv_draw_buf_destroy(native_snapshot); lv_draw_buf_destroy(reference_snapshot);
        pxa_asset_object_release(g_prepared_image); g_prepared_image=NULL;
        test_lvgl_tick();
    }

    /* Core authenticates Canvas ownership; preparation deduplicates, while
     * redraw only consumes ordered descriptors (even after Guest close). */
    {
        bytes_t commands={{0},0};
        create_node(&commands,4,1,PXA_UI_NODE_CANVAS,0);
        uint8_t dimension[8]={PXA_UI_LENGTH_LOGICAL_PX};
        pxa_write_u32(dimension+4,16*64);
        set_property(&commands,4,PXA_UI_PROPERTY_WIDTH,dimension,8);
        set_property(&commands,4,PXA_UI_PROPERTY_HEIGHT,dimension,8);
        transact(17,17,0,PXA_UI_PATCH,&commands);
        assert(!pxa_ui_find_node(service,g_component,PXA_UI_PRIMARY_SURFACE,4,&snapshot));
        pxa_asset_info_t info={.kind=PXA_ASSET_IMAGE,.encoding=PXA_ASSET_ENCODING_RGB565,
            .width=2,.height=2,.format_version=1,.payload_offset=32,.stored_bytes=40,.decoded_bytes=8};
        uint8_t *pixels;
        assert(!pxa_asset_object_create(&info,image_allocate,image_release,NULL,&g_prepared_image,&pixels));
        const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
        for(unsigned i=0;i<4;++i) pxa_write_u16(pixels+2*i,colors[i]);
        pxa_asset_object_finish_loading(g_prepared_image);
        bytes_t frame=native_canvas_frame(image_handle,0);
        unsigned acquires=g_image_acquires;
        assert(!submit_native_canvas(1,&frame));
        assert(g_image_acquires==acquires+1 && pxa_asset_object_reference_count(g_prepared_image)==2);
        lv_obj_t *root=lv_obj_get_child(lv_screen_active(),0);
        lv_obj_t *object=lv_obj_get_child(root,2);
        lv_obj_update_layout(root); lv_area_t area; lv_obj_get_coords(object,&area);
        g_test_rgb565_x=area.x1; g_test_rgb565_y=area.y1; g_test_rgb565_pixels=0;
        lv_obj_invalidate(object); test_lvgl_tick();
        assert(g_test_rgb565_pixels==15);
        /* Partial preparation and a foreign submitter must preserve the frame. */
        bytes_t bad=native_canvas_frame(image_handle,1);
        assert(submit_native_canvas(2,&bad)==PXA_STATUS_NOT_FOUND);
        assert(pxa_asset_object_reference_count(g_prepared_image)==2);
        pxa_ui_canvas_view_t view={0};
        view.node=4; view.node_handle=snapshot.backend_handle; view.component=g_component+1;
        view.display_list=(pxa_bytes_t){frame.data,frame.size};
        assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_DENIED);
        /* A changed resource needs both a spare array and a new wrapper. */
        assert(!pxa_asset_object_create(&info,image_allocate,image_release,NULL,&g_alternate_image,&pixels));
        for(unsigned i=0;i<4;++i) pxa_write_u16(pixels+2*i,colors[3-i]);
        pxa_asset_object_finish_loading(g_alternate_image);
        bytes_t alternate=native_canvas_frame(image_handle+1,0);
        view.component=g_component;
        view.display_list=(pxa_bytes_t){alternate.data,alternate.size};
        for(size_t failure=1;failure<=2;++failure) {
            size_t resident=allocator.current;
            allocator.fail_at=allocator.allocations+failure;
            assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_at=0;
            assert(allocator.current==resident && pxa_asset_object_reference_count(g_prepared_image)==2);
            assert(pxa_asset_object_reference_count(g_alternate_image)==1);
        }
        view.display_list=(pxa_bytes_t){frame.data,frame.size};
        /* Composition failure is later than image preparation. It must not
         * transfer bytes, retire the old images or clear the committed plane. */
        commands=(bytes_t){{0},0};
        uint8_t composition=PXA_UI_COMPOSITION_ALPHA_OVERLAY;
        set_property(&commands,4,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        transact(18,18,0,PXA_UI_PATCH,&commands);
        pxa_lvgl_ui_alpha_plane_t before, after;
        assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
        size_t visible_before, visible_after;
        uint64_t before_hash=alpha_plane_hash(&before,&visible_before);
        size_t resident=allocator.current;
        /* Force a new plane size, so its allocation can fail after snapshot. */
        lv_obj_set_width(object,17); lv_obj_update_layout(root);
        allocator.fail_at=allocator.allocations+1; /* Same bindings: only the plane allocates. */
        assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_at=0;
        assert(allocator.current==resident && pxa_asset_object_reference_count(g_prepared_image)==2);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
        assert(before.pixels==after.pixels && before.revision==after.revision);
        assert(alpha_plane_hash(&after,&visible_after)==before_hash && visible_after==visible_before);
        lv_obj_set_width(object,16); lv_obj_update_layout(root);
        test_lvgl_tick();
        /* A second overlay takes the transactional staging-plane path. */
        commands=(bytes_t){{0},0};
        set_property(&commands,3,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        transact(19,19,0,PXA_UI_PATCH,&commands);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
        before_hash=alpha_plane_hash(&before,&visible_before);
        resident=allocator.current;
        allocator.fail_at=allocator.allocations+1; /* Same bindings: only the plane allocates. */
        assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_at=0;
        /* A failed resize may release unused staging cache, never the visible plane. */
        assert(allocator.current<=resident && pxa_asset_object_reference_count(g_prepared_image)==2);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
        assert(before.pixels==after.pixels && before.revision==after.revision);
        assert(alpha_plane_hash(&after,&visible_after)==before_hash && visible_after==visible_before);
        uint32_t generation=3;
        if (!getenv("PXA_CANVAS_MEASURE")) {
            /* Both transactional planes have warmed; keep submitting the same
             * visible frame without new Core/adapter storage. */
            for(unsigned i=0;i<2;++i) assert(!submit_native_canvas(generation++,&frame));
            size_t allocations=allocator.allocations, steady=allocator.current;
            size_t old_peak=allocator.peak;
            allocator.peak=steady;
            assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
            uint64_t hash=alpha_plane_hash(&before,&visible_before);
            for(unsigned i=0;i<100;++i) assert(!submit_native_canvas(generation++,&frame));
            assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
            assert(alpha_plane_hash(&after,&visible_after)==hash && visible_after==visible_before);
            printf("Alpha staging: frames=100 allocations=%zu resident=%zu final=%zu peak=%zu hash=%llu\n",
                allocator.allocations-allocations,steady,allocator.current,allocator.peak,(unsigned long long)hash);
            if (allocator.peak<old_peak) allocator.peak=old_peak;
            fflush(stdout);
            assert(allocator.allocations==allocations && allocator.current==steady);
            /* Origin changes do not require new pixel storage or a third plane. */
            int32_t root_x=lv_obj_get_x(root);
            int32_t plane_x=after.x;
            for(unsigned i=0;i<20;++i) {
                lv_obj_set_x(root,root_x+(i%2 ? 0 : 1)); lv_obj_update_layout(root);
                assert(!submit_native_canvas(generation++,&frame));
                assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
                assert(after.x==plane_x+(i%2 ? 0 : 1));
                assert(alpha_plane_hash(&after,&visible_after)==hash);
            }
            assert(allocator.allocations==allocations && allocator.current==steady);
#ifdef PXA_TEST_WRAP_SNAPSHOT
            /* Reject each snapshot independently, including after an earlier
             * overlay already changed staging pixels. Previous published pixels
             * and frame ownership must survive without rebuilding or loading. */
            bytes_t shifted=frame; pxa_write_u32(shifted.data+34,4);
            view.display_list=(pxa_bytes_t){shifted.data,shifted.size};
            for(unsigned failure=1;failure<=2;++failure) {
                assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
                g_snapshot_fail_at=g_snapshot_draw_calls+failure;
                assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_RESOURCE_LIMIT);
                assert(g_snapshot_draw_calls==g_snapshot_fail_at);
                g_snapshot_fail_at=0;
                assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
                assert(after.pixels==before.pixels && after.alpha==before.alpha && after.revision==before.revision);
                assert(alpha_plane_hash(&after,&visible_after)==hash && visible_after==visible_before);
                assert(allocator.current==steady && allocator.allocations==allocations);
                assert(pxa_asset_object_reference_count(g_prepared_image)==2);
                assert(!submit_native_canvas(generation++,&frame));
            }
            view.display_list=(pxa_bytes_t){frame.data,frame.size};
            /* Resize the union without enlarging either snapshot. Drop unused
             * cache before growth, test both plane allocations and a later
             * snapshot failure, then return to the exact original residency. */
            int32_t original_x=lv_obj_get_style_translate_x(object,0);
            for(unsigned failure=0;failure<3;++failure) {
                assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
                lv_obj_set_style_translate_x(object,original_x+17,0); lv_obj_update_layout(root);
                if (failure<2) allocator.fail_at=allocator.allocations+failure+1;
                else g_snapshot_fail_at=g_snapshot_draw_calls+2;
                pxa_status_t resized=backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL);
                assert(resized==PXA_STATUS_RESOURCE_LIMIT);
                allocator.fail_at=0; g_snapshot_fail_at=0;
                assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
                assert(after.pixels==before.pixels && after.alpha==before.alpha && after.revision==before.revision);
                assert(alpha_plane_hash(&after,&visible_after)==hash);
                assert(allocator.current<steady);
                lv_obj_set_style_translate_x(object,original_x,0); lv_obj_update_layout(root);
                assert(!submit_native_canvas(generation++,&frame));
                assert(allocator.current==steady);
            }
            puts("Alpha staging rollback: first=preserved second=preserved resize_failures=3 retry=ok");
#endif
        }
        commands=(bytes_t){{0},0}; composition=PXA_UI_COMPOSITION_BASE;
        set_property(&commands,3,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        set_property(&commands,4,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        transact(20,20,0,PXA_UI_PATCH,&commands);
        /* Alternating resources and command counts must not allocate on a
         * warmed submission path. Recycled wrappers must not pin old pixels. */
        bytes_t mixed=native_canvas_frame(image_handle,1);
        bytes_t shorter=mixed; shorter.size=60;
        const bytes_t *cycle[]={&frame,&alternate,&mixed,&shorter};
        uint64_t hashes[4], submission_times[20000];
        unsigned samples=getenv("PXA_CANVAS_MEASURE") ? 20000u : 2000u;
        for(unsigned i=0;i<8;++i) {
            assert(!submit_native_canvas(generation++,cycle[i%4]));
            hashes[i%4]=snapshot_hash(object);
        }
        if (getenv("PXA_CANVAS_MEASURE")) {
            uint64_t until=monotonic_ns()+UINT64_C(200000000);
            do { for (unsigned i=0;i<4;++i) assert(!submit_native_canvas(generation++,cycle[i])); }
            while (monotonic_ns()<until);
        }
        size_t submission_allocations=allocator.allocations, submission_resident=allocator.current;
        size_t preceding_peak=allocator.peak;
        allocator.peak=allocator.current;
        unsigned submission_acquires=g_image_acquires;
        for(unsigned i=0;i<samples;++i) {
            uint64_t start=monotonic_ns();
            assert(!submit_native_canvas(generation++,cycle[i%4]));
            submission_times[i]=monotonic_ns()-start;
            assert(pxa_asset_object_reference_count(g_prepared_image)==(i%4==1 ? 1u : 2u));
            assert(pxa_asset_object_reference_count(g_alternate_image)==(i%4==0 ? 1u : 2u));
        }
        printf("Canvas prepared metadata: frames=%u allocations=%zu resident_before=%zu resident_after=%zu window_peak=%zu acquires=%u\n",
            samples,allocator.allocations-submission_allocations,submission_resident,allocator.current,
            allocator.peak,g_image_acquires-submission_acquires);
        if (allocator.peak<preceding_peak) allocator.peak=preceding_peak;
        const char *timings_path=getenv("PXA_CANVAS_TIMINGS");
        if (timings_path) {
            FILE *timings=fopen(timings_path,"w"); assert(timings);
            fprintf(timings,"frame,submission_ns,pattern,pixel_hash\n");
            for (unsigned i=0;i<samples;++i)
                fprintf(timings,"%u,%llu,%u,%llu\n",i,(unsigned long long)submission_times[i],i%4,
                    (unsigned long long)hashes[i%4]);
            assert(!fclose(timings));
        }
        assert(allocator.allocations==submission_allocations && allocator.current==submission_resident);
        /* Many uses of one image must not trigger a previous-frame scan for
         * every duplicate during ownership transfer. Exercise chunked writes. */
        bytes_t duplicates={{0},0};
        for (unsigned i=0;i<80;++i) put_data(&duplicates,frame.data+30,30);
        bytes_t shifted=duplicates;
        for (unsigned i=0;i<80;++i) pxa_write_u32(shifted.data+30*i+4,4);
        const bytes_t *duplicate_cycle[]={&duplicates,&shifted};
        uint64_t duplicate_hashes[2];
        for (unsigned i=0;i<8;++i) {
            assert(!submit_native_canvas(generation++,duplicate_cycle[i%2]));
            duplicate_hashes[i%2]=snapshot_hash(object);
        }
        assert(duplicate_hashes[0]!=duplicate_hashes[1]);
        if (getenv("PXA_CANVAS_MEASURE")) {
            uint64_t until=monotonic_ns()+UINT64_C(200000000);
            do { for (unsigned i=0;i<2;++i) assert(!submit_native_canvas(generation++,duplicate_cycle[i])); }
            while (monotonic_ns()<until);
        }
        submission_allocations=allocator.allocations; submission_resident=allocator.current;
        submission_acquires=g_image_acquires; preceding_peak=allocator.peak;
        allocator.peak=allocator.current;
        for (unsigned i=0;i<samples;++i) {
            uint64_t start=monotonic_ns();
            assert(!submit_native_canvas(generation++,duplicate_cycle[i%2]));
            submission_times[i]=monotonic_ns()-start;
        }
        printf("Canvas duplicated metadata: frames=%u allocations=%zu resident_before=%zu resident_after=%zu window_peak=%zu acquires=%u\n",
            samples,allocator.allocations-submission_allocations,submission_resident,allocator.current,
            allocator.peak,g_image_acquires-submission_acquires);
        if (allocator.peak<preceding_peak) allocator.peak=preceding_peak;
        if (timings_path) {
            char duplicate_path[2048];
            int length=snprintf(duplicate_path,sizeof(duplicate_path),"%s.duplicates",timings_path);
            assert(length>0 && (size_t)length<sizeof(duplicate_path));
            FILE *timings=fopen(duplicate_path,"w"); assert(timings);
            fprintf(timings,"frame,submission_ns,pattern,pixel_hash\n");
            for (unsigned i=0;i<samples;++i)
                fprintf(timings,"%u,%llu,%u,%llu\n",i,(unsigned long long)submission_times[i],i%2,
                    (unsigned long long)duplicate_hashes[i%2]);
            assert(!fclose(timings));
        }
        assert(allocator.allocations==submission_allocations && allocator.current==submission_resident);
        assert(!submit_native_canvas(generation++,&frame));
        assert(pxa_asset_object_reference_count(g_prepared_image)==2);
        /* Fail after taking a wrapper from the pool and filling reused spare
         * bindings. Neither the old frame nor the pool's ownership may change. */
        bytes_t partial=native_canvas_frame(image_handle+1,1);
        view.display_list=(pxa_bytes_t){partial.data,partial.size};
        resident=allocator.current;
        assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_NOT_FOUND);
        assert(allocator.current==resident && pxa_asset_object_reference_count(g_prepared_image)==2 &&
            pxa_asset_object_reference_count(g_alternate_image)==1);
        commands=(bytes_t){{0},0}; composition=PXA_UI_COMPOSITION_ALPHA_OVERLAY;
        set_property(&commands,4,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        transact(21,21,0,PXA_UI_PATCH,&commands);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&before));
        before_hash=alpha_plane_hash(&before,&visible_before);
        lv_obj_set_width(object,17); lv_obj_update_layout(root);
        view.display_list=(pxa_bytes_t){alternate.data,alternate.size};
        resident=allocator.current;
        allocator.fail_at=allocator.allocations+1; /* Pool and spare array need no allocation. */
        assert(backend.present_canvas(backend.context,&view,unexpected_frame_release,NULL)==PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_at=0;
        assert(allocator.current==resident && pxa_asset_object_reference_count(g_prepared_image)==2 &&
            pxa_asset_object_reference_count(g_alternate_image)==1);
        assert(pxa_lvgl_ui_alpha_plane(adapter,&after));
        assert(before.pixels==after.pixels && before.revision==after.revision &&
            alpha_plane_hash(&after,&visible_after)==before_hash && visible_after==visible_before);
        lv_obj_set_width(object,16); lv_obj_update_layout(root);
        commands=(bytes_t){{0},0}; composition=PXA_UI_COMPOSITION_BASE;
        set_property(&commands,4,PXA_UI_PROPERTY_COMPOSITION,&composition,1);
        transact(22,22,0,PXA_UI_PATCH,&commands);
        pxa_asset_object_release(g_alternate_image); g_alternate_image=NULL;
        pxa_asset_object_release(g_prepared_image); g_prepared_image=NULL;
        assert(submit_native_canvas(generation++,&frame)==PXA_STATUS_NOT_FOUND);
        g_test_rgb565_pixels=0;
        size_t allocations=allocator.allocations; acquires=g_image_acquires;
        for(unsigned i=0;i<4;++i) { lv_obj_invalidate(object); test_lvgl_tick(); }
        assert(g_test_rgb565_pixels==15 && allocator.allocations==allocations && g_image_acquires==acquires);
        g_test_rgb565_x=g_test_rgb565_y=-1;
        bytes_t empty={{0},0}, clip={{0},0};
        put_u32(&clip,0); put_u32(&clip,0); put_u32(&clip,16); put_u32(&clip,16);
        command(&empty,PXA_UI_CANVAS_CLIP_PUSH,&clip);
        clip.size=0; command(&empty,PXA_UI_CANVAS_CLIP_POP,&clip);
        unsigned frees=g_image_frees;
        assert(!submit_native_canvas(generation++,&empty));
        assert(g_image_frees==frees+1); /* No image stays pinned by recycled metadata. */
    }

    /* Property commands can precede removal of their node or an ancestor.
     * Commit must not revisit freed backend handles in its scroll pass. */
    {
        bytes_t commands = {{0}, 0}, payload = {{0}, 0};
        create_node(&commands, 100, 1, PXA_UI_NODE_BOX, 0);
        create_node(&commands, 101, 100, PXA_UI_NODE_TEXT, 0);
        transact(23, 23, 0, PXA_UI_PATCH, &commands);
        commands.size = 0;
        set_property(&commands, 101, PXA_UI_PROPERTY_TEXT, "remove child", 12);
        put_u32(&payload, 100);
        command(&commands, PXA_UI_COMMAND_REMOVE, &payload);
        transact(24, 24, 0, PXA_UI_PATCH, &commands);
        assert(pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE,
                                    100, &snapshot) == PXA_STATUS_NOT_FOUND);
        assert(pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE,
                                    101, &snapshot) == PXA_STATUS_NOT_FOUND);
    }

    /* A late allocation failure must not destroy a removed/replaced subtree.
     * Nested new nodes also exercise recursive cancellation of CREATE handles. */
    {
        bytes_t commands = {{0}, 0}, payload = {{0}, 0};
        create_node(&commands, 100, 1, PXA_UI_NODE_BOX, 0);
        create_node(&commands, 101, 100, PXA_UI_NODE_TEXT, 0);
        set_property(&commands, 101, PXA_UI_PROPERTY_TEXT, "retained", 8);
        transact(25, 25, 0, PXA_UI_PATCH, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *retained = find_label(root, "retained");
        assert(retained);
        uint64_t before_hash = snapshot_hash(root);
        commands.size = 0;
        create_overlay(&commands, 200, 1, 70, 30);
        create_node(&commands, 201, 200, PXA_UI_NODE_TEXT, 0);
        put_u32(&payload, 100);
        command(&commands, PXA_UI_COMMAND_REMOVE, &payload);
        allocator.fail_size = 70 * 30 * sizeof(uint16_t);
        unsigned failures = allocator.size_failures;
        assert(transact_status(26, 26, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_size = 0;
        assert(allocator.size_failures > failures);
        assert(find_label(root, "retained") == retained && snapshot_hash(root) == before_hash);
        assert(!pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 101, &snapshot));
        assert(pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 200, &snapshot) == PXA_STATUS_NOT_FOUND);
        transact(27, 27, 0, PXA_UI_PATCH, &commands);
        assert(!find_label(root, "retained"));
        pxa_lvgl_ui_alpha_plane_t before, after;
        assert(pxa_lvgl_ui_alpha_plane(adapter, &before));
        size_t visible;
        uint64_t alpha_hash = alpha_plane_hash(&before, &visible);
        assert(visible);
        before_hash = snapshot_hash(root);
        assert(!pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 200, &snapshot));
        void *old_handle = snapshot.backend_handle;
        commands.size = 0;
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_overlay(&commands, 200, 1, 110, 31);
        create_node(&commands, 201, 200, PXA_UI_NODE_TEXT, 0);
        /* Fail each of the two plane allocations, not command construction. */
        for (unsigned i = 0; i < 2; ++i) {
            allocator.fail_size = 110 * 31 * (i ? 1 : sizeof(uint16_t));
            failures = allocator.size_failures;
            assert(transact_status(28 + i, 28 + i, 0, PXA_UI_REPLACE_SURFACE, &commands) == PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_size = 0;
            assert(allocator.size_failures > failures);
            assert(lv_obj_get_child_count(lv_screen_active()) == 1 &&
                   lv_obj_get_child(lv_screen_active(), 0) == root);
            assert(snapshot_hash(root) == before_hash);
            assert(!pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 200, &snapshot));
            assert(snapshot.backend_handle == old_handle);
            assert(pxa_lvgl_ui_alpha_plane(adapter, &after));
            assert(after.pixels == before.pixels && after.revision == before.revision &&
                   alpha_plane_hash(&after, &visible) == alpha_hash);
        }
        transact(30, 30, 0, PXA_UI_REPLACE_SURFACE, &commands);
        assert(lv_obj_get_child_count(lv_screen_active()) == 1);
        assert(!pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 200, &snapshot));
        assert(snapshot.backend_handle != old_handle);
        assert(pxa_lvgl_ui_alpha_plane(adapter, &after));
        assert(after.revision != before.revision && after.width == 110 && after.height == 31);
        /* Final virtual state marks both nodes removed; backend replay still
         * needs the transient parent's CREATE handle to build the hierarchy. */
        commands.size = 0;
        payload.size = 0;
        create_node(&commands, 300, 1, PXA_UI_NODE_BOX, 0);
        create_node(&commands, 301, 300, PXA_UI_NODE_TEXT, 0);
        set_property(&commands, 301, PXA_UI_PROPERTY_TEXT, "transient", 9);
        put_u32(&payload, 300);
        command(&commands, PXA_UI_COMMAND_REMOVE, &payload);
        transact(31, 31, 0, PXA_UI_PATCH, &commands);
        assert(lv_obj_get_child_count(lv_screen_active()) == 1);
        assert(!find_label(lv_screen_active(), "transient"));
    }

    /* Old image handles may already be closed by the Guest. Rollback must
     * transfer the held reference back, not attempt to reacquire the handle. */
    {
        pxa_asset_info_t info = {.kind=PXA_ASSET_IMAGE, .encoding=PXA_ASSET_ENCODING_RGB565,
            .width=2, .height=2, .format_version=1, .payload_offset=32, .stored_bytes=40, .decoded_bytes=8};
        uint8_t *pixels;
        assert(!pxa_asset_object_create(&info, image_allocate, image_release, NULL, &g_prepared_image, &pixels));
        for (unsigned i=0; i<4; ++i) pxa_write_u16(pixels + i*2, 0xf800);
        pxa_asset_object_finish_loading(g_prepared_image);
        assert(!pxa_asset_object_create(&info, image_allocate, image_release, NULL, &g_alternate_image, &pixels));
        for (unsigned i=0; i<4; ++i) pxa_write_u16(pixels + i*2, 0x07e0);
        pxa_asset_object_finish_loading(g_alternate_image);
        bytes_t commands = {{0}, 0};
        uint8_t handle[8];
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_overlay(&commands, 2, 1, 70, 30);
        create_node(&commands, 3, 2, PXA_UI_NODE_IMAGE, 0);
        pxa_write_u64(handle, image_handle);
        set_property(&commands, 3, PXA_UI_PROPERTY_IMAGE_HANDLE, handle, 8);
        transact(32, 32, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *object = find_type(root, &lv_image_class);
        const void *source = lv_image_get_src(object);
        pxa_asset_object_t *old_image = g_prepared_image;
        pxa_asset_object_release(g_prepared_image);
        g_prepared_image = NULL;
        unsigned frees = g_image_frees;
        pxa_lvgl_ui_alpha_plane_t before, after;
        size_t visible;
        assert(pxa_lvgl_ui_alpha_plane(adapter, &before));
        uint64_t before_hash = alpha_plane_hash(&before, &visible);
        assert(visible);
        pxa_write_u64(handle, image_handle + 1);
        g_expect_unlocked_assets = 1;
        for (unsigned variant=0; variant<4; ++variant) {
            commands.size = 0;
            if (variant == 3) {
                const char *path = "assets/test.png";
                set_property(&commands, 3, PXA_UI_PROPERTY_ASSET, path, strlen(path));
            }
            if (variant != 1)
                set_property(&commands, 3, PXA_UI_PROPERTY_IMAGE_HANDLE, handle, 8);
            if (variant == 1 || variant == 2) {
                bytes_t clear = {{0}, 0};
                put_u32(&clear, 3); put_u16(&clear, PXA_UI_PROPERTY_IMAGE_HANDLE);
                command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &clear);
                if (variant == 2)
                    set_property(&commands, 3, PXA_UI_PROPERTY_IMAGE_HANDLE, handle, 8);
            }
            /* Multiple overlays require a staging plane, guaranteeing a late
             * allocation after the image changes have been applied. */
            create_overlay(&commands, 9, 1, 11, 13);
            allocator.fail_during_execute = 1;
            assert(transact_status(33 + variant, 33 + variant, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_during_execute = 0;
            assert(g_image_frees == frees && lv_image_get_src(object) == source);
            assert(pxa_asset_object_reference_count(old_image) == 1 &&
                   pxa_asset_object_reference_count(g_alternate_image) == 1);
            assert(pxa_lvgl_ui_alpha_plane(adapter, &after));
            assert(after.pixels == before.pixels && after.revision == before.revision &&
                   alpha_plane_hash(&after, &visible) == before_hash);
            assert(pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 9, &snapshot) == PXA_STATUS_NOT_FOUND);
        }
        g_expect_unlocked_assets = 0;
        commands.size = 0;
        set_property(&commands, 3, PXA_UI_PROPERTY_ASSET, "assets/test.png", 15);
        g_missing_asset = 1;
        assert(transact_status(37, 37, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
        g_missing_asset = 0;
        assert(g_image_frees == frees && lv_image_get_src(object) == source);
        commands.size = 0;
        set_property(&commands, 3, PXA_UI_PROPERTY_IMAGE_HANDLE, handle, 8);
        transact(38, 38, 0, PXA_UI_PATCH, &commands);
        assert(g_image_frees == frees + 1 && pxa_asset_object_reference_count(g_alternate_image) == 2);
        pxa_asset_object_release(g_alternate_image);
        g_alternate_image = NULL;
        commands.size = 0;
        bytes_t clear = {{0}, 0};
        put_u32(&clear, 3); put_u16(&clear, PXA_UI_PROPERTY_IMAGE_HANDLE);
        command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &clear);
        transact(39, 39, 0, PXA_UI_PATCH, &commands);
        assert(g_image_frees == frees + 2);
    }

    /* Grid preparation and MOVE must be atomic with late alpha preparation.
     * Existing children can even be moved into a newly-created subtree. */
    {
        bytes_t commands = {{0}, 0}, columns = {{0}, 0}, rows = {{0}, 0};
        uint8_t size[8] = {PXA_UI_LENGTH_LOGICAL_PX};
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_node(&commands, 2, 1, PXA_UI_NODE_BOX, 0);
        create_node(&commands, 3, 2, PXA_UI_NODE_TEXT, 0);
        create_node(&commands, 4, 2, PXA_UI_NODE_TEXT, 0);
        create_node(&commands, 5, 1, PXA_UI_NODE_BOX, 0);
        create_node(&commands, 6, 5, PXA_UI_NODE_TEXT, 0);
        set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "first", 5);
        set_property(&commands, 4, PXA_UI_PROPERTY_TEXT, "second", 6);
        set_property(&commands, 6, PXA_UI_PROPERTY_TEXT, "third", 5);
        pxa_write_u32(size + 4, 180 * 64);
        set_property(&commands, 2, PXA_UI_PROPERTY_WIDTH, size, 8);
        pxa_write_u32(size + 4, 60 * 64);
        set_property(&commands, 2, PXA_UI_PROPERTY_HEIGHT, size, 8);
        put_grid_track(&columns, PXA_UI_GRID_FRACTION, 1);
        put_grid_track(&columns, PXA_UI_GRID_FRACTION, 2);
        put_grid_track(&rows, PXA_UI_GRID_CONTENT, 0);
        set_property(&commands, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data, columns.size);
        set_property(&commands, 2, PXA_UI_PROPERTY_GRID_ROWS, rows.data, rows.size);
        create_overlay(&commands, 8, 1, 70, 30);
        transact(40, 40, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *grid = lv_obj_get_child(root, 0), *other = lv_obj_get_child(root, 1);
        lv_obj_t *first = find_label(root, "first"), *second = find_label(root, "second");
        const int32_t *old_columns = lv_obj_get_style_grid_column_dsc_array(grid, 0);
        const int32_t *old_rows = lv_obj_get_style_grid_row_dsc_array(grid, 0);
        const uint64_t old_hash = snapshot_hash(root);
        const size_t resident = allocator.current;
        pxa_lvgl_ui_alpha_plane_t before, after;
        size_t visible;
        assert(pxa_lvgl_ui_alpha_plane(adapter, &before));
        const uint64_t old_alpha = alpha_plane_hash(&before, &visible);
        assert(visible);
        columns.size = 0;
        put_grid_track(&columns, PXA_UI_GRID_FIXED, 17 * 64);
        put_grid_track(&columns, PXA_UI_GRID_FRACTION, 1);
        commands.size = 0;
        set_property(&commands, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data, columns.size);
        set_property(&commands, 2, PXA_UI_PROPERTY_GRID_ROWS, rows.data, rows.size);
        /* Fail the first descriptor, then the second after the first has been
         * prepared. Cancellation must free the earlier preparation as well. */
        for (unsigned tracks = 3; tracks >= 2; --tracks) {
            unsigned failures = allocator.size_failures;
            allocator.fail_size = tracks * sizeof(lv_coord_t);
            assert(transact_status(41, 41, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_size = 0;
            assert(allocator.size_failures > failures && allocator.current == resident);
            assert(lv_obj_get_style_grid_column_dsc_array(grid, 0) == old_columns);
            assert(snapshot_hash(root) == old_hash);
        }
        for (unsigned variant = 0; variant < 5; ++variant) {
            commands.size = 0;
            bytes_t payload = {{0}, 0};
            set_property(&commands, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data, columns.size);
            set_property(&commands, 2, PXA_UI_PROPERTY_GRID_ROWS, rows.data, rows.size);
            if (variant == 1 || variant == 2 || variant == 4) {
                put_u32(&payload, 2);
                put_u16(&payload, variant == 4 ? PXA_UI_PROPERTY_GRID_ROWS : PXA_UI_PROPERTY_GRID_COLUMNS);
                command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &payload);
                if (variant == 2)
                    set_property(&commands, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data, columns.size);
                if (variant == 4)
                    set_property(&commands, 2, PXA_UI_PROPERTY_GRID_ROWS, rows.data, rows.size);
            }
            create_overlay(&commands, 9, 1, 11, 13);
            /* Move siblings, then move one again; reverse replay must restore
             * both the original parent and the exact sibling order. */
            payload.size = 0;
            put_u32(&payload, 3); put_u32(&payload, 5); put_u32(&payload, 6);
            command(&commands, PXA_UI_COMMAND_MOVE, &payload);
            payload.size = 0;
            put_u32(&payload, 4); put_u32(&payload, variant >= 3 ? 9 : 5); put_u32(&payload, 0);
            command(&commands, PXA_UI_COMMAND_MOVE, &payload);
            payload.size = 0;
            put_u32(&payload, 3); put_u32(&payload, 2); put_u32(&payload, 0);
            command(&commands, PXA_UI_COMMAND_MOVE, &payload);
            allocator.fail_during_execute = 1;
            assert(transact_status(42 + variant, 42 + variant, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_during_execute = 0;
            assert(lv_obj_get_child_count(root) == 3 && lv_obj_get_child(root, 0) == grid);
            assert(lv_obj_get_child_count(grid) == 2 && lv_obj_get_child(grid, 0) == first &&
                   lv_obj_get_child(grid, 1) == second && lv_obj_get_child_count(other) == 1);
            assert(lv_obj_get_style_grid_column_dsc_array(grid, 0) == old_columns &&
                   lv_obj_get_style_grid_row_dsc_array(grid, 0) == old_rows &&
                   lv_obj_get_style_layout(grid, 0) == LV_LAYOUT_GRID);
            assert(snapshot_hash(root) == old_hash && allocator.current == resident);
            assert(pxa_lvgl_ui_alpha_plane(adapter, &after));
            assert(after.pixels == before.pixels && after.revision == before.revision &&
                   alpha_plane_hash(&after, &visible) == old_alpha);
            assert(!pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 4, &snapshot));
            assert(snapshot.parent == 2);
            assert(pxa_ui_find_node(service, g_component, PXA_UI_PRIMARY_SURFACE, 9, &snapshot) == PXA_STATUS_NOT_FOUND);
        }
        /* Retry the exact final transaction and ensure MOVE survives commit. */
        transact(47, 47, 0, PXA_UI_PATCH, &commands);
        assert(lv_obj_get_child_count(grid) == 1 && lv_obj_get_child(grid, 0) == first);
        assert(lv_obj_get_parent(second) == lv_obj_get_child(root, 3));
        assert(lv_obj_get_style_grid_column_dsc_array(grid, 0)[0] == 17);
        commands.size = 0;
        bytes_t clear = {{0}, 0};
        put_u32(&clear, 2); put_u16(&clear, PXA_UI_PROPERTY_GRID_COLUMNS);
        command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &clear);
        transact(48, 48, 0, PXA_UI_PATCH, &commands);
        assert(lv_obj_get_style_grid_column_dsc_array(grid, 0) == NULL &&
               lv_obj_get_style_layout(grid, 0) == LV_LAYOUT_NONE);
        commands.size = 0;
        set_property(&commands, 2, PXA_UI_PROPERTY_GRID_COLUMNS, columns.data, columns.size);
        transact(49, 49, 0, PXA_UI_PATCH, &commands);
        assert(lv_obj_get_style_layout(grid, 0) == LV_LAYOUT_GRID);
        /* Moving an earlier sibling directly before a later one must not
         * accidentally move it after that sibling. */
        commands.size = 0;
        bytes_t move = {{0}, 0};
        put_u32(&move, 4); put_u32(&move, 2); put_u32(&move, 0);
        command(&commands, PXA_UI_COMMAND_MOVE, &move);
        transact(50, 50, 0, PXA_UI_PATCH, &commands);
        commands.size = 0;
        move.size = 0;
        put_u32(&move, 3); put_u32(&move, 2); put_u32(&move, 4);
        command(&commands, PXA_UI_COMMAND_MOVE, &move);
        transact(51, 51, 0, PXA_UI_PATCH, &commands);
        assert(lv_obj_get_child(grid, 0) == first && lv_obj_get_child(grid, 1) == second);
    }

    /* A rejected PATCH must preserve ordinary properties as well as handles. */
    {
        bytes_t commands = {{0}, 0};
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_overlay(&commands, 2, 1, 70, 30);
        create_node(&commands, 3, 1, PXA_UI_NODE_TEXT, 0);
        set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "original", 8);
        transact(52, 52, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *label = find_label(root, "original");
        assert(label);
        uint64_t hash = snapshot_hash(root);
        size_t resident = allocator.current;
        commands.size = 0;
        set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "changed", 7);
        uint8_t length[8] = {PXA_UI_LENGTH_LOGICAL_PX};
        pxa_write_u32(length + 4, 120 * 64);
        set_property(&commands, 3, PXA_UI_PROPERTY_WIDTH, length, 8);
        create_overlay(&commands, 9, 1, 11, 13);
        allocator.fail_during_execute = 2; /* Text undo, then staging alpha. */
        assert(transact_status(53, 53, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_during_execute = 0;
        assert(!strcmp(lv_label_get_text(label), "original"));
        assert(snapshot_hash(root) == hash && allocator.current == resident);
    }

    {
        /* Mix styling, control state, remove, position and repeated text.
         * A failure while capturing the second text must undo the first. */
        bytes_t commands = {{0}, 0};
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_overlay(&commands, 2, 1, 70, 30);
        create_node(&commands, 3, 1, PXA_UI_NODE_TEXT, 0);
        create_node(&commands, 4, 1, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT);
        create_node(&commands, 5, 1, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_SELECTION);
        create_node(&commands, 6, 1, PXA_UI_NODE_PROGRESS, 0);
        create_node(&commands, 7, 1, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON);
        set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "retained", 8);
        set_property(&commands, 4, PXA_UI_PROPERTY_TEXT, "editable", 8);
        set_property(&commands, 5, PXA_UI_PROPERTY_TEXT, "one\ntwo\nthree", 13);
        uint8_t value[16] = {0};
        pxa_write_u32(value, 65);
        set_property(&commands, 6, PXA_UI_PROPERTY_VALUE, value, 4);
        transact(54, 54, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *label = find_label(root, "retained");
        lv_obj_t *input = find_type(root, &lv_textarea_class);
        lv_obj_t *dropdown = find_type(root, &lv_dropdown_class);
        lv_obj_t *bar = find_type(root, &lv_bar_class);
        lv_obj_t *button = find_type(root, &lv_button_class);
        assert(label && input && dropdown && bar && button);
        lv_textarea_set_cursor_pos(input, 3);
        lv_label_set_text_selection_start(lv_textarea_get_label(input), 1);
        lv_label_set_text_selection_end(lv_textarea_get_label(input), 3);
        uint32_t selection_start = lv_label_get_text_selection_start(lv_textarea_get_label(input));
        uint32_t selection_end = lv_label_get_text_selection_end(lv_textarea_get_label(input));
        lv_dropdown_set_selected(dropdown, 2);
        lv_style_value_t style;
        lv_obj_remove_local_style_prop(label, LV_STYLE_WIDTH, 0);
        assert(lv_obj_get_local_style_prop(label, LV_STYLE_WIDTH, &style, 0) != LV_STYLE_RES_FOUND);
        const lv_style_prop_t checked_styles[] = {LV_STYLE_WIDTH, LV_STYLE_HEIGHT,
            LV_STYLE_MIN_WIDTH, LV_STYLE_MAX_WIDTH, LV_STYLE_MIN_HEIGHT, LV_STYLE_MAX_HEIGHT,
            LV_STYLE_X, LV_STYLE_Y, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_TOP, LV_STYLE_PAD_RIGHT,
            LV_STYLE_PAD_BOTTOM, LV_STYLE_PAD_ROW, LV_STYLE_PAD_COLUMN, LV_STYLE_FLEX_GROW,
            LV_STYLE_FLEX_MAIN_PLACE, LV_STYLE_FLEX_CROSS_PLACE, LV_STYLE_FLEX_TRACK_PLACE,
            LV_STYLE_RADIUS, LV_STYLE_BORDER_WIDTH, LV_STYLE_TEXT_ALIGN, LV_STYLE_TEXT_FONT,
            LV_STYLE_TEXT_COLOR, LV_STYLE_TEXT_OPA, LV_STYLE_OPA,
            LV_STYLE_GRID_CELL_COLUMN_POS, LV_STYLE_GRID_CELL_COLUMN_SPAN,
            LV_STYLE_GRID_CELL_ROW_POS, LV_STYLE_GRID_CELL_ROW_SPAN,
            LV_STYLE_GRID_CELL_X_ALIGN, LV_STYLE_GRID_CELL_Y_ALIGN};
        lv_style_value_t old_values[sizeof(checked_styles)/sizeof(checked_styles[0])] = {0};
        lv_style_res_t old_presence[sizeof(checked_styles)/sizeof(checked_styles[0])];
        for (unsigned i=0; i<sizeof(checked_styles)/sizeof(checked_styles[0]); ++i)
            old_presence[i]=lv_obj_get_local_style_prop(label,checked_styles[i],&old_values[i],0);
        const uint64_t original = snapshot_hash(root);
        const size_t resident = allocator.current;
        const uint32_t button_children = lv_obj_get_child_count(button);
        const int32_t index = lv_obj_get_index(label);
        pxa_lvgl_ui_alpha_plane_t before, after;
        size_t visible;
        assert(pxa_lvgl_ui_alpha_plane(adapter, &before));
        const uint64_t alpha = alpha_plane_hash(&before, &visible);
        for (unsigned variant=0; variant<3; ++variant) {
            commands.size = 0;
            set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "first change", 12);
            bytes_t clear_text = {{0}, 0};
            put_u32(&clear_text,3); put_u16(&clear_text,PXA_UI_PROPERTY_TEXT);
            command(&commands,PXA_UI_COMMAND_CLEAR_PROPERTY,&clear_text);
            set_property(&commands, 3, PXA_UI_PROPERTY_TEXT, "second change", 13);
            uint8_t length[8] = {PXA_UI_LENGTH_LOGICAL_PX};
            pxa_write_u32(length + 4, 123 * 64);
            const uint16_t lengths[] = {PXA_UI_PROPERTY_WIDTH,PXA_UI_PROPERTY_HEIGHT,
                PXA_UI_PROPERTY_MIN_WIDTH,PXA_UI_PROPERTY_MAX_WIDTH,PXA_UI_PROPERTY_MIN_HEIGHT,
                PXA_UI_PROPERTY_MAX_HEIGHT,PXA_UI_PROPERTY_X,PXA_UI_PROPERTY_Y};
            for (unsigned i=0; i<sizeof(lengths)/sizeof(lengths[0]); ++i)
                set_property(&commands,3,lengths[i],length,8);
            for (unsigned i=0;i<4;++i) pxa_write_u32(value+i*4,3*64);
            set_property(&commands,3,PXA_UI_PROPERTY_PADDING,value,16);
            set_property(&commands,3,PXA_UI_PROPERTY_GAP,value,4);
            set_property(&commands,3,PXA_UI_PROPERTY_RADIUS,value,4);
            set_property(&commands,3,PXA_UI_PROPERTY_BORDER_WIDTH,value,4);
            pxa_write_u16(value,2);
            set_property(&commands,3,PXA_UI_PROPERTY_GROW,value,2);
            set_property(&commands,3,PXA_UI_PROPERTY_FONT_ROLE,value,2);
            value[0]=1;
            set_property(&commands,3,PXA_UI_PROPERTY_ALIGN,value,1);
            set_property(&commands,3,PXA_UI_PROPERTY_JUSTIFY,value,1);
            set_property(&commands,3,PXA_UI_PROPERTY_TEXT_ALIGN,value,1);
            value[0]=123;
            set_property(&commands,3,PXA_UI_PROPERTY_OPACITY,value,1);
            bytes_t cell={{0},0}; put_grid_cell(&cell,1,0,1,1);
            set_property(&commands,3,PXA_UI_PROPERTY_GRID_CELL,cell.data,cell.size);
            memset(value, 0, sizeof(value)); value[0]=1;
            set_property(&commands, 3, PXA_UI_PROPERTY_POSITION, value, 1);
            value[0]=0;
            set_property(&commands, 3, PXA_UI_PROPERTY_VISIBLE, value, 1);
            set_property(&commands, 3, PXA_UI_PROPERTY_ENABLED, value, 1);
            value[0]=1; pxa_write_u32(value+4, UINT32_C(0xff00ffff));
            set_property(&commands, 3, PXA_UI_PROPERTY_FOREGROUND, value, 8);
            set_property(&commands, 7, PXA_UI_PROPERTY_BACKGROUND, value, 8);
            pxa_write_u64(value, PXA_UI_EVENT_MASK_POINTER);
            set_property(&commands, 3, PXA_UI_PROPERTY_EVENT_MASK, value, 8);
            set_property(&commands, 7, PXA_UI_PROPERTY_TEXT, "new button text", 15);
            set_property(&commands, 4, PXA_UI_PROPERTY_TEXT, "new input", 9);
            set_property(&commands, 5, PXA_UI_PROPERTY_TEXT, "new", 3);
            pxa_write_u32(value, 20);
            set_property(&commands, 6, PXA_UI_PROPERTY_MAX_VALUE, value, 4);
            bytes_t payload = {{0}, 0};
            if (variant==2) {
                put_u32(&payload, 3);
                command(&commands, PXA_UI_COMMAND_REMOVE, &payload);
            }
            create_overlay(&commands, 9, 1, 11, 13);
            /* Three label snapshots plus input/options precede staging.
             * Variant 0 fails while capturing CLEAR after the first SET. */
            allocator.peak = allocator.current;
            size_t allocations_before = allocator.allocations;
            allocator.fail_during_execute = variant==0 ? 2 : 7;
            assert(transact_status(55+variant, 55+variant, 0, PXA_UI_PATCH, &commands)==PXA_STATUS_RESOURCE_LIMIT);
            allocator.fail_during_execute=0;
            assert(!strcmp(lv_label_get_text(label), "retained"));
            assert(!strcmp(lv_textarea_get_text(input), "editable") && lv_textarea_get_cursor_pos(input)==3);
            assert(lv_label_get_text_selection_start(lv_textarea_get_label(input))==selection_start &&
                   lv_label_get_text_selection_end(lv_textarea_get_label(input))==selection_end);
            assert(!strcmp(lv_dropdown_get_options(dropdown), "one\ntwo\nthree") && lv_dropdown_get_selected(dropdown)==2);
            assert(lv_bar_get_value(bar)==65 && lv_bar_get_max_value(bar)==100);
            assert(lv_obj_get_child_count(button)==button_children);
            assert(lv_obj_get_index(label)==index && !lv_obj_is_hidden(label) &&
                   !lv_obj_is_floating(label));
            assert(!lv_obj_has_state(label, LV_STATE_DISABLED));
            assert(lv_obj_get_local_style_prop(label, LV_STYLE_WIDTH, &style, 0) != LV_STYLE_RES_FOUND);
            assert(!pxa_ui_find_node(service,g_component,PXA_UI_PRIMARY_SURFACE,3,&snapshot));
            assert(snapshot.event_mask==0);
            for (unsigned i=0;i<sizeof(checked_styles)/sizeof(checked_styles[0]);++i) {
                lv_style_value_t current={0};
                assert(lv_obj_get_local_style_prop(label,checked_styles[i],&current,0)==old_presence[i]);
                if (old_presence[i]==LV_STYLE_RES_FOUND) assert(!memcmp(&current,&old_values[i],sizeof(current)));
            }
            assert(snapshot_hash(root)==original && allocator.current==resident);
            printf("Property rollback: variant=%u resident=%zu peak=%zu remaining=%zu allocations=%zu\n",
                   variant,resident,allocator.peak,allocator.current,allocator.allocations-allocations_before);
            assert(pxa_lvgl_ui_alpha_plane(adapter,&after) && after.revision==before.revision &&
                after.pixels==before.pixels && alpha_plane_hash(&after,&visible)==alpha);
        }
        /* Successful retry keeps all mutations and releases only undo data. */
        transact(58,58,0,PXA_UI_PATCH,&commands);
        assert(pxa_ui_find_node(service,g_component,PXA_UI_PRIMARY_SURFACE,3,&snapshot)==PXA_STATUS_NOT_FOUND);
        assert(!strcmp(lv_textarea_get_text(input),"new input") && lv_bar_get_max_value(bar)==20);
    }

    {
        bytes_t commands={{0},0};
        uint8_t value[8]={PXA_UI_LENGTH_LOGICAL_PX};
        create_node(&commands,1,0,PXA_UI_NODE_ROOT,0);
        create_overlay(&commands,2,1,70,30);
        create_node(&commands,3,1,PXA_UI_NODE_VIRTUAL_LIST,0);
        pxa_write_u32(value+4,80*64);
        set_property(&commands,3,PXA_UI_PROPERTY_HEIGHT,value,8);
        pxa_write_u32(value+4,100*64);
        set_property(&commands,3,PXA_UI_PROPERTY_WIDTH,value,8);
        pxa_write_u32(value,100);
        set_property(&commands,3,PXA_UI_PROPERTY_ITEM_COUNT,value,4);
        pxa_write_u32(value,10*64);
        set_property(&commands,3,PXA_UI_PROPERTY_ITEM_EXTENT,value,4);
        pxa_write_u32(value,60*64);
        set_property(&commands,3,PXA_UI_PROPERTY_SCROLL_POSITION,value,4);
        create_node(&commands,4,3,PXA_UI_NODE_BOX,0);
        value[0]=1;
        set_property(&commands,4,PXA_UI_PROPERTY_POSITION,value,1);
        memset(value,0,sizeof(value));
        value[0]=PXA_UI_LENGTH_LOGICAL_PX;
        pxa_write_u32(value+4,70*64);
        set_property(&commands,4,PXA_UI_PROPERTY_Y,value,8);
        pxa_write_u32(value+4,60*64);
        set_property(&commands,4,PXA_UI_PROPERTY_WIDTH,value,8);
        pxa_write_u32(value+4,10*64);
        set_property(&commands,4,PXA_UI_PROPERTY_HEIGHT,value,8);
        transact(59,59,0,PXA_UI_REPLACE_SURFACE,&commands);
        root=lv_obj_get_child(lv_screen_active(),0);
        lv_obj_t *list=lv_obj_get_child(root,1);
        assert(lv_obj_get_scroll_y(list)==60);
        lv_obj_t *row=lv_obj_get_child(list,1);
        lv_area_t row_coords, list_coords;
        lv_obj_get_coords(row,&row_coords);
        lv_obj_get_coords(list,&list_coords);
        assert(lv_obj_is_ignore_layout(row) && !lv_obj_is_floating(row));
        assert(lv_obj_get_y(row)==70 && row_coords.y1==list_coords.y1+10);
        uint64_t hash=snapshot_hash(root);
        size_t resident=allocator.current;
        commands.size=0;
        pxa_write_u32(value,1);
        set_property(&commands,3,PXA_UI_PROPERTY_ITEM_COUNT,value,4);
        pxa_write_u32(value,64);
        set_property(&commands,3,PXA_UI_PROPERTY_ITEM_EXTENT,value,4);
        pxa_write_u32(value,0);
        set_property(&commands,3,PXA_UI_PROPERTY_SCROLL_POSITION,value,4);
        create_overlay(&commands,9,1,11,13);
        allocator.fail_during_execute=1;
        assert(transact_status(60,60,0,PXA_UI_PATCH,&commands)==PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_during_execute=0;
        assert(lv_obj_get_scroll_y(list)==60);
        assert(lv_obj_get_height(lv_obj_get_child(list,0))==1000);
        assert(lv_obj_is_ignore_layout(row) && !lv_obj_is_floating(row));
        lv_obj_get_coords(row,&row_coords);
        assert(row_coords.y1==list_coords.y1+10);
        assert(snapshot_hash(root)==hash && allocator.current==resident);
    }

    /* Dynamic text limits are optional, transactional, and resettable. */
    {
        bytes_t commands = {{0}, 0};
        uint8_t value[8];
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_node(&commands, 2, 1, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_TEXT_INPUT);
        pxa_write_u64(value, PXA_UI_EVENT_MASK_TEXT);
        set_property(&commands, 2, PXA_UI_PROPERTY_EVENT_MASK, value, 8);
        transact(61, 61, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *input = lv_obj_get_child(root, 0);
        commands.size = 0;
        value[0] = 1;
        set_property(&commands, 2, PXA_UI_PROPERTY_TEXT_SINGLE_LINE, value, 1);
        pxa_write_u32(value, 512);
        set_property(&commands, 2, PXA_UI_PROPERTY_TEXT_MAX_BYTES, value, 4);
        value[0] = PXA_UI_LENGTH_LOGICAL_PX; value[1] = value[2] = value[3] = 0;
        pxa_write_u32(value + 4, 32 * 64);
        set_property(&commands, 2, PXA_UI_PROPERTY_HEIGHT, value, 8);
        transact(62, 62, 0, PXA_UI_PATCH, &commands);
        assert(lv_textarea_get_one_line(input) && lv_obj_get_height(input) == 32);
        char longer[520]; memset(longer, 'x', 508);
        memcpy(longer + 508, "中文🙂", 11);
        lv_textarea_set_text(input, longer);
        assert(g_event_value_size == 511 && !memcmp(g_event_value, longer, 511));
        lv_textarea_set_text(input, "");
        assert(g_event_value_size == 0);
        commands.size = 0;
        bytes_t property = {{0}, 0};
        put_u32(&property, 2); put_u16(&property, PXA_UI_PROPERTY_TEXT_MAX_BYTES);
        command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &property);
        transact(63, 63, 0, PXA_UI_PATCH, &commands);
        lv_textarea_set_text(input, longer);
        assert(g_event_value_size == PXA_UI_EVENT_TEXT_LEGACY_BYTES);
        size_t resident = allocator.current;
        commands.size = 0;
        value[0] = 0;
        set_property(&commands, 2, PXA_UI_PROPERTY_TEXT_SINGLE_LINE, value, 1);
        pxa_write_u32(value, 1024);
        set_property(&commands, 2, PXA_UI_PROPERTY_TEXT_MAX_BYTES, value, 4);
        create_overlay(&commands, 3, 1, 11, 13);
        allocator.fail_during_execute = 1;
        assert(transact_status(64, 64, 0, PXA_UI_PATCH, &commands) == PXA_STATUS_RESOURCE_LIMIT);
        allocator.fail_during_execute = 0;
        lv_textarea_set_text(input, "");
        lv_textarea_set_text(input, longer);
        assert(g_event_value_size == PXA_UI_EVENT_TEXT_LEGACY_BYTES);
        assert(allocator.current == resident && lv_textarea_get_one_line(input));
        commands.size = 0; property.size = 0;
        put_u32(&property, 2); put_u16(&property, PXA_UI_PROPERTY_TEXT_SINGLE_LINE);
        command(&commands, PXA_UI_COMMAND_CLEAR_PROPERTY, &property);
        transact(65, 65, 0, PXA_UI_PATCH, &commands);
        assert(!lv_textarea_get_one_line(input) && lv_obj_get_height(input) == 32);
        lv_textarea_set_cursor_pos(input, 10);
        commands.size = 0;
        set_property(&commands, 2, PXA_UI_PROPERTY_TEXT, longer, strlen(longer));
        transact(66, 66, 0, PXA_UI_PATCH, &commands);
        assert(lv_textarea_get_cursor_pos(input) == 10);
        printf("Dynamic text: UTF-8 boundary, empty edit, clear limit and failed transaction rollback OK\n");
    }

    /* A Canvas can contain native controls. Their pressed feedback must
     * refresh once even when the state event bubbles through the Canvas. */
    {
        bytes_t commands = {{0}, 0};
        uint8_t value[8] = {PXA_UI_LENGTH_LOGICAL_PX};
        const uint8_t composition = PXA_UI_COMPOSITION_ALPHA_OVERLAY;
        create_node(&commands, 1, 0, PXA_UI_NODE_ROOT, 0);
        create_node(&commands, 2, 1, PXA_UI_NODE_CANVAS, 0);
        pxa_write_u32(value + 4, 80 * 64);
        set_property(&commands, 2, PXA_UI_PROPERTY_WIDTH, value, 8);
        set_property(&commands, 2, PXA_UI_PROPERTY_HEIGHT, value, 8);
        set_property(&commands, 2, PXA_UI_PROPERTY_COMPOSITION, &composition, 1);
        create_node(&commands, 3, 2, PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON);
        pxa_write_u32(value + 4, 40 * 64);
        set_property(&commands, 3, PXA_UI_PROPERTY_WIDTH, value, 8);
        set_property(&commands, 3, PXA_UI_PROPERTY_HEIGHT, value, 8);
        transact(67, 67, 0, PXA_UI_REPLACE_SURFACE, &commands);
        root = lv_obj_get_child(lv_screen_active(), 0);
        lv_obj_t *control = lv_obj_get_child(lv_obj_get_child(root, 0), 0);
        static const lv_style_prop_t no_transition_props[] = {0};
        static const lv_style_transition_dsc_t no_transition = {.props = no_transition_props};
        lv_obj_set_style_transition(control, &no_transition, LV_STATE_ANY);
        assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
        const uint64_t revision = alpha_plane.revision;
        const uint64_t hash = alpha_plane_hash(&alpha_plane, &alpha_visible);
#ifdef PXA_TEST_WRAP_SNAPSHOT
        const unsigned snapshots = g_snapshot_draw_calls;
#endif
        lv_obj_add_state(control, LV_STATE_PRESSED);
        assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
        assert(alpha_plane.revision > revision);
        assert(alpha_plane_hash(&alpha_plane, &alpha_visible) != hash);
#ifdef PXA_TEST_WRAP_SNAPSHOT
        assert(g_snapshot_draw_calls == snapshots + 1);
#endif
        lv_obj_remove_state(control, LV_STATE_PRESSED);
        assert(pxa_lvgl_ui_alpha_plane(adapter, &alpha_plane));
        assert(alpha_plane_hash(&alpha_plane, &alpha_visible) == hash);
        puts("Canvas input state: no snapshots/allocations; native child pressed feedback preserved");
    }

    assert(pxa_component_finish_start(g_runtime, g_component,
                                      PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_component_request_stop(g_runtime, g_component,
                                      PXA_STOP_NORMAL) == PXA_STATUS_OK);
    assert(pxa_component_begin_stop(g_runtime, g_component) == PXA_STATUS_OK);
    assert(pxa_component_finish_stop(g_runtime, g_component) == PXA_STATUS_OK);
    assert(lv_obj_get_child_count(lv_screen_active()) == 0);
    {
        unsigned before = g_events;
        lv_timer_handler();
        assert(g_events == before);
    }
    assert(allocator.current == 0);
    assert(g_image_frees==6);
    assert(g_asset_resolves == g_asset_releases);

    pxa_lvgl_ui_deinit(adapter);
    if (!getenv("PXA_CANVAS_MEASURE")) test_snapshot_limits();
    if (!getenv("PXA_CANVAS_MEASURE")) test_viewport_density();
    if (!getenv("PXA_CANVAS_MEASURE")) test_text_input_focus();
    if (!getenv("PXA_CANVAS_MEASURE")) test_enabled_inheritance();
    pxa_ui_service_deinit(service);
    pxa_runtime_deinit(g_runtime);
    free(adapter_workspace);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
