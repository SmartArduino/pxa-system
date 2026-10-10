#include "pxa/lvgl/pxa_lvgl_ui.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "src/indev/lv_indev_private.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#if __has_include("src/image/lv_image_decoder_private.h")
#include "src/image/lv_image_decoder_private.h"
#else
#include "src/draw/lv_image_decoder_private.h"
#endif

#define PXA_LVGL_UI_MAGIC UINT32_C(0x504c5632)
#define PXA_LVGL_UI_ALIGNMENT ((size_t)16)

typedef struct pxa_lvgl_ui_node pxa_lvgl_ui_node_t;
typedef struct pxa_lvgl_ui_command pxa_lvgl_ui_command_t;
typedef struct pxa_lvgl_ui_property_undo pxa_lvgl_ui_property_undo_t;
typedef struct pxa_lvgl_ui_transaction pxa_lvgl_ui_transaction_t;
typedef struct pxa_lvgl_ui_canvas_asset pxa_lvgl_ui_canvas_asset_t;
typedef struct pxa_lvgl_ui_image pxa_lvgl_ui_image_t;
typedef struct {
    pxa_handle64_t handle;
    pxa_lvgl_ui_image_t *image;
    /* 0: duplicate, 1: owned, 2: pooled, 3: borrowed until commit. */
    uint8_t owned;
} pxa_lvgl_ui_canvas_image_t;

struct pxa_lvgl_ui_canvas_asset {
    pxa_lvgl_ui_canvas_asset_t *next;
    const void *source;
    size_t path_size;
    uint8_t seen;
    uint8_t path[];
};

typedef struct {
    const uint8_t *bytes;
    size_t size;
    lv_area_t *clip_stack;
    size_t clip_capacity;
    lv_image_dsc_t *bitmap_images;
    size_t bitmap_count;
    size_t bitmap_capacity;
    pxa_lvgl_ui_canvas_asset_t *assets;
    pxa_lvgl_ui_canvas_image_t *images;
    size_t image_count;
    size_t image_capacity;
    pxa_lvgl_ui_canvas_image_t *spare_images;
    size_t spare_image_capacity;
    pxa_lvgl_ui_image_t *free_images;
    pxa_ui_release_fn release;
    void *release_context;
} pxa_lvgl_ui_canvas_t;

struct pxa_lvgl_ui_image {
    lv_image_dsc_t descriptor;
    pxa_asset_object_t *pixels;
    union {
        pxa_lvgl_ui_t *owner; /* Active descriptor. */
        pxa_lvgl_ui_image_t *next_free; /* Empty, unregistered pool entry. */
    } state;
    lv_draw_buf_t draw;
};

struct pxa_lvgl_ui_node {
    pxa_lvgl_ui_node_t *next;
    pxa_lvgl_ui_node_t *previous;
    struct pxa_lvgl_ui *ui;
    lv_obj_t *object;
    lv_obj_t *content;
    pxa_lvgl_ui_canvas_t *canvas;
    const void *asset_source;
    pxa_lvgl_ui_image_t *image;
    pxa_lvgl_ui_command_t *owner_command;
    uint32_t surface;
    uint32_t id;
    uint64_t event_mask;
    uint32_t foreground;
    uint32_t background;
    uint32_t border;
    uint32_t item_count;
    uint32_t visible_first;
    uint32_t visible_count;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    uint8_t foreground_kind;
    uint8_t image_tint;
    uint8_t background_kind;
    uint8_t border_kind;
    uint8_t foreground_token;
    uint8_t background_token;
    uint8_t border_token;
    uint8_t font_role;
    uint8_t opacity;
    uint8_t composition;
    uint8_t visible;
    uint8_t justify;
    uint8_t align;
    uint8_t has_scroll_position;
    int32_t minimum;
    int32_t maximum;
    int32_t item_extent;
    int32_t scroll_position;
    /* Owned grid track descriptors; LVGL keeps the pointer until the object or
     * the template is replaced. */
    lv_coord_t *grid_columns;
    lv_coord_t *grid_rows;
    uint8_t has_grid_columns;
    uint8_t has_grid_rows;
    /* Set while the Guest writes a property, so a text input does not echo the
     * Guest's own write back as a text event. */
    uint8_t suppress_events;
    /* Tentative removal: bit 0 active, bit 1 visible, bit 2 hidden. */
    uint8_t removal_state;
    /* Visual suppression must follow a node across MOVE and rollback. */
    uint8_t alpha_hidden;
    uint8_t range_scheduled;
};

struct pxa_lvgl_ui_command {
    pxa_lvgl_ui_command_t *next;
    pxa_ui_command_view_t view;
    /* Command kinds have disjoint preparation and undo ownership. */
    union {
        pxa_lvgl_ui_node_t *created;
        pxa_lvgl_ui_property_undo_t *property;
        struct {
            /* Before apply: prepared source. After apply: previous source.
             * A second swap restores it without re-resolving Guest handles. */
            pxa_lvgl_ui_image_t *image;
            const void *asset;
            uint8_t applied;
        } source;
        struct {
            lv_coord_t *tracks;
            uint32_t layout;
            uint8_t has_tracks;
            uint8_t applied;
        } grid;
        struct {
            lv_obj_t *parent;
            int32_t index;
            uint8_t applied;
        } move;
    } owned;
    uint8_t value[];
};

struct pxa_lvgl_ui_transaction {
    struct pxa_lvgl_ui *ui;
    pxa_ui_transaction_info_t info;
    pxa_lvgl_ui_command_t *commands;
    pxa_lvgl_ui_command_t *tail;
    pxa_status_t status;
};

typedef struct {
    uint16_t *pixels;
    uint8_t *values;
    int32_t x;
    int32_t y;
    uint16_t width;
    uint16_t height;
    uint16_t content_x;
    uint16_t content_y;
    uint16_t content_width;
    uint16_t content_height;
    uint64_t revision;
} pxa_lvgl_ui_alpha_t;

typedef struct pxa_sized_font {
    struct pxa_sized_font *next;
    lv_font_t *font;
    uint16_t pixels;
} pxa_sized_font_t;

/* Keep only callback/budget configuration; theme and environment already
 * have live copies below. This avoids default storage for duplicate state. */
typedef struct {
    void *allocator_context;
    pxa_ui_allocate_fn allocate;
    pxa_ui_release_fn release;
    void *execute_user_data;
    pxa_lvgl_ui_execute_fn execute;
    pxa_lvgl_ui_resolve_asset_fn resolve_asset;
    pxa_lvgl_ui_acquire_image_fn acquire_image;
    pxa_lvgl_ui_release_asset_fn release_asset;
    void *asset_user_data;
    pxa_lvgl_ui_event_fn event_callback;
    pxa_lvgl_ui_now_us_fn now_us;
    void *callback_user_data;
    void *parent_object;
    size_t snapshot_limit_bytes;
    size_t alpha_limit_bytes;
    const char *sized_text_font_path;
} pxa_lvgl_ui_runtime_config_t;

struct pxa_lvgl_ui {
    uint32_t magic;
    uint8_t transaction_active;
    pxa_lvgl_ui_runtime_config_t config;
    lv_image_decoder_t *image_decoder;
    pxa_lvgl_ui_theme_t theme;
    pxa_ui_environment_t primary_environment;
    pxa_lvgl_ui_node_t *nodes;
    pxa_lvgl_ui_node_t *primary_root;
    pxa_lvgl_ui_alpha_t alpha;
    pxa_lvgl_ui_alpha_t spare_alpha;
    void *snapshot_memory;
    size_t snapshot_capacity;
    uint64_t event_timestamp_us;
    pxa_sized_font_t *sized_fonts;
};

static uint64_t g_alpha_revision;

static void *ui_allocate(pxa_lvgl_ui_t *ui, size_t size) {
    if (ui == NULL || size == 0 || ui->config.allocate == NULL) return NULL;
    return ui->config.allocate(ui->config.allocator_context, size);
}

static void ui_release(pxa_lvgl_ui_t *ui, void *memory) {
    if (ui != NULL && memory != NULL && ui->config.release != NULL)
        ui->config.release(ui->config.allocator_context, memory);
}

/* This flag is private to descriptors allocated by this adapter. Guest bytes
 * can never supply native descriptors. Each decoder accepts only its owner. */
#define PXA_LVGL_PREPARED_IMAGE LV_IMAGE_FLAGS_USER8
static lv_result_t prepared_image_info(lv_image_decoder_t *decoder,
    lv_image_decoder_dsc_t *dsc,lv_image_header_t *header) {
    if (dsc->src_type!=LV_IMAGE_SRC_VARIABLE) return LV_RESULT_INVALID;
    const lv_image_dsc_t *source=dsc->src;
    if (!(source->header.flags & PXA_LVGL_PREPARED_IMAGE)) return LV_RESULT_INVALID;
    const pxa_lvgl_ui_image_t *image=dsc->src;
    if (image->state.owner!=decoder->user_data) return LV_RESULT_INVALID;
    *header=source->header; return LV_RESULT_OK;
}
static lv_result_t prepared_image_open(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *dsc) {
    (void)decoder;
    const pxa_lvgl_ui_image_t *image=dsc->src;
    /* Software UI drawing consumes straight alpha. Do not silently allocate
     * a converted pixel copy for a consumer requiring premultiplied alpha. */
    if (dsc->args.premultiply && image->descriptor.header.cf==LV_COLOR_FORMAT_ARGB8888)
        return LV_RESULT_INVALID;
    dsc->decoded=&image->draw;
    return LV_RESULT_OK;
}
static int ensure_image_decoder(pxa_lvgl_ui_t *ui) {
    lv_lock();
    if (!ui->image_decoder) {
        ui->image_decoder=lv_image_decoder_create();
        if (ui->image_decoder) {
            ui->image_decoder->user_data=ui;
            ui->image_decoder->name="pxa-prepared";
            lv_image_decoder_set_info_cb(ui->image_decoder,prepared_image_info);
            lv_image_decoder_set_open_cb(ui->image_decoder,prepared_image_open);
        }
    }
    int ready=ui->image_decoder!=NULL;
    lv_unlock(); return ready;
}

static void release_image(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_image_t *image) {
    if (!image) return;
    lv_lock(); lv_image_cache_drop(&image->descriptor); lv_unlock();
    pxa_asset_object_release(image->pixels);
    ui_release(ui,image);
}

static void release_canvas_images(pxa_lvgl_ui_t *ui,
    pxa_lvgl_ui_canvas_image_t *images, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (images[i].owned) release_image(ui, images[i].image);
    ui_release(ui, images);
}

/* Recycled wrappers never retain pixels or a decoder cache entry. Capacity
 * follows the maximum overlap of two submitted frames, not the frame count. */
static void recycle_image(pxa_lvgl_ui_canvas_t *canvas, pxa_lvgl_ui_image_t *image) {
    lv_lock();
    lv_image_cache_drop(&image->descriptor);
    lv_unlock();
    pxa_asset_object_release(image->pixels);
    memset(image, 0, sizeof(*image));
    image->state.next_free = canvas->free_images;
    canvas->free_images = image;
}

static void release_image_pool(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_canvas_t *canvas) {
    while (canvas->free_images) {
        pxa_lvgl_ui_image_t *image = canvas->free_images;
        canvas->free_images = image->state.next_free;
        ui_release(ui, image);
    }
}

/* Caller owns both storage and the acquired pixel reference, including on
 * failure. This permits Canvas to reuse metadata without copying pixels. */
