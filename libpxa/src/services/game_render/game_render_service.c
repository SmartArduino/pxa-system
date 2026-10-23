#include "pxa/game_render.h"
#include "pxa/assets.h"
#include "pxa/service.h"

#include "common/checked_math.h"
#include "common/status_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_GAME_RENDER_MAGIC UINT32_C(0x50584752)
#define PXA_GAME_RENDER_SLOT_MAGIC UINT32_C(0x50584743)
#define PXA_GAME_RENDER_SLOT_NONE UINT16_MAX

typedef struct pxa_game_render_resource pxa_game_render_resource_t;

struct pxa_game_render_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_game_render_backend_t backend;
    pxa_game_render_resource_t *contexts;
    uint16_t max_contexts;
    uint16_t max_contexts_per_component;
    uint16_t free_head;
    uint16_t active_head;
    uint8_t min_buffer_count;
    uint8_t max_buffer_count;
    pxa_game_render_target_profile_t auto_target_profile;
    uint8_t registered;
    pxa_raster_spans_execute_fn execute_spans;
    void *execute_spans_context;
    uint64_t (*now_us)(void *);
    void *clock_context;
    const pxa_memory_allocator_t *span_prepare_allocator;
    uint32_t span_prepare_bytes;
};

struct pxa_game_render_resource {
    uint32_t magic;
    pxa_game_render_service_t *service;
    pxa_component_t component;
    uint64_t provider_context;
    uint16_t next;
    uint16_t previous;
    uint32_t max_spans;
    uint64_t batches, rejected, spans, pixels, validation_us, execution_us;
    uint32_t last_validation_us, last_execution_us;
    void *span_prepare;
    uint8_t span_prepare_attempted;
};

static int scale_valid(uint8_t scale) {
    return scale != 0 && scale <= PXA_GAME_RENDER_MAX_SCALE;
}

static int auto_target_profile_valid(
    const pxa_game_render_target_profile_t *profile) {
    return profile->display_width != 0 && profile->display_height != 0 &&
           (profile->supported_scale_mask &
            ~PXA_GAME_RENDER_KNOWN_SCALE_MASK) == 0 &&
           scale_valid(profile->default_scale) &&
           (profile->supported_scale_mask &
            PXA_GAME_RENDER_SCALE_MASK(profile->default_scale)) != 0;
}

static int auto_target_profile_disabled(
    const pxa_game_render_target_profile_t *profile) {
    return profile->display_width == 0 && profile->display_height == 0 &&
           profile->supported_scale_mask == 0 && profile->default_scale == 0;
}

static int auto_target_resolve(
    const pxa_game_render_target_profile_t *profile, uint8_t requested_scale,
    pxa_game_render_target_t *target) {
    uint8_t scale;
    if (!auto_target_profile_valid(profile) || target == NULL ||
        (requested_scale != 0 && !scale_valid(requested_scale)))
        return 0;
    scale = requested_scale != 0 ? requested_scale : profile->default_scale;
    if ((profile->supported_scale_mask & PXA_GAME_RENDER_SCALE_MASK(scale)) ==
            0 ||
        profile->display_width % scale != 0 ||
        profile->display_height % scale != 0)
        return 0;
    target->display_width = profile->display_width;
    target->display_height = profile->display_height;
    target->render_width = (uint16_t)(profile->display_width / scale);
    target->render_height = (uint16_t)(profile->display_height / scale);
    target->render_scale = scale;
    target->supported_scale_mask = profile->supported_scale_mask;
    return 1;
}

static int config_valid(const pxa_game_render_config_t *config) {
    int display_backend;
    if (!config) return 0;
    display_backend = config->backend.create || config->backend.upload ||
        config->backend.submit || config->backend.query || config->backend.close ||
        config->backend.bind_assets;
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->span_prepare_bytes <= 1024u * 1024u &&
           config->max_contexts != 0 &&
           config->max_contexts_per_component != 0 &&
           config->max_contexts_per_component <= config->max_contexts &&
           (!display_backend || (config->min_buffer_count >= 2 &&
           config->min_buffer_count <= config->max_buffer_count &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.create != NULL && config->backend.upload != NULL &&
           config->backend.submit != NULL && config->backend.query != NULL &&
           config->backend.close != NULL)) &&
           (auto_target_profile_disabled(&config->auto_target_profile) ||
            auto_target_profile_valid(&config->auto_target_profile));
}

