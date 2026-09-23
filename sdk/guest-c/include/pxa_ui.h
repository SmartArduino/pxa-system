#ifndef PXA_UI_H
#define PXA_UI_H

#include "pxa.h"

#define PXA_UI_TX_BEGIN 1u
#define PXA_UI_TX_WRITE 2u
#define PXA_UI_TX_COMMIT 3u
#define PXA_UI_TX_CANCEL 4u
#define PXA_UI_SURFACE_OPEN 5u
#define PXA_UI_SURFACE_CLOSE 6u
#define PXA_UI_CANVAS_BEGIN 7u
#define PXA_UI_CANVAS_WRITE 8u
#define PXA_UI_CANVAS_PRESENT 9u
#define PXA_UI_CANVAS_STREAM_OPEN 10u
#define PXA_UI_EVENT 0x8001u
#define PXA_UI_ENVIRONMENT_CHANGED 0x8002u
#define PXA_UI_RESOURCE_PRESSURE 0x8003u
#define PXA_UI_SURFACE_READY 0x8004u
#define PXA_UI_CANVAS_STREAM_READY 0x8005u
#define PXA_UI_CONFIG_ENVIRONMENT 8u

#define PXA_UI_PRIMARY_SURFACE 1u
#define PXA_UI_COLOR_SCHEME_LIGHT 0u
#define PXA_UI_COLOR_SCHEME_DARK 1u
#define PXA_UI_TRANSACTION_PATCH 1u
#define PXA_UI_TRANSACTION_REPLACE_SUBTREE 2u
#define PXA_UI_TRANSACTION_REPLACE_SURFACE 3u
#define PXA_UI_PRESERVE_ON_FAILURE 1u

#define PXA_UI_COMMAND_CREATE 1u
#define PXA_UI_COMMAND_SET_PROPERTY 2u
#define PXA_UI_COMMAND_CLEAR_PROPERTY 3u
#define PXA_UI_COMMAND_MOVE 4u
#define PXA_UI_COMMAND_REMOVE 5u
#define PXA_UI_COMMAND_OPTIONAL 1u

#define PXA_UI_NODE_ROOT 1u
#define PXA_UI_NODE_BOX 2u
#define PXA_UI_NODE_SCROLL 3u
#define PXA_UI_NODE_TEXT 4u
#define PXA_UI_NODE_IMAGE 5u
#define PXA_UI_NODE_CONTROL 6u
#define PXA_UI_NODE_PROGRESS 7u
#define PXA_UI_NODE_CANVAS 8u
#define PXA_UI_NODE_VIRTUAL_LIST 9u
#define PXA_UI_NODE_MEDIA_SURFACE 10u

#define PXA_UI_CONTROL_NONE 0u
#define PXA_UI_CONTROL_BUTTON 1u
#define PXA_UI_CONTROL_TOGGLE 2u
#define PXA_UI_CONTROL_SLIDER 3u
#define PXA_UI_CONTROL_TEXT_INPUT 4u
#define PXA_UI_CONTROL_SELECTION 5u

#define PXA_UI_PROPERTY_VISIBLE 1u
#define PXA_UI_PROPERTY_ENABLED 2u
#define PXA_UI_PROPERTY_EVENT_MASK 3u
#define PXA_UI_PROPERTY_ACCESSIBILITY_ROLE 4u
#define PXA_UI_PROPERTY_ACCESSIBILITY_LABEL 5u
#define PXA_UI_PROPERTY_SEMANTIC_LABEL PXA_UI_PROPERTY_ACCESSIBILITY_LABEL
#define PXA_UI_PROPERTY_WIDTH 256u
#define PXA_UI_PROPERTY_HEIGHT 257u
#define PXA_UI_PROPERTY_MIN_WIDTH 258u
#define PXA_UI_PROPERTY_MAX_WIDTH 259u
#define PXA_UI_PROPERTY_MIN_HEIGHT 260u
#define PXA_UI_PROPERTY_MAX_HEIGHT 261u
#define PXA_UI_PROPERTY_LAYOUT 262u
#define PXA_UI_PROPERTY_WRAP 263u
#define PXA_UI_PROPERTY_JUSTIFY 264u
#define PXA_UI_PROPERTY_ALIGN 265u
#define PXA_UI_PROPERTY_ALIGN_SELF 266u
#define PXA_UI_PROPERTY_GAP 267u
#define PXA_UI_PROPERTY_PADDING 268u
#define PXA_UI_PROPERTY_GROW 269u
#define PXA_UI_PROPERTY_SHRINK 270u
#define PXA_UI_PROPERTY_POSITION 271u
#define PXA_UI_PROPERTY_X 272u
#define PXA_UI_PROPERTY_Y 273u
#define PXA_UI_PROPERTY_GRID_COLUMNS 274u
#define PXA_UI_PROPERTY_GRID_ROWS 275u
#define PXA_UI_PROPERTY_GRID_CELL 276u
#define PXA_UI_PROPERTY_VARIANT 512u
#define PXA_UI_PROPERTY_FOREGROUND 513u
#define PXA_UI_PROPERTY_BACKGROUND 514u
#define PXA_UI_PROPERTY_BORDER_COLOR 515u
#define PXA_UI_PROPERTY_OPACITY 516u
#define PXA_UI_PROPERTY_RADIUS 517u
#define PXA_UI_PROPERTY_BORDER_WIDTH 518u
#define PXA_UI_PROPERTY_FONT_ROLE 519u
#define PXA_UI_PROPERTY_TEXT_ALIGN 520u
#define PXA_UI_PROPERTY_SHADOW 521u
#define PXA_UI_PROPERTY_COMPOSITION 522u
#define PXA_UI_COMPOSITION_BASE 0u
#define PXA_UI_COMPOSITION_ALPHA_OVERLAY 1u