static pxa_status_t initialize_image(pxa_lvgl_ui_t *ui, pxa_asset_object_t *pixels,
                                     pxa_lvgl_ui_image_t *image) {
    pxa_asset_object_view_t view;
    pxa_asset_object_view(pixels, &view);
    if (view.kind != PXA_ASSET_IMAGE || (view.encoding != PXA_ASSET_ENCODING_RGB565 &&
                                         view.encoding != PXA_ASSET_ENCODING_BGRA8888 &&
                                         view.encoding != PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED))
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(image, 0, sizeof(*image));
    image->pixels = pixels;
    image->state.owner = ui;
    image->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->descriptor.header.cf = view.encoding == PXA_ASSET_ENCODING_RGB565
                                      ? LV_COLOR_FORMAT_RGB565
                                      : view.encoding == PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED
                                            ? LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED
                                            : LV_COLOR_FORMAT_ARGB8888;
    image->descriptor.header.w = view.width;
    image->descriptor.header.h = view.height;
    image->descriptor.header.stride =
        view.width * (view.encoding == PXA_ASSET_ENCODING_RGB565 ? 2u : 4u);
    image->descriptor.data = view.data;
    image->descriptor.data_size = view.bytes;
    image->descriptor.header.flags = PXA_LVGL_PREPARED_IMAGE;
    if (image->descriptor.header.stride !=
            lv_draw_buf_width_to_stride(view.width, image->descriptor.header.cf) ||
        (uintptr_t)view.data % LV_DRAW_BUF_ALIGN ||
        lv_draw_buf_init(&image->draw, view.width, view.height,
                         image->descriptor.header.cf, image->descriptor.header.stride,
                         (void *)view.data, view.bytes) != LV_RESULT_OK) {
        return PXA_STATUS_UNSUPPORTED;
    }
    if (!ensure_image_decoder(ui)) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_image(pxa_lvgl_ui_t *ui, pxa_component_t component,
                                  pxa_handle64_t handle, pxa_lvgl_ui_image_t **output) {
    *output = NULL;
    if (!ui->config.acquire_image)
        return PXA_STATUS_UNSUPPORTED;
    pxa_asset_object_t *pixels = NULL;
    pxa_status_t status = ui->config.acquire_image(component, handle, &pixels,
                                                   ui->config.asset_user_data);
    if (status)
        return status;
    pxa_lvgl_ui_image_t *image = ui_allocate(ui, sizeof(*image));
    if (!image) {
        pxa_asset_object_release(pixels);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    status = initialize_image(ui, pixels, image);
    if (status) {
        pxa_asset_object_release(pixels);
        ui_release(ui, image);
        return status;
    }
    *output = image;
    return PXA_STATUS_OK;
}

static void release_asset_source(pxa_lvgl_ui_t *ui, const void *source) {
    if (ui != NULL && source != NULL && ui->config.release_asset != NULL)
        ui->config.release_asset(source, ui->config.asset_user_data);
}

static void release_canvas_assets(pxa_lvgl_ui_t *ui,
                                  pxa_lvgl_ui_canvas_asset_t *assets) {
    while (assets != NULL) {
        pxa_lvgl_ui_canvas_asset_t *next = assets->next;
        release_asset_source(ui, assets->source);
        ui_release(ui, assets);
        assets = next;
    }
}

static pxa_lvgl_ui_canvas_asset_t *find_canvas_asset(
    pxa_lvgl_ui_canvas_asset_t *assets, const uint8_t *path,
    size_t path_size) {
    for (; assets != NULL; assets = assets->next) {
        if (assets->path_size == path_size &&
            memcmp(assets->path, path, path_size) == 0)
            return assets;
    }
    return NULL;
}

static lv_color_t rgba_color(uint32_t rgba) {
    return lv_color_hex(rgba >> 8);
}

static lv_opa_t rgba_opa(uint32_t rgba) {
    return (lv_opa_t)(rgba & UINT32_C(0xff));
}

static uint32_t resolved_color(const pxa_lvgl_ui_t *ui, uint8_t kind,
                               uint8_t token, uint32_t rgba) {
    if (kind != 0 || token >= PXA_UI_THEME_ROLE_COUNT) return rgba;
    if (ui->theme.rgba[token] != 0u) return ui->theme.rgba[token];
    if (token < PXA_UI_THEME_COLOR_COUNT) return rgba;
    if (token == PXA_UI_THEME_PRIMARY_CONTAINER ||
        token == PXA_UI_THEME_SECONDARY ||
        token == PXA_UI_THEME_SECONDARY_CONTAINER ||
        token == PXA_UI_THEME_TERTIARY ||
        token == PXA_UI_THEME_TERTIARY_CONTAINER ||
        token == PXA_UI_THEME_INVERSE_PRIMARY)
        return ui->theme.rgba[PXA_UI_THEME_PRIMARY];
    if (token == PXA_UI_THEME_ON_PRIMARY_CONTAINER ||
        token == PXA_UI_THEME_ON_SECONDARY ||
        token == PXA_UI_THEME_ON_SECONDARY_CONTAINER ||
        token == PXA_UI_THEME_ON_TERTIARY ||
        token == PXA_UI_THEME_ON_TERTIARY_CONTAINER ||
        token == PXA_UI_THEME_ON_ERROR_CONTAINER ||
        token == PXA_UI_THEME_INVERSE_ON_SURFACE)
        return ui->theme.rgba[PXA_UI_THEME_ON_PRIMARY];
    if (token == PXA_UI_THEME_ON_SURFACE_VARIANT)
        return ui->theme.rgba[PXA_UI_THEME_MUTED];
    if (token == PXA_UI_THEME_OUTLINE_VARIANT)
        return ui->theme.rgba[PXA_UI_THEME_BORDER];
    if (token == PXA_UI_THEME_ERROR_CONTAINER)
        return ui->theme.rgba[PXA_UI_THEME_DANGER];
    return ui->theme.rgba[PXA_UI_THEME_SURFACE];
}

static int32_t logical_pixels(const pxa_lvgl_ui_t *ui, int32_t value) {
    int64_t scaled = (int64_t)value * ui->primary_environment.density_q16;
    scaled /= (INT64_C(64) << 16);
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t canvas_pixels(const pxa_lvgl_ui_t *ui, int64_t value) {
    int64_t density = ui->primary_environment.density_q16;
    int64_t scaled;
    if (value > 0 && value > INT64_MAX / density) return LV_COORD_MAX;
    if (value < 0 && value < INT64_MIN / density) return -LV_COORD_MAX;
    scaled = value * density;
    scaled /= INT64_C(1) << 16;
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t canvas_logical_pixels(const pxa_lvgl_ui_t *ui,
                                     int32_t value) {
    int64_t scaled = (int64_t)value * (INT64_C(1) << 16);
    scaled /= ui->primary_environment.density_q16;
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

static int32_t canvas_end_pixels(const pxa_lvgl_ui_t *ui, int64_t value) {
    int32_t result = canvas_pixels(ui, value);
    int64_t density = ui->primary_environment.density_q16;
    if (value > 0 && value <= INT64_MAX / density && result < LV_COORD_MAX &&
        (value * density) % (INT64_C(1) << 16) != 0)
        ++result;
    return result;
}

static int32_t viewport_length(int32_t fraction_q16, uint32_t pixel_extent) {
    /* Environment extents are Surface pixels. Only dp lengths apply density;
     * applying it to vw/vh a second time makes dense-panel layouts overflow. */
    int64_t scaled = (int64_t)pixel_extent * fraction_q16 / (INT64_C(1) << 16);
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t length_value(const pxa_lvgl_ui_t *ui,
                            const uint8_t *value) {
    uint8_t unit = value[0];
    int32_t amount = (int32_t)pxa_read_u32(value + 4);
    switch (unit) {
        case PXA_UI_LENGTH_LOGICAL_PX:
            return logical_pixels(ui, amount);
        case PXA_UI_LENGTH_PERCENT_Q16:
            return LV_PCT(amount >> 16);
        case PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16:
            return viewport_length(amount,
                                   ui->primary_environment.width);
        case PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16:
            return viewport_length(amount,
                                   ui->primary_environment.height);
        case PXA_UI_LENGTH_FILL:
            return LV_PCT(100);
        case PXA_UI_LENGTH_AUTO:
        case PXA_UI_LENGTH_CONTENT:
        default:
            return LV_SIZE_CONTENT;
    }
}

static const lv_font_t *font_for_role(const pxa_lvgl_ui_t *ui,
                                      uint16_t role) {
    const void *font =
        role == PXA_UI_FONT_ROLE_CAPTION ? ui->theme.caption_font
        : role == PXA_UI_FONT_ROLE_LABEL
            ? (ui->theme.label_font != NULL ? ui->theme.label_font
                                            : ui->theme.caption_font)
        : role == PXA_UI_FONT_ROLE_TITLE ? ui->theme.title_font
        : role == PXA_UI_FONT_ROLE_HEADLINE
            ? (ui->theme.headline_font != NULL ? ui->theme.headline_font
                                               : ui->theme.title_font)
        : role == PXA_UI_FONT_ROLE_DISPLAY
            ? (ui->theme.display_font != NULL ? ui->theme.display_font
                                              : ui->theme.title_font)
        : role == PXA_UI_FONT_ROLE_ICON ? ui->theme.icon_font
                                        : ui->theme.body_font;
    return font == NULL ? LV_FONT_DEFAULT : (const lv_font_t *)font;
}

static const lv_font_t *sized_font(pxa_lvgl_ui_t *ui, uint16_t size, int create) {
#if LV_USE_FREETYPE
    int32_t pixels = size;
    unsigned count = 0;
    if (pixels < 1 || pixels > 256 || !ui->config.sized_text_font_path) return NULL;
    for (pxa_sized_font_t *entry=ui->sized_fonts; entry; entry=entry->next) {
        if (entry->pixels==pixels) return entry->font;
        ++count;
    }
    if (!create || count>=16) return NULL;
    pxa_sized_font_t *entry=ui_allocate(ui,sizeof(*entry));
    if (!entry) return NULL;
    entry->font=lv_freetype_font_create(ui->config.sized_text_font_path,
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,(uint32_t)pixels,LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!entry->font) { ui_release(ui,entry); return NULL; }
    entry->pixels=(uint16_t)pixels;entry->next=ui->sized_fonts;ui->sized_fonts=entry;
    return entry->font;
#else
    (void)ui; (void)size; (void)create; return NULL;
#endif
}

static void release_sized_fonts(pxa_lvgl_ui_t *ui) {
    while (ui->sized_fonts) {
        pxa_sized_font_t *entry=ui->sized_fonts;ui->sized_fonts=entry->next;
#if LV_USE_FREETYPE
        lv_freetype_font_delete(entry->font);
#endif
        ui_release(ui,entry);
    }
}

static const char *icon_text(uint32_t icon) {
    static const char *const icons[] = {
        "", "\xef\x81\x93", "\xef\x81\x94", "\xef\x80\x95",
        "\xef\x81\x98", "\xef\x81\x99", "\xef\x80\x8c",
        "\xef\x80\x93", "\xef\x80\x8d", "\xef\x81\x9e",
        "\xef\x80\x82", "\xef\x80\x81", "\xef\x80\x84"};
    return icon < sizeof(icons) / sizeof(icons[0]) ? icons[icon] : "?";
}

static uint64_t input_timestamp_us(const pxa_lvgl_ui_t *ui,
                                   const lv_indev_t *input) {
    uint64_t now_us;
    uint64_t age_us;
    uint32_t age_ms;
    if (ui == NULL || ui->config.now_us == NULL) return 0;
    now_us = ui->config.now_us(ui->config.callback_user_data);
    if (input == NULL) return now_us;
    age_ms = lv_tick_diff((uint32_t)(now_us / 1000u), input->timestamp);
    /* Reject timestamps from a different clock domain or stale synthetic
     * events. A real pointer sample reaches the Canvas in the same LVGL pass. */
    if (age_ms > 1000u) return now_us;
    age_us = (uint64_t)age_ms * 1000u;
    return age_us <= now_us ? now_us - age_us : now_us;
}

static void emit_event(pxa_lvgl_ui_node_t *node, lv_indev_t *input,
                       pxa_ui_event_kind_t kind, uint16_t flags,
                       const void *value, size_t value_size) {
    uint64_t previous_timestamp_us;
    if (node->suppress_events ||
        node->ui->config.event_callback == NULL ||
        (node->event_mask & (UINT64_C(1) << (kind - 1u))) == 0)
        return;
    previous_timestamp_us = node->ui->event_timestamp_us;
    node->ui->event_timestamp_us = input_timestamp_us(node->ui, input);
    node->ui->config.event_callback(node->surface, node->id, kind, flags,
                                    value, value_size,
                                    node->ui->config.callback_user_data);
    node->ui->event_timestamp_us = previous_timestamp_us;
}

static void report_visible_range(void *context);

static void schedule_visible_range(pxa_lvgl_ui_node_t *node) {
    if (node->type == PXA_UI_NODE_VIRTUAL_LIST && !node->range_scheduled &&
        lv_async_call(report_visible_range, node) == LV_RESULT_OK)
        node->range_scheduled = 1;
}

static void report_visible_range(void *context) {
    pxa_lvgl_ui_node_t *node = context;
    int32_t scroll_y;
    int32_t viewport;
    uint32_t first, visible, overscan, count;
    uint64_t end;
    uint8_t range[8];
    node->range_scheduled = 0;
    if (node->ui->transaction_active) {
        schedule_visible_range(node);
        return;
    }
    if (node->object == NULL || !node->visible || node->item_extent <= 0) return;
    scroll_y = lv_obj_get_scroll_y(node->object);
    viewport = lv_obj_get_content_height(node->object);
    if (scroll_y < 0) scroll_y = 0;
    first = (uint32_t)scroll_y / (uint32_t)node->item_extent;
    visible = viewport <= 0 ? 1u :
        (uint32_t)(viewport + node->item_extent - 1) / (uint32_t)node->item_extent;
    overscan = visible / 2u + 1u;
    first = first > overscan ? first - overscan : 0;
    if (first > node->item_count) first = node->item_count;
    end = (uint64_t)first + visible + (uint64_t)overscan * 2u;
    if (end > node->item_count) end = node->item_count;
    count = end > first ? (uint32_t)(end - first) : 0;
    if (first == node->visible_first && count == node->visible_count) return;
    node->visible_first = first;
    node->visible_count = count;
    pxa_write_u32(range, first);
    pxa_write_u32(range + 4, count);
    emit_event(node, NULL, PXA_UI_EVENT_VISIBLE_RANGE, 0, range, sizeof(range));
}

static void on_widget_event(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    int32_t value;
    if (node == NULL) return;
    if (code == LV_EVENT_SIZE_CHANGED) {
        schedule_visible_range(node);
        return;
    }
    /* The created handle stays valid until the transaction commits: the commit
     * reveals every created node once its properties are applied, and a node
     * that reports an event while they are applied (a text input that receives
     * its text, for example) must not be left hidden. */
    if (code == LV_EVENT_CLICKED) {
        /* A tap on a text input starts editing; the action arrives when the
         * input is submitted (LV_EVENT_READY). */
        if (node->type == PXA_UI_NODE_CONTROL &&
            node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
            for (pxa_lvgl_ui_node_t *other = node->ui->nodes; other != NULL;
                 other = other->next)
                if (other != node && other->object != NULL &&
                    other->subtype == PXA_UI_CONTROL_TEXT_INPUT)
                    lv_obj_remove_state(other->object, LV_STATE_FOCUSED);
            lv_obj_add_state(node->object, LV_STATE_FOCUSED);
            return;
        }
        emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_ACTION,
                   PXA_UI_EVENT_FLAG_RELIABLE, NULL, 0);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (node->type != PXA_UI_NODE_CONTROL) return;
        if (node->subtype == PXA_UI_CONTROL_TOGGLE)
            value = lv_obj_has_state(node->object, LV_STATE_CHECKED) ? 1 : 0;
        else if (node->subtype == PXA_UI_CONTROL_SLIDER)
            value = lv_slider_get_value(node->object);
        else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
            const char *text = lv_textarea_get_text(node->object);
            size_t length = text == NULL ? 0u : strlen(text);
            /* Text inputs reuse maximum, which otherwise belongs to sliders.
             * Legacy nodes retain 64 bytes; opted-in nodes allocate no cache. */
            size_t maximum = (size_t)node->maximum;
            if (length > maximum) {
                length = maximum;
                while (length != 0 &&
                       ((uint8_t)text[length] & UINT8_C(0xc0)) == UINT8_C(0x80))
                    --length;
            }
            emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_TEXT,
                       PXA_UI_EVENT_FLAG_RELIABLE, text, length);
            return;
        } else
            return;
        emit_event(node, lv_event_get_indev(event),
                   PXA_UI_EVENT_VALUE_CHANGED, 0,
                   &value, sizeof(value));
    } else if (code == LV_EVENT_READY) {
        emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_ACTION,
                   PXA_UI_EVENT_FLAG_RELIABLE, NULL, 0);
    } else if (code == LV_EVENT_SCROLL) {
        int32_t scroll_y = lv_obj_get_scroll_y(node->object);
        value = canvas_logical_pixels(node->ui, scroll_y);
        emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_SCROLL, 0,
                   &value, sizeof(value));
        schedule_visible_range(node);
    }
}

static void on_canvas_draw(lv_event_t *event);
static void on_canvas_pointer(lv_event_t *event);
static void apply_node_colors(pxa_lvgl_ui_node_t *node);
static int node_is_in_alpha_overlay(const pxa_lvgl_ui_t *ui,
                                    const pxa_lvgl_ui_node_t *node);
static int refresh_alpha_plane(pxa_lvgl_ui_t *ui);

static void on_alpha_overlay_state_changed(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    if (node == NULL || node->object == NULL || node->ui->transaction_active ||
        !node_is_in_alpha_overlay(node->ui, node))
        return;
    (void)refresh_alpha_plane(node->ui);
}

static lv_obj_t *button_content(pxa_lvgl_ui_node_t *node) {
    uint32_t foreground;
    if (node->content != NULL) return node->content;
    node->content = lv_label_create(node->object);
    if (node->content == NULL) return NULL;
    lv_label_set_text(node->content, "");
    lv_obj_set_style_text_font(node->content,
                               font_for_role(node->ui, node->font_role), 0);
    foreground = resolved_color(node->ui, node->foreground_kind,
                                node->foreground_token, node->foreground);
    lv_obj_set_style_text_color(node->content, rgba_color(foreground), 0);
    lv_obj_set_style_text_opa(node->content, rgba_opa(foreground), 0);
    lv_obj_center(node->content);
    return node->content;
}

static void unlink_node(pxa_lvgl_ui_node_t *node) {
    pxa_lvgl_ui_t *ui = node->ui;
    if (node->previous != NULL)
        node->previous->next = node->next;
    else
        ui->nodes = node->next;
    if (node->next != NULL) node->next->previous = node->previous;
    if (ui->primary_root == node) ui->primary_root = NULL;
}

static void on_node_delete(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    if (node == NULL) return;
    if (node->range_scheduled) lv_async_call_cancel(report_visible_range, node);
    /* Deleting a parent recursively destroys children whose CREATE commands
     * have not yet been visited by cancellation or transaction cleanup. */
    if (node->owner_command != NULL) node->owner_command->owned.created = NULL;
    node->object = NULL;
    release_asset_source(node->ui, node->asset_source);
    node->asset_source = NULL;
    release_image(node->ui,node->image); node->image=NULL;
    if (node->canvas != NULL) {
        if (node->canvas->release != NULL && node->canvas->bytes != NULL)
            node->canvas->release(node->canvas->release_context,
                                  (void *)node->canvas->bytes);
        ui_release(node->ui, node->canvas->clip_stack);
        ui_release(node->ui, node->canvas->bitmap_images);
        release_canvas_assets(node->ui, node->canvas->assets);
        release_canvas_images(node->ui, node->canvas->images, node->canvas->image_count);
        ui_release(node->ui,node->canvas->spare_images);
        release_image_pool(node->ui,node->canvas);
        ui_release(node->ui, node->canvas);
        node->canvas = NULL;
    }
    ui_release(node->ui, node->grid_columns);
    ui_release(node->ui, node->grid_rows);
    node->grid_columns = NULL;
    node->grid_rows = NULL;
    unlink_node(node);
    ui_release(node->ui, node);
}

static lv_obj_t *create_object(pxa_lvgl_ui_node_t *node,
                               lv_obj_t *parent) {
    lv_obj_t *object;
    switch (node->type) {
        case PXA_UI_NODE_TEXT:
            object = lv_label_create(parent);
            break;
        case PXA_UI_NODE_IMAGE:
            object = lv_image_create(parent);
            break;
        case PXA_UI_NODE_CONTROL:
            if (node->subtype == PXA_UI_CONTROL_BUTTON)
                object = lv_button_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_TOGGLE)
                object = lv_switch_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                object = lv_slider_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT)
                object = lv_textarea_create(parent);
            else
                object = lv_dropdown_create(parent);
            break;
        case PXA_UI_NODE_PROGRESS:
            object = lv_bar_create(parent);
            break;
        default:
            object = lv_obj_create(parent);
            break;
    }
    if (object == NULL) return NULL;
    node->object = object;
    node->visible = 1;
    node->opacity = LV_OPA_COVER;
    node->composition = PXA_UI_COMPOSITION_BASE;
    node->foreground_kind = 0;
    node->foreground_token = 4;
    node->background_kind = 1;
    node->background = UINT32_C(0x00000000);
    node->border_kind = 1;
    node->border = UINT32_C(0x00000000);
    lv_obj_add_event_cb(object, on_node_delete, LV_EVENT_DELETE, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_CLICKED, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_VALUE_CHANGED, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_SCROLL, node);
    if (node->type == PXA_UI_NODE_CONTROL &&
        node->subtype == PXA_UI_CONTROL_TEXT_INPUT)
        lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_READY, node);
    lv_obj_add_event_cb(object, on_alpha_overlay_state_changed,
                        LV_EVENT_STATE_CHANGED, node);
    if (node->type == PXA_UI_NODE_CANVAS) {
        lv_obj_add_event_cb(object, on_canvas_draw, LV_EVENT_DRAW_MAIN, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSED, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSING, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_RELEASED, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESS_LOST, node);
    }
    lv_obj_set_hidden(object, true);
    /* A node only takes pointer input when the Guest subscribes to events, so
     * a tap on a label, an icon or any other child falls through to the
     * ancestor that does. LVGL enables CLICKABLE on every object by default. */
    lv_obj_set_clickable(object, false);
    /* Events bubble to the ancestors, so a container that subscribes to
     * scrolling or pointer input also observes a gesture that starts on one
     * of its children. The event carries the ancestor's node. */
    lv_obj_set_event_bubble(object, true);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(object, font_for_role(node->ui, 1), 0);
    if (node->type == PXA_UI_NODE_ROOT) {
        node->background_kind = 0;
        node->background_token = 0;
        lv_obj_set_scrollable(object, false);
        lv_obj_set_clickable(object, true);
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_size(object, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    /* Only scroll containers and text inputs own scrolling. Every other node
     * chains a drag to its nearest scrollable ancestor instead of rubber
     * banding its own content, which made list cards feel draggable. */
    if (node->type != PXA_UI_NODE_SCROLL &&
        node->type != PXA_UI_NODE_VIRTUAL_LIST &&
        !(node->type == PXA_UI_NODE_CONTROL &&
          node->subtype == PXA_UI_CONTROL_TEXT_INPUT)) {
        lv_obj_set_scrollable(object, false);
        lv_obj_set_scroll_elastic(object, false);
        lv_obj_set_scroll_momentum(object, false);
    }
    if (node->type == PXA_UI_NODE_VIRTUAL_LIST) {
        lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_SIZE_CHANGED, node);
        lv_obj_set_scroll_dir(object, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
        node->content = lv_obj_create(object);
        if (node->content == NULL) return NULL;
        lv_obj_set_clickable(node->content, false);
        /* An item's events reach the list through this carrier. */
        lv_obj_set_event_bubble(node->content, true);
        /* The list scrolls; its content carrier only carries the extent. */
        lv_obj_set_scrollable(node->content, false);
        lv_obj_set_style_border_width(node->content, 0, 0);
        lv_obj_set_style_pad_all(node->content, 0, 0);
        lv_obj_set_style_bg_opa(node->content, LV_OPA_TRANSP, 0);
        lv_obj_set_size(node->content, 1, 1);
        lv_obj_set_pos(node->content, 0, 0);
    }
    if (node->type == PXA_UI_NODE_CONTROL &&
        node->subtype == PXA_UI_CONTROL_BUTTON) {
        node->foreground_token = 3;
        node->background_kind = 0;
        node->background_token = 2;
        lv_obj_set_style_radius(object, 6, 0);
        lv_obj_set_style_pad_hor(object, 12, 0);
        lv_obj_set_style_pad_ver(object, 8, 0);
        lv_obj_set_style_shadow_width(object, 0, 0);
        lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
        lv_obj_set_style_recolor_opa(object, LV_OPA_TRANSP,
                                     LV_STATE_PRESSED);
    }
    node->minimum = 0;
    node->maximum = node->subtype == PXA_UI_CONTROL_TEXT_INPUT
                        ? PXA_UI_EVENT_TEXT_LEGACY_BYTES : 100;
    apply_node_colors(node);
    return object;
}

/* Grid layout: one 8-byte record per track (kind:u8 | reserved:u8[3] |
 * value:u32) and a u16[4] cell (column, row, column-span, row-span). Cell
 * alignment follows the node's align property. */
static int is_grid_command(const pxa_ui_command_view_t *view) {
    return (view->command == PXA_UI_COMMAND_SET_PROPERTY ||
            view->command == PXA_UI_COMMAND_CLEAR_PROPERTY) &&
           (view->property == PXA_UI_PROPERTY_GRID_COLUMNS ||
            view->property == PXA_UI_PROPERTY_GRID_ROWS);
}

/* Prepare outside execute: allocation failure must reject the whole commit,
 * not silently omit a property after the live tree has started changing. */
static pxa_status_t prepare_grid_tracks(pxa_lvgl_ui_t *ui,
                                        pxa_lvgl_ui_command_t *command) {
    pxa_bytes_t value = command->view.value;
    if (command->view.command == PXA_UI_COMMAND_CLEAR_PROPERTY)
        return PXA_STATUS_OK;
    pxa_status_t status = pxa_ui_validate_grid_tracks(value);
    if (status != PXA_STATUS_OK) return status;
    size_t count = value.size / 8u;
    lv_coord_t *tracks = ui_allocate(ui, (count + 1u) * sizeof(*tracks));
    if (!tracks) return PXA_STATUS_RESOURCE_LIMIT;
    for (size_t index = 0; index < count; ++index) {
        const uint8_t *record = value.data + index * 8u;
        uint32_t amount = pxa_read_u32(record + 4);
        tracks[index] = record[0] == 1u ? LV_GRID_FR((int32_t)amount)
                      : record[0] == 2u ? logical_pixels(ui, (int32_t)amount) : LV_GRID_CONTENT;
        /* Fixed sizes must not alias LVGL's content/fraction/end sentinels. */
        if (record[0] == PXA_UI_GRID_FIXED && tracks[index] >= LV_GRID_CONTENT) {
            ui_release(ui, tracks);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    tracks[count] = LV_GRID_TEMPLATE_LAST;
    command->owned.grid.tracks = tracks;
    command->owned.grid.has_tracks = 1;
    return PXA_STATUS_OK;
}

/* Retain the old descriptors until commit; LVGL borrows these pointers. A
 * failed transaction transfers the exact old allocation back to the node. */
static void swap_grid_tracks(pxa_lvgl_ui_command_t *command, int undo) {
    pxa_lvgl_ui_node_t *node = command->view.node_handle;
    int rows = command->view.property == PXA_UI_PROPERTY_GRID_ROWS;
    lv_coord_t **slot = rows ? &node->grid_rows : &node->grid_columns;
    uint8_t *flag = rows ? &node->has_grid_rows : &node->has_grid_columns;
    lv_coord_t *previous = *slot;
    uint8_t previous_flag = *flag;
    uint32_t previous_layout = lv_obj_get_style_layout(node->object, 0);
    *slot = command->owned.grid.tracks;
    *flag = command->owned.grid.has_tracks;
    command->owned.grid.tracks = previous;
    command->owned.grid.has_tracks = previous_flag;
    lv_obj_set_style_grid_column_dsc_array(node->object, node->grid_columns, 0);
    lv_obj_set_style_grid_row_dsc_array(node->object, node->grid_rows, 0);
    lv_obj_set_layout(node->object, undo ? command->owned.grid.layout :
        node->has_grid_columns && node->has_grid_rows ? LV_LAYOUT_GRID : LV_LAYOUT_NONE);
    command->owned.grid.layout = previous_layout;
}

static void set_grid_cell(pxa_lvgl_ui_node_t *node, pxa_bytes_t value) {
    static const lv_grid_align_t aligns[4] = {
        LV_GRID_ALIGN_START, LV_GRID_ALIGN_CENTER, LV_GRID_ALIGN_END,
        LV_GRID_ALIGN_STRETCH};
    uint16_t column;
    uint16_t row;
    uint16_t column_span;
    uint16_t row_span;
    lv_grid_align_t align;
    if (node == NULL || node->object == NULL || value.size != 8u) return;
    column = pxa_read_u16(value.data);
    row = pxa_read_u16(value.data + 2);
    column_span = pxa_read_u16(value.data + 4);
    row_span = pxa_read_u16(value.data + 6);
    if (column_span == 0) column_span = 1;
    if (row_span == 0) row_span = 1;
    align = aligns[node->align <= 3u ? node->align : 3u];
    lv_obj_set_grid_cell(node->object, align, column, column_span, align, row,
                         row_span);
}

static void apply_node_colors(pxa_lvgl_ui_node_t *node) {
    uint32_t fg = resolved_color(node->ui, node->foreground_kind,
                                 node->foreground_token, node->foreground);
    uint32_t bg = resolved_color(node->ui, node->background_kind,
                                 node->background_token, node->background);
    uint32_t border = resolved_color(node->ui, node->border_kind,
                                     node->border_token, node->border);
    lv_obj_set_style_text_color(node->object, rgba_color(fg), 0);
    lv_obj_set_style_text_opa(node->object, rgba_opa(fg), 0);
    if (node->type == PXA_UI_NODE_IMAGE) {
        lv_obj_set_style_image_recolor(node->object, rgba_color(fg), 0);
        lv_obj_set_style_image_recolor_opa(
            node->object, node->image_tint ? rgba_opa(fg) : LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_bg_color(node->object, rgba_color(bg), 0);
    lv_obj_set_style_bg_opa(node->object, rgba_opa(bg), 0);
    lv_obj_set_style_border_color(node->object, rgba_color(border), 0);
    lv_obj_set_style_border_opa(node->object, rgba_opa(border), 0);
    if (node->type == PXA_UI_NODE_CONTROL &&
        node->subtype == PXA_UI_CONTROL_BUTTON) {
        lv_obj_set_style_bg_color(node->object,
                                  lv_color_darken(rgba_color(bg), 32),
                                  LV_STATE_PRESSED);
    }
    if (node->content != NULL) {
        lv_obj_set_style_text_color(node->content, rgba_color(fg), 0);
        lv_obj_set_style_text_opa(node->content, rgba_opa(fg), 0);
    }
}

static lv_flex_align_t flex_align(uint8_t value) {
    switch (value) {
        case 1: return LV_FLEX_ALIGN_CENTER;
        case 2: return LV_FLEX_ALIGN_END;
        case 4: return LV_FLEX_ALIGN_SPACE_BETWEEN;
        case 5: return LV_FLEX_ALIGN_SPACE_AROUND;
        default: return LV_FLEX_ALIGN_START;
    }
}

static void apply_color_value(pxa_lvgl_ui_node_t *node,
                              pxa_ui_property_t property,
                              const uint8_t *value) {
    uint8_t kind = value[0];
    uint8_t token = value[1];
    uint32_t rgba = pxa_read_u32(value + 4);
    if (property == PXA_UI_PROPERTY_FOREGROUND) {
        node->foreground_kind = kind;
        node->foreground_token = token;
        node->foreground = rgba;
        node->image_tint = 1;
    } else if (property == PXA_UI_PROPERTY_BACKGROUND) {
        node->background_kind = kind;
        node->background_token = token;
        node->background = rgba;
    } else {
        node->border_kind = kind;
        node->border_token = token;
        node->border = rgba;
    }
    apply_node_colors(node);
}

/* The Guest owns the text it writes; only edits made through the Host input
 * path are reported as text events. */
static int property_is_guest_text_write(const pxa_lvgl_ui_node_t *node,
                                        uint16_t property) {
    return node->type == PXA_UI_NODE_CONTROL &&
           node->subtype == PXA_UI_CONTROL_TEXT_INPUT &&
           property == PXA_UI_PROPERTY_TEXT;
}

/* LVGL's mode setter also changes the input height. Preserve Guest layout. */
static void set_text_single_line(lv_obj_t *object, int single_line) {
    lv_style_value_t height;
    int present = lv_obj_get_local_style_prop(object, LV_STYLE_HEIGHT, &height, 0) == LV_STYLE_RES_FOUND;
    lv_textarea_set_one_line(object, single_line != 0);
    if (present) lv_obj_set_local_style_prop(object, LV_STYLE_HEIGHT, height, 0);
    else lv_obj_remove_local_style_prop(object, LV_STYLE_HEIGHT, 0);
}

static void apply_property(pxa_lvgl_ui_node_t *node,
                           pxa_ui_property_t property,
                           pxa_bytes_t value) {
    lv_obj_t *object = node->object;
    const uint8_t *data = value.data;
    int32_t scalar;
    if (object == NULL) return;
    if (property_is_guest_text_write(node, property))
        node->suppress_events = 1;
    switch (property) {
        case PXA_UI_PROPERTY_VISIBLE:
            node->visible = data[0];
            lv_obj_set_hidden(object, !node->visible);
            break;
        case PXA_UI_PROPERTY_ENABLED:
            if (data[0]) lv_obj_remove_state(object, LV_STATE_DISABLED);
            else lv_obj_add_state(object, LV_STATE_DISABLED);
            break;
        case PXA_UI_PROPERTY_EVENT_MASK:
            node->event_mask = pxa_read_u64(data);
            lv_obj_set_clickable(object, node->event_mask != 0);
            /* A node that subscribes to pointer events receives the same
             * samples a Canvas does: press, move, release and press-lost with
             * the position relative to the node. */
            if (node->type != PXA_UI_NODE_CANVAS)
                lv_obj_remove_event_cb_with_user_data(object, on_canvas_pointer, node);
            if (node->type != PXA_UI_NODE_CANVAS &&
                (node->event_mask & PXA_UI_EVENT_MASK_POINTER) != 0) {
                lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSED,
                                    node);
                lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSING,
                                    node);
                lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_RELEASED,
                                    node);
                lv_obj_add_event_cb(object, on_canvas_pointer,
                                    LV_EVENT_PRESS_LOST, node);
            }
            break;
        case PXA_UI_PROPERTY_WIDTH:
            lv_obj_set_width(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_HEIGHT:
            lv_obj_set_height(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_MIN_WIDTH:
            lv_obj_set_style_min_width(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MAX_WIDTH:
            lv_obj_set_style_max_width(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MIN_HEIGHT:
            lv_obj_set_style_min_height(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MAX_HEIGHT:
            lv_obj_set_style_max_height(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_LAYOUT:
            if (data[0] == PXA_UI_LAYOUT_ROW)
                lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
            else if (data[0] == PXA_UI_LAYOUT_COLUMN)
                lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
            else
                lv_obj_set_layout(object, LV_LAYOUT_NONE);
            break;
        case PXA_UI_PROPERTY_WRAP:
            break;
        case PXA_UI_PROPERTY_JUSTIFY:
            node->justify = data[0];
            lv_obj_set_flex_align(object, flex_align(node->justify),
                                  flex_align(node->align),
                                  flex_align(node->align));
            break;
        case PXA_UI_PROPERTY_ALIGN:
            node->align = data[0];
            lv_obj_set_flex_align(object, flex_align(node->justify),
                                  flex_align(node->align),
                                  flex_align(node->align));
            break;
        case PXA_UI_PROPERTY_ALIGN_SELF:
            if (data[0] == 3) lv_obj_set_width(object, LV_PCT(100));
            break;
        case PXA_UI_PROPERTY_GAP:
            scalar = logical_pixels(node->ui, (int32_t)pxa_read_u32(data));
            lv_obj_set_style_pad_row(object, scalar, 0);
            lv_obj_set_style_pad_column(object, scalar, 0);
            break;
        case PXA_UI_PROPERTY_PADDING:
            lv_obj_set_style_pad_left(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            lv_obj_set_style_pad_top(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 4)), 0);
            lv_obj_set_style_pad_right(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 8)), 0);
            lv_obj_set_style_pad_bottom(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 12)), 0);
            break;
        case PXA_UI_PROPERTY_GROW:
            lv_obj_set_flex_grow(object,
                pxa_read_u16(data) > UINT8_MAX ? UINT8_MAX : pxa_read_u16(data));
            break;
        case PXA_UI_PROPERTY_SHRINK:
            break;
        case PXA_UI_PROPERTY_POSITION:
            if (data[0]) {
                /* Absolute children leave layout, but still scroll with their
                 * parent. FLOATING would pin virtual rows to the viewport. */
                lv_obj_set_ignore_layout(object, true);
                lv_obj_move_foreground(object);
            } else {
                lv_obj_set_ignore_layout(object, false);
            }
            break;
        case PXA_UI_PROPERTY_X:
            lv_obj_set_x(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_Y:
            lv_obj_set_y(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_FOREGROUND:
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
            apply_color_value(node, property, data);
            break;
        case PXA_UI_PROPERTY_OPACITY:
            node->opacity = data[0];
            lv_obj_set_style_opa(object,
                                 node->composition ==
                                         PXA_UI_COMPOSITION_ALPHA_OVERLAY
                                     ? LV_OPA_TRANSP : node->opacity,
                                 0);
            break;
        case PXA_UI_PROPERTY_COMPOSITION:
            node->composition = data[0];
            lv_obj_set_style_opa(object,
                                 node->composition ==
                                         PXA_UI_COMPOSITION_ALPHA_OVERLAY
                                     ? LV_OPA_TRANSP : node->opacity,
                                 0);
            break;
        case PXA_UI_PROPERTY_RADIUS:
            lv_obj_set_style_radius(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            break;
        case PXA_UI_PROPERTY_BORDER_WIDTH:
            lv_obj_set_style_border_width(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            break;
        case PXA_UI_PROPERTY_FONT_ROLE:
            node->font_role = (uint8_t)pxa_read_u16(data);
            lv_obj_set_style_text_font(object,
                                       font_for_role(node->ui, node->font_role), 0);
            if (node->content != NULL)
                lv_obj_set_style_text_font(
                    node->content, font_for_role(node->ui, node->font_role), 0);
            break;
        case PXA_UI_PROPERTY_TEXT_ALIGN:
            lv_obj_set_style_text_align(object,
                data[0] == 1 ? LV_TEXT_ALIGN_CENTER
                : data[0] == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
            break;
        case PXA_UI_PROPERTY_TEXT:
            if (node->type == PXA_UI_NODE_TEXT)
                lv_label_set_text(object, value.size == 0 ? "" : (const char *)data);
            else if (node->subtype == PXA_UI_CONTROL_BUTTON) {
                lv_obj_t *content = button_content(node);
                if (content != NULL)
                    lv_label_set_text(content,
                                      value.size == 0 ? "" : (const char *)data);
            }
            else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
                const char *current = lv_textarea_get_text(object);
                /* State bindings echo user edits. Avoid another allocation,
                 * layout pass and cursor reset when Host already has them. */
                if (strlen(current) != value.size ||
                    (value.size != 0 && memcmp(current, data, value.size) != 0))
                    lv_textarea_set_text(object,
                                         value.size == 0 ? "" : (const char *)data);
            }
            else if (node->subtype == PXA_UI_CONTROL_SELECTION)
                lv_dropdown_set_options(object,
                                        value.size == 0 ? "" : (const char *)data);
            break;
        case PXA_UI_PROPERTY_TEXT_SINGLE_LINE:
            set_text_single_line(object, data[0]);
            break;
        case PXA_UI_PROPERTY_TEXT_MAX_BYTES:
            node->maximum = (int32_t)pxa_read_u32(data);
            break;
        case PXA_UI_PROPERTY_ICON:
            node->font_role = 3;
            if (node->type == PXA_UI_NODE_TEXT) {
                lv_obj_set_style_text_font(
                    object, font_for_role(node->ui, node->font_role), 0);
                lv_label_set_text(object, icon_text(pxa_read_u32(data)));
            } else if (node->subtype == PXA_UI_CONTROL_BUTTON) {
                lv_obj_t *content = button_content(node);
                if (content != NULL) {
                    lv_obj_set_style_text_font(
                        content, font_for_role(node->ui, node->font_role), 0);
                    lv_label_set_text(content, icon_text(pxa_read_u32(data)));
                }
            }
            break;
        case PXA_UI_PROPERTY_IMAGE_FIT:
            lv_image_set_inner_align(object,
                data[0] == PXA_UI_IMAGE_FIT_STRETCH ? LV_IMAGE_ALIGN_STRETCH
                : data[0] == PXA_UI_IMAGE_FIT_COVER ? LV_IMAGE_ALIGN_COVER
                                                    : LV_IMAGE_ALIGN_CONTAIN);
            break;
        case PXA_UI_PROPERTY_VALUE:
            scalar = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_value(object, scalar, LV_ANIM_OFF);
            else if (node->subtype == PXA_UI_CONTROL_TOGGLE) {
                if (scalar) lv_obj_add_state(object, LV_STATE_CHECKED);
                else lv_obj_remove_state(object, LV_STATE_CHECKED);
            } else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_value(object, scalar, LV_ANIM_OFF);
            break;
        case PXA_UI_PROPERTY_MIN_VALUE:
            node->minimum = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_range(object, node->minimum, node->maximum);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_range(object, node->minimum, node->maximum);
            break;
        case PXA_UI_PROPERTY_MAX_VALUE:
            node->maximum = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_range(object, node->minimum, node->maximum);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_range(object, node->minimum, node->maximum);
            break;
        case PXA_UI_PROPERTY_STEP:
            break;
        case PXA_UI_PROPERTY_SCROLL_AXIS:
            lv_obj_set_scroll_dir(object,
                data[0] == 1 ? LV_DIR_HOR : data[0] == 2 ? LV_DIR_VER
                : data[0] == 3 ? LV_DIR_ALL : LV_DIR_NONE);
            break;
        case PXA_UI_PROPERTY_SCROLLBAR:
            lv_obj_set_scrollbar_mode(object,
                data[0] == 0 ? LV_SCROLLBAR_MODE_OFF
                : data[0] == 1 ? LV_SCROLLBAR_MODE_AUTO
                               : LV_SCROLLBAR_MODE_ACTIVE);
            break;
        case PXA_UI_PROPERTY_GRID_CELL:
            set_grid_cell(node, value);
            break;
        case PXA_UI_PROPERTY_SCROLL_POSITION:
            node->scroll_position = (int32_t)pxa_read_u32(data);
            node->has_scroll_position = 1;
            break;
        case PXA_UI_PROPERTY_ITEM_COUNT:
            node->item_count = pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_VIRTUAL_LIST &&
                node->content != NULL) {
                int64_t total = (int64_t)node->item_count * node->item_extent;
                if (total < 1) total = 1;
                if (total > LV_COORD_MAX) total = LV_COORD_MAX;
                lv_obj_set_height(node->content, (int32_t)total);
                schedule_visible_range(node);
            }
            break;
        case PXA_UI_PROPERTY_ITEM_EXTENT:
            node->item_extent = logical_pixels(
                node->ui, (int32_t)pxa_read_u32(data));
            if (node->type == PXA_UI_NODE_VIRTUAL_LIST &&
                node->content != NULL) {
                int64_t total = (int64_t)node->item_count * node->item_extent;
                if (total < 1) total = 1;
                if (total > LV_COORD_MAX) total = LV_COORD_MAX;
                lv_obj_set_height(node->content, (int32_t)total);
                schedule_visible_range(node);
            }
            break;
        default:
            break;
    }
    node->suppress_events = 0;
}

static void clear_property(pxa_lvgl_ui_node_t *node,
                           pxa_ui_property_t property) {
    uint8_t value[16] = {0};
    switch (property) {
        case PXA_UI_PROPERTY_TEXT_SINGLE_LINE:
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_TEXT_MAX_BYTES:
            pxa_write_u32(value, PXA_UI_EVENT_TEXT_LEGACY_BYTES);
            apply_property(node, property, (pxa_bytes_t){value, 4});
            break;
        case PXA_UI_PROPERTY_VISIBLE:
        case PXA_UI_PROPERTY_ENABLED:
            value[0] = 1;
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_EVENT_MASK:
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_WIDTH:
        case PXA_UI_PROPERTY_HEIGHT:
        case PXA_UI_PROPERTY_MIN_WIDTH:
        case PXA_UI_PROPERTY_MAX_WIDTH:
        case PXA_UI_PROPERTY_MIN_HEIGHT:
        case PXA_UI_PROPERTY_MAX_HEIGHT:
        case PXA_UI_PROPERTY_X:
        case PXA_UI_PROPERTY_Y:
            value[0] = node->type == PXA_UI_NODE_ROOT
                           ? PXA_UI_LENGTH_FILL
                           : PXA_UI_LENGTH_CONTENT;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_FOREGROUND:
            value[1] = 4;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            node->image_tint = 0;
            apply_node_colors(node);
            break;
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
            value[0] = 1;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_OPACITY:
            value[0] = 255;
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_COMPOSITION:
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_PADDING:
            apply_property(node, property, (pxa_bytes_t){value, 16});
            break;
        case PXA_UI_PROPERTY_TEXT:
            apply_property(node, property, (pxa_bytes_t){value, 0});
            break;
        default:
            apply_property(node, property, (pxa_bytes_t){value, 4});
            break;
    }
}

/* Property undo is transient and only needed for nodes that predate this
 * transaction. Local-style presence matters: inherited values are not local
 * overrides and must remain inherited after a failed commit. */
/* This range contains scalar model state only. Ownership fields (image,
 * canvas, grid descriptors and transaction links) are deliberately excluded. */
#define NODE_STATE_OFFSET offsetof(pxa_lvgl_ui_node_t, event_mask)
#define NODE_STATE_BYTES (offsetof(pxa_lvgl_ui_node_t, grid_columns) - NODE_STATE_OFFSET)

typedef struct {
    lv_obj_t *object;
    lv_style_value_t value;
    lv_style_selector_t selector;
    lv_style_prop_t property;
    uint8_t content;
    uint8_t present;
} property_style_t;

struct pxa_lvgl_ui_property_undo {
    uint8_t state[NODE_STATE_BYTES];
    char *text;
    lv_obj_t *content;
    lv_state_t object_state;
    lv_obj_flag_t flags;
    int32_t index;
    int32_t scroll_x, scroll_y;
    int32_t value, start_value;
    uint32_t cursor;
    uint32_t selection_start, selection_end;
    uint16_t selected;
    lv_dir_t scroll_dir;
    lv_scrollbar_mode_t scrollbar;
    lv_image_align_t image_align;
    uint8_t applied;
    uint8_t style_count;
    property_style_t styles[];
};

/* Entries cover side effects of the setters, not just the named property. */
static unsigned property_styles(uint16_t property, property_style_t *styles) {
    unsigned count = 0;
#define STYLE(prop, child, state) do { \
    styles[count++] = (property_style_t){.property=(prop), .content=(child), .selector=(state)}; \
} while (0)
#define MAIN(prop) STYLE(prop, 0, 0)
    switch (property) {
        case PXA_UI_PROPERTY_WIDTH: case PXA_UI_PROPERTY_ALIGN_SELF: MAIN(LV_STYLE_WIDTH); break;
        case PXA_UI_PROPERTY_HEIGHT: MAIN(LV_STYLE_HEIGHT); break;
        case PXA_UI_PROPERTY_MIN_WIDTH: MAIN(LV_STYLE_MIN_WIDTH); break;
        case PXA_UI_PROPERTY_MAX_WIDTH: MAIN(LV_STYLE_MAX_WIDTH); break;
        case PXA_UI_PROPERTY_MIN_HEIGHT: MAIN(LV_STYLE_MIN_HEIGHT); break;
        case PXA_UI_PROPERTY_MAX_HEIGHT: MAIN(LV_STYLE_MAX_HEIGHT); break;
        case PXA_UI_PROPERTY_X: MAIN(LV_STYLE_X); break;
        case PXA_UI_PROPERTY_Y: MAIN(LV_STYLE_Y); break;
        case PXA_UI_PROPERTY_LAYOUT: MAIN(LV_STYLE_LAYOUT); MAIN(LV_STYLE_FLEX_FLOW); break;
        case PXA_UI_PROPERTY_ALIGN: case PXA_UI_PROPERTY_JUSTIFY:
            MAIN(LV_STYLE_FLEX_MAIN_PLACE); MAIN(LV_STYLE_FLEX_CROSS_PLACE); MAIN(LV_STYLE_FLEX_TRACK_PLACE); break;
        case PXA_UI_PROPERTY_GAP: MAIN(LV_STYLE_PAD_ROW); MAIN(LV_STYLE_PAD_COLUMN); break;
        case PXA_UI_PROPERTY_PADDING:
            MAIN(LV_STYLE_PAD_LEFT); MAIN(LV_STYLE_PAD_TOP); MAIN(LV_STYLE_PAD_RIGHT); MAIN(LV_STYLE_PAD_BOTTOM); break;
        case PXA_UI_PROPERTY_GROW: MAIN(LV_STYLE_FLEX_GROW); break;
        case PXA_UI_PROPERTY_FOREGROUND: case PXA_UI_PROPERTY_BACKGROUND: case PXA_UI_PROPERTY_BORDER_COLOR:
            MAIN(LV_STYLE_TEXT_COLOR); MAIN(LV_STYLE_TEXT_OPA);
            MAIN(LV_STYLE_IMAGE_RECOLOR); MAIN(LV_STYLE_IMAGE_RECOLOR_OPA);
            MAIN(LV_STYLE_BG_COLOR); MAIN(LV_STYLE_BG_OPA);
            MAIN(LV_STYLE_BORDER_COLOR); MAIN(LV_STYLE_BORDER_OPA);
            STYLE(LV_STYLE_BG_COLOR, 0, LV_STATE_PRESSED);
            STYLE(LV_STYLE_TEXT_COLOR, 1, 0); STYLE(LV_STYLE_TEXT_OPA, 1, 0); break;
        case PXA_UI_PROPERTY_OPACITY: case PXA_UI_PROPERTY_COMPOSITION: MAIN(LV_STYLE_OPA); break;
        case PXA_UI_PROPERTY_RADIUS: MAIN(LV_STYLE_RADIUS); break;
        case PXA_UI_PROPERTY_BORDER_WIDTH: MAIN(LV_STYLE_BORDER_WIDTH); break;
        case PXA_UI_PROPERTY_FONT_ROLE: case PXA_UI_PROPERTY_ICON:
            MAIN(LV_STYLE_TEXT_FONT); STYLE(LV_STYLE_TEXT_FONT, 1, 0); break;
        case PXA_UI_PROPERTY_TEXT_ALIGN: MAIN(LV_STYLE_TEXT_ALIGN); break;
        case PXA_UI_PROPERTY_GRID_CELL:
            MAIN(LV_STYLE_GRID_CELL_COLUMN_POS); MAIN(LV_STYLE_GRID_CELL_COLUMN_SPAN);
            MAIN(LV_STYLE_GRID_CELL_ROW_POS); MAIN(LV_STYLE_GRID_CELL_ROW_SPAN);
            MAIN(LV_STYLE_GRID_CELL_X_ALIGN); MAIN(LV_STYLE_GRID_CELL_Y_ALIGN); break;
        case PXA_UI_PROPERTY_ITEM_COUNT: case PXA_UI_PROPERTY_ITEM_EXTENT:
            STYLE(LV_STYLE_HEIGHT, 1, 0); break;
        default: break;
    }
#undef MAIN
#undef STYLE
    return count;
}

static int is_plain_property(const pxa_ui_command_view_t *view) {
    return (view->command == PXA_UI_COMMAND_SET_PROPERTY ||
            view->command == PXA_UI_COMMAND_CLEAR_PROPERTY) &&
           view->property != PXA_UI_PROPERTY_IMAGE_HANDLE &&
           view->property != PXA_UI_PROPERTY_ASSET && !is_grid_command(view);
}

static pxa_status_t prepare_property_undo(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_command_t *command) {
    pxa_lvgl_ui_node_t *node = command->view.node_handle;
    if (!node || node->owner_command) return PXA_STATUS_OK;
    property_style_t styles[11];
    unsigned count = property_styles(command->view.property, styles);
    if (!count) {
        switch (command->view.property) {
            case PXA_UI_PROPERTY_VISIBLE: case PXA_UI_PROPERTY_ENABLED:
            case PXA_UI_PROPERTY_EVENT_MASK: case PXA_UI_PROPERTY_POSITION:
            case PXA_UI_PROPERTY_TEXT: case PXA_UI_PROPERTY_IMAGE_FIT:
            case PXA_UI_PROPERTY_VALUE: case PXA_UI_PROPERTY_MIN_VALUE: case PXA_UI_PROPERTY_MAX_VALUE:
            case PXA_UI_PROPERTY_TEXT_SINGLE_LINE: case PXA_UI_PROPERTY_TEXT_MAX_BYTES:
            case PXA_UI_PROPERTY_SCROLL_AXIS: case PXA_UI_PROPERTY_SCROLLBAR:
            case PXA_UI_PROPERTY_SCROLL_POSITION: break;
            default: return PXA_STATUS_OK;
        }
    }
    pxa_lvgl_ui_property_undo_t *undo = ui_allocate(ui, sizeof(*undo) + count * sizeof(*styles));
    if (!undo) return PXA_STATUS_RESOURCE_LIMIT;
    memset(undo, 0, sizeof(*undo));
    undo->style_count = count;
    memcpy(undo->styles, styles, count * sizeof(*styles));
    command->owned.property = undo;
    return PXA_STATUS_OK;
}

static pxa_status_t capture_property_undo(pxa_lvgl_ui_command_t *command) {
    pxa_lvgl_ui_property_undo_t *undo = command->owned.property;
    if (!undo) return PXA_STATUS_OK;
    pxa_lvgl_ui_node_t *node = command->view.node_handle;
    lv_obj_t *object = node->object;
    if (command->view.command == PXA_UI_COMMAND_SET_PROPERTY &&
        command->view.property == PXA_UI_PROPERTY_TEXT &&
        node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
        const char *current = lv_textarea_get_text(object);
        if (strlen(current) == command->view.value.size &&
            (command->view.value.size == 0 ||
             !memcmp(current, command->view.value.data, command->view.value.size)))
            return PXA_STATUS_OK;
    }
    memcpy(undo->state, (uint8_t *)node + NODE_STATE_OFFSET, NODE_STATE_BYTES);
    undo->content = node->content;
    undo->object_state = lv_obj_get_state(object);
    undo->flags = (lv_obj_is_hidden(object) ? LV_OBJ_FLAG_HIDDEN : 0) |
                  (lv_obj_is_floating(object) ? LV_OBJ_FLAG_FLOATING : 0) |
                  (lv_obj_is_ignore_layout(object) ? LV_OBJ_FLAG_IGNORE_LAYOUT : 0) |
                  (lv_obj_is_clickable(object) ? LV_OBJ_FLAG_CLICKABLE : 0);
    undo->index = lv_obj_get_index(object);
    undo->scroll_x = lv_obj_get_scroll_x(object);
    undo->scroll_y = lv_obj_get_scroll_y(object);
    undo->scroll_dir = lv_obj_get_scroll_dir(object);
    undo->scrollbar = lv_obj_get_scrollbar_mode(object);
    for (unsigned i = 0; i < undo->style_count; ++i) {
        property_style_t *style = &undo->styles[i];
        style->object = style->content ? node->content : object;
        style->present = style->object && lv_obj_get_local_style_prop(
            style->object, style->property, &style->value, style->selector) == LV_STYLE_RES_FOUND;
    }
    uint16_t property = command->view.property;
    if (property == PXA_UI_PROPERTY_TEXT_SINGLE_LINE) undo->selected = lv_textarea_get_one_line(object);
    if (property == PXA_UI_PROPERTY_TEXT || property == PXA_UI_PROPERTY_ICON) {
        const char *text = NULL;
        if (node->type == PXA_UI_NODE_TEXT) text = lv_label_get_text(object);
        else if (node->subtype == PXA_UI_CONTROL_BUTTON && node->content)
            text = lv_label_get_text(node->content);
        else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
            text = lv_textarea_get_text(object);
            undo->cursor = lv_textarea_get_cursor_pos(object);
            lv_obj_t *label = lv_textarea_get_label(object);
            undo->selection_start = lv_label_get_text_selection_start(label);
            undo->selection_end = lv_label_get_text_selection_end(label);
        } else if (node->subtype == PXA_UI_CONTROL_SELECTION) {
            text = lv_dropdown_get_options(object);
            undo->selected = lv_dropdown_get_selected(object);
        }
        if (text) {
            size_t size = strlen(text) + 1;
            undo->text = ui_allocate(node->ui, size);
            if (!undo->text) return PXA_STATUS_RESOURCE_LIMIT;
            memcpy(undo->text, text, size);
        }
    }
    if (property == PXA_UI_PROPERTY_VALUE || property == PXA_UI_PROPERTY_MIN_VALUE || property == PXA_UI_PROPERTY_MAX_VALUE) {
        if (node->type == PXA_UI_NODE_PROGRESS || node->subtype == PXA_UI_CONTROL_SLIDER) {
            undo->value = lv_bar_get_value(object);
            undo->start_value = lv_bar_get_start_value(object);
        }
    }
    if (property == PXA_UI_PROPERTY_IMAGE_FIT) undo->image_align = lv_image_get_inner_align(object);
    undo->applied = 1;
    return PXA_STATUS_OK;
}

static void rollback_property(pxa_lvgl_ui_command_t *command) {
    pxa_lvgl_ui_property_undo_t *undo = command->owned.property;
    if (!undo || !undo->applied) return;
    pxa_lvgl_ui_node_t *node = command->view.node_handle;
    lv_obj_t *object = node->object;
    uint16_t property = command->view.property;
    memcpy((uint8_t *)node + NODE_STATE_OFFSET, undo->state, NODE_STATE_BYTES);
    if (property == PXA_UI_PROPERTY_TEXT_SINGLE_LINE) set_text_single_line(object, undo->selected);
    if (property == PXA_UI_PROPERTY_TEXT || property == PXA_UI_PROPERTY_ICON) {
        if (undo->text) apply_property(node, PXA_UI_PROPERTY_TEXT,
            (pxa_bytes_t){(uint8_t *)undo->text, strlen(undo->text)});
        if (!undo->content && node->content && node->subtype == PXA_UI_CONTROL_BUTTON) {
            lv_obj_delete(node->content);
            node->content = NULL;
        }
        if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT) {
            lv_textarea_set_cursor_pos(object, undo->cursor);
            lv_obj_t *label = lv_textarea_get_label(object);
            lv_label_set_text_selection_start(label, undo->selection_start);
            lv_label_set_text_selection_end(label, undo->selection_end);
        }
        if (node->subtype == PXA_UI_CONTROL_SELECTION) lv_dropdown_set_selected(object, undo->selected);
    }
    if (property == PXA_UI_PROPERTY_EVENT_MASK) {
        uint8_t value[8]; pxa_write_u64(value, node->event_mask);
        apply_property(node, PXA_UI_PROPERTY_EVENT_MASK, (pxa_bytes_t){value, 8});
    }
    if (property == PXA_UI_PROPERTY_VALUE || property == PXA_UI_PROPERTY_MIN_VALUE || property == PXA_UI_PROPERTY_MAX_VALUE) {
        if (node->type == PXA_UI_NODE_PROGRESS || node->subtype == PXA_UI_CONTROL_SLIDER) {
            lv_bar_set_range(object, node->minimum, node->maximum);
            lv_bar_set_value(object, undo->value, LV_ANIM_OFF);
            lv_bar_set_start_value(object, undo->start_value, LV_ANIM_OFF);
        }
    }
    for (unsigned i = 0; i < undo->style_count; ++i) {
        property_style_t *style = &undo->styles[i];
        if (!style->object) continue;
        if (style->present) lv_obj_set_local_style_prop(style->object, style->property, style->value, style->selector);
        else lv_obj_remove_local_style_prop(style->object, style->property, style->selector);
    }
    if (property == PXA_UI_PROPERTY_IMAGE_FIT) lv_image_set_inner_align(object, undo->image_align);
    lv_obj_set_hidden(object, (undo->flags & LV_OBJ_FLAG_HIDDEN) != 0);
    lv_obj_set_floating(object, (undo->flags & LV_OBJ_FLAG_FLOATING) != 0);
    lv_obj_set_ignore_layout(object, (undo->flags & LV_OBJ_FLAG_IGNORE_LAYOUT) != 0);
    lv_obj_set_clickable(object, (undo->flags & LV_OBJ_FLAG_CLICKABLE) != 0);
    lv_obj_remove_state(object, lv_obj_get_state(object) & ~undo->object_state);
    lv_obj_add_state(object, undo->object_state);
    if (property == PXA_UI_PROPERTY_POSITION) lv_obj_move_to_index(object, undo->index);
    lv_obj_set_scroll_dir(object, undo->scroll_dir);
    lv_obj_set_scrollbar_mode(object, undo->scrollbar);
    lv_obj_scroll_to(object, undo->scroll_x, undo->scroll_y, LV_ANIM_OFF);
    undo->applied = 0;
}

static void free_property_undo(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_property_undo_t *undo) {
    if (!undo) return;
    ui_release(ui, undo->text);
    ui_release(ui, undo);
}

static void link_node(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_node_t *node) {
    node->next = ui->nodes;
    if (ui->nodes != NULL) ui->nodes->previous = node;
    ui->nodes = node;
}

static uint8_t expand5(uint16_t value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(uint16_t value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static uint16_t pack565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) | (blue >> 3));
}

static int overlay_has_overlay_parent(const pxa_lvgl_ui_t *ui,
                                      const pxa_lvgl_ui_node_t *node) {
    const lv_obj_t *parent = lv_obj_get_parent(node->object);
    for (; parent != NULL; parent = lv_obj_get_parent(parent)) {
        const pxa_lvgl_ui_node_t *candidate;
        for (candidate = ui->nodes; candidate != NULL;
             candidate = candidate->next) {
            if (candidate->object == parent &&
                candidate->composition == PXA_UI_COMPOSITION_ALPHA_OVERLAY)
                return 1;
        }
    }
    return 0;
}

static int node_is_in_alpha_overlay(const pxa_lvgl_ui_t *ui,
                                    const pxa_lvgl_ui_node_t *node) {
    return node->composition == PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
           overlay_has_overlay_parent(ui, node);
}

/* Alpha-overlay objects remain present for LVGL hit testing, but their visual
 * output is supplied only by the compositor's alpha plane. */
static void set_alpha_overlay_base_visibility(pxa_lvgl_ui_t *ui,
                                              int snapshot_visible) {
    pxa_lvgl_ui_node_t *node;
    for (node = ui->nodes; node != NULL; node = node->next) {
        if (node->object == NULL) continue;
        int in_overlay = node_is_in_alpha_overlay(ui, node);
        if (!in_overlay && !node->alpha_hidden) continue;
        if (snapshot_visible || !in_overlay) {
            node->alpha_hidden = 0;
            lv_obj_set_style_opa(node->object, node->opacity, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_OUTLINE_OPA, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_SHADOW_OPA, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_IMAGE_OPA, 0);
            apply_node_colors(node);
            continue;
        }
        node->alpha_hidden = 1;
        lv_obj_set_style_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_image_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_opa(node->object, LV_OPA_TRANSP, 0);
        if (node->content != NULL)
            lv_obj_set_style_text_opa(node->content, LV_OPA_TRANSP, 0);
    }
}

static void clear_alpha_plane(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_alpha_t *alpha) {
    ui_release(ui, alpha->pixels);
    ui_release(ui, alpha->values);
    memset(alpha, 0, sizeof(*alpha));
}

static void clear_snapshot(pxa_lvgl_ui_t *ui) {
    ui_release(ui, ui->snapshot_memory);
    ui->snapshot_memory = NULL;
    ui->snapshot_capacity = 0;
}

static size_t display_buffer_limit(const pxa_lvgl_ui_t *ui, int snapshot) {
    if (snapshot && ui->config.snapshot_limit_bytes) return ui->config.snapshot_limit_bytes;
    if (!snapshot && ui->config.alpha_limit_bytes) return ui->config.alpha_limit_bytes;
    uint32_t w = ui->primary_environment.width, h = ui->primary_environment.height;
    if (!w || !h || w > UINT16_MAX || h > UINT16_MAX) return 0;
    uint64_t bytes = snapshot ?
        (uint64_t)lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_ARGB8888) * h + LV_DRAW_BUF_ALIGN - 1u :
        (uint64_t)w * h * 3u;
    return bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
}

static size_t snapshot_bytes(lv_obj_t *object) {
    int64_t ext = lv_obj_get_ext_draw_size(object);
    int64_t width = (int64_t)lv_obj_get_width(object) + 2 * ext;
    int64_t height = (int64_t)lv_obj_get_height(object) + 2 * ext;
    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) return 0;
    uint64_t bytes = (uint64_t)lv_draw_buf_width_to_stride((uint32_t)width, LV_COLOR_FORMAT_ARGB8888) * height;
    return bytes > UINT32_MAX || bytes > SIZE_MAX ? 0 : (size_t)bytes;
}

