#include "pxsys/headless_renderer.h"

#include <string.h>

#define PXSYS_HEADLESS_MAGIC UINT32_C(0x50584852)

typedef struct {
    pxsys_surface_config_t config;
    uint64_t transaction_id;
    size_t command_bytes;
    uint32_t generation;
    uint8_t visible;
    uint8_t occupied;
    uint8_t* commands;
} surface_entry_t;

struct pxsys_headless_renderer {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_transaction_bytes;
    pxsys_allocator_t allocator;
    pxsys_theme_snapshot_t theme;
    surface_entry_t* surfaces;
};

static int renderer_valid(const pxsys_headless_renderer_t* renderer) {
    return renderer != NULL && renderer->magic == PXSYS_HEADLESS_MAGIC;
}

static surface_entry_t* resolve(const pxsys_headless_renderer_t* renderer,
                                pxsys_surface_ref_t surface) {
    surface_entry_t* entry;
    if (!renderer_valid(renderer) || surface.slot == UINT32_MAX ||
        (size_t)surface.slot >= renderer->capacity) {
        return NULL;
    }
    entry = &renderer->surfaces[surface.slot];
    return entry->occupied && entry->generation == surface.generation ? entry : NULL;
}

void pxsys_headless_renderer_config_init(pxsys_headless_renderer_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_surfaces = 16;
    config->max_transaction_bytes = 4096;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_headless_renderer_create(const pxsys_headless_renderer_config_t* config,
                                              pxsys_headless_renderer_t** output) {
    pxsys_headless_renderer_t* renderer;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) || config->max_surfaces == 0 ||
        config->max_surfaces > UINT32_MAX ||
        config->max_surfaces > SIZE_MAX / sizeof(surface_entry_t) ||
        config->max_transaction_bytes == 0 ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    renderer = (pxsys_headless_renderer_t*)config->allocator.allocate(config->allocator.context,
                                                                      sizeof(*renderer));
    if (renderer == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(renderer, 0, sizeof(*renderer));
    renderer->surfaces = (surface_entry_t*)config->allocator.allocate(
        config->allocator.context, config->max_surfaces * sizeof(*renderer->surfaces));
    if (renderer->surfaces == NULL) {
        config->allocator.release(config->allocator.context, renderer);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(renderer->surfaces, 0, config->max_surfaces * sizeof(*renderer->surfaces));
    renderer->capacity = config->max_surfaces;
    renderer->max_transaction_bytes = config->max_transaction_bytes;
    renderer->allocator = config->allocator;
    pxsys_theme_snapshot_init(&renderer->theme, PXSYS_COLOR_SCHEME_LIGHT);
    renderer->magic = PXSYS_HEADLESS_MAGIC;
    *output = renderer;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_headless_renderer_destroy(pxsys_headless_renderer_t* renderer) {
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

static pxsys_status_t surface_create(void* context, const pxsys_surface_config_t* config,
                                     pxsys_surface_ref_t* surface) {
    pxsys_headless_renderer_t* renderer = (pxsys_headless_renderer_t*)context;
    surface_entry_t* entry;
    size_t slot;
    if (surface == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    surface->slot = UINT32_MAX;
    surface->generation = 0;
    if (!renderer_valid(renderer) || config == NULL || config->struct_size < sizeof(*config) ||
        config->width == 0 || config->height == 0 || config->role > PXSYS_SURFACE_RAW_CONTENT) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (renderer->count == renderer->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (slot = 0; slot < renderer->capacity; ++slot) {
        if (!renderer->surfaces[slot].occupied)
            break;
    }
    entry = &renderer->surfaces[slot];
    entry->commands = (uint8_t*)renderer->allocator.allocate(renderer->allocator.context,
                                                             renderer->max_transaction_bytes);
    if (entry->commands == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    entry->transaction_id = 0;
    entry->command_bytes = 0;
    entry->visible = 0;
    entry->config = *config;
    entry->config.struct_size = sizeof(entry->config);
    entry->generation = entry->generation == UINT32_MAX ? 1 : entry->generation + 1u;
    entry->occupied = 1;
    renderer->count++;
    surface->slot = (uint32_t)slot;
    surface->generation = entry->generation;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_destroy(void* context, pxsys_surface_ref_t surface) {
    pxsys_headless_renderer_t* renderer = (pxsys_headless_renderer_t*)context;
    surface_entry_t* entry = resolve(renderer, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    renderer->allocator.release(renderer->allocator.context, entry->commands);
    entry->commands = NULL;
    entry->occupied = 0;
    renderer->count--;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t surface_set_visible(void* context, pxsys_surface_ref_t surface, int visible) {
    surface_entry_t* entry = resolve((pxsys_headless_renderer_t*)context, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    entry->visible = visible != 0;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t apply(void* context, pxsys_surface_ref_t surface,
                            const pxsys_ui_transaction_t* transaction) {
    pxsys_headless_renderer_t* renderer = (pxsys_headless_renderer_t*)context;
    surface_entry_t* entry = resolve(renderer, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (transaction == NULL || transaction->struct_size < sizeof(*transaction) ||
        transaction->commands.size > renderer->max_transaction_bytes ||
        (transaction->commands.size != 0 && transaction->commands.data == NULL)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (transaction->commands.size != 0) {
        memcpy(entry->commands, transaction->commands.data, transaction->commands.size);
    }
    entry->command_bytes = transaction->commands.size;
    entry->transaction_id = transaction->transaction_id;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t theme_changed(void* context, const pxsys_theme_snapshot_t* theme) {
    pxsys_headless_renderer_t* renderer = (pxsys_headless_renderer_t*)context;
    if (!renderer_valid(renderer) ||
        pxsys_theme_snapshot_validate(theme) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    renderer->theme = *theme;
    renderer->theme.struct_size = sizeof(renderer->theme);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_headless_renderer_provider(pxsys_headless_renderer_t* renderer,
                                                pxsys_renderer_provider_t* provider) {
    if (!renderer_valid(renderer) || provider == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(provider, 0, sizeof(*provider));
    provider->struct_size = sizeof(*provider);
    provider->renderer_id = pxsys_string_from_cstr(PXSYS_HEADLESS_RENDERER_ID);
    provider->version = (pxsys_version_t){0, 1};
    provider->context = renderer;
    provider->surface_create = surface_create;
    provider->surface_destroy = surface_destroy;
    provider->surface_set_visible = surface_set_visible;
    provider->apply = apply;
    provider->theme_changed = theme_changed;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_headless_renderer_snapshot(const pxsys_headless_renderer_t* renderer,
                                                pxsys_surface_ref_t surface,
                                                pxsys_headless_surface_snapshot_t* snapshot) {
    surface_entry_t* entry;
    if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    entry = resolve(renderer, surface);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    snapshot->config = entry->config;
    snapshot->last_transaction_id = entry->transaction_id;
    snapshot->command_bytes = entry->command_bytes;
    snapshot->visible = entry->visible;
    return PXSYS_STATUS_OK;
}
