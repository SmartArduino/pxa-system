#ifndef PXA_UI_H
#define PXA_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_UI_SERVICE_ID UINT16_C(3)
#define PXA_UI_SERVICE_MAJOR UINT16_C(0)
#define PXA_UI_SERVICE_MINOR UINT16_C(3)
#define PXA_UI_SERVICE_PATCH UINT16_C(0)

#define PXA_UI_TX_BEGIN UINT16_C(1)
#define PXA_UI_TX_WRITE UINT16_C(2)
#define PXA_UI_TX_COMMIT UINT16_C(3)
#define PXA_UI_TX_CANCEL UINT16_C(4)
#define PXA_UI_SURFACE_OPEN UINT16_C(5)
#define PXA_UI_SURFACE_CLOSE UINT16_C(6)
#define PXA_UI_CANVAS_BEGIN UINT16_C(7)
#define PXA_UI_CANVAS_WRITE UINT16_C(8)
#define PXA_UI_CANVAS_PRESENT UINT16_C(9)
#define PXA_UI_CANVAS_STREAM_OPEN UINT16_C(10)
#define PXA_UI_EVENT UINT16_C(0x8001)
#define PXA_UI_ENVIRONMENT_CHANGED UINT16_C(0x8002)
#define PXA_UI_RESOURCE_PRESSURE UINT16_C(0x8003)
#define PXA_UI_SURFACE_READY UINT16_C(0x8004)
#define PXA_UI_CANVAS_STREAM_READY UINT16_C(0x8005)

/* Core startup-configuration record containing UI environment records. */
#define PXA_UI_CONFIG_ENVIRONMENT UINT16_C(8)

#define PXA_UI_PRIMARY_SURFACE UINT32_C(1)

typedef uint8_t pxa_ui_color_scheme_t;
#define PXA_UI_COLOR_SCHEME_LIGHT ((pxa_ui_color_scheme_t)0)
#define PXA_UI_COLOR_SCHEME_DARK ((pxa_ui_color_scheme_t)1)

typedef uint8_t pxa_ui_transaction_kind_t;
#define PXA_UI_PATCH ((pxa_ui_transaction_kind_t)1)
#define PXA_UI_REPLACE_SUBTREE ((pxa_ui_transaction_kind_t)2)
#define PXA_UI_REPLACE_SURFACE ((pxa_ui_transaction_kind_t)3)
#define PXA_UI_PRESERVE_ON_FAILURE UINT8_C(1)

#define PXA_UI_COMMAND_CREATE UINT8_C(1)
#define PXA_UI_COMMAND_SET_PROPERTY UINT8_C(2)
#define PXA_UI_COMMAND_CLEAR_PROPERTY UINT8_C(3)
#define PXA_UI_COMMAND_MOVE UINT8_C(4)
#define PXA_UI_COMMAND_REMOVE UINT8_C(5)
#define PXA_UI_COMMAND_OPTIONAL UINT8_C(1)

typedef uint8_t pxa_ui_node_type_t;
#define PXA_UI_NODE_ROOT ((pxa_ui_node_type_t)1)
#define PXA_UI_NODE_BOX ((pxa_ui_node_type_t)2)
#define PXA_UI_NODE_SCROLL ((pxa_ui_node_type_t)3)
#define PXA_UI_NODE_TEXT ((pxa_ui_node_type_t)4)
#define PXA_UI_NODE_IMAGE ((pxa_ui_node_type_t)5)
#define PXA_UI_NODE_CONTROL ((pxa_ui_node_type_t)6)
#define PXA_UI_NODE_PROGRESS ((pxa_ui_node_type_t)7)
#define PXA_UI_NODE_CANVAS ((pxa_ui_node_type_t)8)
#define PXA_UI_NODE_VIRTUAL_LIST ((pxa_ui_node_type_t)9)
#define PXA_UI_NODE_MEDIA_SURFACE ((pxa_ui_node_type_t)10)

typedef uint8_t pxa_ui_control_type_t;
#define PXA_UI_CONTROL_NONE ((pxa_ui_control_type_t)0)
#define PXA_UI_CONTROL_BUTTON ((pxa_ui_control_type_t)1)
#define PXA_UI_CONTROL_TOGGLE ((pxa_ui_control_type_t)2)
#define PXA_UI_CONTROL_SLIDER ((pxa_ui_control_type_t)3)
#define PXA_UI_CONTROL_TEXT_INPUT ((pxa_ui_control_type_t)4)
#define PXA_UI_CONTROL_SELECTION ((pxa_ui_control_type_t)5)

