#include "pxa/assets.h"
#include "pxa/service.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <string.h>

typedef struct {
    pxa_image_decoder_t *decoder;
    pxa_handle64_t source;
    uint32_t encoded_bytes;
    uint16_t source_type;
    uint8_t with_frames;
} image_job_t;

#define ASSETS_MAGIC UINT32_C(0x50584153)

typedef struct {
    pxa_component_t component;
    pxa_request_token_t request_token;
    uint64_t identity;
    pxa_asset_ticket_t ticket;
    union {
        uint8_t info[PXA_ASSETS_INFO_BYTES];
        struct { uint32_t offset, bytes, total; } read;
        image_job_t *image;
        /* Byte storage keeps the union's 32-bit alignment on ESP32. A u64
         * member would pad every pending slot, even without image frames. */
        uint8_t frame[8];
    } data;
    uint8_t operation;
} pending_t;

typedef struct {
    pxa_assets_service_t *service;
    pxa_component_t component;
    union { pxa_asset_ticket_t ticket; image_job_t *image; } retained;
    pxa_raster_asset_t *asset;
    uint8_t kind;
} resource_t;
#define IMAGE_FRAMES_KIND (PXA_ASSET_IMAGE | 128u)

static void destroy_image_job(image_job_t *job) {
    if (!job) return;
    pxa_image_decoder_destroy(job->decoder);
    pxa_memory_release(job);
}

struct pxa_assets_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_assets_config_t config;
    pending_t *pending;
    resource_t *resources;
    uint8_t registered;
};

static int valid_config(const pxa_assets_config_t *c) {
    if (!c || c->struct_size < sizeof(*c)) return 0;
    int package_backend = c->backend.find && c->backend.request && c->backend.query &&
        c->backend.acquire && c->backend.release && c->backend.prefetch && c->backend.inspect &&
        c->backend.read && c->backend.read_result && c->backend.read_release;
    int empty_backend = !c->backend.find && !c->backend.request && !c->backend.query &&
        !c->backend.acquire && !c->backend.release && !c->backend.prefetch && !c->backend.inspect &&
        !c->backend.read && !c->backend.read_result && !c->backend.read_release;
    return c && c->struct_size >= sizeof(*c) && c->max_pending &&
        c->max_pending_per_component && c->max_pending_per_component <= c->max_pending &&
        c->max_resources && c->max_resources_per_component &&
        c->max_resources_per_component <= c->max_resources &&
        (package_backend || (empty_backend && c->image_allocator && c->image_temporary_allocator && c->image_backend));
}

size_t pxa_assets_service_workspace_size(const pxa_assets_config_t *c) {
    size_t bytes = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u + sizeof(pxa_assets_service_t);
    if (!valid_config(c) || c->max_pending > (SIZE_MAX - bytes) / sizeof(pending_t)) return 0;
    bytes += c->max_pending * sizeof(pending_t);
    if (c->max_resources > (SIZE_MAX - bytes) / sizeof(resource_t)) return 0;
    return bytes + c->max_resources * sizeof(resource_t);
}

pxa_status_t pxa_assets_service_init(void *workspace, size_t bytes,
    pxa_runtime_t *runtime, const pxa_assets_config_t *config,
    pxa_assets_service_t **output) {
    size_t required = pxa_assets_service_workspace_size(config);
    pxa_assets_service_t *s;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!workspace || !runtime || !required || bytes < required ||
        (uintptr_t)workspace > UINTPTR_MAX - bytes) return PXA_STATUS_INVALID_ARGUMENT;
    s = (void *)pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(s, 0, sizeof(*s));
    s->magic = ASSETS_MAGIC;
    s->runtime = runtime;
    s->config = *config;
    s->pending = (void *)(s + 1);
    s->resources = (void *)(s->pending + config->max_pending);
    memset(s->pending, 0, config->max_pending * sizeof(pending_t));
    memset(s->resources, 0, config->max_resources * sizeof(resource_t));
    *output = s;
    return PXA_STATUS_OK;
}

