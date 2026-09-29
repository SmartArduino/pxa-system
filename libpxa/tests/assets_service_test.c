#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pxa/assets.h"
#include "pxa/game_render.h"
#include "pxa/service.h"
#include "pxa_assets.h"
#include "pxa_asset_scene.h"
#include "pxa_game_render.h"

static pxa_runtime_t *runtime;
static pxa_component_t component;
static pxa_assets_service_t *service;
static pxa_asset_cache_t *cache;
static pxa_raster_bindings_t bindings;
static size_t live_bytes;
static unsigned requests, binds, read_requests, read_releases;
static const char blob_path[] = "assets/map.bin";
static struct { uint64_t ticket; pxa_component_t owner; uint8_t data[136]; size_t size; int ready; } reads[2];
static uint64_t read_sequence;
static pxa_status_t read_error;
static int malformed_read;
static pxa_status_t read_blob(void *ctx, pxa_component_t owner, pxa_bytes_t path,
    uint32_t offset, uint32_t count, uint64_t *ticket) {
    (void)ctx; assert(path.size == sizeof(blob_path)-1); ++read_requests;
    *ticket = 0;
    if (read_error) return read_error;
    for (unsigned i = 0; i < 2; ++i) if (!reads[i].ticket) {
        uint32_t n = 8193-offset;
        if (n > count) n = count;
        assert(n <= 128);
        reads[i].ticket = ++read_sequence; reads[i].owner = owner;
        reads[i].size = 8+n; reads[i].ready = 0;
        pxa_write_u32(reads[i].data,offset); pxa_write_u32(reads[i].data+4,8193);
        for (uint32_t j = 0; j < n; ++j) reads[i].data[8+j] = (uint8_t)((offset+j)%251);
        if (malformed_read) ++reads[i].data[4];
        *ticket = reads[i].ticket; return 0;
    }
    return PXA_STATUS_WOULD_BLOCK;
}
static pxa_status_t read_result(void *ctx, pxa_component_t owner, uint64_t ticket, pxa_bytes_t *out) {
    (void)ctx; *out = (pxa_bytes_t){0};
    for (unsigned i = 0; i < 2; ++i) if (reads[i].ticket == ticket && reads[i].owner == owner) {
        if (!reads[i].ready) return PXA_STATUS_WOULD_BLOCK;
        *out = (pxa_bytes_t){reads[i].data,reads[i].size}; return 0;
    }
    return PXA_STATUS_NOT_FOUND;
}
static void read_release(void *ctx, pxa_component_t owner, uint64_t ticket) {
    (void)ctx;
    for (unsigned i = 0; i < 2; ++i) if (reads[i].ticket == ticket && reads[i].owner == owner) {
        ++read_releases; memset(reads[i].data,0xa5,sizeof(reads[i].data));
        reads[i].ticket = 0; return;
    }
    assert(!"release wrong read ticket");
}
static pxa_package_file_t files[3], sound_file, image_file;
static pxa_package_file_t lit_file;
static const char lit_path[] = "assets/lit.pxr";
static uint8_t lit_digest[32] = {9};
static const char sound_path[]="assets/click.pcm";
static uint8_t sound_digest[32]={7};
static const char image_path[]="assets/ui.pxr";
static const char *paths[] = {"assets/a.pxr", "assets/b.pxr", "assets/p.pxr"};
static uint8_t digests[3][32], package_key[32];