typedef uint16_t pxa_ui_property_t;
#define PXA_UI_PROPERTY_VISIBLE ((pxa_ui_property_t)1)
#define PXA_UI_PROPERTY_ENABLED ((pxa_ui_property_t)2)
#define PXA_UI_PROPERTY_EVENT_MASK ((pxa_ui_property_t)3)
#define PXA_UI_PROPERTY_ACCESSIBILITY_ROLE ((pxa_ui_property_t)4)
#define PXA_UI_PROPERTY_ACCESSIBILITY_LABEL ((pxa_ui_property_t)5)
#define PXA_UI_PROPERTY_WIDTH ((pxa_ui_property_t)256)
#define PXA_UI_PROPERTY_HEIGHT ((pxa_ui_property_t)257)
#define PXA_UI_PROPERTY_MIN_WIDTH ((pxa_ui_property_t)258)
#define PXA_UI_PROPERTY_MAX_WIDTH ((pxa_ui_property_t)259)
#define PXA_UI_PROPERTY_MIN_HEIGHT ((pxa_ui_property_t)260)
#define PXA_UI_PROPERTY_MAX_HEIGHT ((pxa_ui_property_t)261)
#define PXA_UI_PROPERTY_LAYOUT ((pxa_ui_property_t)262)
#define PXA_UI_PROPERTY_WRAP ((pxa_ui_property_t)263)
#define PXA_UI_PROPERTY_JUSTIFY ((pxa_ui_property_t)264)
#define PXA_UI_PROPERTY_ALIGN ((pxa_ui_property_t)265)
#define PXA_UI_PROPERTY_ALIGN_SELF ((pxa_ui_property_t)266)
#define PXA_UI_PROPERTY_GAP ((pxa_ui_property_t)267)
#define PXA_UI_PROPERTY_PADDING ((pxa_ui_property_t)268)
#define PXA_UI_PROPERTY_GROW ((pxa_ui_property_t)269)
#define PXA_UI_PROPERTY_SHRINK ((pxa_ui_property_t)270)
#define PXA_UI_PROPERTY_POSITION ((pxa_ui_property_t)271)
#define PXA_UI_PROPERTY_X ((pxa_ui_property_t)272)
#define PXA_UI_PROPERTY_Y ((pxa_ui_property_t)273)
/* Grid tracks: an 8-byte record per track (kind:u8 | reserved:u8[3] |
 * value:u32). Kind 0 is content sized, 1 is a fraction whose weight is the
 * value and 2 is a fixed size in 1/64 dp. A grid cell is u16[4]: column, row,
 * column-span and row-span; the node's align property aligns the cell. */
#define PXA_UI_GRID_CONTENT 0u
#define PXA_UI_GRID_FRACTION 1u
#define PXA_UI_GRID_FIXED 2u
#define PXA_UI_GRID_TRACK_BYTES 8u
#define PXA_UI_GRID_CELL_BYTES 8u
#define PXA_UI_GRID_MAX_TRACKS 64u
#define PXA_UI_PROPERTY_GRID_COLUMNS ((pxa_ui_property_t)274)
#define PXA_UI_PROPERTY_GRID_ROWS ((pxa_ui_property_t)275)
#define PXA_UI_PROPERTY_GRID_CELL ((pxa_ui_property_t)276)
#define PXA_UI_PROPERTY_VARIANT ((pxa_ui_property_t)512)
#define PXA_UI_PROPERTY_FOREGROUND ((pxa_ui_property_t)513)
#define PXA_UI_PROPERTY_BACKGROUND ((pxa_ui_property_t)514)
#define PXA_UI_PROPERTY_BORDER_COLOR ((pxa_ui_property_t)515)
#define PXA_UI_PROPERTY_OPACITY ((pxa_ui_property_t)516)
#define PXA_UI_PROPERTY_RADIUS ((pxa_ui_property_t)517)
#define PXA_UI_PROPERTY_BORDER_WIDTH ((pxa_ui_property_t)518)
#define PXA_UI_PROPERTY_FONT_ROLE ((pxa_ui_property_t)519)
#define PXA_UI_PROPERTY_TEXT_ALIGN ((pxa_ui_property_t)520)
#define PXA_UI_PROPERTY_SHADOW ((pxa_ui_property_t)521)
#define PXA_UI_PROPERTY_COMPOSITION ((pxa_ui_property_t)522)