static void release_pending(pxa_assets_service_t *s, pending_t *p) {
    if (p->operation == PXA_ASSETS_DECODE_IMAGE && p->data.image) {
        destroy_image_job(p->data.image);
    }
    if (p->ticket) (p->operation == PXA_ASSETS_READ ? s->config.backend.read_release : s->config.backend.release)(
        s->config.backend.context, p->component, p->ticket);
    memset(p, 0, sizeof(*p));
}

static void close_resource(void *context) {
    resource_t *r = context;
    if (!r || !r->component) return;
    /* The cache ticket still pins a cache reference during this release. */
    pxa_raster_asset_release(r->asset);
    if (r->kind == IMAGE_FRAMES_KIND) destroy_image_job(r->retained.image);
    else if (r->retained.ticket) r->service->config.backend.release(r->service->config.backend.context, r->component, r->retained.ticket);
    memset(r, 0, sizeof(*r));
}
static const pxa_resource_ops_t resource_ops = {sizeof(pxa_resource_ops_t), NULL};

pxa_status_t pxa_assets_acquire_handle(pxa_runtime_t *runtime,
    pxa_component_t component, pxa_handle64_t handle, uint8_t expected_kind,
    pxa_raster_asset_t **output) {
    pxa_resource_t native;
    pxa_status_t status;
    resource_t *r;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (expected_kind != PXA_ASSET_TEXTURE && expected_kind != PXA_ASSET_PALETTE && expected_kind != PXA_ASSET_AUDIO && expected_kind != PXA_ASSET_IMAGE)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_handle_get64(runtime, component, handle, PXA_RESOURCE_ASSET, &native);
    if (status != PXA_STATUS_OK) return status;
    if (native.operations != &resource_ops || native.close != close_resource)
        return PXA_STATUS_DENIED;
    r = native.context;
    if (!r || r->component != component || (r->kind & 127u) != expected_kind || !r->asset)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_raster_asset_retain(r->asset);
    *output = r->asset;
    return PXA_STATUS_OK;
}

static resource_t *find_resource_slot(pxa_assets_service_t *s, pxa_component_t component) {
    resource_t *empty = NULL;
    unsigned count = 0;
    for (unsigned i = 0; i < s->config.max_resources; ++i) {
        resource_t *r = &s->resources[i];
        if (r->component == component) ++count;
        else if (!r->component && !empty) empty = r;
    }
    return count < s->config.max_resources_per_component ? empty : NULL;
}