/* The draw buffer descriptor lives on the stack. Snapshot drawing completes
 * synchronously; it never owns or frees the tracked, aligned pixel storage. */
static lv_draw_buf_t *take_snapshot(lv_obj_t *object, void *memory, size_t capacity,
                                    lv_draw_buf_t *draw) {
    uintptr_t aligned = ((uintptr_t)memory + LV_DRAW_BUF_ALIGN - 1u) /
                         LV_DRAW_BUF_ALIGN * LV_DRAW_BUF_ALIGN;
    if (lv_draw_buf_init(draw, 1, 1, LV_COLOR_FORMAT_ARGB8888, 0,
                        (void *)aligned, (uint32_t)capacity) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(object, LV_COLOR_FORMAT_ARGB8888, draw) != LV_RESULT_OK)
        return NULL;
    return draw;
}

/* Return 1 for reused storage, 2 for newly allocated storage, 0 on failure. */
static int prepare_alpha_plane(pxa_lvgl_ui_t *ui, const lv_area_t *area, int staging) {
    const int32_t width = lv_area_get_width(area);
    const int32_t height = lv_area_get_height(area);
    const size_t pixels = (size_t)width * height;
    uint16_t *colors;
    uint8_t *alpha;
    if (width <= 0 || height <= 0 || width > UINT16_MAX ||
        height > UINT16_MAX || pixels > SIZE_MAX / sizeof(*colors) ||
        pixels > display_buffer_limit(ui, 0) / 3u)
        return 0;
    if (ui->alpha.pixels != NULL && ui->alpha.values != NULL &&
        ui->alpha.width == (uint16_t)width &&
        ui->alpha.height == (uint16_t)height) {
        ui->alpha.x = area->x1;
        ui->alpha.y = area->y1;
        memset(ui->alpha.pixels, 0, pixels * sizeof(*colors));
        memset(ui->alpha.values, 0, pixels);
        return 1;
    }
    /* Only staging is disposable before replacement succeeds. Release it
     * here, using the final computed geometry, to avoid a third live plane. */
    if (staging) clear_alpha_plane(ui, &ui->alpha);
    colors = ui_allocate(ui, pixels * sizeof(*colors));
    alpha = ui_allocate(ui, pixels);
    if (colors == NULL || alpha == NULL) {
        ui_release(ui, colors);
        ui_release(ui, alpha);
        return 0;
    }
    clear_alpha_plane(ui, &ui->alpha);
    ui->alpha.pixels = colors;
    ui->alpha.values = alpha;
    ui->alpha.x = area->x1;
    ui->alpha.y = area->y1;
    ui->alpha.width = (uint16_t)width;
    ui->alpha.height = (uint16_t)height;
    memset(colors, 0, pixels * sizeof(*colors));
    memset(alpha, 0, pixels);
    return 2;
}