/* Semantic typography roles selected by the Host theme. */
#define PXA_UI_FONT_ROLE_CAPTION 0u
#define PXA_UI_FONT_ROLE_BODY 1u
#define PXA_UI_FONT_ROLE_TITLE 2u
#define PXA_UI_FONT_ROLE_ICON 3u
#define PXA_UI_FONT_ROLE_LABEL 4u
#define PXA_UI_FONT_ROLE_HEADLINE 5u
#define PXA_UI_FONT_ROLE_DISPLAY 6u

#define PXA_UI_PROPERTY_TEXT 768u
#define PXA_UI_PROPERTY_ICON 769u
#define PXA_UI_PROPERTY_ASSET 770u
#define PXA_UI_PROPERTY_IMAGE_FIT 771u
#define PXA_UI_PROPERTY_VALUE 772u
#define PXA_UI_PROPERTY_MIN_VALUE 773u
#define PXA_UI_PROPERTY_MAX_VALUE 774u
#define PXA_UI_PROPERTY_STEP 775u
#define PXA_UI_PROPERTY_SCROLL_AXIS 776u
#define PXA_UI_PROPERTY_SCROLLBAR 777u
#define PXA_UI_PROPERTY_ITEM_COUNT 778u
#define PXA_UI_PROPERTY_ITEM_EXTENT 779u
#define PXA_UI_PROPERTY_SCROLL_POSITION 780u

/* Grid layout: a node becomes a grid once it has both a column and a row
 * template. Track kinds describe one track; alignments reuse PXA_UI_ALIGN_*. */
#define PXA_UI_GRID_CONTENT 0u
#define PXA_UI_GRID_FRACTION 1u
#define PXA_UI_GRID_FIXED 2u

typedef struct {
    uint8_t kind;    /* PXA_UI_GRID_CONTENT, FRACTION or FIXED */
    uint32_t value;  /* fraction weight, or 1/64 dp size when fixed */
} pxa_ui_grid_track_t;

#define PXA_UI_IMAGE_FIT_CONTAIN 0u
#define PXA_UI_IMAGE_FIT_STRETCH 1u
#define PXA_UI_IMAGE_FIT_COVER 2u

#define PXA_UI_LENGTH_AUTO 0u
#define PXA_UI_LENGTH_LOGICAL_PX 1u
#define PXA_UI_LENGTH_PX PXA_UI_LENGTH_LOGICAL_PX
#define PXA_UI_LENGTH_PERCENT_Q16 2u
#define PXA_UI_LENGTH_PERCENT PXA_UI_LENGTH_PERCENT_Q16
#define PXA_UI_LENGTH_CONTENT 3u
#define PXA_UI_LENGTH_FILL 4u
#define PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16 5u
#define PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16 6u

#define PXA_UI_LAYOUT_ROW 1u
#define PXA_UI_LAYOUT_COLUMN 2u
#define PXA_UI_LAYOUT_STACK 3u
#define PXA_UI_LAYOUT_GRID 4u

#define PXA_UI_ALIGN_START 0u
#define PXA_UI_ALIGN_CENTER 1u
#define PXA_UI_ALIGN_END 2u
#define PXA_UI_ALIGN_STRETCH 3u
#define PXA_UI_ALIGN_SPACE_BETWEEN 4u
#define PXA_UI_ALIGN_SPACE_AROUND 5u

#define PXA_UI_THEME_BACKGROUND 0u
#define PXA_UI_THEME_SURFACE 1u
#define PXA_UI_THEME_PRIMARY 2u
#define PXA_UI_THEME_ON_PRIMARY 3u
#define PXA_UI_THEME_TEXT 4u
#define PXA_UI_THEME_MUTED 5u
#define PXA_UI_THEME_BORDER 6u
#define PXA_UI_THEME_SUCCESS 7u
#define PXA_UI_THEME_WARNING 8u
#define PXA_UI_THEME_DANGER 9u

#define PXA_UI_EVENT_ACTION_KIND 1u
#define PXA_UI_EVENT_VALUE_CHANGED_KIND 2u
#define PXA_UI_EVENT_SCROLL_KIND 3u
#define PXA_UI_EVENT_FOCUS_KIND 4u
#define PXA_UI_EVENT_KEY_KIND 5u
#define PXA_UI_EVENT_TEXT_KIND 6u
#define PXA_UI_EVENT_POINTER_KIND 7u
#define PXA_UI_EVENT_ACCESSIBILITY_KIND 8u
#define PXA_UI_EVENT_VISIBLE_RANGE_KIND 9u
#define PXA_UI_EVENT_CONTROLLER_STATE_KIND 10u
#define PXA_UI_EVENT_FLAG_RELIABLE UINT16_C(1)
#define PXA_UI_EVENT_FLAG_COALESCIBLE UINT16_C(2)
#define PXA_UI_EVENT_CLICK_KIND PXA_UI_EVENT_ACTION_KIND
#define PXA_UI_EVENT_LONG_PRESS_KIND PXA_UI_EVENT_ACTION_KIND
#define PXA_UI_EVENT_MASK_CLICK (UINT64_C(1) << 0)
#define PXA_UI_EVENT_MASK_VALUE_CHANGED (UINT64_C(1) << 1)
#define PXA_UI_EVENT_MASK_LONG_PRESS PXA_UI_EVENT_MASK_CLICK
#define PXA_UI_EVENT_MASK_SCROLL (UINT64_C(1) << 2)
#define PXA_UI_EVENT_MASK_KEY (UINT64_C(1) << 4)
#define PXA_UI_EVENT_MASK_TEXT (UINT64_C(1) << 5)
#define PXA_UI_EVENT_TEXT_MAX_BYTES 64u
#define PXA_UI_EVENT_MASK_POINTER (UINT64_C(1) << 6)
#define PXA_UI_EVENT_MASK_VISIBLE_RANGE (UINT64_C(1) << 8)
#define PXA_UI_EVENT_MASK_CONTROLLER_STATE (UINT64_C(1) << 9)