static void encode_info(uint8_t output[PXA_ASSETS_INFO_BYTES], const pxa_asset_info_t *info);
static pxa_status_t publish_image(pxa_assets_service_t *s, pxa_component_t component,
                                  pxa_decoded_image_t *decoded, image_job_t *frames,
                                  pxa_handle64_t *handle) {
    resource_t *r = find_resource_slot(s, component);
    if (!r) return PXA_STATUS_RESOURCE_LIMIT;
    r->service = s; r->component = component; r->asset = decoded->image;
    r->kind = frames ? IMAGE_FRAMES_KIND : PXA_ASSET_IMAGE;
    if (frames) r->retained.image = frames;
    else r->retained.ticket = 0;
    pxa_resource_t native = {r, &resource_ops, close_resource};
    pxa_status_t status = pxa_handle_open64(s->runtime, component, PXA_RESOURCE_ASSET, 0, &native, handle);
    if (status) memset(r, 0, sizeof(*r));
    else decoded->image = NULL;
    return status;
}
static void encode_frame(uint8_t output[PXA_ASSETS_FRAME_RESULT_BYTES], pxa_handle64_t handle,
                          const pxa_decoded_image_t *decoded, uint32_t bytes,
                          const pxa_image_frame_info_t *frame) {
    pxa_write_u64(output, handle);
    encode_info(output + 8, &decoded->info);
    pxa_write_u16(output + 28, decoded->source_width);
    pxa_write_u16(output + 30, decoded->source_height);
    pxa_write_u32(output + 32, bytes);
    pxa_write_u32(output + 36, frame->delay_ms);
    pxa_write_u32(output + 40, frame->iteration);
    pxa_write_u32(output + 44, frame->last ? 1u : 0u);
}
static int poll_image(pxa_assets_service_t *s, pending_t *p) {
    image_job_t *job = p->data.image;
    pxa_status_t status = PXA_STATUS_OK;
    int finished = 0;
    /* Bounded stack scratch and work per poll; independent of image size.
     * Resolve through Core on every read: a retained pointer could outlive a
     * source close, revocation or generation change. The borrowed operations
     * are the public byte-resource interface, not a backend-specific API. */
    uint8_t input[1024];
    size_t work = 0;
    for (unsigned slice = 0; slice < 4; ++slice) {
        size_t budget = pxa_image_decoder_input_budget(job->decoder);
        if (work >= budget) break;
        size_t capacity = budget - work;
        if (capacity > sizeof(input)) capacity = sizeof(input);
        pxa_resource_t source = {0};
        status = pxa_handle_get64(s->runtime, p->component, job->source, job->source_type, &source);
        if (status) {
            finished = 1;
            break;
        }
        const pxa_resource_ops_t *ops = source.operations;
        if (!ops || ops->struct_size < sizeof(*ops) || !ops->io) {
            status = PXA_STATUS_UNSUPPORTED;
            finished = 1;
            break;
        }
        int32_t count = ops->io(source.context, PXA_IO_READ, input, capacity);
        if (count == PXA_STATUS_WOULD_BLOCK)
            return 0;
        if (count < 0) {
            status = pxa_status_normalize(count);
            finished = 1;
            break;
        }
        if ((size_t)count > capacity) {
            status = PXA_STATUS_PROTOCOL_ERROR;
            finished = 1;
            break;
        }
        if (!count) {
            finished = 1;
            break;
        }
        status = pxa_image_decoder_write(job->decoder, (pxa_bytes_t){input, (size_t)count});
        if (status) {
            finished = 1;
            break;
        }
        job->encoded_bytes += (uint32_t)count;
        work += (size_t)count;
    }
    if (!finished)
        return 1;
    pxa_decoded_image_t decoded = {0};
    pxa_image_frame_info_t frame = {0};
    if (!status)
        status = job->with_frames ? pxa_image_decoder_next(job->decoder, &decoded, &frame) :
                                   pxa_image_decoder_finish(job->decoder, &decoded);
    if (!status && !decoded.image) status = PXA_STATUS_PROTOCOL_ERROR;
    pxa_handle64_t handle = 0;
    uint8_t result[PXA_ASSETS_FRAME_RESULT_BYTES] = {0};
    const size_t result_bytes = job->with_frames ? sizeof(result) : PXA_ASSETS_IMAGE_RESULT_BYTES;
    if (!status) {
        status = publish_image(s, p->component, &decoded,
                               job->with_frames && !frame.last ? job : NULL, &handle);
        if (!status) {
            encode_frame(result, handle, &decoded, job->encoded_bytes, &frame);
            if (job->with_frames && !frame.last) p->data.image = NULL;
        }
    }
    pxa_asset_object_release(decoded.image);
    pxa_status_t complete =
        pxa_request_complete(s->runtime, p->component, p->request_token, status,
                             status ? NULL : result, status ? 0 : result_bytes);
    if (complete && handle)
        (void)pxa_handle_close64(s->runtime, p->component, handle);
    release_pending(s, p);
    return 1;
}