static void alpha_plane_blend_pixel(pxa_lvgl_ui_t *ui, size_t index,
                                    lv_color32_t source) {
    const uint8_t source_alpha = source.alpha;
    const uint8_t destination_alpha = ui->alpha.values[index];
    const uint16_t destination = ui->alpha.pixels[index];
    const uint16_t inverse = (uint16_t)(255u - source_alpha);
    const uint16_t output_alpha = (uint16_t)(source_alpha +
        (destination_alpha * inverse + 127u) / 255u);
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    if (source_alpha == 0) return;
    if (output_alpha == 0) return;
    red = (uint32_t)source.red * source_alpha +
          ((uint32_t)expand5(destination >> 11) * destination_alpha *
           inverse + 127u) / 255u;
    green = (uint32_t)source.green * source_alpha +
            ((uint32_t)expand6((destination >> 5) & 0x3fu) *
             destination_alpha * inverse + 127u) / 255u;
    blue = (uint32_t)source.blue * source_alpha +
           ((uint32_t)expand5(destination & 0x1fu) * destination_alpha *
            inverse + 127u) / 255u;
    ui->alpha.pixels[index] = pack565((uint8_t)(red / output_alpha),
                                      (uint8_t)(green / output_alpha),
                                      (uint8_t)(blue / output_alpha));
    ui->alpha.values[index] = (uint8_t)output_alpha;
}

