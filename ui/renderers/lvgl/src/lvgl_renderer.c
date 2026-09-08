#include "pxsys/lvgl_renderer.h"

#include <string.h>

#define PXSYS_LVGL_RENDERER_MAGIC UINT32_C(0x50584c56)

typedef struct {
    pxsys_surface_config_t config;
    lv_obj_t* root;
    uint64_t transaction_id;
    uint32_t generation;
    uint8_t occupied;
    uint8_t visible;
} surface_entry_t;

struct pxsys_lvgl_renderer {
    uint32_t magic;
    size_t capacity;
    size_t count;
    lv_obj_t* parent;
    void* transaction_context;
    pxsys_lvgl_transaction_fn apply_transaction;
    pxsys_allocator_t allocator;
    pxsys_theme_snapshot_t theme;
    surface_entry_t* surfaces;
};

static int renderer_valid(const pxsys_lvgl_renderer_t* renderer) {
    return renderer != NULL && renderer->magic == PXSYS_LVGL_RENDERER_MAGIC;
}

static int theme_valid(const pxsys_theme_snapshot_t* theme) {
    return theme != NULL && theme->struct_size >= sizeof(*theme) &&
           theme->effective_scheme <= PXSYS_COLOR_SCHEME_DARK &&
           theme->contrast <= PXSYS_CONTRAST_HIGH;
}

static surface_entry_t* resolve(const pxsys_lvgl_renderer_t* renderer,
                                pxsys_surface_ref_t surface) {
    surface_entry_t* entry;
    if (!renderer_valid(renderer) || surface.slot == UINT32_MAX ||
        (size_t)surface.slot >= renderer->capacity) {
        return NULL;
    }
    entry = &renderer->surfaces[surface.slot];
    return entry->occupied && entry->generation == surface.generation ? entry : NULL;
}

static lv_color_t color_value(uint32_t argb) {
    return lv_color_hex(argb & UINT32_C(0x00ffffff));
}

static lv_opa_t color_opacity(uint32_t argb) {
    return (lv_opa_t)(argb >> 24);
}

static uint32_t background_token(const surface_entry_t* entry) {
    switch (entry->config.role) {
    case PXSYS_SURFACE_DIALOG:
    case PXSYS_SURFACE_SYSTEM_BAR:
        return PXSYS_COLOR_SURFACE;
    case PXSYS_SURFACE_OVERLAY:
        return PXSYS_COLOR_SCRIM;
    case PXSYS_SURFACE_APPLICATION:
    case PXSYS_SURFACE_RAW_CONTENT:
    default:
        return PXSYS_COLOR_BACKGROUND;
    }
}

static void apply_surface_theme(pxsys_lvgl_renderer_t* renderer,
                                surface_entry_t* entry) {
    const uint32_t background = renderer->theme.colors[background_token(entry)];
    lv_obj_set_style_bg_color(entry->root, color_value(background), 0);
    lv_obj_set_style_bg_opa(entry->root, color_opacity(background), 0);
    lv_obj_set_style_text_color(
        entry->root, color_value(renderer->theme.colors[PXSYS_COLOR_TEXT_PRIMARY]), 0);
    lv_obj_set_style_border_color(
        entry->root, color_value(renderer->theme.colors[PXSYS_COLOR_BORDER]), 0);
}