static int poll_frame(pxa_assets_service_t *s, pending_t *p) {
    pxa_resource_t native = {0};
    pxa_status_t status = pxa_handle_get64(s->runtime, p->component, pxa_read_u64(p->data.frame),
                                          PXA_RESOURCE_ASSET, &native);
    resource_t *source = native.context;
    if (!status && (native.operations != &resource_ops || native.close != close_resource ||
                    !source || source->kind != IMAGE_FRAMES_KIND)) status = PXA_STATUS_BAD_STATE;
    pxa_decoded_image_t decoded = {0};
    pxa_image_frame_info_t frame = {0};
    pxa_handle64_t handle = 0;
    uint8_t result[PXA_ASSETS_FRAME_RESULT_BYTES] = {0};
    if (!status) {
        image_job_t *job = source->retained.image;
        status = pxa_image_decoder_next(job->decoder, &decoded, &frame);
        if (!status && !decoded.image) status = PXA_STATUS_PROTOCOL_ERROR;
        if (!status) status = publish_image(s, p->component, &decoded, frame.last ? NULL : job, &handle);
        if (!status) encode_frame(result, handle, &decoded, job->encoded_bytes, &frame);
        /* Moving decoder ownership never changes old published pixels. A
         * failed/cancelled advance releases state and leaves the old still. */
        if (status || frame.last) destroy_image_job(job);
        source->retained.ticket = 0;
        source->kind = PXA_ASSET_IMAGE;
    }
    pxa_asset_object_release(decoded.image);
    pxa_status_t complete = pxa_request_complete(s->runtime, p->component, p->request_token,
        status, status ? NULL : result, status ? 0 : sizeof(result));
    if (complete && handle) (void)pxa_handle_close64(s->runtime, p->component, handle);
    release_pending(s, p);
    return 1;
}

int pxa_assets_service_poll(pxa_assets_service_t *s) {
    if (!s || s->magic != ASSETS_MAGIC) return 0;
    int progress = 0;
    for (unsigned i = 0; i < s->config.max_pending; ++i) {
        pending_t *p = &s->pending[i];
        pxa_asset_request_state_t state = {0};
        uint8_t result[PXA_ASSETS_LOAD_RESULT_BYTES];
        pxa_handle64_t handle = 0;
        pxa_status_t status, complete;
        resource_t *r;
        if (!p->component) continue;
        if (pxa_request_identity(s->runtime, p->component, p->request_token) != p->identity) {
            release_pending(s, p);
            continue;
        }
        if (p->operation == PXA_ASSETS_DECODE_IMAGE) {
            progress |= poll_image(s,p);
            continue;
        }
        if (p->operation == PXA_ASSETS_NEXT_IMAGE_FRAME) {
            progress |= poll_frame(s, p);
            continue;
        }
        if (p->operation == PXA_ASSETS_READ) {
            pxa_bytes_t bytes = {0};
            status = pxa_status_normalize(s->config.backend.read_result(
                s->config.backend.context,p->component,p->ticket,&bytes));
            if (status == PXA_STATUS_WOULD_BLOCK) continue;
            if (!status) {
                uint32_t expected = p->data.read.total-p->data.read.offset;
                if (expected > p->data.read.bytes) expected = p->data.read.bytes;
                if (!bytes.data || bytes.size != 8u+expected ||
                    pxa_read_u32(bytes.data) != p->data.read.offset ||
                    pxa_read_u32(bytes.data+4) != p->data.read.total) status = PXA_STATUS_PROTOCOL_ERROR;
            }
            (void)pxa_request_complete(s->runtime,p->component,p->request_token,status,
                status ? NULL : bytes.data,status ? 0 : bytes.size);
            release_pending(s,p);
            progress = 1;
            continue;
        }
        status = pxa_status_normalize(s->config.backend.query(
            s->config.backend.context, p->component, p->ticket, &state));
        if (status == PXA_STATUS_OK) {
            if (state.state == PXA_ASSET_REQUEST_QUEUED || state.state == PXA_ASSET_REQUEST_LOADING)
                continue;
            status = state.state == PXA_ASSET_REQUEST_READY ? PXA_STATUS_OK :
                state.state == PXA_ASSET_REQUEST_CANCELLED ? PXA_STATUS_CANCELLED :
                state.state == PXA_ASSET_REQUEST_FAILED && state.status != PXA_STATUS_OK ?
                    pxa_status_normalize(state.status) : PXA_STATUS_INTERNAL;
        }
        if (status == PXA_STATUS_OK && p->operation == PXA_ASSETS_LOAD) {
            r = find_resource_slot(s, p->component);
            if (!r) status = PXA_STATUS_RESOURCE_LIMIT;
            else {
                pxa_resource_t native = {r, &resource_ops, close_resource};
                status = pxa_status_normalize(s->config.backend.acquire(
                    s->config.backend.context, p->component, p->ticket, &r->asset));
                if (status == PXA_STATUS_OK && !r->asset) status = PXA_STATUS_INTERNAL;
                if (status == PXA_STATUS_OK) {
                    r->service = s;
                    r->component = p->component;
                    r->retained.ticket = p->ticket;
                    r->kind = p->data.info[0];
                    status = pxa_handle_open64(s->runtime, p->component, PXA_RESOURCE_ASSET, 0, &native, &handle);
                    if (status == PXA_STATUS_OK) p->ticket = 0; /* Handle owns the pin. */
                    else {
                        pxa_raster_asset_release(r->asset);
                        memset(r, 0, sizeof(*r));
                    }
                }
            }
        }
        pxa_write_u64(result, handle);
        memcpy(result + 8, p->data.info, PXA_ASSETS_INFO_BYTES);
        /* Runtime serialization makes publication versus Core cancellation
         * atomic. Do not commit while I/O is pending: it must stay cancellable. */
        complete = pxa_request_complete(s->runtime, p->component, p->request_token,
            status, status == PXA_STATUS_OK ? (p->operation == PXA_ASSETS_PREFETCH ? p->data.info : result) : NULL,
            status == PXA_STATUS_OK ? (p->operation == PXA_ASSETS_PREFETCH ? PXA_ASSETS_INFO_BYTES : sizeof(result)) : 0);
        if (complete != PXA_STATUS_OK && handle)
            (void)pxa_handle_close64(s->runtime, p->component, handle);
        release_pending(s, p);
        progress = 1;
    }
    return progress;
}