static pxa_status_t find(void *ctx, pxa_component_t owner, pxa_bytes_t path, pxa_asset_info_t *info) {
    (void)ctx; (void)owner;
    if (path.size == sizeof(lit_path)-1 && !memcmp(path.data,lit_path,path.size)) {
        *info=(pxa_asset_info_t){.path=lit_file.path,.file=&lit_file,.kind=PXA_ASSET_PALETTE,
            .encoding=PXA_ASSET_ENCODING_RGB565,.width=256,.height=16,.decoded_bytes=8192,
            .stored_bytes=8224,.payload_offset=32,.format_version=1}; return 0;
    }
    if (path.size == sizeof(sound_path)-1 && !memcmp(path.data,sound_path,path.size)) {
        memset(info,0,sizeof(*info)); info->kind=PXA_ASSET_AUDIO;
        info->encoding=PXA_ASSET_ENCODING_PCM_U8_16K_MONO;
        info->path=sound_file.path; info->file=&sound_file; info->decoded_bytes=info->stored_bytes=160; return 0;
    }
    if (path.size == sizeof(image_path)-1 && !memcmp(path.data,image_path,path.size)) {
        *info=(pxa_asset_info_t){.path=image_file.path,.file=&image_file,.kind=PXA_ASSET_IMAGE,
            .encoding=PXA_ASSET_ENCODING_BGRA8888,.width=2,.height=2,.decoded_bytes=16,
            .stored_bytes=48,.payload_offset=32,.format_version=1}; return 0;
    }
    if (path.size == sizeof(blob_path)-1 && !memcmp(path.data,blob_path,path.size)) {
        memset(info,0,sizeof(*info)); info->kind = PXA_ASSET_BLOB;
        info->stored_bytes = info->decoded_bytes = 8193; return 0;
    }
    unsigned i;
    for (i = 0; i < 3; ++i)
        if (strlen(paths[i]) == path.size && !memcmp(paths[i], path.data, path.size)) break;
    if (i == 3) return PXA_STATUS_NOT_FOUND;
    memset(info, 0, sizeof(*info));
    info->path = files[i].path; info->file = &files[i];
    info->kind = i == 2 ? PXA_ASSET_PALETTE : PXA_ASSET_TEXTURE;
    info->encoding = i == 2 ? PXA_ASSET_ENCODING_RGB565 : PXA_ASSET_ENCODING_INDEX8;
    info->width = i == 2 ? 256 : 2; info->height = i == 2 ? 1 : 2;
    info->decoded_bytes = i == 2 ? 512 : 4;
    info->stored_bytes = info->decoded_bytes + 32; info->payload_offset = 32; info->format_version = 1;
    return 0;
}
static pxa_status_t request(void *ctx, pxa_component_t owner, pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    pxa_asset_info_t info;
    assert(find(ctx, owner, path, &info) == 0); ++requests;
    return pxa_asset_cache_request(cache, owner, package_key, &info, cls, ticket);
}
static pxa_status_t prefetch(void *ctx, pxa_component_t owner, pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    pxa_asset_info_t info;
    assert(find(ctx, owner, path, &info) == 0); ++requests;
    return pxa_asset_cache_prefetch(cache, owner, package_key, &info, cls, ticket);
}
static pxa_status_t inspect(void *ctx, pxa_component_t owner, pxa_bytes_t path, pxa_asset_request_state_t *state) {
    pxa_asset_info_t info;
    pxa_status_t status = find(ctx, owner, path, &info);
    if (status != PXA_STATUS_OK) return status;
    return pxa_asset_cache_inspect(cache, owner, package_key, &info,
        pxa_asset_default_memory_class(&info), state);
}
static pxa_status_t query(void *ctx, pxa_component_t owner, pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state) {
    (void)ctx; return pxa_asset_cache_query(cache, owner, ticket, state);
}
static pxa_status_t acquire(void *ctx, pxa_component_t owner, pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset) {
    (void)ctx; return pxa_asset_cache_acquire(cache, owner, ticket, asset);
}
static void release(void *ctx, pxa_component_t owner, pxa_asset_ticket_t ticket) {
    (void)ctx; assert(pxa_asset_cache_release(cache, owner, ticket) == 0);
}
static void *allocate(void *ctx, size_t bytes) { (void)ctx; live_bytes += bytes; return malloc(bytes); }
static void deallocate(void *ctx, void *memory) {
    (void)ctx; live_bytes -= pxa_raster_asset_allocation_bytes(memory); free(memory);
}
static void run_jobs(void) {
    for (unsigned i = 0; i < 2; ++i) if (reads[i].ticket) reads[i].ready = 1;
    pxa_asset_job_t job, discard;
    while (pxa_asset_cache_next_job(cache, &job) == 0) {
        if (job.kind == PXA_ASSET_JOB_RELEASE) {
            pxa_raster_asset_release(job.asset);
            assert(pxa_asset_cache_finish_release(cache, job.token) == 0);
        } else if (job.kind == PXA_ASSET_JOB_LOAD) {
            pxa_raster_asset_t *asset; uint8_t *payload;
            assert(pxa_asset_object_create(&job.info, allocate, deallocate, NULL, &asset, &payload) == 0);
            if (job.info.kind == PXA_ASSET_PALETTE)
                for (unsigned i = 0; i < job.info.decoded_bytes / 2; ++i) pxa_write_u16(payload + i * 2, (uint16_t)(i * 501));
            else memset(payload, job.info.file->sha256[0], job.info.decoded_bytes);
            pxa_raster_asset_finish_loading(asset);
            assert(pxa_asset_cache_finish_load(cache, job.token, 0, asset, &discard) == 0);
            if (discard.kind) {
                assert(discard.kind == PXA_ASSET_JOB_RELEASE);
                pxa_raster_asset_release(discard.asset);
                assert(pxa_asset_cache_finish_release(cache, discard.token) == 0);
            }
        }
    }
    pxa_assets_service_poll(service);
}