size_t pxa_game_render_service_workspace_size(
    const pxa_game_render_config_t *config) {
    size_t slots_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        sizeof(pxa_game_render_resource_t) >
            SIZE_MAX / (size_t)config->max_contexts)
        return 0;
    slots_size = (size_t)config->max_contexts *
                 sizeof(pxa_game_render_resource_t);
    if (sizeof(pxa_game_render_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_game_render_service_t);
    if (slots_size > SIZE_MAX - size) return 0;
    return size + slots_size;
}

pxa_status_t pxa_game_render_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_game_render_config_t *config,
    pxa_game_render_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_game_render_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_game_render_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size)
        return PXA_STATUS_INVALID_ARGUMENT;
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace,
                                        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_game_render_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->contexts = (pxa_game_render_resource_t *)cursor;
    cursor += (size_t)config->max_contexts * sizeof(service->contexts[0]);
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->backend = config->backend;
    service->execute_spans = config->execute_spans;
    service->execute_spans_context = config->execute_spans_context;
    service->now_us = config->now_us;
    service->clock_context = config->clock_context;
    service->span_prepare_allocator = config->span_prepare_allocator;
    service->span_prepare_bytes = config->span_prepare_bytes;
    service->max_contexts = config->max_contexts;
    service->max_contexts_per_component = config->max_contexts_per_component;
    service->min_buffer_count = config->min_buffer_count;
    service->max_buffer_count = config->max_buffer_count;
    service->auto_target_profile = config->auto_target_profile;
    service->free_head = 0;
    service->active_head = PXA_GAME_RENDER_SLOT_NONE;
    memset(service->contexts, 0, (size_t)config->max_contexts * sizeof(service->contexts[0]));
    for (index = 0; index < service->max_contexts; ++index) {
        service->contexts[index].magic = PXA_GAME_RENDER_SLOT_MAGIC;
        service->contexts[index].service = service;
        service->contexts[index].next =
            index + 1u < service->max_contexts
                ? (uint16_t)(index + 1u)
                : PXA_GAME_RENDER_SLOT_NONE;
        service->contexts[index].previous = PXA_GAME_RENDER_SLOT_NONE;
    }
    service->magic = PXA_GAME_RENDER_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_game_render_service_t *service) {
    return service != NULL && service->magic == PXA_GAME_RENDER_MAGIC;
}

