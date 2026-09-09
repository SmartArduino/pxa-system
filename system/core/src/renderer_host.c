#include "pxsys/renderer_host.h"

#include <string.h>

#define PXSYS_RENDERER_HOST_MAGIC UINT32_C(0x50585248)

struct pxsys_renderer_host {
    uint32_t magic;
    size_t max_renderer_id_bytes;
    size_t surface_count;
    pxsys_allocator_t allocator;
    pxsys_theme_snapshot_t theme;
    pxsys_renderer_provider_t provider;
    char* renderer_id;
};

static int host_valid(const pxsys_renderer_host_t* host) {
    return host != NULL && host->magic == PXSYS_RENDERER_HOST_MAGIC;
}

static int theme_valid(const pxsys_theme_snapshot_t* theme) {
    return pxsys_theme_snapshot_validate(theme) == PXSYS_STATUS_OK;
}

static int provider_valid(const pxsys_renderer_host_t* host,
                          const pxsys_renderer_provider_t* provider) {
    return provider != NULL && provider->struct_size >= sizeof(*provider) &&
           pxsys_identifier_validate(provider->renderer_id,
                                     host->max_renderer_id_bytes) &&
           provider->surface_create != NULL && provider->surface_destroy != NULL &&
           provider->surface_set_visible != NULL && provider->apply != NULL &&
           provider->theme_changed != NULL;
}

void pxsys_renderer_host_config_init(pxsys_renderer_host_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_renderer_id_bytes = 64;
    pxsys_theme_snapshot_init(&config->initial_theme, PXSYS_COLOR_SCHEME_LIGHT);
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_renderer_host_create(const pxsys_renderer_host_config_t* config,
                                          pxsys_renderer_host_t** output) {
    pxsys_renderer_host_t* host;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_renderer_id_bytes == 0 || config->max_renderer_id_bytes == SIZE_MAX ||
        !theme_valid(&config->initial_theme) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    host = (pxsys_renderer_host_t*)config->allocator.allocate(config->allocator.context,
                                                              sizeof(*host));
    if (host == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(host, 0, sizeof(*host));
    host->max_renderer_id_bytes = config->max_renderer_id_bytes;
    host->allocator = config->allocator;
    host->theme = config->initial_theme;
    host->theme.struct_size = sizeof(host->theme);
    host->magic = PXSYS_RENDERER_HOST_MAGIC;
    *output = host;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_renderer_host_destroy(pxsys_renderer_host_t* host) {
    pxsys_allocator_t allocator;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->surface_count != 0)
        return PXSYS_STATUS_BUSY;
    allocator = host->allocator;
    if (host->renderer_id != NULL)
        allocator.release(allocator.context, host->renderer_id);
    host->magic = 0;
    allocator.release(allocator.context, host);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_renderer_host_bind(pxsys_renderer_host_t* host,
                                        const pxsys_renderer_provider_t* provider) {
    char* renderer_id;
    pxsys_status_t status;
    if (!host_valid(host) || !provider_valid(host, provider))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->renderer_id != NULL)
        return PXSYS_STATUS_ALREADY_EXISTS;
    renderer_id = (char*)host->allocator.allocate(host->allocator.context,
                                                  provider->renderer_id.size + 1u);
    if (renderer_id == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memcpy(renderer_id, provider->renderer_id.data, provider->renderer_id.size);
    renderer_id[provider->renderer_id.size] = '\0';
    status = provider->theme_changed(provider->context, &host->theme);
    if (status != PXSYS_STATUS_OK) {
        host->allocator.release(host->allocator.context, renderer_id);
        return status;
    }
    host->provider = *provider;
    host->provider.struct_size = sizeof(host->provider);
    host->provider.renderer_id = pxsys_string(renderer_id, provider->renderer_id.size);
    host->renderer_id = renderer_id;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_renderer_host_unbind(pxsys_renderer_host_t* host,
                                          pxsys_string_t renderer_id) {
    if (!host_valid(host) || !pxsys_identifier_validate(renderer_id,
                                                        host->max_renderer_id_bytes))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->renderer_id == NULL || host->provider.renderer_id.size != renderer_id.size ||
        memcmp(host->renderer_id, renderer_id.data, renderer_id.size) != 0)
        return PXSYS_STATUS_NOT_FOUND;
    if (host->surface_count != 0)
        return PXSYS_STATUS_BUSY;
    host->allocator.release(host->allocator.context, host->renderer_id);
    host->renderer_id = NULL;
    memset(&host->provider, 0, sizeof(host->provider));
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_renderer_host_info(const pxsys_renderer_host_t* host,
                                        pxsys_renderer_info_t* output) {
    if (!host_valid(host) || output == NULL || output->struct_size < sizeof(*output))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->renderer_id == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    output->struct_size = sizeof(*output);
    output->renderer_id = host->provider.renderer_id;
    output->version = host->provider.version;
    output->features = host->provider.features;
    output->surface_count = host->surface_count;
    return PXSYS_STATUS_OK;
}

size_t pxsys_renderer_host_surface_count(const pxsys_renderer_host_t* host) {
    return host_valid(host) ? host->surface_count : 0;
}

pxsys_status_t pxsys_renderer_host_set_theme(pxsys_renderer_host_t* host,
                                             const pxsys_theme_snapshot_t* theme) {
    pxsys_status_t status;
    if (!host_valid(host) || !theme_valid(theme))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->renderer_id != NULL) {
        status = host->provider.theme_changed(host->provider.context, theme);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    host->theme = *theme;
    host->theme.struct_size = sizeof(host->theme);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_renderer_surface_create(pxsys_renderer_host_t* host,
                                             const pxsys_surface_config_t* config,
                                             pxsys_surface_ref_t* surface) {
    pxsys_status_t status;
    if (!host_valid(host) || surface == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    surface->slot = UINT32_MAX;
    surface->generation = 0;
    if (host->renderer_id == NULL)
        return PXSYS_STATUS_UNAVAILABLE;
    if (host->surface_count == SIZE_MAX)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    status = host->provider.surface_create(host->provider.context, config, surface);
    if (status == PXSYS_STATUS_OK)
        host->surface_count++;
    return status;
}

pxsys_status_t pxsys_renderer_surface_destroy(pxsys_renderer_host_t* host,
                                              pxsys_surface_ref_t surface) {
    pxsys_status_t status;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->renderer_id == NULL)
        return PXSYS_STATUS_UNAVAILABLE;
    status = host->provider.surface_destroy(host->provider.context, surface);
    if (status == PXSYS_STATUS_OK) {
        if (host->surface_count == 0)
            return PXSYS_STATUS_INTERNAL;
        host->surface_count--;
    }
    return status;
}

pxsys_status_t pxsys_renderer_surface_set_visible(pxsys_renderer_host_t* host,
                                                  pxsys_surface_ref_t surface,
                                                  int visible) {
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    return host->renderer_id == NULL
               ? PXSYS_STATUS_UNAVAILABLE
               : host->provider.surface_set_visible(host->provider.context, surface,
                                                    visible != 0);
}

pxsys_status_t pxsys_renderer_apply(pxsys_renderer_host_t* host,
                                    pxsys_surface_ref_t surface,
                                    const pxsys_ui_transaction_t* transaction) {
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    return host->renderer_id == NULL
               ? PXSYS_STATUS_UNAVAILABLE
               : host->provider.apply(host->provider.context, surface, transaction);
}
