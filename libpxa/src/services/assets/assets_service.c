#include "pxa/assets.h"
#include "pxa/service.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <string.h>

#define ASSETS_MAGIC UINT32_C(0x50584153)

typedef struct {
    pxa_component_t component;
    uint32_t request_id;
    uint64_t identity;
    pxa_asset_ticket_t ticket;
    union {
        uint8_t info[PXA_ASSETS_INFO_BYTES];
        struct { uint32_t offset, bytes, total; } read;
    } data;
    uint8_t operation;
} pending_t;

typedef struct {
    pxa_assets_service_t *service;
    pxa_component_t component;
    pxa_asset_ticket_t ticket;
    pxa_raster_asset_t *asset;
    uint8_t kind;
} resource_t;

struct pxa_assets_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_assets_config_t config;
    pending_t *pending;
    resource_t *resources;
    uint8_t registered;
};

static int valid_config(const pxa_assets_config_t *c) {
    return c && c->struct_size >= sizeof(*c) && c->max_pending &&
        c->max_pending_per_component && c->max_pending_per_component <= c->max_pending &&
        c->max_resources && c->max_resources_per_component &&
        c->max_resources_per_component <= c->max_resources &&
        c->backend.find && c->backend.request && c->backend.query &&
        c->backend.acquire && c->backend.release && c->backend.prefetch && c->backend.inspect &&
        c->backend.read && c->backend.read_result && c->backend.read_release;
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
    if (p->ticket) (p->operation == PXA_ASSETS_READ ? s->config.backend.read_release : s->config.backend.release)(
        s->config.backend.context, p->component, p->ticket);
    memset(p, 0, sizeof(*p));
}

static void close_resource(void *context) {
    resource_t *r = context;
    if (!r || !r->component) return;
    /* The cache ticket still pins a cache reference during this release. */
    pxa_raster_asset_release(r->asset);
    r->service->config.backend.release(r->service->config.backend.context, r->component, r->ticket);
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
    if (!r || r->component != component || r->kind != expected_kind || !r->asset)
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

void pxa_assets_service_poll(pxa_assets_service_t *s) {
    if (!s || s->magic != ASSETS_MAGIC) return;
    for (unsigned i = 0; i < s->config.max_pending; ++i) {
        pending_t *p = &s->pending[i];
        pxa_asset_request_state_t state = {0};
        uint8_t result[PXA_ASSETS_LOAD_RESULT_BYTES];
        pxa_handle64_t handle = 0;
        pxa_status_t status, complete;
        resource_t *r;
        if (!p->component) continue;
        if (pxa_request_identity(s->runtime, p->component, p->request_id) != p->identity) {
            release_pending(s, p);
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
            (void)pxa_request_complete(s->runtime,p->component,p->request_id,status,
                status ? NULL : bytes.data,status ? 0 : bytes.size);
            release_pending(s,p);
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
                    r->ticket = p->ticket;
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
        complete = pxa_request_complete(s->runtime, p->component, p->request_id,
            status, status == PXA_STATUS_OK ? (p->operation == PXA_ASSETS_PREFETCH ? p->data.info : result) : NULL,
            status == PXA_STATUS_OK ? (p->operation == PXA_ASSETS_PREFETCH ? PXA_ASSETS_INFO_BYTES : sizeof(result)) : 0);
        if (complete != PXA_STATUS_OK && handle)
            (void)pxa_handle_close64(s->runtime, p->component, handle);
        release_pending(s, p);
    }
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

static pxa_status_t control(void *context, pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message) {
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
    if (pxa_component_core_major(runtime, component, &major) != PXA_STATUS_OK || major != 1)
        return PXA_STATUS_UNSUPPORTED;
    if (message->opcode != PXA_ASSETS_QUERY && message->opcode != PXA_ASSETS_LOAD &&
        message->opcode != PXA_ASSETS_PREFETCH && message->opcode != PXA_ASSETS_STATUS &&
        message->opcode != PXA_ASSETS_READ)
        return PXA_STATUS_UNSUPPORTED;
    const int reading = message->opcode == PXA_ASSETS_READ;
    size_t prefix = reading ? 12u : 4u;
    if (!message->request_id || message->payload.size <= prefix || !message->payload.data)
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
    status = pxa_request_begin_reserved(runtime, component, message->request_id,
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
        return pxa_request_complete(runtime, component, message->request_id, status,
            status == PXA_STATUS_OK ? snapshot : NULL, status == PXA_STATUS_OK ? sizeof(snapshot) : 0);
    }
    memset(&info, 0, sizeof(info));
    status = pxa_status_normalize(s->config.backend.find(s->config.backend.context, component, path, &info));
    if (status == PXA_STATUS_OK && reading) {
        if (info.kind != PXA_ASSET_BLOB) status = PXA_STATUS_UNSUPPORTED;
        else if (offset > info.stored_bytes) status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            p->component = component; p->request_id = message->request_id;
            p->identity = pxa_request_identity(runtime,component,message->request_id);
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
            p->request_id = message->request_id;
            p->operation = (uint8_t)message->opcode;
            p->identity = pxa_request_identity(runtime, component, message->request_id);
            memcpy(p->data.info, encoded, sizeof(encoded));
            status = pxa_status_normalize((p->operation == PXA_ASSETS_PREFETCH ? s->config.backend.prefetch : s->config.backend.request)(s->config.backend.context,
                component, path, pxa_asset_default_memory_class(&info),
                &p->ticket));
            if (status == PXA_STATUS_OK && p->ticket) return PXA_STATUS_OK;
            if (status == PXA_STATUS_OK) status = PXA_STATUS_INTERNAL;
            release_pending(s, p);
        }
    }
    return pxa_request_complete(runtime, component, message->request_id, status,
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
    ops.context = s;
    ops.control = control;
    ops.component_stopped = component_stopped;
    status = pxa_service_register(s->runtime, &ops);
    if (status == PXA_STATUS_OK) s->registered = 1;
    return status;
}