static int render_alpha_plane(pxa_lvgl_ui_t *ui, int staging, int *created_plane) {
    pxa_lvgl_ui_node_t *node;
    lv_area_t union_area = {0};
    uint16_t content_min_x = UINT16_MAX;
    uint16_t content_min_y = UINT16_MAX;
    uint16_t content_max_x = 0;
    uint16_t content_max_y = 0;
    unsigned found = 0;
    pxa_lvgl_ui_node_t *only_node = NULL;
    lv_draw_buf_t draw;
    lv_draw_buf_t *prepared_snapshot = NULL;
    void *candidate = NULL;
    void *memory = ui->snapshot_memory;
    size_t capacity = ui->snapshot_capacity;
    size_t required = 0;
    size_t limit = display_buffer_limit(ui, 1);
    int result = 1;
    if (created_plane) *created_plane = 0;
    set_alpha_overlay_base_visibility(ui, 1);
    for (node = ui->nodes; node != NULL; node = node->next) {
        lv_area_t area;
        if (node->object == NULL || !node->visible ||
            node->composition != PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
            overlay_has_overlay_parent(ui, node))
            continue;
        size_t bytes = snapshot_bytes(node->object);
        if (!bytes || limit < LV_DRAW_BUF_ALIGN - 1u || bytes > limit - (LV_DRAW_BUF_ALIGN - 1u)) {
            result = 0;
            goto finish_snapshot;
        }
        if (bytes > required) required = bytes;
        lv_obj_get_coords(node->object, &area);
        if (!found) union_area = area;
        else {
            if (area.x1 < union_area.x1) union_area.x1 = area.x1;
            if (area.y1 < union_area.y1) union_area.y1 = area.y1;
            if (area.x2 > union_area.x2) union_area.x2 = area.x2;
            if (area.y2 > union_area.y2) union_area.y2 = area.y2;
        }
        ++found;
        only_node = node;
    }
    if (!found) {
        clear_alpha_plane(ui, &ui->alpha);
        clear_snapshot(ui);
        goto finish_snapshot;
    }
    if (capacity < required || capacity > limit - (LV_DRAW_BUF_ALIGN - 1u)) {
        candidate = ui_allocate(ui, required + LV_DRAW_BUF_ALIGN - 1u);
        if (!candidate) { result = 0; goto finish_snapshot; }
        memory = candidate;
        capacity = required;
    }
    /* Common case: take the only fallible snapshot before touching the old
     * plane. Same-size updates can then reuse its pixels without a second plane. */
    if (found == 1) {
        prepared_snapshot = take_snapshot(only_node->object, memory, capacity, &draw);
        if (!prepared_snapshot) { result = 0; goto finish_snapshot; }
    }
    int prepared = prepare_alpha_plane(ui, &union_area, staging);
    if (created_plane) *created_plane = prepared == 2;
    if (!prepared) {
        result = 0;
        goto finish_snapshot;
    }
    ui->alpha.content_x = 0;
    ui->alpha.content_y = 0;
    ui->alpha.content_width = 0;
    ui->alpha.content_height = 0;
    set_alpha_overlay_base_visibility(ui, 1);
    for (node = ui->nodes; node != NULL; node = node->next) {
        lv_draw_buf_t *snapshot;
        lv_area_t area;
        if (node->object == NULL || !node->visible ||
            node->composition != PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
            overlay_has_overlay_parent(ui, node))
            continue;
        snapshot = prepared_snapshot ? prepared_snapshot :
            take_snapshot(node->object, memory, capacity, &draw);
        prepared_snapshot = NULL;
        if (snapshot == NULL) {
            result = 0;
            break;
        }
        lv_obj_get_coords(node->object, &area);
        for (uint32_t y = 0; y < snapshot->header.h; ++y) {
            const lv_color32_t *source = (const lv_color32_t *)(
                (const uint8_t *)snapshot->data +
                (size_t)y * snapshot->header.stride);
            for (uint32_t x = 0; x < snapshot->header.w; ++x) {
                const int32_t plane_x = area.x1 + (int32_t)x - ui->alpha.x;
                const int32_t plane_y = area.y1 + (int32_t)y - ui->alpha.y;
                if (plane_x >= 0 && plane_y >= 0 &&
                    plane_x < ui->alpha.width && plane_y < ui->alpha.height) {
                    alpha_plane_blend_pixel(
                        ui, (size_t)plane_y * ui->alpha.width + plane_x,
                        source[x]);
                    if (source[x].alpha != 0) {
                        const uint16_t px = (uint16_t)plane_x;
                        const uint16_t py = (uint16_t)plane_y;
                        if (px < content_min_x) content_min_x = px;
                        if (py < content_min_y) content_min_y = py;
                        if (px > content_max_x) content_max_x = px;
                        if (py > content_max_y) content_max_y = py;
                    }
                }
            }
        }
    }
    if (result && content_min_x != UINT16_MAX) {
        ui->alpha.content_x = content_min_x;
        ui->alpha.content_y = content_min_y;
        ui->alpha.content_width =
            (uint16_t)(content_max_x - content_min_x + 1u);
        ui->alpha.content_height =
            (uint16_t)(content_max_y - content_min_y + 1u);
        ui->alpha.revision = ++g_alpha_revision;
    }
finish_snapshot:
    set_alpha_overlay_base_visibility(ui, 0);
    if (candidate) {
        if (result) {
            clear_snapshot(ui);
            ui->snapshot_memory = candidate;
            ui->snapshot_capacity = capacity;
        } else ui_release(ui, candidate);
    }
    return result;
}

/* Keep the committed plane immutable until every overlay has rendered.
 * Two same-size planes alternate on warm multi-overlay refreshes. Geometry
 * changes discard only unused staging storage before allocating its replacement,
 * so no third plane is retained. Failed preparation may trim this private cache
 * but cannot publish pixels or a revision. Native LVGL draw allocations remain
 * separate from these tracked pixel buffers. */
static int refresh_alpha_plane(pxa_lvgl_ui_t *ui) {
    unsigned overlays = 0;
    for (pxa_lvgl_ui_node_t *node = ui->nodes; node; node = node->next) {
        if (node->object && node->visible &&
            node->composition == PXA_UI_COMPOSITION_ALPHA_OVERLAY &&
            !overlay_has_overlay_parent(ui, node)) ++overlays;
    }
    if (overlays <= 1) {
        clear_alpha_plane(ui, &ui->spare_alpha);
        return render_alpha_plane(ui, 0, NULL);
    }
    pxa_lvgl_ui_alpha_t previous = ui->alpha;
    ui->alpha = ui->spare_alpha;
    memset(&ui->spare_alpha, 0, sizeof(ui->spare_alpha));
    int created_plane = 0;
    int result = render_alpha_plane(ui, 1, &created_plane);
    if (!result) {
        /* Retain only an already admitted warm buffer after a failed attempt.
         * A newly allocated plane from that attempt must be returned. */
        if (created_plane) clear_alpha_plane(ui, &ui->alpha);
        ui->spare_alpha = ui->alpha;
        ui->alpha = previous;
        return 0;
    }
    ui->spare_alpha = previous;
    return 1;
}

static void discard_created(pxa_lvgl_ui_transaction_t *transaction) {
    pxa_lvgl_ui_command_t *command;
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        if (command->view.command != PXA_UI_COMMAND_CREATE) continue;
        pxa_lvgl_ui_node_t *node = command->owned.created;
        if (node == NULL) continue;
        command->owned.created = NULL;
        if (node->object != NULL)
            lv_obj_delete(node->object);
        else
            ui_release(transaction->ui, node);
    }
}

/* Exclude retiring nodes from layout and alpha preparation without freeing
 * handles. Their original visibility is restored if preparation fails. */
static void stage_removal(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_node_t *root) {
    if (root == NULL || root->object == NULL) return;
    for (pxa_lvgl_ui_node_t *node = ui->nodes; node; node = node->next) {
        if (node->removal_state || node->object == NULL) continue;
        lv_obj_t *ancestor = node->object;
        while (ancestor && ancestor != root->object)
            ancestor = lv_obj_get_parent(ancestor);
        if (ancestor == NULL) continue;
        node->removal_state = 1u | (node->visible ? 2u : 0u) |
            (lv_obj_is_hidden(node->object) ? 4u : 0u);
        node->visible = 0;
        lv_obj_set_hidden(node->object, true);
    }
}

static void restore_removals(pxa_lvgl_ui_t *ui) {
    for (pxa_lvgl_ui_node_t *node = ui->nodes; node; node = node->next) {
        if (!node->removal_state) continue;
        node->visible = (node->removal_state & 2u) != 0;
        if (!(node->removal_state & 4u))
            lv_obj_set_hidden(node->object, false);
        node->removal_state = 0;
    }
}

static int is_image_source_command(const pxa_ui_command_view_t *view) {
    return (view->command == PXA_UI_COMMAND_SET_PROPERTY ||
            view->command == PXA_UI_COMMAND_CLEAR_PROPERTY) &&
           (view->property == PXA_UI_PROPERTY_IMAGE_HANDLE ||
            view->property == PXA_UI_PROPERTY_ASSET);
}

static void swap_image_source(pxa_lvgl_ui_command_t *command) {
    pxa_lvgl_ui_node_t *node = command->view.node_handle;
    pxa_lvgl_ui_image_t *previous_image = node->image;
    const void *previous_asset = node->asset_source;
    lv_image_set_src(node->object, command->owned.source.image
        ? &command->owned.source.image->descriptor : command->owned.source.asset);
    node->image = command->owned.source.image;
    node->asset_source = command->owned.source.asset;
    command->owned.source.image = previous_image;
    command->owned.source.asset = previous_asset;
}

static void rollback_commands(pxa_lvgl_ui_transaction_t *transaction) {
    /* A node can be set/cleared several times. Undo in reverse command order.
     * This failed transaction will only be cancelled and freed afterwards. */
    pxa_lvgl_ui_command_t *command = transaction->commands;
    pxa_lvgl_ui_command_t *reversed = NULL;
    transaction->tail = command;
    while (command) {
        pxa_lvgl_ui_command_t *next = command->next;
        command->next = reversed;
        reversed = command;
        command = next;
    }
    transaction->commands = reversed;
    /* MOVE executes after properties, irrespective of stream order. Restore
     * parents before deleting new subtrees that may contain existing nodes. */
    for (command = reversed; command; command = command->next) {
        if (command->view.command != PXA_UI_COMMAND_MOVE || !command->owned.move.applied)
            continue;
        pxa_lvgl_ui_node_t *node = command->view.node_handle;
        lv_obj_set_parent(node->object, command->owned.move.parent);
        lv_obj_move_to_index(node->object, command->owned.move.index);
        command->owned.move.applied = 0;
    }
    for (command = reversed; command; command = command->next) {
        if (is_image_source_command(&command->view) && command->owned.source.applied) {
            swap_image_source(command);
            command->owned.source.applied = 0;
        } else if (is_grid_command(&command->view) && command->owned.grid.applied) {
            swap_grid_tracks(command, 1);
            command->owned.grid.applied = 0;
        } else if (is_plain_property(&command->view)) {
            rollback_property(command);
        }
    }
}

