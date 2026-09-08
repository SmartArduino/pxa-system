#include "ui/ui_internal.h"

#include <limits.h>
#include <string.h>

void pxa_ui_config_init(pxa_ui_config_t *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_dynamic_bytes = 1024u * 1024u;
    config->max_transaction_bytes = 256u * 1024u;
    config->max_canvas_bytes = 256u * 1024u;
    config->features = 0;
    config->density_q16 = UINT32_C(1) << 16;
    config->font_scale_q16 = UINT32_C(1) << 16;
    config->color_scheme = PXA_UI_COLOR_SCHEME_LIGHT;
}

void *pxa_ui_alloc(pxa_ui_service_t *service, size_t size) {
    pxa_ui_alloc_header_t *header;
    size_t total;
    if (service == NULL || size == 0 || service->config.allocate == NULL ||
        size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }
    total = sizeof(*header) + size;
    if (service->current_bytes > service->config.max_dynamic_bytes ||
        total > service->config.max_dynamic_bytes - service->current_bytes)
        return NULL;
    header = (pxa_ui_alloc_header_t *)service->config.allocate(
        service->config.allocator_context, total);
    if (header == NULL) return NULL;
    header->size = total;
    service->current_bytes += total;
    if (service->current_bytes > service->peak_bytes)
        service->peak_bytes = service->current_bytes;
    return header + 1;
}

void pxa_ui_free(pxa_ui_service_t *service, void *memory) {
    pxa_ui_alloc_header_t *header;
    if (service == NULL || memory == NULL || service->config.release == NULL)
        return;
    header = (pxa_ui_alloc_header_t *)memory - 1;
    if (header->size <= service->current_bytes)
        service->current_bytes -= header->size;
    else
        service->current_bytes = 0;
    service->config.release(service->config.allocator_context, header);
}

void *pxa_ui_grow(pxa_ui_service_t *service, void *memory,
                     size_t used, size_t *capacity, size_t required,
                     size_t maximum) {
    size_t next;
    void *replacement;
    if (service == NULL || capacity == NULL || used > *capacity ||
        required > maximum || required < used)
        return NULL;
    if (required <= *capacity) return memory;
    next = *capacity == 0 ? (maximum < 256u ? maximum : 256u) : *capacity;
    while (next < required) {
        size_t doubled = next <= SIZE_MAX / 2u ? next * 2u : SIZE_MAX;
        doubled = doubled > maximum ? maximum : doubled;
        if (doubled <= next) return NULL;
        next = doubled;
    }
    replacement = pxa_ui_alloc(service, next);
    if (replacement == NULL) return NULL;
    if (used != 0) memcpy(replacement, memory, used);
    pxa_ui_free(service, memory);
    *capacity = next;
    return replacement;
}

pxa_ui_entry_t *pxa_ui_find_entry(pxa_ui_service_t *service,
                                        pxa_component_t component) {
    pxa_ui_entry_t *entry;
    if (service == NULL || component == PXA_COMPONENT_INVALID) return NULL;
    for (entry = service->entries; entry != NULL; entry = entry->next)
        if (entry->component == component) return entry;
    return NULL;
}

const pxa_ui_entry_t *pxa_ui_find_entry_const(
    const pxa_ui_service_t *service, pxa_component_t component) {
    return pxa_ui_find_entry((pxa_ui_service_t *)service, component);
}

pxa_ui_surface_t *pxa_ui_find_surface(pxa_ui_entry_t *entry,
                                            uint32_t surface) {
    pxa_ui_surface_t *current;
    if (entry == NULL || surface == 0) return NULL;
    for (current = entry->surfaces; current != NULL; current = current->next)
        if (current->id == surface) return current;
    return NULL;
}

const pxa_ui_surface_t *pxa_ui_find_surface_const(
    const pxa_ui_entry_t *entry, uint32_t surface) {
    return pxa_ui_find_surface((pxa_ui_entry_t *)entry, surface);
}