static void encode_info(uint8_t output[PXA_ASSETS_INFO_BYTES], const pxa_asset_info_t *info) {
    output[0] = info->kind;
    output[1] = info->encoding;
    pxa_write_u16(output + 2, info->format_version);
    pxa_write_u16(output + 4, info->width);
    pxa_write_u16(output + 6, info->height);
    pxa_write_u32(output + 8, info->stored_bytes);
    pxa_write_u32(output + 12, info->decoded_bytes);
    pxa_write_u32(output + 16, (uint32_t)pxa_asset_object_required_bytes(info));
}

static pxa_status_t image_control(pxa_assets_service_t *s, pxa_component_t component,
                                  const pxa_message_view_t *message) {
    const uint8_t *data = message->payload.data;
    if (!message->request_token || !data || message->payload.size < 24)
        return PXA_STATUS_INVALID_ARGUMENT;
    const uint16_t flags = pxa_read_u16(data + 10);
    const int frames = (flags & PXA_ASSETS_IMAGE_WITH_FRAMES) != 0;
    if ((flags & ~(PXA_ASSETS_IMAGE_FORMATS_MASK | PXA_ASSETS_IMAGE_WITH_FRAMES)) ||
        message->payload.size != (frames ? 32u : 24u))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!s->config.image_allocator || !s->config.image_temporary_allocator ||
        !s->config.image_backend)
        return PXA_STATUS_UNSUPPORTED;
    uint16_t type = pxa_read_u16(data + 8);
    if (type != PXA_RESOURCE_FILE && type != PXA_RESOURCE_STREAM)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_handle64_t source = pxa_read_u64(data);
    pxa_resource_t native = {0};
    pxa_status_t status = pxa_handle_get64(s->runtime, component, source, type, &native);
    if (status)
        return status;
    const pxa_resource_ops_t *ops = native.operations;
    if (!ops || ops->struct_size < sizeof(*ops) || !ops->io)
        return PXA_STATUS_UNSUPPORTED;
    pxa_assets_service_poll(s);
    pending_t *p = NULL;
    unsigned count = 0;
    for (unsigned i = 0; i < s->config.max_pending; ++i) {
        pending_t *entry = &s->pending[i];
        if (entry->component == component) {
            ++count;
            if (entry->operation == PXA_ASSETS_DECODE_IMAGE && entry->data.image->source == source)
                return PXA_STATUS_BAD_STATE;
        } else if (!entry->component && !p)
            p = entry;
    }
    if (!p || count >= s->config.max_pending_per_component)
        return PXA_STATUS_WOULD_BLOCK;
    status = pxa_request_begin_reserved(s->runtime, component, message->request_token,
                                        PXA_ASSETS_SERVICE_ID, message->opcode, 0,
                                        frames ? PXA_ASSETS_FRAME_RESULT_BYTES : PXA_ASSETS_IMAGE_RESULT_BYTES);
    if (status)
        return status;
    image_job_t *job = pxa_memory_allocate(s->config.image_temporary_allocator, sizeof(*job));
    if (!job)
        status = PXA_STATUS_RESOURCE_LIMIT;
    else {
        memset(job, 0, sizeof(*job));
        job->source = source;
        job->source_type = type;
        job->with_frames = (uint8_t)frames;
        pxa_image_decode_options_t options = {
            .max_width = pxa_read_u16(data + 12), .max_height = pxa_read_u16(data + 14),
            .max_pixels = pxa_read_u32(data + 16), .max_encoded_bytes = pxa_read_u32(data + 20),
            .max_animation_pixels = frames ? pxa_read_u32(data + 24) : 0,
            .max_animation_encoded_bytes = frames ? pxa_read_u32(data + 28) : 0,
            .formats = (uint8_t)(flags & PXA_ASSETS_IMAGE_FORMATS_MASK)};
        status = pxa_image_decoder_create(&options, s->config.image_allocator,
                                          s->config.image_temporary_allocator,
                                          s->config.image_backend, &job->decoder);
        if (!status) {
            p->component = component;
            p->request_token = message->request_token;
            p->identity = pxa_request_identity(s->runtime, component, message->request_token);
            p->operation = PXA_ASSETS_DECODE_IMAGE;
            p->data.image = job;
            return PXA_STATUS_OK;
        }
        pxa_memory_release(job);
    }
    return pxa_request_complete(s->runtime, component, message->request_token, status, NULL, 0);
}