/* Alpha-overlay nodes are rendered into a separate transparent UI plane.
 * They keep their regular input behavior but are composed after app Surfaces
 * instead of being flattened into the primary RGB565 framebuffer. */
#define PXA_UI_COMPOSITION_BASE UINT8_C(0)
#define PXA_UI_COMPOSITION_ALPHA_OVERLAY UINT8_C(1)

/* Semantic typography roles selected by the Host theme. */
#define PXA_UI_FONT_ROLE_CAPTION ((uint16_t)0)
#define PXA_UI_FONT_ROLE_BODY ((uint16_t)1)
#define PXA_UI_FONT_ROLE_TITLE ((uint16_t)2)
#define PXA_UI_FONT_ROLE_ICON ((uint16_t)3)
/* Appended roles preserve the original wire values. Hosts without dedicated
 * faces fall back to caption/body/title as appropriate. */
#define PXA_UI_FONT_ROLE_LABEL ((uint16_t)4)
#define PXA_UI_FONT_ROLE_HEADLINE ((uint16_t)5)
#define PXA_UI_FONT_ROLE_DISPLAY ((uint16_t)6)

#define PXA_UI_PROPERTY_TEXT ((pxa_ui_property_t)768)
#define PXA_UI_PROPERTY_ICON ((pxa_ui_property_t)769)
#define PXA_UI_PROPERTY_ASSET ((pxa_ui_property_t)770)
#define PXA_UI_PROPERTY_IMAGE_FIT ((pxa_ui_property_t)771)
#define PXA_UI_PROPERTY_VALUE ((pxa_ui_property_t)772)
#define PXA_UI_PROPERTY_MIN_VALUE ((pxa_ui_property_t)773)
#define PXA_UI_PROPERTY_MAX_VALUE ((pxa_ui_property_t)774)
#define PXA_UI_PROPERTY_STEP ((pxa_ui_property_t)775)
#define PXA_UI_PROPERTY_SCROLL_AXIS ((pxa_ui_property_t)776)
#define PXA_UI_PROPERTY_SCROLLBAR ((pxa_ui_property_t)777)
#define PXA_UI_PROPERTY_ITEM_COUNT ((pxa_ui_property_t)778)
#define PXA_UI_PROPERTY_ITEM_EXTENT ((pxa_ui_property_t)779)
#define PXA_UI_PROPERTY_SCROLL_POSITION ((pxa_ui_property_t)780)

#define PXA_UI_IMAGE_FIT_CONTAIN UINT8_C(0)
#define PXA_UI_IMAGE_FIT_STRETCH UINT8_C(1)
#define PXA_UI_IMAGE_FIT_COVER UINT8_C(2)

#define PXA_UI_LENGTH_AUTO UINT8_C(0)
#define PXA_UI_LENGTH_LOGICAL_PX UINT8_C(1)
#define PXA_UI_LENGTH_PERCENT_Q16 UINT8_C(2)
#define PXA_UI_LENGTH_CONTENT UINT8_C(3)
#define PXA_UI_LENGTH_FILL UINT8_C(4)
#define PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16 UINT8_C(5)
#define PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16 UINT8_C(6)

#define PXA_UI_LAYOUT_ROW UINT8_C(1)
#define PXA_UI_LAYOUT_COLUMN UINT8_C(2)
#define PXA_UI_LAYOUT_STACK UINT8_C(3)
#define PXA_UI_LAYOUT_GRID UINT8_C(4)

#define PXA_UI_CANVAS_RECT UINT8_C(1)
#define PXA_UI_CANVAS_ELLIPSE UINT8_C(2)
#define PXA_UI_CANVAS_LINE UINT8_C(3)
#define PXA_UI_CANVAS_ARC UINT8_C(4)
#define PXA_UI_CANVAS_TEXT UINT8_C(5)
#define PXA_UI_CANVAS_IMAGE UINT8_C(6)
#define PXA_UI_CANVAS_CLIP_PUSH UINT8_C(7)
#define PXA_UI_CANVAS_CLIP_POP UINT8_C(8)
#define PXA_UI_CANVAS_TEXT_BOX UINT8_C(9)
#define PXA_UI_CANVAS_BITMAP_RGB565 UINT8_C(10)