#define PXA_UI_KEY_VOLUME_UP 1u
#define PXA_UI_KEY_VOLUME_DOWN 2u
#define PXA_UI_KEY_VOLUME_UP_RELEASED 3u
#define PXA_UI_KEY_VOLUME_DOWN_RELEASED 4u

#define PXA_UI_FEATURE_GRID (UINT64_C(1) << 0)
#define PXA_UI_FEATURE_CANVAS (UINT64_C(1) << 1)
#define PXA_UI_FEATURE_RGB565_BITMAP (UINT64_C(1) << 8)
#define PXA_UI_FEATURE_CONTROLLER_INPUT (UINT64_C(1) << 9)
#define PXA_UI_FEATURE_CANVAS_STREAM_IO (UINT64_C(1) << 10)
#define PXA_UI_FEATURE_VIRTUAL_LIST (UINT64_C(1) << 2)
#define PXA_UI_FEATURE_MEDIA_SURFACE (UINT64_C(1) << 3)
#define PXA_UI_FEATURE_ANIMATION (UINT64_C(1) << 4)
#define PXA_UI_FEATURE_ACCESSIBILITY (UINT64_C(1) << 5)
#define PXA_UI_FEATURE_MULTIPLE_SURFACES (UINT64_C(1) << 6)

#define PXA_UI_PRESSURE_NORMAL 0u
#define PXA_UI_PRESSURE_CONSTRAINED 1u
#define PXA_UI_PRESSURE_CRITICAL 2u

#define PXA_UI_SURFACE_APPLICATION 1u
#define PXA_UI_SURFACE_DIALOG 2u
#define PXA_UI_SURFACE_OVERLAY 3u
#define PXA_UI_SURFACE_EXTERNAL 4u

typedef struct {
    uint32_t transaction;
    uint32_t generation;
    uint32_t surface;
    uint8_t* scratch;
    size_t scratch_capacity;
    uint8_t active;
    uint8_t failed;
} pxa_ui_transaction_t;

typedef struct {
    uint32_t surface;
    uint32_t node;
    uint32_t generation;
    uint16_t kind;
    uint16_t flags;
    uint64_t timestamp_us;
    const uint8_t* data;
    uint32_t data_size;
    int32_t value;
} pxa_ui_event_data_t;

typedef struct {
    uint32_t surface;
    uint32_t node;
    uint32_t generation;
    uint8_t pointer_id;
    uint8_t phase;
    uint16_t buttons;
    int32_t x;
    int32_t y;
    uint64_t timestamp_us;
} pxa_ui_pointer_data_t;

#define PXA_CONTROLLER_UP (UINT32_C(1) << 0)
#define PXA_CONTROLLER_DOWN (UINT32_C(1) << 1)
#define PXA_CONTROLLER_LEFT (UINT32_C(1) << 2)
#define PXA_CONTROLLER_RIGHT (UINT32_C(1) << 3)
#define PXA_CONTROLLER_A (UINT32_C(1) << 4)
#define PXA_CONTROLLER_B (UINT32_C(1) << 5)
#define PXA_CONTROLLER_START (UINT32_C(1) << 6)
#define PXA_CONTROLLER_SELECT (UINT32_C(1) << 7)
#define PXA_CONTROLLER_BUTTON_MASK UINT32_C(0xff)

typedef struct {
    uint32_t surface;
    uint32_t node;
    uint32_t generation;
    uint32_t buttons;
    uint64_t timestamp_us;
    uint8_t controller;
    uint8_t connected;
} pxa_ui_controller_data_t;

typedef struct {
    uint32_t surface;
    uint32_t width;
    uint32_t height;
    uint32_t density_q16;
    uint32_t font_scale_q16;
    /* Safe-area insets in logical pixels: top, right, bottom, left. */
    uint32_t safe_insets[4];
    uint64_t input_capabilities;
    uint64_t features;
    uint32_t recommended_write_bytes;
    uint8_t color_scheme;
    uint8_t direction;
} pxa_ui_environment_t;

static inline void pxa_ui_write_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static inline void pxa_ui_write_u32(uint8_t* output, uint32_t value) {
    pxa_ui_write_u16(output, (uint16_t)value);
    pxa_ui_write_u16(output + 2, (uint16_t)(value >> 16));
}

static inline void pxa_ui_write_u64(uint8_t* output, uint64_t value) {
    pxa_ui_write_u32(output, (uint32_t)value);
    pxa_ui_write_u32(output + 4, (uint32_t)(value >> 32));
}

static inline int pxa_ui_send_packet(uint8_t* scratch, size_t capacity,
                                     uint16_t opcode, const uint8_t* payload,
                                     size_t payload_size) {
    pxa_writer_t packet;
    if (scratch == NULL || capacity < 12 || payload_size > capacity - 12)
        return 0;
    pxa_writer_init(&packet, scratch, capacity);
    return pxa_message(&packet, PXA_SERVICE_UI, opcode, 0, payload,
                       payload_size) &&
           pxa_control(packet.data, (uint32_t)packet.length) == PXA_STATUS_OK;
}