static pxa_status_t frame_control(pxa_assets_service_t *s, pxa_component_t component,
                                  const pxa_message_view_t *message) {
    if (!message->request_token || !message->payload.data || message->payload.size != 8)
        return PXA_STATUS_INVALID_ARGUMENT;
    const pxa_handle64_t handle = pxa_read_u64(message->payload.data);
    pxa_resource_t native = {0};
    pxa_status_t status = pxa_handle_get64(s->runtime, component, handle,
                                          PXA_RESOURCE_ASSET, &native);
    if (status) return status;
    resource_t *resource = native.context;
    if (native.operations != &resource_ops || native.close != close_resource ||
        !resource || resource->kind != IMAGE_FRAMES_KIND)
        return PXA_STATUS_BAD_STATE;
    pending_t *available = NULL;
    unsigned count = 0;
    for (unsigned i = 0; i < s->config.max_pending; ++i) {
        pending_t *p = &s->pending[i];
        if (p->component == component) {
            ++count;
            if (p->operation == PXA_ASSETS_NEXT_IMAGE_FRAME && pxa_read_u64(p->data.frame) == handle)
                return PXA_STATUS_BAD_STATE;
        } else if (!p->component && !available) available = p;
    }
    if (!available || count >= s->config.max_pending_per_component)
        return PXA_STATUS_WOULD_BLOCK;
    if (!find_resource_slot(s, component)) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_request_begin_reserved(s->runtime, component, message->request_token,
        PXA_ASSETS_SERVICE_ID, message->opcode, 0, PXA_ASSETS_FRAME_RESULT_BYTES);
    if (status) return status;
    available->component = component;
    available->request_token = message->request_token;
    available->identity = pxa_request_identity(s->runtime, component, message->request_token);
    available->operation = PXA_ASSETS_NEXT_IMAGE_FRAME;
    pxa_write_u64(available->data.frame, handle);
    return PXA_STATUS_OK;
}