static void execute_transaction(void *data) {
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)data;
    pxa_lvgl_ui_command_t *command;
    pxa_lvgl_ui_node_t *new_root = NULL;
    pxa_lvgl_ui_node_t *old_root =
        (pxa_lvgl_ui_node_t *)transaction->info.target_handle;
    int32_t old_index = -1;
    transaction->ui->transaction_active = 1;
    if (old_root != NULL && old_root->object != NULL)
        old_index = lv_obj_get_index(old_root->object);
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node;
        pxa_lvgl_ui_node_t *parent;
        lv_obj_t *parent_object;
        if (command->view.command != PXA_UI_COMMAND_CREATE) continue;
        node = command->owned.created;
        parent = (pxa_lvgl_ui_node_t *)command->view.parent_handle;
        parent_object = parent == NULL
                            ? (transaction->ui->config.parent_object != NULL
                                   ? transaction->ui->config.parent_object
                                   : lv_screen_active())
                            : parent->object;
        /* DELETE callbacks must only see nodes already linked into the UI,
         * including objects whose widget-specific initialization fails. */
        link_node(transaction->ui, node);
        if (parent_object == NULL || create_object(node, parent_object) == NULL) {
            if (node->object == NULL) unlink_node(node);
            transaction->status = PXA_STATUS_RESOURCE_LIMIT;
            discard_created(transaction);
            transaction->ui->transaction_active = 0;
            return;
        }
        if ((transaction->info.kind == PXA_UI_REPLACE_SURFACE &&
             node->type == PXA_UI_NODE_ROOT) ||
            (transaction->info.kind == PXA_UI_REPLACE_SUBTREE &&
             node->id == transaction->info.target))
            new_root = node;
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node =
            command->view.command == PXA_UI_COMMAND_CREATE
                ? command->owned.created
                : (pxa_lvgl_ui_node_t *)command->view.node_handle;
        if (node == NULL) continue;
        if (is_image_source_command(&command->view)) {
            swap_image_source(command);
            command->owned.source.applied = 1;
        } else if (is_grid_command(&command->view)) {
            swap_grid_tracks(command, 0);
            command->owned.grid.applied = 1;
        } else if (is_plain_property(&command->view)) {
            transaction->status = capture_property_undo(command);
            if (transaction->status != PXA_STATUS_OK) goto failed;
            if (command->view.command == PXA_UI_COMMAND_SET_PROPERTY)
                apply_property(node, command->view.property, command->view.value);
            else clear_property(node, command->view.property);
        }
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        if (command->view.command == PXA_UI_COMMAND_MOVE) {
            pxa_lvgl_ui_node_t *node =
                (pxa_lvgl_ui_node_t *)command->view.node_handle;
            pxa_lvgl_ui_node_t *parent =
                (pxa_lvgl_ui_node_t *)command->view.parent_handle;
            pxa_lvgl_ui_node_t *before =
                (pxa_lvgl_ui_node_t *)command->view.before_handle;
            if (node != NULL && node->object != NULL && parent != NULL &&
                parent->object != NULL) {
                command->owned.move.parent = lv_obj_get_parent(node->object);
                command->owned.move.index = lv_obj_get_index(node->object);
                command->owned.move.applied = 1;
                lv_obj_set_parent(node->object, parent->object);
                if (before != NULL && before->object != NULL) {
                    int32_t index = lv_obj_get_index(before->object);
                    /* Removing an earlier sibling shifts the target left. */
                    if (lv_obj_get_index(node->object) < index) --index;
                    lv_obj_move_to_index(node->object, index);
                } else {
                    lv_obj_move_to_index(node->object, -1);
                }
            }
        }
    }
    if (transaction->info.kind != PXA_UI_PATCH) {
        stage_removal(transaction->ui, old_root);
        if (new_root != NULL && new_root->object != NULL && old_index >= 0)
            lv_obj_move_to_index(new_root->object, old_index);
    } else {
        for (command = transaction->commands; command != NULL;
             command = command->next) {
            if (command->view.command == PXA_UI_COMMAND_REMOVE) {
                pxa_lvgl_ui_node_t *node =
                    (pxa_lvgl_ui_node_t *)command->view.node_handle;
                stage_removal(transaction->ui, node);
            }
        }
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        if (command->view.command != PXA_UI_COMMAND_CREATE) continue;
        pxa_lvgl_ui_node_t *node = command->owned.created;
        if (node != NULL && node->object != NULL && node->visible)
            lv_obj_set_hidden(node->object, false);
    }
    lv_obj_update_layout(lv_screen_active());
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node = command->view.command == PXA_UI_COMMAND_CREATE
                                       ? command->owned.created
                                       : (pxa_lvgl_ui_node_t *)command->view.node_handle;
        if (node == NULL || node->object == NULL || !node->has_scroll_position)
            continue;
        lv_obj_scroll_to_y(node->object,
                           logical_pixels(node->ui, node->scroll_position),
                           LV_ANIM_OFF);
        node->has_scroll_position = 0;
    }
    transaction->status = refresh_alpha_plane(transaction->ui)
                              ? PXA_STATUS_OK : PXA_STATUS_RESOURCE_LIMIT;
    if (transaction->status != PXA_STATUS_OK) {
failed:
        restore_removals(transaction->ui);
        rollback_commands(transaction);
        discard_created(transaction);
        set_alpha_overlay_base_visibility(transaction->ui, 0);
        lv_obj_update_layout(lv_screen_active());
        transaction->ui->transaction_active = 0;
        return;
    }
    /* No fallible preparation or command-handle access follows destruction. */
    if (transaction->info.kind != PXA_UI_PATCH) {
        if (old_root != NULL && old_root->object != NULL)
            lv_obj_delete(old_root->object);
    } else {
        for (command = transaction->commands; command; command = command->next) {
            if (command->view.command == PXA_UI_COMMAND_REMOVE) {
                pxa_lvgl_ui_node_t *node = command->view.node_handle;
                if (node != NULL && node->object != NULL)
                    lv_obj_delete(node->object);
            }
        }
    }
    if (transaction->info.kind == PXA_UI_REPLACE_SURFACE &&
        transaction->info.surface == PXA_UI_PRIMARY_SURFACE)
        transaction->ui->primary_root = new_root;
    transaction->ui->transaction_active = 0;
}

static void free_transaction(pxa_lvgl_ui_transaction_t *transaction,
                             int keep_created) {
    pxa_lvgl_ui_command_t *command;
    pxa_lvgl_ui_command_t *next;
    if (!keep_created) discard_created(transaction);
    for (command = transaction->commands; command != NULL; command = next) {
        next = command->next;
        if (command->view.command == PXA_UI_COMMAND_CREATE) {
            if (keep_created && command->owned.created != NULL) {
                command->owned.created->owner_command = NULL;
                command->owned.created = NULL;
            }
        } else if (is_image_source_command(&command->view)) {
            release_image(transaction->ui, command->owned.source.image);
            release_asset_source(transaction->ui, command->owned.source.asset);
        } else if (is_grid_command(&command->view)) {
            ui_release(transaction->ui, command->owned.grid.tracks);
        } else if (is_plain_property(&command->view)) {
            free_property_undo(transaction->ui, command->owned.property);
        }
        ui_release(transaction->ui, command);
    }
    ui_release(transaction->ui, transaction);
}

static pxa_status_t backend_begin(
    void *context, const pxa_ui_transaction_info_t *info,
    void **backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || info == NULL ||
        backend_transaction == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    transaction = (pxa_lvgl_ui_transaction_t *)ui_allocate(
        ui, sizeof(*transaction));
    if (transaction == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(transaction, 0, sizeof(*transaction));
    transaction->ui = ui;
    transaction->info = *info;
    transaction->status = PXA_STATUS_INTERNAL;
    *backend_transaction = transaction;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_apply(
    void *context, void *backend_transaction,
    const pxa_ui_command_view_t *view, void **created_handle) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    pxa_lvgl_ui_command_t *command;
    size_t size;
    if (ui == NULL || transaction == NULL || transaction->ui != ui ||
        view == NULL || view->value.size > SIZE_MAX - sizeof(*command))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (view->value.size == SIZE_MAX - sizeof(*command))
        return PXA_STATUS_RESOURCE_LIMIT;
    size = sizeof(*command) + view->value.size + 1u;
    command = (pxa_lvgl_ui_command_t *)ui_allocate(ui, size);
    if (command == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(command, 0, sizeof(*command));
    command->view = *view;
    if (view->value.size != 0) {
        memcpy(command->value, view->value.data, view->value.size);
        command->view.value.data = command->value;
    }
    command->value[view->value.size] = 0;
    if (view->command==PXA_UI_COMMAND_SET_PROPERTY && view->property==PXA_UI_PROPERTY_IMAGE_HANDLE) {
        pxa_status_t status=view->value.size==8 && pxa_read_u64(view->value.data) ?
            prepare_image(ui,transaction->info.component,pxa_read_u64(view->value.data),&command->owned.source.image) : PXA_STATUS_INVALID_ARGUMENT;
        if (status) { ui_release(ui,command); return status; }
    }
    if (view->command == PXA_UI_COMMAND_SET_PROPERTY && view->property == PXA_UI_PROPERTY_ASSET) {
        if (ui->config.resolve_asset)
            command->owned.source.asset = ui->config.resolve_asset(command->value,
                view->value.size, ui->config.asset_user_data);
        if (!command->owned.source.asset) {
            ui_release(ui, command);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
    }
    if (is_grid_command(view)) {
        pxa_status_t status = prepare_grid_tracks(ui, command);
        if (status != PXA_STATUS_OK) { ui_release(ui, command); return status; }
    }
    if (is_plain_property(view)) {
        pxa_status_t status = prepare_property_undo(ui, command);
        if (status != PXA_STATUS_OK) { ui_release(ui, command); return status; }
    }
    if (view->command == PXA_UI_COMMAND_CREATE) {
        command->owned.created = (pxa_lvgl_ui_node_t *)ui_allocate(
            ui, sizeof(*command->owned.created));
        if (command->owned.created == NULL) {
            ui_release(ui, command);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        memset(command->owned.created, 0, sizeof(*command->owned.created));
        command->owned.created->ui = ui;
        command->owned.created->surface = transaction->info.surface;
        command->owned.created->id = view->node;
        command->owned.created->type = view->type;
        command->owned.created->subtype = view->subtype;
        command->owned.created->visible = 1;
        command->owned.created->owner_command = command;
        if (created_handle != NULL) *created_handle = command->owned.created;
    }
    if (transaction->tail == NULL)
        transaction->commands = command;
    else
        transaction->tail->next = command;
    transaction->tail = command;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_commit(void *context, void *backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    pxa_status_t status;
    if (ui == NULL || transaction == NULL || transaction->ui != ui)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = ui->config.execute(execute_transaction, transaction,
                                ui->config.execute_user_data);
    if (status != PXA_STATUS_OK) return status;
    status = transaction->status;
    if (status == PXA_STATUS_OK) free_transaction(transaction, 1);
    return status;
}

static void backend_cancel(void *context, void *backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    if (ui != NULL && transaction != NULL && transaction->ui == ui)
        free_transaction(transaction, 0);
}

typedef struct {
    pxa_lvgl_ui_node_t *node;
    pxa_ui_canvas_view_t view;
    pxa_ui_release_fn release;
    void *release_context;
    pxa_status_t status;
    pxa_lvgl_ui_canvas_asset_t *assets;
    lv_area_t *grown_clips;
    size_t clip_capacity;
    lv_image_dsc_t *grown_bitmap_images;
    size_t bitmap_count;
    uint8_t reuse_assets;
    pxa_lvgl_ui_canvas_image_t *images;
    size_t image_count;
    size_t image_capacity;
    uint8_t reuse_image_storage;
    uint8_t reuse_image_bindings;
} canvas_swap_t;

static size_t canvas_clip_depth(const uint8_t *data, size_t size) {
    size_t offset = 0;
    size_t depth = 0;
    size_t maximum = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_CLIP_PUSH) {
            ++depth;
            if (depth > maximum) maximum = depth;
        } else if (type == PXA_UI_CANVAS_CLIP_POP && depth != 0) {
            --depth;
        }
        offset += length;
    }
    return maximum;
}

static size_t canvas_bitmap_count(const uint8_t *data, size_t size) {
    size_t offset = 0;
    size_t count = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u) ++count;
        offset += length;
    }
    return count;
}

static void populate_canvas_bitmaps(pxa_lvgl_ui_canvas_t *canvas) {
    size_t offset = 0;
    size_t index = 0;
    while (offset + 4u <= canvas->size && index < canvas->bitmap_count) {
        uint8_t type = canvas->bytes[offset];
        uint16_t length = pxa_read_u16(canvas->bytes + offset + 2u);
        const uint8_t *value;
        offset += 4u;
        if (length > canvas->size - offset) break;
        value = canvas->bytes + offset;
        offset += length;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u) {
            lv_image_dsc_t *image = &canvas->bitmap_images[index++];
            memset(image, 0, sizeof(*image));
            image->header.magic = LV_IMAGE_HEADER_MAGIC;
            image->header.cf = LV_COLOR_FORMAT_RGB565;
            image->header.w = (uint16_t)pxa_read_u32(value + 8);
            image->header.h = (uint16_t)pxa_read_u32(value + 12);
            image->header.stride = (uint16_t)pxa_read_u32(value + 16);
            image->data_size = length - 20u;
            image->data = value + 20;
        }
    }
}

static pxa_status_t acquire_canvas_assets(
    pxa_lvgl_ui_node_t *node, const uint8_t *data, size_t size,
    pxa_lvgl_ui_canvas_asset_t **output) {
    pxa_lvgl_ui_canvas_asset_t *assets = NULL;
    size_t offset = 0;
    *output = NULL;
    if (node->ui->config.resolve_asset == NULL) return PXA_STATUS_OK;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        const uint8_t *value;
        size_t path_size;
        pxa_lvgl_ui_canvas_asset_t *asset;
        offset += 4u;
        if (length > size - offset) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        value = data + offset;
        if (type != PXA_UI_CANVAS_IMAGE || length <= 18u) {
            offset += length;
            continue;
        }
        path_size = length - 18u;
        if (find_canvas_asset(assets, value + 18u, path_size) != NULL) {
            offset += length;
            continue;
        }
        if (path_size > SIZE_MAX - sizeof(*asset)) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset = (pxa_lvgl_ui_canvas_asset_t *)ui_allocate(
            node->ui, sizeof(*asset) + path_size);
        if (asset == NULL) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset->source = node->ui->config.resolve_asset(
            value + 18u, path_size, node->ui->config.asset_user_data);
        if (asset->source == NULL) {
            ui_release(node->ui, asset);
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset->path_size = path_size;
        memcpy(asset->path, value + 18u, path_size);
        asset->next = assets;
        assets = asset;
        offset += length;
    }
    *output = assets;
    return PXA_STATUS_OK;
}

static int canvas_assets_match(pxa_lvgl_ui_canvas_asset_t *assets,
                                const uint8_t *data, size_t size) {
    pxa_lvgl_ui_canvas_asset_t *asset;
    size_t offset = 0;
    for (asset = assets; asset != NULL; asset = asset->next) asset->seen = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_IMAGE && length > 18u) {
            asset = find_canvas_asset(assets, data + offset + 18u, length - 18u);
            if (asset == NULL) return 0;
            asset->seen = 1;
        }
        offset += length;
    }
    for (asset = assets; asset != NULL; asset = asset->next)
        if (!asset->seen) return 0;
    return offset == size;
}

/* Ordered bindings match IMAGE_HANDLE commands, including clipped commands.
 * Duplicate handles share a single retained descriptor within this frame.
 * Resolve every new frame through Core: a closed handle cannot be resurrected
 * by finding it in the previous display list. No lookup occurs while drawing. */
static pxa_status_t prepare_canvas_images(canvas_swap_t *swap) {
    pxa_lvgl_ui_t *ui = swap->node->ui;
    pxa_lvgl_ui_canvas_t *canvas = swap->node->canvas;
    const uint8_t *data = swap->view.display_list.data;
    size_t size = swap->view.display_list.size;
    size_t total = 0, offset = 0;
    int same_bindings = canvas != NULL;
    while (offset < size) {
        if (size - offset < 4)
            return PXA_STATUS_INVALID_ARGUMENT;
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2);
        offset += 4;
        if (length > size - offset)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (type == PXA_UI_CANVAS_IMAGE_HANDLE) {
            if (length != 26)
                return PXA_STATUS_INVALID_ARGUMENT;
            if (same_bindings && (total >= canvas->image_count ||
                canvas->images[total].handle != pxa_read_u64(data + offset + 18)))
                same_bindings = 0;
            ++total;
        }
        offset += length;
    }
    if (!total)
        return PXA_STATUS_OK;
    if (!ui->config.acquire_image)
        return PXA_STATUS_UNSUPPORTED;
    /* Positions, opacity and clipping live in the new command bytes. An
     * unchanged handle sequence can keep its immutable binding array too. */
    if (same_bindings && total == canvas->image_count) {
        for (size_t i = 0; i < total; ++i) {
            if (!canvas->images[i].owned) continue;
            pxa_asset_object_t *pixels = NULL;
            pxa_status_t status = ui->config.acquire_image(swap->view.component,
                canvas->images[i].handle, &pixels, ui->config.asset_user_data);
            if (status) return status;
            int matches = pixels == canvas->images[i].image->pixels;
            pxa_asset_object_release(pixels);
            if (!matches) { same_bindings = 0; break; }
        }
        if (same_bindings) {
            swap->images = canvas->images;
            swap->image_count = total;
            swap->image_capacity = canvas->image_capacity;
            swap->reuse_image_bindings = 1;
            return PXA_STATUS_OK;
        }
    }
    if (total > SIZE_MAX / sizeof(*swap->images))
        return PXA_STATUS_RESOURCE_LIMIT;
    if (canvas && total <= canvas->spare_image_capacity) {
        swap->images = canvas->spare_images;
        swap->image_capacity = canvas->spare_image_capacity;
        swap->reuse_image_storage = 1;
    } else {
        swap->images = ui_allocate(ui, total * sizeof(*swap->images));
        swap->image_capacity = total;
    }
    pxa_lvgl_ui_canvas_image_t *images = swap->images;
    if (!images)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(images, 0, total * sizeof(*images));
    size_t index = 0;
    offset = 0;
    while (offset < size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2);
        const uint8_t *value = data + offset + 4;
        offset += 4 + length;
        if (type != PXA_UI_CANVAS_IMAGE_HANDLE)
            continue;
        images[index].handle = pxa_read_u64(value + 18);
        for (size_t i = 0; i < index; ++i) {
            if (images[i].handle == images[index].handle) {
                images[index].image = images[i].image;
                break;
            }
        }
        if (!images[index].image) {
            pxa_asset_object_t *pixels = NULL;
            pxa_status_t status =
                ui->config.acquire_image(swap->view.component, images[index].handle,
                                         &pixels, ui->config.asset_user_data);
            if (status)
                return status;
            /* Borrow a previous descriptor only after fresh authentication
             * and object identity verification. Ownership moves at commit. */
            for (size_t i = 0; canvas && i < canvas->image_count; ++i) {
                if (canvas->images[i].owned &&
                    canvas->images[i].handle == images[index].handle &&
                    canvas->images[i].image->pixels == pixels) {
                    images[index].image = canvas->images[i].image;
                    images[index].owned = 3;
                    break;
                }
            }
            if (images[index].image) {
                pxa_asset_object_release(pixels);
            } else {
                pxa_lvgl_ui_image_t *image;
                if (canvas && canvas->free_images) {
                    image = canvas->free_images;
                    canvas->free_images = image->state.next_free;
                    images[index].owned = 2; /* Return storage to pool on rollback. */
                } else {
                    image = ui_allocate(ui, sizeof(*image));
                    images[index].owned = 1;
                }
                if (!image) {
                    pxa_asset_object_release(pixels);
                    return PXA_STATUS_RESOURCE_LIMIT;
                }
                status = initialize_image(ui, pixels, image);
                if (status) {
                    pxa_asset_object_release(pixels);
                    if (images[index].owned == 2) {
                        memset(image, 0, sizeof(*image));
                        image->state.next_free = canvas->free_images;
                        canvas->free_images = image;
                    } else
                        ui_release(ui, image);
                    return status;
                }
                images[index].image = image;
            }
        }
        ++index;
        swap->image_count = index;
    }
    return PXA_STATUS_OK;
}