static inline int pxa_ui_write_stream(pxa_ui_transaction_t* transaction,
                                      const uint8_t* data, size_t size) {
    size_t offset = 0;
    if (transaction == NULL || !transaction->active || transaction->failed ||
        (data == NULL && size != 0) || transaction->scratch_capacity < 17u) {
        if (transaction != NULL) transaction->failed = 1;
        return 0;
    }
    while (offset < size) {
        pxa_writer_t packet;
        size_t chunk = size - offset;
        size_t maximum = transaction->scratch_capacity - 16u;
        if (chunk > maximum) chunk = maximum;
        pxa_writer_init(&packet, transaction->scratch,
                        transaction->scratch_capacity);
        if (!pxa_put_u16(&packet, PXA_SERVICE_UI) ||
            !pxa_put_u16(&packet, PXA_UI_TX_WRITE) ||
            !pxa_put_u32(&packet, 0) ||
            !pxa_put_u32(&packet, (uint32_t)(chunk + 4u)) ||
            !pxa_put_u32(&packet, transaction->transaction) ||
            !pxa_put_bytes(&packet, data + offset, chunk) ||
            pxa_control(packet.data, (uint32_t)packet.length) != PXA_STATUS_OK) {
            transaction->failed = 1;
            return 0;
        }
        offset += chunk;
    }
    return 1;
}

static inline int pxa_ui_transaction_begin_target(
    pxa_ui_transaction_t* transaction, uint32_t transaction_id,
    uint32_t generation, uint32_t surface, uint32_t target, uint8_t kind,
    uint8_t* scratch, size_t scratch_capacity) {
    uint8_t payload[20] = {0};
    pxa_writer_t value;
    if (transaction == NULL || transaction_id == 0 || generation == 0 ||
        surface == 0 || kind < PXA_UI_TRANSACTION_PATCH ||
        kind > PXA_UI_TRANSACTION_REPLACE_SURFACE || scratch == NULL ||
        scratch_capacity < 32)
        return 0;
    pxa_writer_init(&value, payload, sizeof(payload));
    if (!pxa_put_u32(&value, surface) ||
        !pxa_put_u32(&value, transaction_id) ||
        !pxa_put_u32(&value, generation) || !pxa_put_u32(&value, target) ||
        !pxa_put_u8(&value, kind) ||
        !pxa_put_u8(&value, PXA_UI_PRESERVE_ON_FAILURE) ||
        !pxa_put_u16(&value, 0) ||
        !pxa_ui_send_packet(scratch, scratch_capacity, PXA_UI_TX_BEGIN,
                            payload, sizeof(payload)))
        return 0;
    transaction->transaction = transaction_id;
    transaction->generation = generation;
    transaction->surface = surface;
    transaction->scratch = scratch;
    transaction->scratch_capacity = scratch_capacity;
    transaction->active = 1;
    transaction->failed = 0;
    return 1;
}

static inline int pxa_ui_transaction_begin(pxa_ui_transaction_t* transaction,
                                           uint32_t generation, uint8_t kind,
                                           uint8_t* scratch,
                                           size_t scratch_capacity) {
    return pxa_ui_transaction_begin_target(
        transaction, generation, generation, PXA_UI_PRIMARY_SURFACE, 0,
        kind, scratch, scratch_capacity);
}

static inline int pxa_ui_emit(pxa_ui_transaction_t* transaction,
                              uint8_t command, uint8_t flags,
                              const uint8_t* payload, size_t payload_size) {
    uint8_t header[4];
    pxa_writer_t value;
    if (transaction == NULL || !transaction->active || transaction->failed ||
        command == 0 || payload_size > UINT16_MAX ||
        (payload == NULL && payload_size != 0)) {
        if (transaction != NULL) transaction->failed = 1;
        return 0;
    }
    pxa_writer_init(&value, header, sizeof(header));
    if (!pxa_put_u8(&value, command) || !pxa_put_u8(&value, flags) ||
        !pxa_put_u16(&value, (uint16_t)payload_size) ||
        !pxa_ui_write_stream(transaction, header, sizeof(header)) ||
        (payload_size != 0 &&
         !pxa_ui_write_stream(transaction, payload, payload_size))) {
        transaction->failed = 1;
        return 0;
    }
    return 1;
}

static inline int pxa_ui_create_typed(pxa_ui_transaction_t* transaction,
                                      uint32_t node, uint32_t parent,
                                      uint32_t before, uint8_t type,
                                      uint8_t subtype) {
    uint8_t payload[16];
    pxa_writer_t value;
    if (node == 0 || type < PXA_UI_NODE_ROOT ||
        type > PXA_UI_NODE_MEDIA_SURFACE ||
        (type != PXA_UI_NODE_CONTROL && subtype != 0))
        return 0;
    pxa_writer_init(&value, payload, sizeof(payload));
    return pxa_put_u32(&value, node) && pxa_put_u32(&value, parent) &&
           pxa_put_u32(&value, before) && pxa_put_u8(&value, type) &&
           pxa_put_u8(&value, subtype) && pxa_put_u16(&value, 0) &&
           pxa_ui_emit(transaction, PXA_UI_COMMAND_CREATE, 0, payload,
                       sizeof(payload));
}

static inline int pxa_ui_create(pxa_ui_transaction_t* transaction,
                                uint32_t node, uint32_t parent,
                                uint32_t before, uint8_t type) {
    return pxa_ui_create_typed(transaction, node, parent, before, type,
                               PXA_UI_CONTROL_NONE);
}

