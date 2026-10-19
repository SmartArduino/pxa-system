#undef NDEBUG
#include "pxa/assets.h"
#include "pxa/service.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pxa_image_target_t *target;
    unsigned bytes;
    unsigned frame;
} codec_t;
static unsigned live_sessions, io_calls, block_reads, input_position;
static int codec_error, fail_read;
static uint16_t decode_flags;
static uint32_t animation_pixels, animation_bytes;
static size_t input_hint;
static int gif_signature;
static const pxa_memory_allocator_t *allocator;
static void *allocate(void *context, size_t n) {
    (void)context;
    return malloc(n);
}
static void release(void *context, void *p) {
    (void)context;
    free(p);
}
static pxa_status_t codec_open(pxa_bytes_t signature, pxa_image_target_t *target,
                               const pxa_memory_allocator_t *temporary, void **out) {
    assert(signature.size == 8);
    pxa_image_target_set_input_budget(target, input_hint);
    codec_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s)
        return PXA_STATUS_RESOURCE_LIMIT;
    *s = (codec_t){.target = target};
    *out = s;
    ++live_sessions;
    return 0;
}
static pxa_status_t codec_write(void *p, pxa_bytes_t bytes) {
    ((codec_t *)p)->bytes += (unsigned)bytes.size;
    return codec_error;
}
static pxa_status_t codec_finish(void *p) {
    codec_t *s = p;
    assert(s->bytes == 6000);
    pxa_status_t status = pxa_image_target_begin(s->target, 320, 180, 1);
    if (status)
        return status;
    const uint8_t rgba[4] = {255, 127, 31, 90};
    pxa_image_tile_t tile = {0, 0, 320, 180, 1, 1, 4, PXA_IMAGE_RGBA8888, rgba};
    return pxa_image_target_blit(s->target, &tile);
}
static void codec_close(void *p) {
    assert(live_sessions);
    --live_sessions;
    pxa_memory_release(p);
}
static pxa_status_t codec_next(void *p, pxa_image_frame_info_t *frame) {
    codec_t *s = p;
    if (codec_error) return codec_error;
    if (s->frame) {
        pxa_status_t status = pxa_image_target_begin_frame(s->target, NULL);
        if (status) return status;
        const uint8_t rgba[4] = {31, 127, 255, 90};
        pxa_image_tile_t tile = {0, 0, 320, 180, 1, 1, 4, PXA_IMAGE_RGBA8888, rgba};
        status = pxa_image_target_blit(s->target, &tile);
        if (status) return status;
    }
    *frame = (pxa_image_frame_info_t){.delay_ms = s->frame ? 655350 : 30,
                                    .iteration = 0, .last = s->frame == 2};
    ++s->frame;
    return PXA_STATUS_OK;
}
static int32_t read_source(void *context, uint32_t operation, uint8_t *bytes, size_t n) {
    (void)context;
    assert(operation == PXA_IO_READ && n <= 1024);
    ++io_calls;
    if (block_reads) {
        --block_reads;
        return PXA_STATUS_WOULD_BLOCK;
    }
    if (fail_read)
        return PXA_STATUS_IO_ERROR;
    if (n > 6000 - input_position)
        n = 6000 - input_position;
    memset(bytes, 0x42, n);
    if (!input_position && gif_signature && n >= 6) memcpy(bytes, "GIF89a", 6);
    input_position += (unsigned)n;
    return (int32_t)n;
}
static pxa_runtime_t *runtime;
static pxa_component_t component, other;
static pxa_assets_service_t *assets;
static pxa_handle64_t source;
static pxa_status_t request(pxa_request_token_t token, uint16_t type) {
    uint8_t bytes[32] = {0};
    pxa_write_u64(bytes, source);
    pxa_write_u16(bytes + 8, type);
    pxa_write_u16(bytes + 10, decode_flags);
    pxa_write_u16(bytes + 12, 64);
    pxa_write_u16(bytes + 14, 64);
    pxa_write_u32(bytes + 16, 4096);
    pxa_write_u32(bytes + 20, 131072);
    pxa_write_u32(bytes + 24, animation_pixels);
    pxa_write_u32(bytes + 28, animation_bytes);
    pxa_message_view_t message = {21, 6, token,
        {bytes, decode_flags & PXA_ASSETS_IMAGE_WITH_FRAMES ? sizeof(bytes) : 24}};
    assert(!pxa_component_begin_event(runtime, component));
    pxa_status_t status = pxa_runtime_control_view(runtime, component, &message);
    assert(!pxa_component_finish_event(runtime, component, 1));
    return status;
}
static pxa_status_t advance(pxa_request_token_t token, pxa_handle64_t image) {
    uint8_t bytes[8];
    pxa_write_u64(bytes, image);
    pxa_message_view_t message = {21, 7, token, {bytes, sizeof(bytes)}};
    assert(!pxa_component_begin_event(runtime, component));
    pxa_status_t status = pxa_runtime_control_view(runtime, component, &message);
    assert(!pxa_component_finish_event(runtime, component, 1));
    return status;
}
static pxa_status_t frame_result(pxa_request_token_t token, uint16_t opcode,
                                 pxa_handle64_t *image, int last) {
    uint8_t data[128]; size_t size;
    pxa_message_view_t message;
    assert(!pxa_event_pop(runtime, component, data, sizeof(data), &size));
    assert(!pxa_message_decode(data, size, sizeof(data), &message));
    assert(message.request_token == token && message.opcode == opcode);
    pxa_status_t status = (pxa_status_t)pxa_read_u32(message.payload.data);
    if (!status) {
        assert(message.payload.size == 52);
        *image = pxa_read_u64(message.payload.data + 4);
        assert(pxa_read_u16(message.payload.data + 32) == 320);
        assert(pxa_read_u16(message.payload.data + 34) == 180);
        assert(pxa_read_u32(message.payload.data + 36) == 6000);
        assert(pxa_read_u32(message.payload.data + 40) == (opcode == 6 ? 30u : 655350u));
        assert(!pxa_read_u32(message.payload.data + 44));
        assert(pxa_read_u32(message.payload.data + 48) == (unsigned)last);
    } else assert(message.payload.size == 4);
    return status;
}
static pxa_status_t result(pxa_request_token_t token, pxa_handle64_t *image) {
    uint8_t data[128];
    size_t size;
    pxa_message_view_t message;
    assert(!pxa_event_pop(runtime, component, data, sizeof(data), &size));
    assert(!pxa_message_decode(data, size, sizeof(data), &message));
    if (message.request_token != token || message.opcode != 6)
        fprintf(stderr, "expected token=%" PRIu64 " got=%" PRIu64 " opcode=%u service=%u\n", token,
                message.request_token, message.opcode, message.service);
    assert(message.request_token == token && message.opcode == 6);
    pxa_status_t status = (pxa_status_t)pxa_read_u32(message.payload.data);
    if (!status) {
        assert(message.payload.size == 40);
        *image = pxa_read_u64(message.payload.data + 4);
        assert(pxa_read_u16(message.payload.data + 32) == 320);
        assert(pxa_read_u16(message.payload.data + 34) == 180);
        assert(pxa_read_u32(message.payload.data + 36) == 6000);
    }
    return status;
}
static void reset_input(void) {
    input_position = io_calls = block_reads = 0;
    codec_error = fail_read = 0;
    decode_flags = 0;
    animation_pixels = animation_bytes = 0;
    gif_signature = 0;
}
int main(void) {
    pxa_memory_budget_t budget;
    pxa_memory_budget_config_t bc = {0};
    bc.limit[1] = 262144;
    bc.temporary_limit[1] = 65536;
    assert(!pxa_memory_budget_init(&budget, &bc));
    pxa_memory_owner_t owner;
    assert(!pxa_memory_owner_open(&budget, bc.limit, &owner));
    pxa_memory_allocator_t pixels = {&budget, owner, 1, PXA_MEMORY_IMAGE, NULL, allocate, release};
    pxa_memory_allocator_t temporary = pixels;
    temporary.kind = PXA_MEMORY_TEMPORARY;
    allocator = &temporary;
    (void)allocator;
    pxa_image_codec_t codec = {.open = codec_open, .write = codec_write,
                                    .finish = codec_finish, .close = codec_close};
    const pxa_image_codec_t *codecs[] = {&codec};
    const pxa_image_backend_t backend = {codecs, 1};
    pxa_runtime_limits_t limits;
    pxa_runtime_limits_init(&limits);
    limits.max_components = 2;
    limits.max_services = 1;
    limits.max_requests = 4;
    limits.max_requests_per_component = 4;
    size_t n = pxa_runtime_workspace_size(&limits);
    void *rw = malloc(n);
    assert(rw);
    assert(!pxa_runtime_init(rw, n, &limits, &runtime));
    pxa_assets_config_t c = {0};
    c.struct_size = sizeof(c);
    c.max_pending = c.max_pending_per_component = 2;
    c.max_resources = c.max_resources_per_component = 2;
    c.image_allocator = &pixels;
    c.image_temporary_allocator = &temporary;
    c.image_backend = &backend;
    n = pxa_assets_service_workspace_size(&c);
    void *sw = malloc(n);
    assert(sw);
    assert(!pxa_assets_service_init(sw, n, runtime, &c, &assets));
    assert(!pxa_assets_service_register(assets));
    assert(!pxa_component_create(runtime, 1, &component));
    assert(!pxa_component_set_wire_profile(runtime, component, 1));
    assert(!pxa_component_begin_start(runtime, component));
    assert(!pxa_component_finish_start(runtime, component, 0));
    assert(!pxa_component_create(runtime, 2, &other));
    assert(!pxa_component_begin_start(runtime, other));
    assert(!pxa_component_finish_start(runtime, other, 0));
    const pxa_resource_ops_t ops = {sizeof(ops), read_source};
    const pxa_resource_t native = {NULL, &ops, NULL};
    assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_STREAM, 55, &native, &source));
    pxa_handle64_t image = 0;
    reset_input();
    block_reads = 2;
    assert(!request(1, 2));
    assert(!pxa_assets_service_poll(assets));
    assert(io_calls == 1 && !input_position);
    assert(!pxa_assets_service_poll(assets));
    assert(io_calls == 2 && !input_position);
    assert(pxa_assets_service_poll(assets));
    assert(io_calls == 6 && input_position == 64+3*1024 && live_sessions == 1);
    /* The owner schedules this solely from the preceding return value;
     * there are no Guest ticks and no pending Net wait operation. */
    assert(pxa_assets_service_poll(assets));
    assert(!result(1, &image) && image && !live_sessions);
    assert(!pxa_assets_service_poll(assets));
    pxa_raster_asset_t *view;
    assert(!pxa_assets_acquire_handle(runtime, component, image, PXA_ASSET_IMAGE, &view));
    assert(pxa_assets_acquire_handle(runtime, other, image, PXA_ASSET_IMAGE, &view) ==
           PXA_STATUS_NOT_FOUND);
    /* The first retained reference stays valid after both source and Guest image
     * close. */
    assert(!pxa_assets_acquire_handle(runtime, component, image, PXA_ASSET_IMAGE, &view));
    assert(!pxa_handle_close64(runtime, component, image));
    pxa_asset_object_release(view);
    pxa_asset_object_release(view);
    /* A codec can ask the owner loop for short input slices. The initial
     * probe is at most 64 bytes; later polls honor the selected codec, and a
     * cancellation closes the partially decoded session before another read. */
    input_hint = 16;
    reset_input();
    assert(!request(101, PXA_RESOURCE_STREAM));
    assert(pxa_assets_service_poll(assets) && input_position == 64 && live_sessions == 1);
    for (unsigned i = 0; i < 4; ++i) {
        unsigned before = input_position;
        assert(pxa_assets_service_poll(assets) && input_position - before == 16);
    }
    unsigned before_cancel = input_position;
    assert(!pxa_request_cancel(runtime, component, 101));
    pxa_assets_service_poll(assets);
    assert(!live_sessions && input_position == before_cancel);
    assert(result(101, &image) == PXA_STATUS_CANCELLED);
    input_hint = 0;
    /* A plain ready file must make progress until EOF without native wakes. */
    assert(!pxa_handle_close64(runtime, component, source));
    assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_FILE, 0, &native, &source));
    reset_input();
    assert(!request(9, PXA_RESOURCE_FILE));
    unsigned turns = 0;
    while (pxa_assets_service_poll(assets)) assert(++turns <= 2);
    assert(turns == 2 && input_position == 6000);
    assert(!result(9, &image));
    assert(!pxa_handle_close64(runtime, component, image));
    assert(!pxa_handle_close64(runtime, component, source));
    assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_STREAM, 55, &native, &source));
    reset_input();
    assert(!request(2, 2));
    pxa_assets_service_poll(assets);
    assert(live_sessions == 1);
    assert(!pxa_request_cancel(runtime, component, 2));
    pxa_assets_service_poll(assets);
    assert(!live_sessions);
    assert(result(2, &image) == PXA_STATUS_CANCELLED);
    reset_input();
    assert(!request(3, 2));
    assert(!pxa_handle_close64(runtime, component, source));
    pxa_assets_service_poll(assets);
    assert(result(3, &image) == PXA_STATUS_NOT_FOUND);
    assert(!pxa_handle_open64(runtime, other, PXA_RESOURCE_STREAM, 0, &native, &source));
    assert(request(4, 2) == PXA_STATUS_NOT_FOUND);
    assert(!pxa_handle_close64(runtime, other, source));
    assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_FILE, 55, &native, &source));
    reset_input();
    assert(!request(5, 3));
    assert(!pxa_authority_revoke(runtime, component, 55));
    pxa_assets_service_poll(assets);
    assert(result(5, &image) == PXA_STATUS_NOT_FOUND);
    assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_FILE, 0, &native, &source));
    reset_input();
    codec_error = PXA_STATUS_PROTOCOL_ERROR;
    assert(!request(6, 3));
    pxa_assets_service_poll(assets);
    assert(result(6, &image) == PXA_STATUS_PROTOCOL_ERROR && !live_sessions);
    reset_input();
    fail_read = 1;
    assert(!request(7, 3));
    pxa_assets_service_poll(assets);
    assert(result(7, &image) == PXA_STATUS_IO_ERROR);
    /* A format mask rejects disguised GIF before allocating a codec session. */
    reset_input(); gif_signature = 1; decode_flags = 3;
    assert(!request(10, 3));
    pxa_assets_service_poll(assets);
    assert(result(10, &image) == PXA_STATUS_UNSUPPORTED && !live_sessions);
    reset_input(); decode_flags = 8;
    assert(request(11, 3) == PXA_STATUS_INVALID_ARGUMENT);
    codec.next = codec_next;
    reset_input(); decode_flags = PXA_ASSETS_IMAGE_WITH_FRAMES | 4; gif_signature = 1;
    animation_pixels = 1024;
    assert(!request(12, 3));
    while (pxa_assets_service_poll(assets)) {}
    assert(!frame_result(12, 6, &image, 0) && live_sessions == 1);
    assert(!pxa_assets_acquire_handle(runtime, component, image, PXA_ASSET_IMAGE, &view));
    pxa_asset_object_view_t old_view;
    pxa_asset_object_view(view, &old_view);
    assert(old_view.width * old_view.height <= 1024 && old_view.data[0] == 31);
    const pxa_handle64_t first = image;
    assert(!advance(13, first));
    assert(advance(14, first) == PXA_STATUS_BAD_STATE); /* overlapping request */
    assert(!pxa_request_cancel(runtime, component, 13));
    pxa_assets_service_poll(assets);
    assert(frame_result(13, 7, &image, 0) == PXA_STATUS_CANCELLED && live_sessions == 1);
    assert(!advance(14, first)); /* cancelled before polling: same frame owner */
    pxa_assets_service_poll(assets);
    assert(!frame_result(14, 7, &image, 0) && image != first && live_sessions == 1);
    assert(advance(15, first) == PXA_STATUS_BAD_STATE);
    assert(advance(15, image) == PXA_STATUS_RESOURCE_LIMIT); /* both slots occupied */
    assert(old_view.data[0] == 31); /* previous pixels did not change */
    assert(!pxa_handle_close64(runtime, component, first));
    assert(!advance(15, image));
    const pxa_handle64_t second = image;
    pxa_assets_service_poll(assets);
    assert(!frame_result(15, 7, &image, 1) && !live_sessions);
    assert(advance(16, image) == PXA_STATUS_BAD_STATE); /* final still, no state */
    assert(!pxa_handle_close64(runtime, component, second));
    assert(!pxa_handle_close64(runtime, component, image));
    assert(old_view.data[0] == 31); /* independently retained after all closes */
    pxa_asset_object_release(view);
    /* State owner closes before a pending advance. No UAF or leaked decoder. */
    reset_input(); decode_flags = PXA_ASSETS_IMAGE_WITH_FRAMES;
    assert(!request(17, 3));
    while (pxa_assets_service_poll(assets)) {}
    assert(!frame_result(17, 6, &image, 0) && live_sessions == 1);
    assert(!advance(18, image));
    assert(!pxa_handle_close64(runtime, component, image) && !live_sessions);
    pxa_assets_service_poll(assets);
    assert(frame_result(18, 7, &image, 0) == PXA_STATUS_NOT_FOUND);
    /* Frame decode error terminates state but preserves the old still. */
    reset_input(); decode_flags = PXA_ASSETS_IMAGE_WITH_FRAMES;
    assert(!request(19, 3));
    while (pxa_assets_service_poll(assets)) {}
    assert(!frame_result(19, 6, &image, 0));
    codec_error = PXA_STATUS_PROTOCOL_ERROR;
    assert(!advance(20, image));
    pxa_assets_service_poll(assets);
    assert(frame_result(20, 7, &image, 0) == PXA_STATUS_PROTOCOL_ERROR && !live_sessions);
    assert(!pxa_handle_close64(runtime, component, image));
    /* The animation input ceiling also applies after codec selection. */
    reset_input(); decode_flags = PXA_ASSETS_IMAGE_WITH_FRAMES; animation_bytes = 1024;
    assert(!request(21, 3));
    while (pxa_assets_service_poll(assets)) {}
    assert(frame_result(21, 6, &image, 0) == PXA_STATUS_LIMIT_EXCEEDED && !live_sessions);
    codec.next = NULL;
    reset_input();
    assert(!request(8, 3));
    pxa_assets_service_poll(assets);
    assert(live_sessions == 1);
    assert(!pxa_component_request_stop(runtime, component, 0));
    assert(!pxa_component_begin_stop(runtime, component));
    assert(!pxa_component_finish_stop(runtime, component));
    assert(!live_sessions);
    pxa_memory_stats_t stats;
    assert(!pxa_memory_budget_stats(&budget, owner, &stats));
    assert(!stats.charged[1]);
    assert(!pxa_memory_owner_close(&budget, owner));
    free(sw);
    free(rw);
    puts("image source service PASS: Host-only transfer, bounded yielding, "
         "ownership, cancellation, stale source, revocation, stop");
}