#define PXA_UI_CANVAS_TEXT_ALIGN_TOP UINT8_C(0)
#define PXA_UI_CANVAS_TEXT_ALIGN_MIDDLE UINT8_C(1)
#define PXA_UI_CANVAS_TEXT_ALIGN_BOTTOM UINT8_C(2)

typedef uint16_t pxa_ui_event_kind_t;
#define PXA_UI_EVENT_ACTION ((pxa_ui_event_kind_t)1)
#define PXA_UI_EVENT_VALUE_CHANGED ((pxa_ui_event_kind_t)2)
#define PXA_UI_EVENT_SCROLL ((pxa_ui_event_kind_t)3)
#define PXA_UI_EVENT_FOCUS ((pxa_ui_event_kind_t)4)
#define PXA_UI_EVENT_KEY ((pxa_ui_event_kind_t)5)
#define PXA_UI_EVENT_TEXT ((pxa_ui_event_kind_t)6)
#define PXA_UI_EVENT_POINTER ((pxa_ui_event_kind_t)7)
#define PXA_UI_EVENT_ACCESSIBILITY ((pxa_ui_event_kind_t)8)
#define PXA_UI_EVENT_VISIBLE_RANGE ((pxa_ui_event_kind_t)9)
#define PXA_UI_EVENT_CONTROLLER_STATE ((pxa_ui_event_kind_t)10)
#define PXA_UI_EVENT_FLAG_RELIABLE UINT16_C(1)
#define PXA_UI_EVENT_FLAG_COALESCIBLE UINT16_C(2)

#define PXA_UI_EVENT_MASK_ACTION (UINT64_C(1) << 0)
#define PXA_UI_EVENT_MASK_VALUE_CHANGED (UINT64_C(1) << 1)
#define PXA_UI_EVENT_MASK_KEY (UINT64_C(1) << 4)
#define PXA_UI_EVENT_MASK_TEXT (UINT64_C(1) << 5)
#define PXA_UI_EVENT_MASK_POINTER (UINT64_C(1) << 6)
#define PXA_UI_EVENT_MASK_CONTROLLER_STATE (UINT64_C(1) << 9)

/* A text event payload is the current UTF-8 text of a text input, without a
 * terminator. Hosts and Guests agree on this bound so both sides can use fixed
 * buffers. */
#define PXA_UI_EVENT_TEXT_MAX_BYTES 64u

#define PXA_UI_KEY_VOLUME_UP UINT32_C(1)
#define PXA_UI_KEY_VOLUME_DOWN UINT32_C(2)
#define PXA_UI_KEY_VOLUME_UP_RELEASED UINT32_C(3)
#define PXA_UI_KEY_VOLUME_DOWN_RELEASED UINT32_C(4)

typedef uint64_t pxa_ui_features_t;
#define PXA_UI_FEATURE_GRID (UINT64_C(1) << 0)
#define PXA_UI_FEATURE_CANVAS (UINT64_C(1) << 1)
#define PXA_UI_FEATURE_VIRTUAL_LIST (UINT64_C(1) << 2)
#define PXA_UI_FEATURE_MEDIA_SURFACE (UINT64_C(1) << 3)
#define PXA_UI_FEATURE_ANIMATION (UINT64_C(1) << 4)
#define PXA_UI_FEATURE_ACCESSIBILITY (UINT64_C(1) << 5)
#define PXA_UI_FEATURE_MULTIPLE_SURFACES (UINT64_C(1) << 6)
#define PXA_UI_FEATURE_SHARED_COMMAND_BUFFER (UINT64_C(1) << 7)
#define PXA_UI_FEATURE_RGB565_BITMAP (UINT64_C(1) << 8)
#define PXA_UI_FEATURE_CONTROLLER_INPUT (UINT64_C(1) << 9)
#define PXA_UI_FEATURE_CANVAS_STREAM_IO (UINT64_C(1) << 10)