static void discard_canvas_images(canvas_swap_t *swap) {
    if (swap->reuse_image_bindings) return;
    pxa_lvgl_ui_t *ui = swap->node->ui;
    for (size_t i = 0; i < swap->image_count; ++i) {
        if (swap->images[i].owned == 1)
            release_image(ui, swap->images[i].image);
        else if (swap->images[i].owned == 2)
            recycle_image(swap->node->canvas, swap->images[i].image);
    }
    if (!swap->reuse_image_storage)
        ui_release(ui, swap->images);
}

static void commit_canvas_images(canvas_swap_t *swap, pxa_lvgl_ui_canvas_t *previous) {
    pxa_lvgl_ui_t *ui = swap->node->ui;
    pxa_lvgl_ui_canvas_t *canvas = swap->node->canvas;
    if (swap->reuse_image_bindings) {
        swap->images = NULL; swap->image_count = 0;
        return;
    }
    if (!swap->image_count) {
        release_canvas_images(ui, previous->images, previous->image_count);
        ui_release(ui, previous->spare_images);
        release_image_pool(ui, canvas);
        canvas->spare_images = NULL;
        canvas->spare_image_capacity = 0;
    } else {
        for (size_t i = 0; i < swap->image_count; ++i) {
            pxa_lvgl_ui_canvas_image_t *image = &swap->images[i];
            if (image->owned == 3)
                for (size_t j = 0; j < previous->image_count; ++j) {
                    if (previous->images[j].owned &&
                        previous->images[j].image == image->image) {
                        previous->images[j].owned = 0;
                        image->owned = 1;
                        break;
                    }
                }
            else if (image->owned)
                image->owned = 1;
        }
        for (size_t i = 0; i < previous->image_count; ++i)
            if (previous->images[i].owned)
                recycle_image(canvas, previous->images[i].image);
        if (!swap->reuse_image_storage)
            ui_release(ui, previous->spare_images);
        canvas->spare_images = previous->images;
        canvas->spare_image_capacity = previous->image_capacity;
    }
    swap->images = NULL;
    swap->image_count = 0;
}

static void execute_canvas_swap(void *data) {
    canvas_swap_t *swap = (canvas_swap_t *)data;
    pxa_lvgl_ui_canvas_t *canvas = swap->node->canvas;
    int first_frame = canvas == NULL;
    /* Preflight fonts under the LVGL execution lock. Keep them alive until
     * reset so queued draw tasks can never reference an evicted face. */
    for (size_t at=0;at+4<=swap->view.display_list.size;) {
        const uint8_t *data=swap->view.display_list.data;
        uint16_t length=pxa_read_u16(data+at+2);
        if (length>swap->view.display_list.size-at-4) {
            swap->status=PXA_STATUS_INVALID_ARGUMENT;return;
        }
        if (data[at]==PXA_UI_CANVAS_TEXT_SIZED &&
            (length<24 || !sized_font(swap->node->ui,pxa_read_u16(data+at+24),1))) {
            swap->status=swap->node->ui->config.sized_text_font_path
                ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_UNSUPPORTED;
            return;
        }
        at+=4+length;
    }
    if (canvas == NULL) {
        canvas = (pxa_lvgl_ui_canvas_t *)ui_allocate(
            swap->node->ui, sizeof(*canvas));
        if (canvas == NULL) {
            swap->status = PXA_STATUS_RESOURCE_LIMIT;
            return;
        }
        memset(canvas, 0, sizeof(*canvas));
    }
    pxa_lvgl_ui_canvas_t previous = *canvas;
    if (!swap->reuse_assets) canvas->assets = swap->assets;
    if (swap->grown_clips != NULL) {
        canvas->clip_stack = swap->grown_clips;
        canvas->clip_capacity = swap->clip_capacity;
    }
    if (swap->grown_bitmap_images != NULL) {
        canvas->bitmap_images = swap->grown_bitmap_images;
        canvas->bitmap_capacity = swap->bitmap_count;
    }
    canvas->images = swap->images;
    canvas->image_count = swap->image_count;
    canvas->image_capacity = swap->image_capacity;
    canvas->bytes = swap->view.display_list.data;
    canvas->size = swap->view.display_list.size;
    canvas->bitmap_count = swap->bitmap_count;
    populate_canvas_bitmaps(canvas);
    canvas->release = swap->release;
    canvas->release_context = swap->release_context;
    swap->node->canvas = canvas;
    if (node_is_in_alpha_overlay(swap->node->ui, swap->node) &&
        !refresh_alpha_plane(swap->node->ui)) {
        *canvas = previous;
        /* The bitmap descriptor array may have been reused while previewing. */
        populate_canvas_bitmaps(canvas);
        if (first_frame) {
            swap->node->canvas = NULL;
            ui_release(swap->node->ui, canvas);
        }
        swap->status = PXA_STATUS_RESOURCE_LIMIT;
        return;
    }
    if (previous.release && previous.bytes)
        previous.release(previous.release_context, (void *)previous.bytes);
    if (!swap->reuse_assets) {
        release_canvas_assets(swap->node->ui, previous.assets);
        swap->assets = NULL;
    }
    if (swap->grown_clips) {
        ui_release(swap->node->ui, previous.clip_stack);
        swap->grown_clips = NULL;
    }
    if (swap->grown_bitmap_images) {
        ui_release(swap->node->ui, previous.bitmap_images);
        swap->grown_bitmap_images = NULL;
    }
    commit_canvas_images(swap,&previous);
    if (first_frame || swap->view.dirty_count == 0) {
        lv_obj_invalidate(swap->node->object);
    } else {
        lv_area_t origin;
        uint8_t index;
        lv_obj_get_coords(swap->node->object, &origin);
        for (index = 0; index < swap->view.dirty_count; ++index) {
            const int32_t *rect = swap->view.dirty_rects[index];
            lv_area_t area;
            area.x1 = origin.x1 + canvas_pixels(swap->node->ui, rect[0]);
            area.y1 = origin.y1 + canvas_pixels(swap->node->ui, rect[1]);
            area.x2 = origin.x1 + canvas_end_pixels(swap->node->ui,
                (int64_t)rect[0] + rect[2]) - 1;
            area.y2 = origin.y1 + canvas_end_pixels(swap->node->ui,
                (int64_t)rect[1] + rect[3]) - 1;
            area.x1 = LV_MAX(area.x1, origin.x1);
            area.y1 = LV_MAX(area.y1, origin.y1);
            area.x2 = LV_MIN(area.x2, origin.x2);
            area.y2 = LV_MIN(area.y2, origin.y2);
            if (area.x1 <= area.x2 && area.y1 <= area.y2)
                lv_obj_invalidate_area(swap->node->object, &area);
        }
    }
    swap->status = PXA_STATUS_OK;
}

static pxa_status_t backend_canvas(
    void *context, const pxa_ui_canvas_view_t *canvas,
    pxa_ui_release_fn release, void *release_context) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_node_t *node;
    canvas_swap_t swap;
    pxa_status_t status;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || canvas == NULL ||
        release == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    node = (pxa_lvgl_ui_node_t *)canvas->node_handle;
    if (node == NULL || node->ui != ui || node->object == NULL ||
        node->type != PXA_UI_NODE_CANVAS || node->id != canvas->node)
        return PXA_STATUS_NOT_FOUND;
    memset(&swap, 0, sizeof(swap));
    swap.node = node;
    swap.view = *canvas;
    swap.release = release;
    swap.release_context = release_context;
    swap.status = PXA_STATUS_INTERNAL;
    /* Resolve prepared handles and prepare lists before taking the execution lock. */
    swap.reuse_assets = node->canvas != NULL && canvas_assets_match(
        node->canvas->assets, canvas->display_list.data, canvas->display_list.size);
    if (!swap.reuse_assets) {
        status = acquire_canvas_assets(node, canvas->display_list.data,
                                         canvas->display_list.size, &swap.assets);
        if (status != PXA_STATUS_OK) return status;
    }
    swap.clip_capacity = canvas_clip_depth(canvas->display_list.data,
                                            canvas->display_list.size);
    if (node->canvas == NULL || swap.clip_capacity > node->canvas->clip_capacity) {
        if (swap.clip_capacity > SIZE_MAX / sizeof(*swap.grown_clips)) {
            release_canvas_assets(ui, swap.assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        if (swap.clip_capacity != 0) {
            swap.grown_clips = (lv_area_t *)ui_allocate(
                ui, swap.clip_capacity * sizeof(*swap.grown_clips));
            if (swap.grown_clips == NULL) {
                release_canvas_assets(ui, swap.assets);
                return PXA_STATUS_RESOURCE_LIMIT;
            }
        }
    }
    swap.bitmap_count = canvas_bitmap_count(canvas->display_list.data,
                                             canvas->display_list.size);
    if (node->canvas == NULL ||
        swap.bitmap_count > node->canvas->bitmap_capacity) {
        if (swap.bitmap_count > SIZE_MAX / sizeof(*swap.grown_bitmap_images)) {
            release_canvas_assets(ui, swap.assets);
            ui_release(ui, swap.grown_clips);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        if (swap.bitmap_count != 0) {
            swap.grown_bitmap_images = (lv_image_dsc_t *)ui_allocate(
                ui, swap.bitmap_count * sizeof(*swap.grown_bitmap_images));
            if (swap.grown_bitmap_images == NULL) {
                release_canvas_assets(ui, swap.assets);
                ui_release(ui, swap.grown_clips);
                return PXA_STATUS_RESOURCE_LIMIT;
            }
        }
    }
    status = prepare_canvas_images(&swap);
    if (status == PXA_STATUS_OK)
        status = ui->config.execute(execute_canvas_swap, &swap,
                                    ui->config.execute_user_data);
    discard_canvas_images(&swap);
    release_canvas_assets(ui, swap.assets);
    ui_release(ui, swap.grown_clips);
    ui_release(ui, swap.grown_bitmap_images);
    return status == PXA_STATUS_OK ? swap.status : status;
}

static void execute_reset(void *data) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)data;
    while (ui->nodes != NULL) {
        pxa_lvgl_ui_node_t *node = ui->nodes;
        if (node->object != NULL)
            lv_obj_delete(node->object);
        else {
            unlink_node(node);
            ui_release(ui, node);
        }
    }
    clear_alpha_plane(ui, &ui->alpha);
    clear_alpha_plane(ui, &ui->spare_alpha);
    clear_snapshot(ui);
    release_sized_fonts(ui);
    ui->primary_root = NULL;
}

static void backend_reset(void *context) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    if (ui != NULL && ui->magic == PXA_LVGL_UI_MAGIC)
        (void)ui->config.execute(execute_reset, ui, ui->config.execute_user_data);
}

typedef struct {
    pxa_lvgl_ui_t *ui;
    pxa_lvgl_ui_node_t *node;
    bool focused;
    pxa_status_t status;
} text_input_focus_t;

static void execute_text_input_focus(void *data) {
    text_input_focus_t *focus = (text_input_focus_t *)data;
    pxa_lvgl_ui_node_t *node;
    /* The core passes a handle from this component's committed registry. */
    for (node = focus->ui->nodes; node != NULL; node = node->next)
        if (node == focus->node) break;
    if (node == NULL || node->object == NULL) {
        focus->status = PXA_STATUS_NOT_FOUND;
        return;
    }
    if (node->type != PXA_UI_NODE_CONTROL ||
        node->subtype != PXA_UI_CONTROL_TEXT_INPUT) {
        focus->status = PXA_STATUS_INVALID_ARGUMENT;
        return;
    }
    if (focus->focused && (!lv_obj_is_visible(node->object) ||
                            lv_obj_has_state(node->object, LV_STATE_DISABLED))) {
        focus->status = PXA_STATUS_DENIED;
        return;
    }
    if (focus->focused) {
        for (pxa_lvgl_ui_node_t *other = focus->ui->nodes; other != NULL;
             other = other->next)
            if (other != node && other->object != NULL &&
                other->subtype == PXA_UI_CONTROL_TEXT_INPUT)
                lv_obj_remove_state(other->object, LV_STATE_FOCUSED);
        lv_obj_add_state(node->object, LV_STATE_FOCUSED);
    } else {
        lv_obj_remove_state(node->object, LV_STATE_FOCUSED);
    }
    focus->status = PXA_STATUS_OK;
}

static pxa_status_t backend_text_input_focus(void *context, void *node_handle,
                                             bool focused) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    text_input_focus_t focus = {ui, node_handle, focused, PXA_STATUS_BAD_STATE};
    pxa_status_t status = ui->config.execute(execute_text_input_focus, &focus,
                                             ui->config.execute_user_data);
    return status == PXA_STATUS_OK ? focus.status : status;
}

typedef struct {
    pxa_lvgl_ui_t *ui;
    pxa_ui_environment_t environment;
} environment_change_t;

static void execute_environment_change(void *data) {
    environment_change_t *change = (environment_change_t *)data;
    change->ui->primary_environment = change->environment;
    if (change->ui->primary_root != NULL &&
        change->ui->primary_root->object != NULL)
        lv_obj_invalidate(change->ui->primary_root->object);
}

static pxa_status_t backend_environment(
    void *context, const pxa_ui_environment_t *environment) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    environment_change_t change;
    if (ui == NULL || environment == NULL ||
        environment->surface != PXA_UI_PRIMARY_SURFACE)
        return PXA_STATUS_UNSUPPORTED;
    change.ui = ui;
    change.environment = *environment;
    return ui->config.execute(execute_environment_change, &change,
                              ui->config.execute_user_data);
}

static void intersect_clip(lv_area_t *target, const lv_area_t *clip) {
    if (target->x1 < clip->x1) target->x1 = clip->x1;
    if (target->y1 < clip->y1) target->y1 = clip->y1;
    if (target->x2 > clip->x2) target->x2 = clip->x2;
    if (target->y2 > clip->y2) target->y2 = clip->y2;
}