static inline int pxa_ui_move(pxa_ui_transaction_t* transaction,
                              uint32_t node, uint32_t parent,
                              uint32_t before) {
    uint8_t payload[12];
    pxa_writer_t value;
    pxa_writer_init(&value, payload, sizeof(payload));
    return node != 0 && parent != 0 && pxa_put_u32(&value, node) &&
           pxa_put_u32(&value, parent) && pxa_put_u32(&value, before) &&
           pxa_ui_emit(transaction, PXA_UI_COMMAND_MOVE, 0, payload,
                       sizeof(payload));
}

static inline int pxa_ui_remove(pxa_ui_transaction_t* transaction,
                                uint32_t node) {
    uint8_t payload[4];
    pxa_ui_write_u32(payload, node);
    return node != 0 && pxa_ui_emit(transaction, PXA_UI_COMMAND_REMOVE, 0,
                                    payload, sizeof(payload));
}

static inline int pxa_ui_clear_property(pxa_ui_transaction_t* transaction,
                                        uint32_t node, uint16_t property) {
    uint8_t payload[6];
    pxa_writer_t value;
    pxa_writer_init(&value, payload, sizeof(payload));
    return node != 0 && property != 0 && pxa_put_u32(&value, node) &&
           pxa_put_u16(&value, property) &&
           pxa_ui_emit(transaction, PXA_UI_COMMAND_CLEAR_PROPERTY, 0,
                       payload, sizeof(payload));
}

static inline int pxa_ui_set_property(pxa_ui_transaction_t* transaction,
                                      uint32_t node, uint16_t property,
                                      const void* data, size_t size) {
    uint8_t header[4];
    uint8_t prefix[6];
    pxa_writer_t writer;
    size_t payload_size;
    if (transaction == NULL || node == 0 || property == 0 ||
        (data == NULL && size != 0) || size > UINT16_MAX - sizeof(prefix))
        return 0;
    payload_size = sizeof(prefix) + size;
    pxa_writer_init(&writer, header, sizeof(header));
    if (!pxa_put_u8(&writer, PXA_UI_COMMAND_SET_PROPERTY) ||
        !pxa_put_u8(&writer, 0) ||
        !pxa_put_u16(&writer, (uint16_t)payload_size))
        return 0;
    pxa_writer_init(&writer, prefix, sizeof(prefix));
    if (!pxa_put_u32(&writer, node) || !pxa_put_u16(&writer, property) ||
        !pxa_ui_write_stream(transaction, header, sizeof(header)) ||
        !pxa_ui_write_stream(transaction, prefix, sizeof(prefix)) ||
        (size != 0 && !pxa_ui_write_stream(
                          transaction, (const uint8_t*)data, size))) {
        transaction->failed = 1;
        return 0;
    }
    return 1;
}

static inline int pxa_ui_set_u8(pxa_ui_transaction_t* transaction,
                                uint32_t node, uint16_t property,
                                uint8_t value) {
    return pxa_ui_set_property(transaction, node, property, &value, 1);
}

static inline int pxa_ui_set_i32(pxa_ui_transaction_t* transaction,
                                 uint32_t node, uint16_t property,
                                 int32_t value) {
    uint8_t encoded[4];
    pxa_ui_write_u32(encoded, (uint32_t)value);
    return pxa_ui_set_property(transaction, node, property, encoded, 4);
}

static inline int pxa_ui_set_u16(pxa_ui_transaction_t* transaction,
                                 uint32_t node, uint16_t property,
                                 uint16_t value) {
    uint8_t encoded[2];
    pxa_ui_write_u16(encoded, value);
    return pxa_ui_set_property(transaction, node, property, encoded, 2);
}

static inline int pxa_ui_set_u32(pxa_ui_transaction_t* transaction,
                                 uint32_t node, uint16_t property,
                                 uint32_t value) {
    uint8_t encoded[4];
    pxa_ui_write_u32(encoded, value);
    return pxa_ui_set_property(transaction, node, property, encoded, 4);
}

#define PXA_UI_GRID_MAX_TRACKS 16u

/* One grid track: kind is content, fraction or fixed; value is the fraction
 * weight or the logical pixel size. */
static inline int pxa_ui_set_grid_tracks(pxa_ui_transaction_t* transaction,
                                         uint32_t node, uint16_t property,
                                         const pxa_ui_grid_track_t* tracks,
                                         uint16_t count) {
    uint8_t encoded[8u * PXA_UI_GRID_MAX_TRACKS];
    size_t offset = 0;
    uint16_t index;
    if (transaction == NULL || node == 0 ||
        (property != PXA_UI_PROPERTY_GRID_COLUMNS &&
         property != PXA_UI_PROPERTY_GRID_ROWS) ||
        tracks == NULL || count == 0 || count > PXA_UI_GRID_MAX_TRACKS)
        return 0;
    for (index = 0; index < count; ++index) {
        if (tracks[index].kind > PXA_UI_GRID_FIXED) return 0;
        encoded[offset++] = tracks[index].kind;
        encoded[offset++] = 0;
        encoded[offset++] = 0;
        encoded[offset++] = 0;
        pxa_ui_write_u32(encoded + offset, tracks[index].value);
        offset += 4u;
    }
    return pxa_ui_set_property(transaction, node, property, encoded, offset);
}

static inline int pxa_ui_set_grid_columns(pxa_ui_transaction_t* transaction,
                                          uint32_t node,
                                          const pxa_ui_grid_track_t* tracks,
                                          uint16_t count) {
    return pxa_ui_set_grid_tracks(transaction, node,
                                  PXA_UI_PROPERTY_GRID_COLUMNS, tracks, count);
}