static pxa_status_t control(void *context, pxa_runtime_t *runtime, pxa_component_t component,
                            const pxa_message_view_t *message) {
    pxa_assets_service_t *s = context;
    pxa_asset_info_t info;
    pxa_bytes_t path;
    pending_t *p = NULL;
    pxa_status_t status;
    uint16_t major = 0;
    uint8_t encoded[PXA_ASSETS_INFO_BYTES];
    unsigned count = 0;
    if (!s || s->magic != ASSETS_MAGIC || runtime != s->runtime)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (pxa_component_wire_profile(runtime, component, &major) != PXA_STATUS_OK || major != 1)
        return PXA_STATUS_UNSUPPORTED;
    if(message->opcode==PXA_ASSETS_DECODE_IMAGE)
        return image_control(s,component,message);
    if(message->opcode==PXA_ASSETS_NEXT_IMAGE_FRAME)
        return frame_control(s,component,message);
    if(!s->config.backend.find) return PXA_STATUS_UNSUPPORTED;
    if (message->opcode != PXA_ASSETS_QUERY && message->opcode != PXA_ASSETS_LOAD &&
        message->opcode != PXA_ASSETS_PREFETCH && message->opcode != PXA_ASSETS_STATUS &&
        message->opcode != PXA_ASSETS_READ)
        return PXA_STATUS_UNSUPPORTED;
    const int reading = message->opcode == PXA_ASSETS_READ;
    size_t prefix = reading ? 12u : 4u;
    if (!message->request_token || message->payload.size <= prefix || !message->payload.data)
        return PXA_STATUS_INVALID_ARGUMENT;
    const uint8_t *data = message->payload.data;
    uint32_t offset = reading ? pxa_read_u32(data+4) : 0;
    uint32_t read_bytes = reading ? pxa_read_u32(data+8) : 0;
    if (reading && (!read_bytes || read_bytes > PXA_ASSET_READ_MAX_BYTES)) return PXA_STATUS_INVALID_ARGUMENT;
    path = (pxa_bytes_t){data + prefix, message->payload.size - prefix};
    if (pxa_read_u16(data) != path.size || data[3] || !pxa_asset_path_valid(path) ||
        ((message->opcode == PXA_ASSETS_QUERY || message->opcode == PXA_ASSETS_STATUS || reading) && data[2]) ||
        ((message->opcode == PXA_ASSETS_LOAD || message->opcode == PXA_ASSETS_PREFETCH) && data[2] != PXA_ASSET_TEXTURE && data[2] != PXA_ASSET_PALETTE && data[2] != PXA_ASSET_AUDIO && data[2] != PXA_ASSET_IMAGE))
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_assets_service_poll(s);
    if (message->opcode == PXA_ASSETS_LOAD || message->opcode == PXA_ASSETS_PREFETCH || reading) {
        for (unsigned i = 0; i < s->config.max_pending; ++i) {
            if (s->pending[i].component == component) ++count;
            else if (!s->pending[i].component && !p) p = &s->pending[i];
        }
        if (!p || count >= s->config.max_pending_per_component) return PXA_STATUS_WOULD_BLOCK;
    }
    status = pxa_request_begin_reserved(runtime, component, message->request_token,
        PXA_ASSETS_SERVICE_ID, message->opcode, 0,
        reading ? 8u+read_bytes : message->opcode == PXA_ASSETS_LOAD ? PXA_ASSETS_LOAD_RESULT_BYTES :
        message->opcode == PXA_ASSETS_STATUS ? PXA_ASSETS_STATUS_BYTES : PXA_ASSETS_INFO_BYTES);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_ASSETS_STATUS) {
        pxa_asset_request_state_t state = {0};
        uint8_t snapshot[PXA_ASSETS_STATUS_BYTES] = {0};
        status = pxa_status_normalize(s->config.backend.inspect(
            s->config.backend.context, component, path, &state));
        if (status == PXA_STATUS_OK && state.state > PXA_ASSET_REQUEST_FAILED)
            status = PXA_STATUS_INTERNAL;
        snapshot[0] = state.state;
        pxa_write_u32(snapshot + 4, (uint32_t)state.status);
        return pxa_request_complete(runtime, component, message->request_token, status,
            status == PXA_STATUS_OK ? snapshot : NULL, status == PXA_STATUS_OK ? sizeof(snapshot) : 0);
    }
    memset(&info, 0, sizeof(info));
    status = pxa_status_normalize(s->config.backend.find(s->config.backend.context, component, path, &info));
    if (status == PXA_STATUS_OK && reading) {
        if (info.kind != PXA_ASSET_BLOB) status = PXA_STATUS_UNSUPPORTED;
        else if (offset > info.stored_bytes) status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            p->component = component; p->request_token = message->request_token;
            p->identity = pxa_request_identity(runtime,component,message->request_token);
            p->operation = PXA_ASSETS_READ;
            p->data.read.offset = offset; p->data.read.bytes = read_bytes; p->data.read.total = info.stored_bytes;
            status = pxa_status_normalize(s->config.backend.read(s->config.backend.context,
                component,path,offset,read_bytes,&p->ticket));
            if (!status && p->ticket) return PXA_STATUS_OK;
            if (!status) status = PXA_STATUS_INTERNAL;
            release_pending(s,p);
        }
    }
    if (status == PXA_STATUS_OK) encode_info(encoded, &info);
    if (status == PXA_STATUS_OK && (message->opcode == PXA_ASSETS_LOAD || message->opcode == PXA_ASSETS_PREFETCH)) {
        if (info.kind != data[2] || !pxa_asset_object_required_bytes(&info)) status = PXA_STATUS_UNSUPPORTED;
        else {
            p->component = component;
            p->request_token = message->request_token;
            p->operation = (uint8_t)message->opcode;
            p->identity = pxa_request_identity(runtime, component, message->request_token);
            memcpy(p->data.info, encoded, sizeof(encoded));
            status = pxa_status_normalize((p->operation == PXA_ASSETS_PREFETCH ? s->config.backend.prefetch : s->config.backend.request)(s->config.backend.context,
                component, path, pxa_asset_default_memory_class(&info),
                &p->ticket));
            if (status == PXA_STATUS_OK && p->ticket) return PXA_STATUS_OK;
            if (status == PXA_STATUS_OK) status = PXA_STATUS_INTERNAL;
            release_pending(s, p);
        }
    }
    return pxa_request_complete(runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? encoded : NULL, status == PXA_STATUS_OK ? sizeof(encoded) : 0);
}