static void on_canvas_draw(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_layer_t *layer;
    lv_area_t origin;
    lv_area_t saved_clip;
    const uint8_t *data;
    size_t size;
    size_t offset = 0;
    size_t clip_depth = 0;
    size_t bitmap_index = 0;
    size_t image_index = 0;
    if (node == NULL || node->canvas == NULL) return;
    data = node->canvas->bytes;
    size = node->canvas->size;
    layer = lv_event_get_layer(event);
    lv_obj_get_coords(node->object, &origin);
    saved_clip = layer->_clip_area;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        const uint8_t *value;
        offset += 4;
        if (length > size - offset) break;
        value = data + offset;
        offset += length;
        lv_image_dsc_t *bitmap_image = NULL;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u &&
            bitmap_index < node->canvas->bitmap_count)
            bitmap_image = &node->canvas->bitmap_images[bitmap_index++];
        pxa_lvgl_ui_image_t *prepared_image = NULL;
        if (type == PXA_UI_CANVAS_IMAGE_HANDLE && length == 26 &&
            image_index < node->canvas->image_count)
            prepared_image = node->canvas->images[image_index++].image;
        if ((type == PXA_UI_CANVAS_RECT && length == 28) ||
            (type == PXA_UI_CANVAS_ELLIPSE && length == 26) ||
            (type == PXA_UI_CANVAS_IMAGE && length > 18) ||
            (type == PXA_UI_CANVAS_IMAGE_HANDLE && length == 26) ||
            (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20)) {
            int32_t x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            int32_t y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            int32_t width = canvas_pixels(node->ui, pxa_read_u32(value + 8));
            int32_t height = canvas_pixels(node->ui, pxa_read_u32(value + 12));
            if (x > layer->_clip_area.x2 || y > layer->_clip_area.y2 ||
                (int64_t)x + width <= layer->_clip_area.x1 ||
                (int64_t)y + height <= layer->_clip_area.y1) continue;
        }
        if (type == PXA_UI_CANVAS_RECT && length == 28) {
            lv_area_t area;
            lv_draw_rect_dsc_t descriptor;
            uint32_t fill = pxa_read_u32(value + 16);
            uint32_t border = pxa_read_u32(value + 24);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = area.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            lv_draw_rect_dsc_init(&descriptor);
            descriptor.bg_color = rgba_color(fill);
            descriptor.bg_opa = rgba_opa(fill);
            descriptor.radius = canvas_pixels(node->ui,
                                               pxa_read_u16(value + 20));
            descriptor.border_width = canvas_pixels(
                node->ui, pxa_read_u16(value + 22));
            descriptor.border_color = rgba_color(border);
            descriptor.border_opa = rgba_opa(border);
            lv_draw_rect(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_LINE && length == 22) {
            lv_draw_line_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 16);
            lv_draw_line_dsc_init(&descriptor);
            descriptor.p1.x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            descriptor.p1.y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            descriptor.p2.x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 8));
            descriptor.p2.y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 12));
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.width = canvas_pixels(node->ui,
                                              pxa_read_u16(value + 20));
            lv_draw_line(layer, &descriptor);
        } else if (type == PXA_UI_CANVAS_ELLIPSE && length == 26) {
            lv_area_t area;
            lv_draw_rect_dsc_t descriptor;
            uint32_t fill = pxa_read_u32(value + 16);
            uint32_t border = pxa_read_u32(value + 22);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = area.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            lv_draw_rect_dsc_init(&descriptor);
            descriptor.bg_color = rgba_color(fill);
            descriptor.bg_opa = rgba_opa(fill);
            descriptor.radius = LV_RADIUS_CIRCLE;
            descriptor.border_width = canvas_pixels(
                node->ui, pxa_read_u16(value + 20));
            descriptor.border_color = rgba_color(border);
            descriptor.border_opa = rgba_opa(border);
            lv_draw_rect(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_ARC && length == 22) {
            lv_draw_arc_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 16);
            lv_draw_arc_dsc_init(&descriptor);
            descriptor.center.x = origin.x1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value));
            descriptor.center.y = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            descriptor.radius = canvas_pixels(node->ui,
                                               pxa_read_u32(value + 8));
            descriptor.start_angle = pxa_read_u16(value + 12);
            descriptor.end_angle = pxa_read_u16(value + 14);
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.width = canvas_pixels(node->ui,
                                              pxa_read_u16(value + 20));
            lv_draw_arc(layer, &descriptor);
        } else if (type == PXA_UI_CANVAS_TEXT && length >= 18) {
            lv_area_t area;
            lv_draw_label_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 12);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = origin.y2;
            lv_draw_label_dsc_init(&descriptor);
            descriptor.text = (const char *)(value + 18);
            descriptor.text_length = length - 18u;
            descriptor.text_local = 1;
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.font = font_for_role(node->ui, value[16]);
            descriptor.align = value[17] == 1 ? LV_TEXT_ALIGN_CENTER
                               : value[17] == 2 ? LV_TEXT_ALIGN_RIGHT
                                                : LV_TEXT_ALIGN_LEFT;
            lv_draw_label(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_TEXT_SIZED && length >= 24) {
            const lv_font_t *font=sized_font(node->ui,pxa_read_u16(value+20),0);
            if (!font) continue;
            lv_area_t box={origin.x1+canvas_pixels(node->ui,(int32_t)pxa_read_u32(value)),
                origin.y1+canvas_pixels(node->ui,(int32_t)pxa_read_u32(value+4)),0,0};
            box.x2=box.x1+canvas_pixels(node->ui,pxa_read_u32(value+8))-1;
            box.y2=box.y1+canvas_pixels(node->ui,pxa_read_u32(value+12))-1;
            lv_area_t clip=layer->_clip_area;
            layer->_clip_area.x1=LV_MAX(clip.x1,box.x1);layer->_clip_area.y1=LV_MAX(clip.y1,box.y1);
            layer->_clip_area.x2=LV_MIN(clip.x2,box.x2);layer->_clip_area.y2=LV_MIN(clip.y2,box.y2);
            lv_draw_label_dsc_t descriptor;lv_draw_label_dsc_init(&descriptor);
            descriptor.text=(const char *)(value+24);descriptor.text_length=length-24;
            descriptor.text_local=1;descriptor.font=font;
            descriptor.color=rgba_color(pxa_read_u32(value+16));descriptor.opa=rgba_opa(pxa_read_u32(value+16));
            descriptor.align=value[22]==1?LV_TEXT_ALIGN_CENTER:value[22]==2?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT;
            if (layer->_clip_area.x1<=layer->_clip_area.x2 && layer->_clip_area.y1<=layer->_clip_area.y2)
                lv_draw_label(layer,&descriptor,&box);
            layer->_clip_area=clip;
        } else if (type == PXA_UI_CANVAS_TEXT_BOX && length >= 24) {
            lv_area_t area;
            lv_draw_label_dsc_t descriptor;
            const lv_font_t *font = font_for_role(node->ui, value[20]);
            int32_t box_height = canvas_pixels(node->ui,
                                                pxa_read_u32(value + 12));
            int32_t text_y = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            uint32_t color = pxa_read_u32(value + 16);
            if (box_height <= 0) continue;
            if (value[22] == PXA_UI_CANVAS_TEXT_ALIGN_MIDDLE &&
                box_height > font->line_height) {
                text_y += (box_height - font->line_height) / 2;
            } else if (value[22] == PXA_UI_CANVAS_TEXT_ALIGN_BOTTOM &&
                       box_height > font->line_height) {
                text_y += box_height - font->line_height;
            }
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = text_y;
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = text_y + font->line_height - 1;
            lv_draw_label_dsc_init(&descriptor);
            descriptor.text = (const char *)(value + 24);
            descriptor.text_length = length - 24u;
            descriptor.text_local = 1;
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.font = font;
            descriptor.align = value[21] == 1 ? LV_TEXT_ALIGN_CENTER
                               : value[21] == 2 ? LV_TEXT_ALIGN_RIGHT
                                                : LV_TEXT_ALIGN_LEFT;
            lv_draw_label(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20) {
            lv_area_t target_area;
            lv_area_t image_area;
            lv_area_t saved_image_clip;
            lv_draw_image_dsc_t descriptor;
            uint32_t width = pxa_read_u32(value + 8);
            uint32_t height = pxa_read_u32(value + 12);
            int32_t target_width = canvas_pixels(node->ui, width);
            int32_t target_height = canvas_pixels(node->ui, height);
            int64_t scale_x;
            int64_t scale_y;
            if (width == 0 || height == 0 || target_width <= 0 ||
                target_height <= 0)
                continue;
            if (bitmap_image == NULL) continue;
            target_area.x1 = origin.x1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value));
            target_area.y1 = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            target_area.x2 = target_area.x1 + target_width - 1;
            target_area.y2 = target_area.y1 + target_height - 1;
            image_area.x1 = target_area.x1;
            image_area.y1 = target_area.y1;
            image_area.x2 = image_area.x1 + (int32_t)width - 1;
            image_area.y2 = image_area.y1 + (int32_t)height - 1;
            lv_draw_image_dsc_init(&descriptor);
            descriptor.src = bitmap_image;
            descriptor.opa = LV_OPA_COVER;
            scale_x = (int64_t)target_width * LV_SCALE_NONE / width;
            scale_y = (int64_t)target_height * LV_SCALE_NONE / height;
            descriptor.scale_x = scale_x < 1 ? 1
                                 : scale_x > INT32_MAX ? INT32_MAX
                                                       : (int32_t)scale_x;
            descriptor.scale_y = scale_y < 1 ? 1
                                 : scale_y > INT32_MAX ? INT32_MAX
                                                       : (int32_t)scale_y;
            descriptor.image_area = image_area;
            saved_image_clip = layer->_clip_area;
            intersect_clip(&layer->_clip_area, &target_area);
            lv_draw_image(layer, &descriptor, &image_area);
            layer->_clip_area = saved_image_clip;
        } else if ((type == PXA_UI_CANVAS_IMAGE && length > 18) ||
                   (type == PXA_UI_CANVAS_IMAGE_HANDLE && length == 26)) {
            const void *source = prepared_image ? &prepared_image->descriptor : NULL;
            if (type == PXA_UI_CANVAS_IMAGE) {
                pxa_lvgl_ui_canvas_asset_t *asset = find_canvas_asset(
                    node->canvas->assets, value + 18, length - 18u);
                source = asset ? asset->source : NULL;
            }
            if (source != NULL) {
                lv_area_t area;
                lv_area_t image_area;
                lv_area_t image_clip;
                lv_draw_image_dsc_t descriptor;
                lv_image_header_t header;
                int32_t target_width;
                int32_t target_height;
                int32_t scale_x = LV_SCALE_NONE;
                int32_t scale_y = LV_SCALE_NONE;
                int32_t drawn_width;
                int32_t drawn_height;
                int64_t raw_scale_x;
                int64_t raw_scale_y;
                area.x1 = origin.x1 + canvas_pixels(node->ui,
                    (int32_t)pxa_read_u32(value));
                area.y1 = origin.y1 + canvas_pixels(node->ui,
                    (int32_t)pxa_read_u32(value + 4));
                area.x2 = area.x1 + canvas_pixels(node->ui,
                    pxa_read_u32(value + 8)) - 1;
                area.y2 = area.y1 + canvas_pixels(node->ui,
                    pxa_read_u32(value + 12)) - 1;
                lv_draw_image_dsc_init(&descriptor);
                descriptor.src = source;
                descriptor.opa = value[16];
                target_width = lv_area_get_width(&area);
                target_height = lv_area_get_height(&area);
                lv_result_t info_status = LV_RESULT_OK;
                if (prepared_image) header = prepared_image->descriptor.header;
                else info_status = lv_image_decoder_get_info(source, &header);
                if (info_status == LV_RESULT_OK && header.w != 0 && header.h != 0) {
                    raw_scale_x = (int64_t)target_width * LV_SCALE_NONE /
                                  header.w;
                    raw_scale_y = (int64_t)target_height * LV_SCALE_NONE /
                                  header.h;
                    scale_x = raw_scale_x > INT32_MAX
                                  ? INT32_MAX : (int32_t)raw_scale_x;
                    scale_y = raw_scale_y > INT32_MAX
                                  ? INT32_MAX : (int32_t)raw_scale_y;
                    if (value[17] != 1) {
                        int32_t uniform = value[17] == 2
                                              ? LV_MAX(scale_x, scale_y)
                                              : LV_MIN(scale_x, scale_y);
                        scale_x = uniform;
                        scale_y = uniform;
                    }
                    if (scale_x < 1) scale_x = 1;
                    if (scale_y < 1) scale_y = 1;
                    drawn_width = (int32_t)((int64_t)header.w * scale_x /
                                            LV_SCALE_NONE);
                    drawn_height = (int32_t)((int64_t)header.h * scale_y /
                                             LV_SCALE_NONE);
                    image_area.x1 = area.x1 + (target_width - drawn_width) / 2;
                    image_area.y1 = area.y1 + (target_height - drawn_height) / 2;
                    image_area.x2 = image_area.x1 + header.w - 1;
                    image_area.y2 = image_area.y1 + header.h - 1;
                    descriptor.scale_x = scale_x;
                    descriptor.scale_y = scale_y;
                    descriptor.image_area = image_area;
                    image_clip = layer->_clip_area;
                    intersect_clip(&layer->_clip_area, &area);
                    lv_draw_image(layer, &descriptor, &image_area);
                    layer->_clip_area = image_clip;
                } else {
                    lv_draw_image(layer, &descriptor, &area);
                }
            }
        } else if (type == PXA_UI_CANVAS_CLIP_PUSH && length == 16 &&
                   clip_depth < node->canvas->clip_capacity) {
            lv_area_t clip;
            node->canvas->clip_stack[clip_depth++] = layer->_clip_area;
            clip.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            clip.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            clip.x2 = clip.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            clip.y2 = clip.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            intersect_clip(&clip, &layer->_clip_area);
            layer->_clip_area = clip;
        } else if (type == PXA_UI_CANVAS_CLIP_POP && length == 0 &&
                   clip_depth != 0) {
            layer->_clip_area = node->canvas->clip_stack[--clip_depth];
        }
    }
    layer->_clip_area = saved_clip;
}

/* Stable pointer id for multi-touch: the ordinal among pointer input
 * devices. Boards that register several pointer indevs (one per reported
 * touch point) automatically produce distinct ids for Guest pointer
 * events. */
static uint8_t pointer_id_for_indev(lv_indev_t *input) {
    lv_indev_t *item = NULL;
    uint8_t index = 0;
    while ((item = lv_indev_get_next(item)) != NULL) {
        if (item == input) break;
        if (lv_indev_get_type(item) == LV_INDEV_TYPE_POINTER) ++index;
    }
    return index;
}

static void on_canvas_pointer(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_indev_t *input = lv_event_get_indev(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    lv_area_t area;
    uint8_t value[12] = {0};
    uint8_t phase;
    uint16_t flags;
    if (node == NULL || input == NULL) return;
    if (code == LV_EVENT_PRESSED) phase = 0;
    else if (code == LV_EVENT_PRESSING) phase = 1;
    else if (code == LV_EVENT_RELEASED) phase = 2;
    else phase = 3;
    lv_indev_get_point(input, &point);
    lv_obj_get_coords(node->object, &area);
    value[0] = pointer_id_for_indev(input);
    value[1] = phase;
    pxa_write_u32(value + 4, (uint32_t)canvas_logical_pixels(
        node->ui, point.x - area.x1));
    pxa_write_u32(value + 8, (uint32_t)canvas_logical_pixels(
        node->ui, point.y - area.y1));
    flags = phase == 1 ? 0 : PXA_UI_EVENT_FLAG_RELIABLE;
    emit_event(node, input, PXA_UI_EVENT_POINTER, flags, value,
               sizeof(value));
}

void pxa_lvgl_ui_theme_init(pxa_lvgl_ui_theme_t *theme) {
    static const uint32_t colors[PXA_UI_THEME_ROLE_COUNT] = {
        UINT32_C(0x0b1018ff), UINT32_C(0x17212cff),
        UINT32_C(0x23a7d9ff), UINT32_C(0xffffffff),
        UINT32_C(0xf1f5f9ff), UINT32_C(0x91a4b7ff),
        UINT32_C(0x34475aff), UINT32_C(0x34c785ff),
        UINT32_C(0xf5bd4fff), UINT32_C(0xef5d67ff),
        UINT32_C(0x151e29ff), UINT32_C(0x1c2734ff),
        UINT32_C(0x243342ff), UINT32_C(0x2d3e50ff),
        UINT32_C(0x34475aff), UINT32_C(0xc8d5e2ff),
        UINT32_C(0x164762ff), UINT32_C(0xc5ecfaff),
        UINT32_C(0x98c7dbff), UINT32_C(0x0f2d3aff),
        UINT32_C(0x254655ff), UINT32_C(0xcbe9f4ff),
        UINT32_C(0xc6badfff), UINT32_C(0x33294fff),
        UINT32_C(0x463c5bff), UINT32_C(0xe6dcffff),
        UINT32_C(0x44576aff), UINT32_C(0x6b2630ff),
        UINT32_C(0xffd9dfff), UINT32_C(0xe1e8efff),
        UINT32_C(0x263440ff), UINT32_C(0x0b6d95ff)};
    if (theme == NULL) return;
    memset(theme, 0, sizeof(*theme));
    memcpy(theme->rgba, colors, sizeof(colors));
}

size_t pxa_lvgl_ui_workspace_size(void) {
    return sizeof(pxa_lvgl_ui_t) + PXA_LVGL_UI_ALIGNMENT - 1u;
}

pxa_status_t pxa_lvgl_ui_init(void *workspace, size_t workspace_size,
                                 const pxa_lvgl_ui_config_t *config,
                                 pxa_lvgl_ui_t **output,
                                 pxa_ui_backend_t *backend) {
    uintptr_t address;
    uintptr_t aligned;
    pxa_lvgl_ui_t *ui;
    if (output == NULL || backend == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    memset(backend, 0, sizeof(*backend));
    if (workspace == NULL || workspace_size < pxa_lvgl_ui_workspace_size() ||
        config == NULL || config->struct_size < sizeof(*config) ||
        config->allocate == NULL || config->release == NULL ||
        config->execute == NULL ||
        ((config->resolve_asset == NULL) != (config->release_asset == NULL)))
        return PXA_STATUS_INVALID_ARGUMENT;
    address = (uintptr_t)workspace;
    aligned = (address + PXA_LVGL_UI_ALIGNMENT - 1u) &
              ~(uintptr_t)(PXA_LVGL_UI_ALIGNMENT - 1u);
    ui = (pxa_lvgl_ui_t *)aligned;
    memset(ui, 0, sizeof(*ui));
    ui->config = (pxa_lvgl_ui_runtime_config_t){
        .allocator_context=config->allocator_context,
        .allocate=config->allocate,
        .release=config->release,
        .execute_user_data=config->execute_user_data,
        .execute=config->execute,
        .resolve_asset=config->resolve_asset,
        .acquire_image=config->acquire_image,
        .release_asset=config->release_asset,
        .asset_user_data=config->asset_user_data,
        .event_callback=config->event_callback,
        .now_us=config->now_us,
        .callback_user_data=config->callback_user_data,
        .parent_object=config->parent_object,
        .snapshot_limit_bytes=config->snapshot_limit_bytes,
        .alpha_limit_bytes=config->alpha_limit_bytes,
        .sized_text_font_path=config->sized_text_font_path,
    };
    ui->theme = config->theme;
    ui->primary_environment = config->primary_environment;
    if (ui->primary_environment.surface == 0)
        ui->primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    if (ui->primary_environment.density_q16 == 0)
        ui->primary_environment.density_q16 = UINT32_C(1) << 16;
    if (ui->primary_environment.font_scale_q16 == 0)
        ui->primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    ui->magic = PXA_LVGL_UI_MAGIC;
    backend->struct_size = sizeof(*backend);
    backend->context = ui;
    backend->begin = backend_begin;
    backend->apply = backend_apply;
    backend->commit = backend_commit;
    backend->cancel = backend_cancel;
    backend->present_canvas = backend_canvas;
    backend->reset = backend_reset;
    backend->environment_changed = backend_environment;
    backend->text_input_focus = backend_text_input_focus;
    *output = ui;
    return PXA_STATUS_OK;
}

typedef struct {
    pxa_lvgl_ui_t *ui;
    const pxa_lvgl_ui_theme_t *theme;
} theme_update_t;

static void execute_theme(void *data) {
    theme_update_t *update = (theme_update_t *)data;
    pxa_lvgl_ui_node_t *node;
    update->ui->theme = *update->theme;
    for (node = update->ui->nodes; node != NULL; node = node->next) {
        apply_node_colors(node);
        lv_obj_set_style_text_font(node->object,
                                   font_for_role(node->ui, node->font_role), 0);
        if (node->content != NULL)
            lv_obj_set_style_text_font(
                node->content, font_for_role(node->ui, node->font_role), 0);
    }
}

pxa_status_t pxa_lvgl_ui_set_theme(
    pxa_lvgl_ui_t *ui, const pxa_lvgl_ui_theme_t *theme) {
    theme_update_t update;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || theme == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    update.ui = ui;
    update.theme = theme;
    return ui->config.execute(execute_theme, &update,
                              ui->config.execute_user_data);
}

void pxa_lvgl_ui_reset(pxa_lvgl_ui_t *ui) {
    backend_reset(ui);
}

bool pxa_lvgl_ui_alpha_plane(const pxa_lvgl_ui_t *ui,
                              pxa_lvgl_ui_alpha_plane_t *output) {
    if (output == NULL) return false;
    memset(output, 0, sizeof(*output));
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC ||
        ui->alpha.pixels == NULL || ui->alpha.values == NULL ||
        ui->alpha.width == 0 || ui->alpha.height == 0 ||
        ui->alpha.content_width == 0 || ui->alpha.content_height == 0)
        return false;
    {
        const size_t offset =
            (size_t)ui->alpha.content_y * ui->alpha.width +
            ui->alpha.content_x;
        output->pixels = ui->alpha.pixels + offset;
        output->alpha = ui->alpha.values + offset;
    }
    output->pixel_stride_bytes = (uint32_t)ui->alpha.width *
                                 sizeof(*ui->alpha.pixels);
    output->alpha_stride_bytes = ui->alpha.width;
    output->x = ui->alpha.x + ui->alpha.content_x;
    output->y = ui->alpha.y + ui->alpha.content_y;
    output->width = ui->alpha.content_width;
    output->height = ui->alpha.content_height;
    output->revision = ui->alpha.revision;
    return true;
}

uint64_t pxa_lvgl_ui_event_timestamp_us(const pxa_lvgl_ui_t *ui) {
    return ui != NULL && ui->magic == PXA_LVGL_UI_MAGIC
               ? ui->event_timestamp_us
               : 0;
}

void pxa_lvgl_ui_deinit(pxa_lvgl_ui_t *ui) {
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC) return;
    backend_reset(ui);
    lv_lock();
    if (ui->image_decoder) lv_image_decoder_delete(ui->image_decoder);
    ui->image_decoder=NULL;
    lv_unlock();
    ui->magic = 0;
}