static inline int pxa_ui_set_grid_rows(pxa_ui_transaction_t* transaction,
                                       uint32_t node,
                                       const pxa_ui_grid_track_t* tracks,
                                       uint16_t count) {
    return pxa_ui_set_grid_tracks(transaction, node, PXA_UI_PROPERTY_GRID_ROWS,
                                  tracks, count);
}

/* Places one grid child. The node's align property aligns the cell. */
static inline int pxa_ui_set_grid_cell(pxa_ui_transaction_t* transaction,
                                       uint32_t node, uint16_t column,
                                       uint16_t row, uint16_t column_span,
                                       uint16_t row_span) {
    uint8_t encoded[8];
    if (transaction == NULL || node == 0 || column_span == 0 || row_span == 0)
        return 0;
    pxa_ui_write_u16(encoded, column);
    pxa_ui_write_u16(encoded + 2, row);
    pxa_ui_write_u16(encoded + 4, column_span);
    pxa_ui_write_u16(encoded + 6, row_span);
    return pxa_ui_set_property(transaction, node, PXA_UI_PROPERTY_GRID_CELL,
                               encoded, sizeof(encoded));
}

static inline int pxa_ui_set_u64(pxa_ui_transaction_t* transaction,
                                 uint32_t node, uint16_t property,
                                 uint64_t value) {
    uint8_t encoded[8];
    pxa_ui_write_u64(encoded, value);
    return pxa_ui_set_property(transaction, node, property, encoded, 8);
}

static inline int pxa_ui_set_dp(pxa_ui_transaction_t* transaction,
                                uint32_t node, uint16_t property,
                                int32_t value) {
    if ((property != PXA_UI_PROPERTY_GAP &&
         property != PXA_UI_PROPERTY_RADIUS &&
         property != PXA_UI_PROPERTY_BORDER_WIDTH &&
         property != PXA_UI_PROPERTY_ITEM_EXTENT &&
         property != PXA_UI_PROPERTY_SCROLL_POSITION) ||
        value < 0 || value > INT32_MAX / 64)
        return 0;
    return pxa_ui_set_i32(transaction, node, property, value * 64);
}

static inline int pxa_ui_set_event_mask(pxa_ui_transaction_t* transaction,
                                        uint32_t node, uint64_t mask) {
    return pxa_ui_set_u64(transaction, node, PXA_UI_PROPERTY_EVENT_MASK,
                          mask);
}

static inline int pxa_ui_set_icon(pxa_ui_transaction_t* transaction,
                                  uint32_t node, uint32_t icon) {
    return pxa_ui_set_u32(transaction, node, PXA_UI_PROPERTY_ICON, icon);
}

static inline int pxa_ui_set_font_role(pxa_ui_transaction_t* transaction,
                                       uint32_t node, uint16_t role) {
    return pxa_ui_set_u16(transaction, node, PXA_UI_PROPERTY_FONT_ROLE, role);
}

static inline int pxa_ui_set_accessibility_role(
    pxa_ui_transaction_t* transaction, uint32_t node, uint16_t role) {
    return pxa_ui_set_u16(transaction, node,
                          PXA_UI_PROPERTY_ACCESSIBILITY_ROLE, role);
}

static inline int pxa_ui_set_length(pxa_ui_transaction_t* transaction,
                                    uint32_t node, uint16_t property,
                                    uint8_t unit, int32_t value) {
    uint8_t encoded[8] = {0};
    if (unit > PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16) return 0;
    if (unit == PXA_UI_LENGTH_LOGICAL_PX) {
        if (value > INT32_MAX / 64 || value < INT32_MIN / 64) return 0;
        value *= 64;
    } else if (unit == PXA_UI_LENGTH_PERCENT_Q16 &&
               value >= -100 && value <= 100) {
        value *= 65536;
    }
    encoded[0] = unit;
    pxa_ui_write_u32(encoded + 4, (uint32_t)value);
    return pxa_ui_set_property(transaction, node, property,
                               encoded, sizeof(encoded));
}

static inline int pxa_ui_set_theme_color(pxa_ui_transaction_t* transaction,
                                         uint32_t node, uint16_t property,
                                         uint8_t token) {
    uint8_t encoded[8] = {0};
    if (token >= 32) return 0;
    encoded[1] = token;
    return pxa_ui_set_property(transaction, node, property,
                               encoded, sizeof(encoded));
}