/* Native SDK transport test. Real WAMR maintains its own full-token map;
 * this bridge restricts request tokens to u32 but never truncates handles. */
int32_t pxa_submit(const uint8_t *data, uint32_t size) {
    pxa_event_t wire;
    assert(pxa_parse_event(data, size, &wire));
    assert(wire.token <= UINT32_MAX);
    pxa_message_view_t message = {wire.service, wire.opcode, (uint32_t)wire.token,
        {wire.payload, wire.payload_size}};
    assert(pxa_component_begin_event(runtime, component) == 0);
    pxa_status_t status;
    if (wire.service == 1 && wire.opcode == 1) status = pxa_request_cancel(runtime, component, (uint32_t)pxa_load_u64(wire.payload));
    else if (wire.service == 1 && wire.opcode == 2) status = pxa_handle_close64(runtime, component, pxa_load_u64(wire.payload));
    else status = pxa_runtime_control_view(runtime, component, &message);
    assert(pxa_component_finish_event(runtime, component, 1) == 0);
    return status;
}
int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data, uint32_t size) {
    assert(pxa_component_begin_event(runtime, component) == 0);
    int32_t status = pxa_runtime_io64(runtime, component, handle, operation, data, size);
    assert(pxa_component_finish_event(runtime, component, 1) == 0);
    return status;
}
static size_t pop(uint32_t id, uint16_t opcode, uint8_t *data) {
    uint8_t buffer[512]; size_t size; pxa_message_view_t message;
    assert(pxa_event_pop(runtime, component, buffer, sizeof(buffer), &size) == 0);
    assert(pxa_message_decode(buffer, size, sizeof(buffer), &message) == 0);
    assert(message.request_id == id && message.opcode == opcode);
    memcpy(data, message.payload.data, message.payload.size);
    return message.payload.size;
}
static pxa_asset_result_t result(uint32_t id, uint16_t opcode, pxa_status_t expected) {
    uint8_t payload[64]; pxa_asset_result_t output;
    size_t size = pop(id, opcode, payload);
    pxa_event_t event = {21, opcode, id, payload, (uint32_t)size};
    assert(pxa_assets_parse_result(&event, id, opcode, &output));
    assert(output.status == expected);
    return output;
}
static void blob_result(uint32_t token, int32_t expected, uint32_t offset, uint32_t count) {
    uint8_t payload[512]; pxa_asset_read_result_t output;
    size_t n = pop(token,PXA_ASSETS_READ,payload);
    pxa_event_t event = {21,PXA_ASSETS_READ,token,payload,(uint32_t)n};
    assert(pxa_assets_parse_read(&event,token,&output) && output.status == expected);
    if (expected) return;
    assert(output.offset == offset && output.total_bytes == 8193 && output.bytes == count);
    for (uint32_t i = 0; i < count; ++i) assert(output.data[i] == (offset+i)%251);
    assert(!pxa_assets_parse_read(&event,token+1,&output));
    payload[8] = 0; payload[9] = 0; /* invalid total */
    assert(!pxa_assets_parse_read(&event,token,&output));
}
static void status_result(uint32_t token, const char *path, uint8_t expected) {
    uint8_t payload[64]; pxa_asset_status_t output;
    assert(!pxa_assets_status(token, path));
    size_t size = pop(token, PXA_ASSETS_STATUS, payload);
    pxa_event_t event = {21, PXA_ASSETS_STATUS, token, payload, (uint32_t)size};
    assert(pxa_assets_parse_status(&event, token, &output));
    assert(!output.status && output.state == expected);
    payload[5] = 1; assert(!pxa_assets_parse_status(&event, token, &output));
}
static pxa_status_t create_render(void *ctx, const pxa_game_render_desc_t *desc, uint64_t *id, uint32_t *caps) {
    (void)ctx; assert(desc->width == 2 && desc->height == 2); *id = 1; *caps = PXA_RASTER_CAP_KNOWN_MASK; return 0;
}
static pxa_status_t unused_packet(void *ctx, uint64_t id, const uint8_t *p, size_t n) {
    (void)ctx; (void)id; (void)p; (void)n; return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t query_render(void *ctx, uint64_t id, pxa_raster_telemetry_t *t) {
    (void)ctx; (void)id; memset(t, 0, sizeof(*t)); return 0;
}
static void close_render(void *ctx, uint64_t id) { (void)ctx; (void)id; pxa_raster_bindings_release(&bindings); }
static pxa_status_t bind_render(void *ctx, uint64_t id, const pxa_raster_bindings_t *r, uint64_t mask, uint8_t palette) {
    pxa_raster_bindings_t retired = {0}; (void)ctx; assert(id == 1); ++binds;
    pxa_raster_bindings_update(&bindings, r, mask, palette, &retired);
    pxa_raster_bindings_release(&retired); return 0;
}
static uint16_t pixel(const pxa_raster_bindings_t *b) {
    pxa_raster_resources_t v; pxa_raster_bindings_view(b, PXA_RASTER_CAP_KNOWN_MASK, &v);
    assert(v.textures[0].pixels && v.palette);
    return v.palette[v.textures[0].pixels[0]];
}

int main(void) {
    pxa_runtime_limits_t limits; pxa_assets_config_t config = {0};
    pxa_asset_cache_config_t cc = {8, 8, 8, 8, {4096, 16384}, {4096, 16384}};
    pxa_component_t other;
    pxa_game_render_config_t gc = {0}; pxa_game_render_service_t *render;
    pxa_asset_result_t a, b, p;
    pxa_raster_bindings_t frame = {0};
    uint8_t data[512]; uint32_t size;
    for (unsigned i = 0; i < 3; ++i) {
        digests[i][0] = (uint8_t)(i + 1);
        files[i].path = (pxa_bytes_t){(const uint8_t *)paths[i], strlen(paths[i])};
        files[i].sha256 = digests[i]; files[i].size = i == 2 ? 544 : 36;
    }
    image_file=(pxa_package_file_t){{(const uint8_t *)image_path,sizeof(image_path)-1},48,sound_digest};
    sound_file=(pxa_package_file_t){{(const uint8_t *)sound_path,sizeof(sound_path)-1},160,sound_digest};
    lit_file=(pxa_package_file_t){{(const uint8_t *)lit_path,sizeof(lit_path)-1},8224,lit_digest};
    pxa_runtime_limits_init(&limits);
    limits.max_components = 2; limits.max_requests = 8; limits.max_requests_per_component = 8;
    limits.max_services = 3; limits.max_handles = 8; limits.max_events = 16;
    limits.mailbox_capacity = 8; limits.event_block_count = 32;
    size_t n = pxa_runtime_workspace_size(&limits); void *rw = malloc(n);
    assert(pxa_runtime_init(rw, n, &limits, &runtime) == 0);
    void *cw = malloc(pxa_asset_cache_workspace_size(&cc));
    assert(pxa_asset_cache_init(cw, pxa_asset_cache_workspace_size(&cc), &cc, &cache) == 0);
    config.struct_size = sizeof(config); config.max_pending = config.max_pending_per_component = 2;
    config.max_resources = config.max_resources_per_component = 3;
    config.backend = (pxa_assets_backend_t){NULL, find, request, query, acquire, release, prefetch, inspect, read_blob, read_result, read_release};
    n = pxa_assets_service_workspace_size(&config); void *sw = malloc(n);
    assert(pxa_assets_service_init(sw, n, runtime, &config, &service) == 0);
    assert(pxa_assets_service_register(service) == 0);
    gc.struct_size = sizeof(gc); gc.max_contexts = gc.max_contexts_per_component = 1;
    gc.min_buffer_count = 2; gc.max_buffer_count = 3;
    gc.backend = (pxa_game_render_backend_t){sizeof(gc.backend), NULL, create_render, unused_packet, unused_packet,
        query_render, close_render, bind_render};
    n = pxa_game_render_service_workspace_size(&gc); void *gw = malloc(n);
    assert(pxa_game_render_service_init(gw, n, runtime, &gc, &render) == 0);
    assert(pxa_game_render_service_register(render) == 0);
    assert(pxa_component_create(runtime, 1, &component) == 0);
    assert(pxa_component_set_core_major(runtime, component, 1) == 0);
    assert(pxa_component_begin_start(runtime, component) == 0);
    assert(pxa_component_finish_start(runtime, component, 0) == 0);
    assert(pxa_component_create(runtime, 2, &other) == 0);
    assert(pxa_component_set_core_major(runtime, other, 1) == 0);
    assert(pxa_component_begin_start(runtime, other) == 0);
    assert(pxa_component_finish_start(runtime, other, 0) == 0);

    /* Full-width SDK token roundtrip independently of the native bridge. */
    assert(pxa_assets_build(data, sizeof(data), 2, UINT64_C(0xabcdef0100000001), paths[0], 1, &size));
    pxa_event_t wire; assert(pxa_parse_event(data, size, &wire));
    assert(wire.token == UINT64_C(0xabcdef0100000001));
    assert(pxa_assets_query(1, paths[0]) == 0);
    a = result(1, 1, 0); assert(!a.handle && a.info.decoded_bytes == 4 && a.info.resident_bytes > 4);
    assert(requests == 0);
    status_result(100, paths[0], PXA_ASSET_ABSENT);
    assert(!pxa_assets_prefetch_texture(101, paths[0]));
    status_result(102, paths[0], PXA_ASSET_QUEUED);
    assert(!pxa_cancel(101)); result(101, PXA_ASSETS_PREFETCH, PXA_STATUS_CANCELLED);
    status_result(103, paths[0], PXA_ASSET_ABSENT);
    assert(!pxa_assets_prefetch_texture(104, paths[0]));
    assert(!pxa_assets_load_texture(105, paths[0]));
    run_jobs();
    a = result(104, PXA_ASSETS_PREFETCH, 0); assert(!a.handle && a.info.width == 2);
    a = result(105, PXA_ASSETS_LOAD, 0); assert(a.handle);
    assert(!pxa_close_handle(a.handle));
    status_result(106, paths[0], PXA_ASSET_READY);
    pxa_asset_cache_stats_t prefetch_stats; pxa_asset_cache_stats(cache, &prefetch_stats);
    assert(!prefetch_stats.requests); // no persistent Guest handle or cache ticket
    assert(pxa_asset_cache_trim(cache, 1, SIZE_MAX)); run_jobs();
    status_result(107, paths[0], PXA_ASSET_ABSENT);
    assert(pxa_assets_load_texture(2, paths[0]) == 0);
    uint64_t identity = pxa_request_identity(runtime, component, 2); assert(identity);
    assert(pxa_cancel(2) == 0); result(2, 2, PXA_STATUS_CANCELLED);
    /* Another service immediately reuses the same ID before assets poll. */
    assert(pxa_request_begin_reserved(runtime, component, 2, 4, 1, 0, 0) == 0);
    assert(pxa_request_identity(runtime, component, 2) != identity);
    run_jobs();
    assert(pxa_request_is_active(runtime, component, 2));
    assert(pxa_request_complete(runtime, component, 2, 0, NULL, 0) == 0);
    assert(pop(2, 1, data) == 4);
    pxa_asset_cache_cancel_owner(cache, component); run_jobs();

    assert(pxa_assets_load_texture(3, paths[0]) == 0);
    assert(pxa_assets_load_texture(4, paths[1]) == 0);
    assert(pxa_assets_load_palette(5, paths[2]) == PXA_STATUS_WOULD_BLOCK);
    assert(!pxa_request_identity(runtime, component, 5));
    run_jobs(); a = result(3, 2, 0); b = result(4, 2, 0);
    assert(a.handle > UINT32_MAX && b.handle > UINT32_MAX);
    assert(pxa_assets_load_palette(5, paths[2]) == 0);
    run_jobs(); p = result(5, 2, 0);
    assert(pxa_assets_load_texture(6, paths[0]) == 0);
    run_jobs(); result(6, 2, PXA_STATUS_RESOURCE_LIMIT);
    /* Prefetch does not need a resource handle even when that table is full. */
    assert(!pxa_assets_prefetch_palette(108, paths[2]));
    run_jobs(); assert(!result(108, PXA_ASSETS_PREFETCH, 0).handle);

    pxa_raster_asset_t *native = NULL;
    assert(pxa_assets_acquire_handle(runtime, other, a.handle, 1, &native) != 0 && !native);
    assert(pxa_assets_acquire_handle(runtime, component, a.handle, 2, &native) != 0 && !native);
    pxa_game_render_options_t options = {0}; options.width = options.height = 2; options.buffer_count = 2;
    assert(pxa_game_render_build_create(data, sizeof(data), 7, &options, &size));
    assert(pxa_submit(data, size) == 0); assert(pop(7, 1, data) == 24);
    uint64_t context = pxa_read_u64(data + 4);
    pxa_game_render_binding_t batch[2] = {{a.handle, 1, 0}, {p.handle, 2, 0}};
    assert(pxa_game_render_bind_assets(context, batch, 2) == 28 && binds == 1);
    assert(pixel(&bindings) == 501);
    pxa_raster_bindings_snapshot(&frame, &bindings);
    assert(pxa_close_handle(a.handle) == 0);
    batch[0].handle = b.handle; batch[1].handle = a.handle; /* Wrong/stale palette: no partial update. */
    assert(pxa_game_render_bind_assets(context, batch, 2) < 0 && binds == 1);
    assert(pixel(&bindings) == 501);
    assert(pxa_game_render_bind_assets(context, batch, 1) == 16 && binds == 2);
    assert(pxa_close_handle(b.handle) == 0 && pxa_close_handle(p.handle) == 0);
    assert(pixel(&frame) == 501 && pixel(&bindings) == 1002);
    assert(pxa_assets_acquire_handle(runtime, component, a.handle, 1, &native) != 0);
    batch[0].handle = 0; batch[1].handle = 0;
    assert(pxa_game_render_bind_assets(context, batch, 2) == 28);
    assert(!bindings.textures[0] && !bindings.palette && pixel(&frame) == 501);
    pxa_raster_bindings_release(&frame);
    assert(pxa_close_handle(context) == 0);

    /* Scene helper through real Core: cancel loses to a queued success, which
     * must be consumed and closed before reusing the caller's fixed storage. */
    pxa_asset_scene_t scene = {0};
    pxa_game_render_binding_t scene_storage[2] = {0};
    const pxa_asset_scene_item_t scene_items[] = {{paths[0],1,0}, {paths[1],1,1}};
    assert(!pxa_asset_scene_begin(&scene,scene_items,scene_storage,2,200,1));
    run_jobs();
    assert(!pxa_asset_scene_release(&scene));
    assert(pxa_asset_scene_begin(&scene,scene_items,scene_storage,2,210,1) == PXA_STATUS_BUSY);
    size_t scene_size = pop(200, PXA_ASSETS_LOAD, data);
    pxa_event_t scene_event = {21,PXA_ASSETS_LOAD,200,data,(uint32_t)scene_size};
    assert(pxa_asset_scene_on_event(&scene,&scene_event));
    assert(scene.state == PXA_SCENE_CANCELLED && !scene_storage[0].handle);
    assert(!pxa_asset_scene_begin(&scene,scene_items,scene_storage,2,210,1));
    for (unsigned i = 0; i < 2; ++i) {
        run_jobs(); scene_size = pop(210+i,PXA_ASSETS_LOAD,data);
        scene_event = (pxa_event_t){21,PXA_ASSETS_LOAD,210+i,data,(uint32_t)scene_size};
        assert(pxa_asset_scene_on_event(&scene,&scene_event));
    }
    assert(scene.state == PXA_SCENE_READY && scene_storage[0].handle && scene_storage[1].handle);
    assert(!pxa_asset_scene_release(&scene));

    assert(!pxa_assets_load_sound(290,sound_path)); run_jobs();
    a=result(290,PXA_ASSETS_LOAD,0); assert(a.info.kind==PXA_ASSET_AUDIO && a.info.decoded_bytes==160);
    assert(pxa_assets_acquire_handle(runtime,other,a.handle,PXA_ASSET_AUDIO,&native)!=0);
    assert(pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_TEXTURE,&native)!=0);
    assert(!pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_AUDIO,&native));
    pxa_asset_object_view_t sound_view; pxa_asset_object_view(native,&sound_view);
    assert(sound_view.kind==PXA_ASSET_AUDIO && sound_view.bytes==160 && sound_view.data[159]==7);
    assert(!pxa_close_handle(a.handle));
    pxa_asset_cache_trim(cache,1,SIZE_MAX); run_jobs();
    assert(sound_view.data[0]==7 && pxa_asset_object_reference_count(native)==2);
    pxa_asset_object_release_pinned(native); native=NULL;
    pxa_asset_cache_trim(cache,1,SIZE_MAX); run_jobs();

    /* The UI consumer keeps pixels alive after the Guest closes its handle. */
    assert(!pxa_assets_load_image(291,image_path)); run_jobs();
    a=result(291,PXA_ASSETS_LOAD,0);
    assert(a.info.kind==PXA_ASSET_IMAGE && a.info.encoding==PXA_ASSET_ENCODING_BGRA8888 && a.info.decoded_bytes==16);
    assert(pxa_assets_acquire_handle(runtime,other,a.handle,PXA_ASSET_IMAGE,&native)!=0 && !native);
    assert(pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_TEXTURE,&native)!=0 && !native);
    assert(!pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_IMAGE,&native));
    pxa_asset_object_view_t image_view; pxa_asset_object_view(native,&image_view);
    assert((uintptr_t)image_view.data % PXA_ASSET_IMAGE_ALIGNMENT == 0);
    assert(image_view.bytes==16 && image_view.data[15]==7);
    assert(!pxa_close_handle(a.handle));
    pxa_asset_cache_trim(cache,1,SIZE_MAX); run_jobs();
    assert(image_view.data[0]==7 && pxa_asset_object_reference_count(native)==2);
    pxa_asset_object_t *stale=NULL;
    assert(pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_IMAGE,&stale)!=0 && !stale);
    pxa_asset_object_release_pinned(native); native=NULL;
    pxa_asset_cache_trim(cache,1,SIZE_MAX); run_jobs();

    /* Expanded lighting tables must load even when they cannot fit the SRAM
     * budget. PREFETCH, path query and LOAD must agree on their cache class. */
    assert(!pxa_assets_prefetch_palette(311,lit_path)); run_jobs();
    assert(!result(311,PXA_ASSETS_PREFETCH,0).handle);
    pxa_asset_request_state_t lit_state;
    assert(!inspect(NULL,component,lit_file.path,&lit_state));
    assert(lit_state.state==PXA_ASSET_REQUEST_READY);
    assert(!pxa_assets_load_palette(312,lit_path)); run_jobs();
    a=result(312,PXA_ASSETS_LOAD,0);
    assert(!pxa_assets_acquire_handle(runtime,component,a.handle,PXA_ASSET_PALETTE,&native));
    pxa_asset_object_view_t lit_view; pxa_asset_object_view(native,&lit_view);
    assert(lit_view.bytes==8192 && pxa_read_u16(lit_view.data+8190)==(uint16_t)(4095u*501u));
    pxa_asset_cache_stats_t lit_stats; pxa_asset_cache_stats(cache,&lit_stats);
    assert(lit_stats.charged[PXA_ASSET_MEMORY_EXTERNAL]>=8192);
    assert(lit_stats.charged[PXA_ASSET_MEMORY_INTERNAL]<=4096);
    assert(!pxa_close_handle(a.handle)); pxa_asset_object_release_pinned(native); native=NULL;
    pxa_asset_cache_trim(cache,PXA_ASSET_MEMORY_EXTERNAL,SIZE_MAX); run_jobs();

    /* READ reserves event storage before starting backend I/O. The small
     * test pool cannot hold a maximum-size read; rejection leaves no request. */
    unsigned before = read_requests;
    assert(pxa_assets_read(300,blob_path,0,PXA_ASSETS_READ_MAX_BYTES) < 0);
    assert(read_requests == before && !pxa_request_identity(runtime,component,300));
    assert(!pxa_assets_build_read(data,sizeof(data),0,blob_path,0,1,&size));
    assert(!pxa_assets_build_read(data,sizeof(data),300,blob_path,0,4097,&size));
    assert(pxa_assets_build_read(data,sizeof(data),UINT64_C(0xfedcba980000012c),blob_path,4090,128,&size));
    assert(pxa_parse_event(data,size,&wire) && wire.token == UINT64_C(0xfedcba980000012c));
    assert(!pxa_assets_read(300,blob_path,4090,128));
    assert(!pxa_assets_read(301,blob_path,8193,128));
    assert(pxa_assets_read(302,blob_path,0,128) == PXA_STATUS_WOULD_BLOCK);
    run_jobs(); blob_result(300,0,4090,128); blob_result(301,0,8193,0);
    assert(!pxa_assets_read(302,blob_path,17,128));
    run_jobs(); blob_result(302,0,17,128); /* event owns a copy after release */
    assert(!pxa_assets_read(303,blob_path,8194,1));
    blob_result(303,PXA_STATUS_INVALID_ARGUMENT,0,0);
    assert(!pxa_assets_read(304,paths[0],0,1));
    blob_result(304,PXA_STATUS_UNSUPPORTED,0,0);
    assert(!pxa_assets_read(305,"assets/missing.bin",0,1));
    blob_result(305,PXA_STATUS_NOT_FOUND,0,0);
    assert(pxa_assets_read(306,"../map.bin",0,1) == PXA_STATUS_INVALID_ARGUMENT);
    read_error = PXA_STATUS_RESOURCE_LIMIT;
    assert(!pxa_assets_read(306,blob_path,0,1));
    blob_result(306,PXA_STATUS_RESOURCE_LIMIT,0,0); read_error = 0;
    malformed_read = 1;
    assert(!pxa_assets_read(307,blob_path,0,1)); run_jobs();
    blob_result(307,PXA_STATUS_PROTOCOL_ERROR,0,0); malformed_read = 0;
    assert(!pxa_assets_read(308,blob_path,0,128));
    assert(!pxa_cancel(308)); blob_result(308,PXA_STATUS_CANCELLED,0,0);
    assert(!pxa_request_begin_reserved(runtime,component,308,4,1,0,0));
    run_jobs(); assert(pxa_request_is_active(runtime,component,308));
    assert(!pxa_request_complete(runtime,component,308,0,NULL,0)); assert(pop(308,1,data) == 4);
    assert(!pxa_assets_read(309,blob_path,4097,64)); run_jobs();
    assert(!pxa_cancel(309)); blob_result(309,0,4097,64); /* completion won */
    assert(read_releases == read_requests-1); /* one immediate backend failure */
    assert(!pxa_assets_read(310,blob_path,0,128));

    /* Stop while a request remains queued; no worker result may reopen handles. */
    assert(pxa_assets_load_texture(8, paths[1]) == 0);
    assert(pxa_component_abort(runtime, component, PXA_STOP_NORMAL) == 0);
    run_jobs();
    pxa_asset_cache_stats_t stats; pxa_asset_cache_stats(cache, &stats); assert(stats.requests == 0);
    pxa_asset_cache_shutdown(cache); run_jobs(); assert(pxa_asset_cache_drained(cache));
    assert(live_bytes == 0 && !reads[0].ticket && !reads[1].ticket);
    pxa_runtime_deinit(runtime);
    free(gw); free(sw); free(cw); free(rw);
    puts("assets service: SDK, async cancellation/ID reuse, ownership, atomic binding and shutdown passed");
    return 0;
}