static void component_stopped(void *context, pxa_runtime_t *runtime, pxa_component_t component) {
    pxa_assets_service_t *s = context;
    (void)runtime;
    for (unsigned i = 0; i < s->config.max_pending; ++i)
        if (s->pending[i].component == component) release_pending(s, &s->pending[i]);
    /* Core closes resources; their callbacks release ready cache tickets. */
}

pxa_status_t pxa_assets_service_register(pxa_assets_service_t *s) {
    pxa_service_ops_t ops = {0};
    pxa_status_t status;
    if (!s || s->magic != ASSETS_MAGIC) return PXA_STATUS_INVALID_ARGUMENT;
    if (s->registered) return PXA_STATUS_BAD_STATE;
    ops.struct_size = sizeof(ops);
    ops.service_id = PXA_ASSETS_SERVICE_ID;
    ops.major = PXA_ASSETS_SERVICE_MAJOR;
    ops.minor = PXA_ASSETS_SERVICE_MINOR;
    if (s->config.image_allocator && s->config.image_temporary_allocator && s->config.image_backend)
        ops.features |= PXA_ASSETS_FEATURE_IMAGE_DECODE | PXA_ASSETS_FEATURE_IMAGE_FRAMES;
    ops.context = s;
    ops.control = control;
    ops.component_stopped = component_stopped;
    status = pxa_service_register(s->runtime, &ops);
    if (status == PXA_STATUS_OK) s->registered = 1;
    return status;
}