static inline int pxa_ui_set_rgba(pxa_ui_transaction_t* transaction,
                                  uint32_t node, uint16_t property,
                                  uint32_t rgba) {
    uint8_t encoded[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    pxa_ui_write_u32(encoded + 4, rgba);
    return pxa_ui_set_property(transaction, node, property,
                               encoded, sizeof(encoded));
}

static inline int pxa_ui_set_padding(pxa_ui_transaction_t* transaction,
                                     uint32_t node, uint16_t left,
                                     uint16_t top, uint16_t right,
                                     uint16_t bottom) {
    uint8_t encoded[16];
    pxa_ui_write_u32(encoded, (uint32_t)left * 64u);
    pxa_ui_write_u32(encoded + 4, (uint32_t)top * 64u);
    pxa_ui_write_u32(encoded + 8, (uint32_t)right * 64u);
    pxa_ui_write_u32(encoded + 12, (uint32_t)bottom * 64u);
    return pxa_ui_set_property(transaction, node, PXA_UI_PROPERTY_PADDING,
                               encoded, sizeof(encoded));
}

static inline int pxa_ui_set_text(pxa_ui_transaction_t* transaction,
                                  uint32_t node, const char* text,
                                  size_t size) {
    return pxa_ui_set_property(transaction, node, PXA_UI_PROPERTY_TEXT,
                               text, size);
}

static inline int pxa_ui_transaction_commit(
    pxa_ui_transaction_t* transaction) {
    uint8_t payload[4];
    int result;
    if (transaction == NULL || !transaction->active || transaction->failed)
        return 0;
    pxa_ui_write_u32(payload, transaction->transaction);
    result = pxa_ui_send_packet(transaction->scratch,
                                transaction->scratch_capacity,
                                PXA_UI_TX_COMMIT, payload, sizeof(payload));
    /* The Host consumes and resets a transaction for every COMMIT result. */
    transaction->active = 0;
    return result;
}

static inline int pxa_ui_transaction_cancel(
    pxa_ui_transaction_t* transaction) {
    uint8_t payload[4];
    int result;
    if (transaction == NULL || !transaction->active) return 0;
    pxa_ui_write_u32(payload, transaction->transaction);
    result = pxa_ui_send_packet(transaction->scratch,
                                transaction->scratch_capacity,
                                PXA_UI_TX_CANCEL, payload, sizeof(payload));
    transaction->active = 0;
    return result;
}

static inline int pxa_ui_parse_event(const pxa_event_t* event,
                                     pxa_ui_event_data_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_UI ||
        event->opcode != PXA_UI_EVENT || event->payload == NULL ||
        event->payload_length < 24)
        return 0;
    output->surface = pxa_read_u32(event->payload);
    output->node = pxa_read_u32(event->payload + 4);
    output->generation = pxa_read_u32(event->payload + 8);
    output->kind = pxa_read_u16(event->payload + 12);
    output->flags = pxa_read_u16(event->payload + 14);
    output->timestamp_us = pxa_read_u64(event->payload + 16);
    output->data = event->payload + 24;
    output->data_size = event->payload_length - 24u;
    output->value = output->data_size >= 4
                        ? (int32_t)pxa_read_u32(output->data) : 0;
    return 1;
}

/* Text events carry the current UTF-8 text of a text input without a
 * terminator and never longer than PXA_UI_EVENT_TEXT_MAX_BYTES. The copy is
 * always terminated; the text is truncated when `capacity` is too small. */
static inline int pxa_ui_event_text(const pxa_ui_event_data_t* event,
                                    char* output, size_t capacity) {
    size_t copy;
    if (event == NULL || output == NULL || capacity == 0 ||
        event->kind != PXA_UI_EVENT_TEXT_KIND || event->data == NULL ||
        event->data_size == 0 ||
        event->data_size > PXA_UI_EVENT_TEXT_MAX_BYTES)
        return 0;
    copy = event->data_size < capacity - 1u ? event->data_size : capacity - 1u;
    for (size_t index = 0; index < copy; ++index)
        output[index] = (char)event->data[index];
    output[copy] = '\0';
    return 1;
}

static inline int pxa_ui_parse_text(const pxa_event_t* event, char* output,
                                    size_t capacity) {
    pxa_ui_event_data_t parsed;
    return pxa_ui_parse_event(event, &parsed) &&
           pxa_ui_event_text(&parsed, output, capacity);
}

static inline int pxa_ui_parse_pointer(const pxa_event_t* event,
                                       pxa_ui_pointer_data_t* output) {
    pxa_ui_event_data_t parsed;
    if (output == NULL || !pxa_ui_parse_event(event, &parsed) ||
        parsed.kind != PXA_UI_EVENT_POINTER_KIND || parsed.data_size != 12)
        return 0;
    output->surface = parsed.surface;
    output->node = parsed.node;
    output->generation = parsed.generation;
    output->pointer_id = parsed.data[0];
    output->phase = parsed.data[1];
    output->buttons = pxa_read_u16(parsed.data + 2);
    output->x = (int32_t)pxa_read_u32(parsed.data + 4);
    output->y = (int32_t)pxa_read_u32(parsed.data + 8);
    output->timestamp_us = parsed.timestamp_us;
    return 1;
}

static inline int pxa_ui_parse_controller(const pxa_event_t* event,
                                          pxa_ui_controller_data_t* output) {
    pxa_ui_event_data_t parsed;
    if (output == NULL || !pxa_ui_parse_event(event, &parsed) ||
        parsed.kind != PXA_UI_EVENT_CONTROLLER_STATE_KIND ||
        parsed.data_size != 8 || parsed.data[1] > 1 ||
        parsed.data[2] != 0 || parsed.data[3] != 0)
        return 0;
    output->surface = parsed.surface;
    output->node = parsed.node;
    output->generation = parsed.generation;
    output->controller = parsed.data[0];
    output->connected = parsed.data[1];
    output->buttons = pxa_read_u32(parsed.data + 4);
    output->timestamp_us = parsed.timestamp_us;
    return output->connected || output->buttons == 0;
}

static inline int pxa_ui_event_is_current(const pxa_ui_event_data_t* event,
                                          uint32_t surface,
                                          uint32_t generation) {
    return event != NULL && event->surface == surface &&
           event->generation == generation;
}