#define PXA_UI_CONTROLLER_UP (UINT32_C(1) << 0)
#define PXA_UI_CONTROLLER_DOWN (UINT32_C(1) << 1)
#define PXA_UI_CONTROLLER_LEFT (UINT32_C(1) << 2)
#define PXA_UI_CONTROLLER_RIGHT (UINT32_C(1) << 3)
#define PXA_UI_CONTROLLER_A (UINT32_C(1) << 4)
#define PXA_UI_CONTROLLER_B (UINT32_C(1) << 5)
#define PXA_UI_CONTROLLER_START (UINT32_C(1) << 6)
#define PXA_UI_CONTROLLER_SELECT (UINT32_C(1) << 7)
#define PXA_UI_CONTROLLER_BUTTON_MASK UINT32_C(0xff)

typedef uint8_t pxa_ui_pressure_t;
#define PXA_UI_PRESSURE_NORMAL ((pxa_ui_pressure_t)0)
#define PXA_UI_PRESSURE_CONSTRAINED ((pxa_ui_pressure_t)1)
#define PXA_UI_PRESSURE_CRITICAL ((pxa_ui_pressure_t)2)

typedef uint8_t pxa_ui_surface_role_t;
#define PXA_UI_SURFACE_APPLICATION ((pxa_ui_surface_role_t)1)
#define PXA_UI_SURFACE_DIALOG ((pxa_ui_surface_role_t)2)
#define PXA_UI_SURFACE_OVERLAY ((pxa_ui_surface_role_t)3)
#define PXA_UI_SURFACE_EXTERNAL ((pxa_ui_surface_role_t)4)

typedef struct {
    uint32_t surface;
    uint32_t width;
    uint32_t height;
    uint32_t density_q16;
    uint32_t font_scale_q16;
    /* Safe-area insets in logical pixels, ordered top, right, bottom, left.
     * Includes cutout coverage and physical safe margins but not system
     * chrome; use the window service snapshot for system bar insets. */
    uint32_t safe_insets[4];
    uint64_t input_capabilities;
    pxa_ui_features_t features;
    uint32_t recommended_write_bytes;
    uint8_t color_scheme;
    uint8_t direction;
} pxa_ui_environment_t;

typedef void *(*pxa_ui_allocate_fn)(void *context, size_t size);
typedef void (*pxa_ui_release_fn)(void *context, void *memory);
typedef uint64_t (*pxa_ui_clock_fn)(void *context);

typedef struct {
    uint32_t struct_size;
    void *allocator_context;
    pxa_ui_allocate_fn allocate;
    pxa_ui_release_fn release;
    void *clock_context;
    pxa_ui_clock_fn now_us;
    size_t max_dynamic_bytes;
    size_t max_transaction_bytes;
    size_t max_canvas_bytes;
    pxa_ui_features_t features;
    uint32_t primary_width;
    uint32_t primary_height;
    uint32_t density_q16;
    uint32_t font_scale_q16;
    pxa_ui_color_scheme_t color_scheme;
    /* Safe-area insets applied to the primary surface environment at bind
     * time, ordered top, right, bottom, left. Hosts that learn the real
     * display metrics before a component binds should pass them here so the
     * Guest start configuration already carries the correct environment. */
    uint32_t safe_insets[4];
} pxa_ui_config_t;

typedef struct {
    uint32_t surface;
    uint32_t transaction;
    uint32_t generation;
    uint32_t target;
    pxa_ui_transaction_kind_t kind;
    uint8_t flags;
    void *target_handle;
} pxa_ui_transaction_info_t;

typedef struct {
    uint8_t command;
    uint8_t flags;
    uint32_t node;
    uint32_t parent;
    uint32_t before;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    pxa_ui_property_t property;
    pxa_bytes_t value;
    void *node_handle;
    void *parent_handle;
    void *before_handle;
} pxa_ui_command_view_t;

typedef struct {
    uint32_t surface;
    uint32_t node;
    uint32_t frame;
    uint8_t dirty_count;
    int32_t dirty_rects[4][4];
    pxa_bytes_t display_list;
    void *node_handle;
} pxa_ui_canvas_view_t;

typedef pxa_status_t (*pxa_ui_backend_begin_fn)(
    void *context, const pxa_ui_transaction_info_t *info,
    void **backend_transaction);
typedef pxa_status_t (*pxa_ui_backend_apply_fn)(
    void *context, void *backend_transaction,
    const pxa_ui_command_view_t *command, void **created_handle);
typedef pxa_status_t (*pxa_ui_backend_commit_fn)(
    void *context, void *backend_transaction);
typedef void (*pxa_ui_backend_cancel_fn)(
    void *context, void *backend_transaction);