static pxa_game_render_resource_t *allocate_context(
    pxa_game_render_service_t *service, pxa_component_t component) {
    pxa_game_render_resource_t *resource;
    uint16_t cursor;
    uint16_t count = 0;
    for (cursor = service->active_head; cursor != PXA_GAME_RENDER_SLOT_NONE;
         cursor = service->contexts[cursor].next) {
        if (service->contexts[cursor].component == component) ++count;
    }
    if (count >= service->max_contexts_per_component ||
        service->free_head == PXA_GAME_RENDER_SLOT_NONE)
        return NULL;
    cursor = service->free_head;
    resource = &service->contexts[cursor];
    service->free_head = resource->next;
    resource->previous = PXA_GAME_RENDER_SLOT_NONE;
    resource->next = service->active_head;
    if (service->active_head != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[service->active_head].previous = cursor;
    service->active_head = cursor;
    resource->component = component;
    return resource;
}

static void release_context(pxa_game_render_resource_t *resource) {
    pxa_game_render_service_t *service = resource->service;
    uint16_t index = (uint16_t)(resource - service->contexts);
    pxa_memory_release(resource->span_prepare);
    resource->span_prepare = NULL;
    resource->span_prepare_attempted = 0;
    if (resource->previous != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[resource->previous].next = resource->next;
    else
        service->active_head = resource->next;
    if (resource->next != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[resource->next].previous = resource->previous;
    resource->component = PXA_COMPONENT_INVALID;
    resource->provider_context = 0;
    resource->previous = PXA_GAME_RENDER_SLOT_NONE;
    resource->next = service->free_head;
    service->free_head = index;
}

static int context_valid(const pxa_game_render_resource_t *resource) {
    return resource != NULL && resource->magic == PXA_GAME_RENDER_SLOT_MAGIC &&
           service_valid(resource->service) &&
           resource->component != PXA_COMPONENT_INVALID &&
           (resource->provider_context != 0 || resource->max_spans != 0);
}

static void close_context(void *context) {
    pxa_game_render_resource_t *resource = context;
    if (!context_valid(resource)) return;
    if (resource->provider_context)
        resource->service->backend.close(resource->service->backend.context,
                                         resource->provider_context);
    resource->provider_context = 0;
    resource->max_spans = 0;
    release_context(resource);
}

static uint64_t now_us(pxa_game_render_service_t *service) {
    return service->now_us ? service->now_us(service->clock_context) : 0;
}

static int32_t span_context_io(pxa_game_render_resource_t *r,
    uint32_t operation, uint8_t *data, size_t size) {
    pxa_game_render_service_t *service = r->service;
    if (operation == PXA_GAME_RENDER_IO_SPAN_TELEMETRY) {
        if (size != PXA_GAME_RENDER_SPAN_TELEMETRY_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
        pxa_write_u64(data, r->batches); pxa_write_u64(data + 8, r->rejected);
        pxa_write_u64(data + 16, r->spans); pxa_write_u64(data + 24, r->pixels);
        pxa_write_u64(data + 32, r->validation_us);
        pxa_write_u64(data + 40, r->execution_us);
        pxa_write_u32(data + 48, r->last_validation_us);
        pxa_write_u32(data + 52, r->last_execution_us);
        pxa_write_u32(data + 56, r->max_spans); pxa_write_u32(data + 60, 0);
        return (int32_t)size;
    }
    if (operation == PXA_GAME_RENDER_IO_EXECUTE_SPANS ||
        operation == PXA_GAME_RENDER_IO_EXECUTE_PROJECTED_SPANS ||
        operation == PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS ||
        operation == PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME ||
        operation == PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT ||
        operation == PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES) {
        pxa_raster_spans_t batch;
        uint8_t *memory;
        size_t memory_size;
        uint64_t before = now_us(service), validated, done;
        pxa_status_t status = pxa_runtime_borrow_memory(service->runtime,
            r->component, &memory, &memory_size);
        if (status == PXA_STATUS_OK &&
            (operation == PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS ||
             operation == PXA_GAME_RENDER_IO_EXECUTE_PROJECTED_SPANS)) {
            if (!r->span_prepare_attempted) {
                r->span_prepare_attempted = 1;
                if (service->span_prepare_allocator && service->span_prepare_bytes)
                    r->span_prepare = pxa_memory_allocate(service->span_prepare_allocator,
                                                          service->span_prepare_bytes);
            }
            status = (operation == PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS ?
                pxa_raster_frame_spans_prepare : pxa_raster_projected_spans_prepare)(
                data, size, memory, memory_size, r->max_spans,
                r->span_prepare, service->span_prepare_bytes, &batch);
        } else if (status == PXA_STATUS_OK)
            status = (operation == PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES ?
                pxa_raster_priority_tiles_validate :
                (operation == PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME ||
                operation == PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT) ? pxa_raster_blit_validate :
                pxa_raster_spans_validate)(data, size, memory, memory_size,
                                              r->max_spans, &batch);
        validated = now_us(service);
        r->last_validation_us = (uint32_t)(validated - before);
        r->validation_us += validated - before;
        if (status != PXA_STATUS_OK) { ++r->rejected; return status; }
        if (operation == PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT)
            batch.state.blit.exact_sampling = 1;
        if (service->execute_spans)
            status = service->execute_spans(service->execute_spans_context, &batch);
        else
            pxa_raster_spans_execute_range(&batch, 0, batch.target_bytes);
        done = now_us(service);
        r->last_execution_us = (uint32_t)(done - validated);
        r->execution_us += done - validated;
        if (status != PXA_STATUS_OK) { ++r->rejected; return status; }
        ++r->batches; r->spans += batch.kind == PXA_RASTER_SPANS_FRAME ? batch.state.frame.rows : batch.count; r->pixels += batch.pixels;
        return (int32_t)size;
    }
    return PXA_STATUS_UNSUPPORTED;
}

static pxa_status_t bind_assets(pxa_game_render_resource_t *r,
                                const uint8_t *data, size_t size) {
    pxa_raster_bindings_t replacement = {0};
    uint64_t mask = 0;
    uint8_t palette = 0;
    pxa_status_t status = PXA_STATUS_INVALID_ARGUMENT;
    uint16_t major = 0, count;
    if (!r->service->backend.bind_assets) return PXA_STATUS_UNSUPPORTED;
    if (pxa_component_wire_profile(r->service->runtime, r->component, &major) != PXA_STATUS_OK || major != 1)
        return PXA_STATUS_UNSUPPORTED;
    if (size < 4 || pxa_read_u16(data + 2)) return status;
    count = pxa_read_u16(data);
    if (!count || count > PXA_RASTER_MAX_TEXTURES + 1 || size != 4u + (size_t)count * 12u)
        return status;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *entry = data + 4u + (size_t)i * 12u;
        pxa_raster_asset_t **target;
        uint64_t handle = pxa_read_u64(entry + 4);
        status = PXA_STATUS_INVALID_ARGUMENT;
        if (pxa_read_u16(entry + 2)) goto done;
        if (entry[0] == PXA_ASSET_TEXTURE) {
            if (entry[1] >= PXA_RASTER_MAX_TEXTURES || (mask & (UINT64_C(1) << entry[1]))) goto done;
            mask |= UINT64_C(1) << entry[1];
            target = &replacement.textures[entry[1]];
        } else if (entry[0] == PXA_ASSET_PALETTE && !entry[1] && !palette) {
            palette = 1;
            target = &replacement.palette;
        } else goto done;
        if (handle) {
            status = pxa_assets_acquire_handle(r->service->runtime, r->component, handle, entry[0], target);
            if (status != PXA_STATUS_OK) goto done;
        }
    }
    status = pxa_status_normalize(r->service->backend.bind_assets(r->service->backend.context,
        r->provider_context, &replacement, mask, palette));
done:
    pxa_raster_bindings_release(&replacement);
    return status;
}

static int32_t context_io(void *context, uint32_t operation, uint8_t *data,
                          size_t size) {
    pxa_game_render_resource_t *resource = context;
    pxa_status_t status;
    if (!context_valid(resource)) return PXA_STATUS_BAD_STATE;
    if (data == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (resource->max_spans)
        return span_context_io(resource, operation, data, size);
    if (operation == PXA_GAME_RENDER_IO_BIND_ASSETS) {
        status = bind_assets(resource, data, size);
    } else if (operation == PXA_GAME_RENDER_IO_UPLOAD) {
        status = pxa_status_normalize(resource->service->backend.upload(
            resource->service->backend.context, resource->provider_context,
            data, size));
    } else if (operation == PXA_GAME_RENDER_IO_SUBMIT) {
        status = pxa_status_normalize(resource->service->backend.submit(
            resource->service->backend.context, resource->provider_context,
            data, size));
    } else if (operation == PXA_GAME_RENDER_IO_TELEMETRY) {
        pxa_raster_telemetry_t telemetry;
        if (size != PXA_GAME_RENDER_TELEMETRY_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
        memset(&telemetry, 0, sizeof(telemetry));
        status = pxa_status_normalize(resource->service->backend.query(
            resource->service->backend.context, resource->provider_context,
            &telemetry));
        if (status == PXA_STATUS_OK) {
            pxa_write_u64(data, telemetry.submitted_frames);
            pxa_write_u64(data + 8, telemetry.draw_list_bytes);
            pxa_write_u64(data + 16, telemetry.covered_pixels);
            pxa_write_u64(data + 24, telemetry.host_raster_us);
            pxa_write_u64(data + 32, telemetry.queue_wait_us);
            pxa_write_u64(data + 40, telemetry.present_us);
            pxa_write_u64(data + 48, telemetry.dropped_frames);
            pxa_write_u32(data + 56, telemetry.clear_commands);
            pxa_write_u32(data + 60, telemetry.flat_quad_commands);
            pxa_write_u32(data + 64, telemetry.textured_quad_commands);
            pxa_write_u32(data + 68, telemetry.sprite_commands);
            pxa_write_u32(data + 72, telemetry.rejected_lists);
            pxa_write_u32(data + 76, telemetry.last_draw_list_bytes);
            pxa_write_u32(data + 80, telemetry.last_covered_pixels);
            pxa_write_u32(data + 84, telemetry.last_host_raster_us);
            pxa_write_u64(data + 88, telemetry.rendered_frames);
            pxa_write_u64(data + 96, telemetry.visible_frames);
        }
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    if (status != PXA_STATUS_OK) return status;
    return size <= (size_t)INT32_MAX ? (int32_t)size
                                    : PXA_STATUS_LIMIT_EXCEEDED;
}

static const pxa_resource_ops_t k_resource_ops = {
    sizeof(pxa_resource_ops_t), context_io};

static pxa_status_t create_context(pxa_game_render_service_t *service,
                                   pxa_component_t component,
                                   pxa_bytes_t payload, int auto_target,
                                   uint16_t wire_profile, uint8_t result[32],
                                   size_t *result_size,
                                   pxa_handle64_t *opened_handle) {
    pxa_game_render_desc_t desc;
    pxa_game_render_resource_t *context;
    pxa_resource_t handle_resource;
    uint64_t provider_context = 0;
    uint32_t capabilities = 0;
    size_t offset = wire_profile == 1 ? 8u : 4u;
    pxa_status_t status;
    pxa_game_render_target_t target = {0};
    if (!service->backend.create) return PXA_STATUS_UNSUPPORTED;
    if (payload.size != 8 && payload.size != 12)
        return PXA_STATUS_INVALID_ARGUMENT;
    desc.width = pxa_read_u16(payload.data);
    desc.height = pxa_read_u16(payload.data + 2);
    desc.buffer_count = payload.data[4];
    desc.flags = payload.data[5];
    desc.scratch_mode = payload.data[7];
    desc.max_draw_bytes = payload.size == 12
                              ? pxa_read_u32(payload.data + 8)
                              : 0;
    if (desc.scratch_mode > PXA_GAME_RENDER_SCRATCH_COVERAGE_2BIT ||
        (payload.size == 12 &&
         (desc.max_draw_bytes < PXA_RASTER_DRAW_HEADER_BYTES ||
          desc.max_draw_bytes > PXA_RASTER_MAX_DRAW_BYTES)) ||
        desc.buffer_count < service->min_buffer_count ||
        desc.buffer_count > service->max_buffer_count ||
        (desc.flags & ~PXA_GAME_RENDER_FLAG_KNOWN_MASK) != 0)
        return PXA_STATUS_UNSUPPORTED;
    if (auto_target) {
        if (desc.width != 0 || desc.height != 0 ||
            !auto_target_resolve(&service->auto_target_profile,
                                 payload.data[6], &target))
            return PXA_STATUS_UNSUPPORTED;
        desc.width = target.render_width;
        desc.height = target.render_height;
    } else if (payload.data[6] != 0 || desc.width == 0 || desc.height == 0) {
        return PXA_STATUS_UNSUPPORTED;
    }
    context = allocate_context(service, component);
    if (context == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_status_normalize(service->backend.create(
        service->backend.context, &desc, &provider_context, &capabilities));
    if (status != PXA_STATUS_OK) {
        release_context(context);
        return status;
    }
    if (provider_context == 0 ||
        (capabilities & ~PXA_RASTER_CAP_KNOWN_MASK) != 0) {
        if (provider_context != 0)
            service->backend.close(service->backend.context, provider_context);
        release_context(context);
        return PXA_STATUS_INTERNAL;
    }
    context->provider_context = provider_context;
    memset(&handle_resource, 0, sizeof(handle_resource));
    handle_resource.context = context;
    handle_resource.operations = &k_resource_ops;
    handle_resource.close = close_context;
    if (wire_profile == 1) {
        status = pxa_handle_open64(service->runtime, component,
                                   PXA_RESOURCE_GAME_RENDER_CONTEXT, 0,
                                   &handle_resource, opened_handle);
    } else {
        pxa_handle_t legacy_handle = PXA_HANDLE_INVALID;
        status = pxa_handle_open(service->runtime, component,
                                 PXA_RESOURCE_GAME_RENDER_CONTEXT, 0,
                                 &handle_resource, &legacy_handle);
        *opened_handle = legacy_handle;
    }
    if (status != PXA_STATUS_OK) {
        close_context(context);
        return status;
    }
    if (wire_profile == 1)
        pxa_write_u64(result, *opened_handle);
    else
        pxa_write_u32(result, (pxa_handle_t)*opened_handle);
    pxa_write_u32(result + offset, capabilities);
    pxa_write_u32(result + offset + 4,
                  desc.max_draw_bytes != 0 ? desc.max_draw_bytes
                                           : PXA_RASTER_MAX_DRAW_BYTES);
    pxa_write_u16(result + offset + 8, PXA_RASTER_MAX_TEXTURE_DIMENSION);
    result[offset + 10] = PXA_RASTER_MAX_TEXTURES;
    result[offset + 11] = 0;
    if (auto_target) {
        pxa_write_u16(result + offset + 12, target.display_width);
        pxa_write_u16(result + offset + 14, target.display_height);
        pxa_write_u16(result + offset + 16, target.render_width);
        pxa_write_u16(result + offset + 18, target.render_height);
        result[offset + 20] = target.render_scale;
        result[offset + 21] = target.supported_scale_mask;
        result[offset + 22] = result[offset + 23] = 0;
        *result_size = offset + 24;
    } else {
        *result_size = offset + 12;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t game_render_control(
    void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_game_render_service_t *service = context;
    uint8_t result[32] = {0};
    size_t result_size = 0;
    pxa_handle64_t opened_handle = PXA_HANDLE64_INVALID;
    uint16_t wire_profile = 0;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_component_wire_profile(service->runtime, component,
                                      &wire_profile);
    if (status != PXA_STATUS_OK) return status;
    if (message->request_token == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_GAME_RENDER_CREATE_RASTER_CONTEXT &&
        message->opcode != PXA_GAME_RENDER_CREATE_CONTEXT &&
        message->opcode != PXA_GAME_RENDER_CREATE_AUTO_CONTEXT)
        return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin_reserved(
        service->runtime, component, message->request_token,
        PXA_GAME_RENDER_SERVICE_ID, message->opcode, 0,
        (message->opcode == PXA_GAME_RENDER_CREATE_AUTO_CONTEXT ? 28u : 16u) +
            (wire_profile == 1 ? 4u : 0u));
    if (status != PXA_STATUS_OK) return status;
    status = pxa_request_commit(service->runtime, component,
                                message->request_token);
    if (status == PXA_STATUS_OK &&
        message->opcode == PXA_GAME_RENDER_CREATE_RASTER_CONTEXT) {
        pxa_game_render_resource_t *r = NULL;
        uint32_t max_spans = 0;
        if (wire_profile != 1 || message->payload.size != 8 ||
            pxa_read_u32(message->payload.data + 4))
            status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            max_spans = pxa_read_u32(message->payload.data);
            if (!max_spans || max_spans > 4096)
                status = PXA_STATUS_LIMIT_EXCEEDED;
        }
        if (status == PXA_STATUS_OK) {
            r = allocate_context(service, component);
            if (!r) status = PXA_STATUS_RESOURCE_LIMIT;
        }
        if (status == PXA_STATUS_OK) {
            pxa_resource_t resource = {r, &k_resource_ops, close_context};
            r->provider_context = 0; r->max_spans = max_spans;
            r->batches = r->rejected = r->spans = r->pixels = 0;
            r->validation_us = r->execution_us = 0;
            r->last_validation_us = r->last_execution_us = 0;
            status = pxa_handle_open64(service->runtime, component,
                PXA_RESOURCE_GAME_RENDER_CONTEXT, 0, &resource, &opened_handle);
            if (status != PXA_STATUS_OK) close_context(r);
            else {
                pxa_write_u64(result, opened_handle);
                pxa_write_u32(result + 8, max_spans);
                pxa_write_u32(result + 12, 0);
                result_size = 16;
            }
        }
    } else if (status == PXA_STATUS_OK)
        status = create_context(
            service, component, message->payload,
            message->opcode == PXA_GAME_RENDER_CREATE_AUTO_CONTEXT,
            wire_profile, result, &result_size, &opened_handle);
    complete = pxa_request_complete(
        service->runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE64_INVALID) {
            if (wire_profile == 1)
                (void)pxa_handle_close64(service->runtime, component,
                                          opened_handle);
            else
                (void)pxa_handle_close(service->runtime, component,
                                        (pxa_handle_t)opened_handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_token);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_game_render_service_register(
    pxa_game_render_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_GAME_RENDER_SERVICE_ID;
    operations.major = PXA_GAME_RENDER_SERVICE_MAJOR;
    operations.minor = PXA_GAME_RENDER_SERVICE_MINOR;
    if (service->backend.bind_assets) operations.features = PXA_GAME_RENDER_FEATURE_ASSET_BINDINGS;
    operations.features |= PXA_GAME_RENDER_FEATURE_BORROWED_INDEXED_SPANS |
        PXA_GAME_RENDER_FEATURE_PROJECTED_INDEXED_SPANS |
        PXA_GAME_RENDER_FEATURE_INDEXED_FRAME_BATCHES |
        PXA_GAME_RENDER_FEATURE_BORROWED_INDEXED_BLIT |
        PXA_GAME_RENDER_FEATURE_EXACT_INDEXED_BLIT |
        PXA_GAME_RENDER_FEATURE_PRIORITY_TILE_BATCHES;
    operations.context = service;
    operations.control = game_render_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