static inline int pxa_ui_parse_environment_records(
    const uint8_t* data, size_t size, pxa_ui_environment_t* output) {
    pxa_ui_environment_t value = {0};
    uint16_t seen = 0;
    size_t offset = 0;
    if (output == NULL || (data == NULL && size != 0)) return 0;
    while (offset < size) {
        const uint8_t* record;
        uint16_t tag;
        uint16_t length;
        if (size - offset < 4) return 0;
        tag = pxa_read_u16(data + offset);
        length = pxa_read_u16(data + offset + 2);
        offset += 4;
        if (length > size - offset) return 0;
        record = data + offset;
        switch (tag) {
            case 1:
                if (length != 4) return 0;
                value.surface = pxa_read_u32(record);
                break;
            case 2:
                if (length != 4) return 0;
                value.width = pxa_read_u32(record);
                break;
            case 3:
                if (length != 4) return 0;
                value.height = pxa_read_u32(record);
                break;
            case 4:
                if (length != 4) return 0;
                value.density_q16 = pxa_read_u32(record);
                break;
            case 5:
                if (length != 4) return 0;
                value.font_scale_q16 = pxa_read_u32(record);
                break;
            case 6:
                if (length != 16) return 0;
                value.safe_insets[0] = pxa_read_u32(record);
                value.safe_insets[1] = pxa_read_u32(record + 4);
                value.safe_insets[2] = pxa_read_u32(record + 8);
                value.safe_insets[3] = pxa_read_u32(record + 12);
                break;
            case 7:
                if (length != 1) return 0;
                value.color_scheme = record[0];
                break;
            case 8:
                if (length != 1) return 0;
                value.direction = record[0];
                break;
            case 9:
                if (length != 8) return 0;
                value.input_capabilities = pxa_read_u64(record);
                break;
            case 10:
                if (length != 8) return 0;
                value.features = pxa_read_u64(record);
                break;
            case 11:
                if (length != 4) return 0;
                value.recommended_write_bytes = pxa_read_u32(record);
                break;
            default:
                offset += length;
                continue;
        }
        if (tag <= 11) {
            uint16_t bit = (uint16_t)(1u << (tag - 1u));
            if ((seen & bit) != 0) return 0;
            seen = (uint16_t)(seen | bit);
        }
        offset += length;
    }
    if (seen != UINT16_C(0x07ff) || value.surface == 0 ||
        value.density_q16 == 0 || value.font_scale_q16 == 0)
        return 0;
    *output = value;
    return 1;
}

static inline int pxa_ui_parse_start_environment(
    const uint8_t* config, size_t config_size, pxa_ui_environment_t* output) {
    size_t offset = 0;
    int found = 0;
    if (output == NULL || (config == NULL && config_size != 0)) return 0;
    while (offset < config_size) {
        uint16_t tag;
        uint16_t length;
        if (config_size - offset < 4) return 0;
        tag = pxa_read_u16(config + offset);
        length = pxa_read_u16(config + offset + 2);
        offset += 4;
        if (length > config_size - offset) return 0;
        if (tag == PXA_UI_CONFIG_ENVIRONMENT) {
            if (found || !pxa_ui_parse_environment_records(
                             config + offset, length, output))
                return 0;
            found = 1;
        }
        offset += length;
    }
    return found;
}

static inline int pxa_ui_parse_environment_event(
    const pxa_event_t* event, pxa_ui_environment_t* output) {
    return event != NULL && event->service == PXA_SERVICE_UI &&
           event->opcode == PXA_UI_ENVIRONMENT_CHANGED &&
           pxa_ui_parse_environment_records(event->payload,
                                            event->payload_length, output);
}

static inline int pxa_ui_parse_resource_pressure(const pxa_event_t* event,
                                                 uint8_t* pressure) {
    if (event == NULL || pressure == NULL ||
        event->service != PXA_SERVICE_UI ||
        event->opcode != PXA_UI_RESOURCE_PRESSURE ||
        event->payload == NULL || event->payload_length != 4 ||
        event->payload[0] > PXA_UI_PRESSURE_CRITICAL ||
        event->payload[1] != 0 || event->payload[2] != 0 ||
        event->payload[3] != 0)
        return 0;
    *pressure = event->payload[0];
    return 1;
}

static inline int pxa_ui_surface_open(uint32_t request, uint8_t role) {
    uint8_t payload[8] = {0};
    if (request == 0 || role < PXA_UI_SURFACE_APPLICATION ||
        role > PXA_UI_SURFACE_EXTERNAL)
        return 0;
    pxa_ui_write_u32(payload, request);
    payload[4] = role;
    return pxa_send(PXA_SERVICE_UI, PXA_UI_SURFACE_OPEN, 0, payload,
                    sizeof(payload));
}

static inline int pxa_ui_surface_close(uint32_t surface) {
    uint8_t payload[4];
    if (surface == 0 || surface == PXA_UI_PRIMARY_SURFACE) return 0;
    pxa_ui_write_u32(payload, surface);
    return pxa_send(PXA_SERVICE_UI, PXA_UI_SURFACE_CLOSE, 0, payload,
                    sizeof(payload));
}

static inline int pxa_ui_parse_surface_ready(
    const pxa_event_t* event, uint32_t* request, int32_t* status,
    pxa_ui_environment_t* environment) {
    uint32_t surface;
    if (event == NULL || request == NULL || status == NULL ||
        environment == NULL || event->service != PXA_SERVICE_UI ||
        event->opcode != PXA_UI_SURFACE_READY || event->payload == NULL ||
        event->payload_length < 12)
        return 0;
    *request = pxa_read_u32(event->payload);
    surface = pxa_read_u32(event->payload + 4);
    *status = (int32_t)pxa_read_u32(event->payload + 8);
    if (*status != PXA_STATUS_OK)
        return surface == 0 && event->payload_length == 12;
    if (!pxa_ui_parse_environment_records(event->payload + 12,
                                          event->payload_length - 12,
                                          environment) ||
        environment->surface != surface)
        return 0;
    return 1;
}

#endif