/* On success, the backend owns canvas->display_list and must release it once.
 * On failure, ownership remains with Core and release must not be called. */
typedef pxa_status_t (*pxa_ui_backend_canvas_fn)(
    void *context, const pxa_ui_canvas_view_t *canvas,
    pxa_ui_release_fn release, void *release_context);
typedef void (*pxa_ui_backend_reset_fn)(void *context);
typedef pxa_status_t (*pxa_ui_backend_surface_open_fn)(
    void *context, pxa_ui_surface_role_t role,
    pxa_ui_environment_t *environment);
typedef void (*pxa_ui_backend_surface_close_fn)(void *context,
                                                   uint32_t surface);
typedef pxa_status_t (*pxa_ui_backend_environment_fn)(
    void *context, const pxa_ui_environment_t *environment);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_ui_backend_begin_fn begin;
    pxa_ui_backend_apply_fn apply;
    pxa_ui_backend_commit_fn commit;
    pxa_ui_backend_cancel_fn cancel;
    pxa_ui_backend_canvas_fn present_canvas;
    pxa_ui_backend_reset_fn reset;
    pxa_ui_backend_surface_open_fn surface_open;
    pxa_ui_backend_surface_close_fn surface_close;
    pxa_ui_backend_environment_fn environment_changed;
} pxa_ui_backend_t;

typedef struct {
    uint32_t id;
    uint32_t surface;
    uint32_t parent;
    uint64_t event_mask;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    void *backend_handle;
} pxa_ui_node_snapshot_t;

typedef struct {
    size_t current_bytes;
    size_t peak_bytes;
    size_t transaction_bytes;
    size_t canvas_bytes;
    size_t node_count;
    size_t canvas_count;
    size_t surface_count;
    uint64_t commit_count;
    uint64_t last_commit_us;
    uint64_t max_commit_us;
} pxa_ui_memory_snapshot_t;

typedef struct pxa_ui_service pxa_ui_service_t;

void pxa_ui_config_init(pxa_ui_config_t *config);
size_t pxa_ui_service_workspace_size(void);
pxa_status_t pxa_ui_service_init(void *workspace, size_t workspace_size,
                                    pxa_runtime_t *runtime,
                                    const pxa_ui_config_t *config,
                                    pxa_ui_service_t **output);
void pxa_ui_service_deinit(pxa_ui_service_t *service);
pxa_status_t pxa_ui_service_register(pxa_ui_service_t *service);
pxa_status_t pxa_ui_bind(pxa_ui_service_t *service,
                            pxa_component_t component,
                            const pxa_ui_backend_t *backend);
pxa_status_t pxa_ui_unbind(pxa_ui_service_t *service,
                              pxa_component_t component);

pxa_status_t pxa_ui_find_node(const pxa_ui_service_t *service,
                                 pxa_component_t component, uint32_t surface,
                                 uint32_t node,
                                 pxa_ui_node_snapshot_t *output);
pxa_status_t pxa_ui_memory_snapshot(
    const pxa_ui_service_t *service, pxa_component_t component,
    pxa_ui_memory_snapshot_t *output);
pxa_status_t pxa_ui_set_pressure(pxa_ui_service_t *service,
                                    pxa_component_t component,
                                    pxa_ui_pressure_t pressure);
pxa_status_t pxa_ui_get_environment(
    const pxa_ui_service_t *service, pxa_component_t component,
    uint32_t surface, pxa_ui_environment_t *output);
pxa_status_t pxa_ui_update_environment(
    pxa_ui_service_t *service, pxa_component_t component,
    const pxa_ui_environment_t *environment);
pxa_status_t pxa_ui_encode_environment(
    const pxa_ui_environment_t *environment, void *buffer, size_t capacity,
    size_t *encoded_size);

pxa_status_t pxa_ui_queue_event(pxa_ui_service_t *service,
                                   pxa_component_t component,
                                   uint32_t surface, uint32_t node,
                                   uint16_t kind, uint16_t flags,
                                   uint64_t timestamp_us,
                                   const void *value, size_t value_size);

/* The caller must serialize this query with the UI service owner. */
bool pxa_ui_accepts_event(pxa_ui_service_t *service,
                          pxa_component_t component,
                          uint32_t surface, uint32_t node,
                          uint16_t kind);

#ifdef __cplusplus
}
#endif

#endif