void pxsys_lvgl_renderer_config_init(pxsys_lvgl_renderer_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_surfaces = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_lvgl_renderer_create(const pxsys_lvgl_renderer_config_t* config,
                                          pxsys_lvgl_renderer_t** output) {
    pxsys_lvgl_renderer_t* renderer;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_surfaces == 0 || config->max_surfaces > UINT32_MAX ||
        config->max_surfaces > SIZE_MAX / sizeof(surface_entry_t) ||
        config->parent == NULL ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    renderer = (pxsys_lvgl_renderer_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*renderer));
    if (renderer == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(renderer, 0, sizeof(*renderer));
    renderer->surfaces = (surface_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_surfaces * sizeof(*renderer->surfaces));
    if (renderer->surfaces == NULL) {
        config->allocator.release(config->allocator.context, renderer);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(renderer->surfaces, 0,
           config->max_surfaces * sizeof(*renderer->surfaces));
    renderer->capacity = config->max_surfaces;
    renderer->parent = config->parent;
    renderer->transaction_context = config->transaction_context;
    renderer->apply_transaction = config->apply_transaction;
    renderer->allocator = config->allocator;
    pxsys_theme_snapshot_init(&renderer->theme, PXSYS_COLOR_SCHEME_LIGHT);
    renderer->magic = PXSYS_LVGL_RENDERER_MAGIC;
    *output = renderer;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_lvgl_renderer_destroy(pxsys_lvgl_renderer_t* renderer) {
    pxsys_allocator_t allocator;
    if (!renderer_valid(renderer))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (renderer->count != 0)
        return PXSYS_STATUS_BUSY;
    allocator = renderer->allocator;
    renderer->magic = 0;
    allocator.release(allocator.context, renderer->surfaces);
    allocator.release(allocator.context, renderer);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_create(void* context,
                                     const pxsys_surface_config_t* config,
                                     pxsys_surface_ref_t* surface) {
    pxsys_lvgl_renderer_t* renderer = (pxsys_lvgl_renderer_t*)context;
    surface_entry_t* entry;
    size_t slot;
    if (surface == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    surface->slot = UINT32_MAX;
    surface->generation = 0;
    if (!renderer_valid(renderer) || config == NULL ||
        config->struct_size < sizeof(*config) || config->width == 0 ||
        config->height == 0 || config->role > PXSYS_SURFACE_RAW_CONTENT) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (renderer->count == renderer->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (slot = 0; slot < renderer->capacity; ++slot) {
        if (!renderer->surfaces[slot].occupied)
            break;
    }
    entry = &renderer->surfaces[slot];
    entry->root = lv_obj_create(renderer->parent);
    if (entry->root == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    entry->config = *config;
    entry->config.struct_size = sizeof(entry->config);
    entry->generation = entry->generation == UINT32_MAX ? 1 : entry->generation + 1u;
    entry->transaction_id = 0;
    entry->visible = 0;
    entry->occupied = 1;
    lv_obj_set_pos(entry->root, 0, 0);
    lv_obj_set_size(entry->root, (int32_t)config->width, (int32_t)config->height);
    lv_obj_set_style_pad_all(entry->root, 0, 0);
    lv_obj_set_style_border_width(entry->root, 0, 0);
    lv_obj_set_style_radius(entry->root, 0, 0);
    lv_obj_set_scrollbar_mode(entry->root, LV_SCROLLBAR_MODE_OFF);
    /* A surface container owns composition and visibility, not input. Its
     * interactive descendants receive events; an empty container must not
     * cover a runtime-owned UI tree rendered on the same LVGL screen. */
    lv_obj_remove_flag(entry->root,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(entry->root, LV_OBJ_FLAG_HIDDEN);
    apply_surface_theme(renderer, entry);
    renderer->count++;
    surface->slot = (uint32_t)slot;
    surface->generation = entry->generation;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_destroy(void* context, pxsys_surface_ref_t surface) {
    pxsys_lvgl_renderer_t* renderer = (pxsys_lvgl_renderer_t*)context;
    surface_entry_t* entry = resolve(renderer, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    lv_obj_delete(entry->root);
    entry->root = NULL;
    entry->occupied = 0;
    entry->visible = 0;
    renderer->count--;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_set_visible(void* context,
                                          pxsys_surface_ref_t surface,
                                          int visible) {
    surface_entry_t* entry = resolve((pxsys_lvgl_renderer_t*)context, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (visible) {
        lv_obj_remove_flag(entry->root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(entry->root);
    } else {
        lv_obj_add_flag(entry->root, LV_OBJ_FLAG_HIDDEN);
    }
    entry->visible = visible != 0;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t apply(void* context, pxsys_surface_ref_t surface,
                            const pxsys_ui_transaction_t* transaction) {
    pxsys_lvgl_renderer_t* renderer = (pxsys_lvgl_renderer_t*)context;
    surface_entry_t* entry = resolve(renderer, surface);
    pxsys_status_t status;
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (transaction == NULL || transaction->struct_size < sizeof(*transaction) ||
        (transaction->commands.size != 0 && transaction->commands.data == NULL)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (renderer->apply_transaction == NULL) {
        if (transaction->commands.size != 0)
            return PXSYS_STATUS_UNSUPPORTED;
        entry->transaction_id = transaction->transaction_id;
        return PXSYS_STATUS_OK;
    }
    status = renderer->apply_transaction(renderer->transaction_context, entry->root,
                                         transaction);
    if (status == PXSYS_STATUS_OK)
        entry->transaction_id = transaction->transaction_id;
    return status;
}

static pxsys_status_t theme_changed(void* context,
                                    const pxsys_theme_snapshot_t* theme) {
    pxsys_lvgl_renderer_t* renderer = (pxsys_lvgl_renderer_t*)context;
    size_t index;
    if (!renderer_valid(renderer) || !theme_valid(theme))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    renderer->theme = *theme;
    renderer->theme.struct_size = sizeof(renderer->theme);
    for (index = 0; index < renderer->capacity; ++index) {
        if (renderer->surfaces[index].occupied)
            apply_surface_theme(renderer, &renderer->surfaces[index]);
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_lvgl_renderer_provider(pxsys_lvgl_renderer_t* renderer,
                                            pxsys_renderer_provider_t* provider) {
    if (!renderer_valid(renderer) || provider == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(provider, 0, sizeof(*provider));
    provider->struct_size = sizeof(*provider);
    provider->renderer_id = pxsys_string_from_cstr(PXSYS_LVGL_RENDERER_ID);
    provider->version = (pxsys_version_t){0, 1};
    provider->features = PXSYS_LVGL_RENDERER_FEATURE_NATIVE_ROOT;
    provider->context = renderer;
    provider->surface_create = surface_create;
    provider->surface_destroy = surface_destroy;
    provider->surface_set_visible = surface_set_visible;
    provider->apply = apply;
    provider->theme_changed = theme_changed;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_lvgl_renderer_surface_root(pxsys_lvgl_renderer_t* renderer,
                                                pxsys_surface_ref_t surface,
                                                lv_obj_t** root) {
    surface_entry_t* entry;
    if (root == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *root = NULL;
    entry = resolve(renderer, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    *root = entry->root;
    return PXSYS_STATUS_OK;
}
