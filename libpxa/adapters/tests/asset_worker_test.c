#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/posix/pxa_posix_asset_worker.h"
#include "pxa/openssl/pxa_openssl.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../tests/asset_read_worker_checks.h"

static pthread_mutex_t allocation_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_condition = PTHREAD_COND_INITIALIZER;
static int io_block, io_entered;
static void before_read(void *context) {
    (void)context;
    pthread_mutex_lock(&io_mutex);
    if (io_block) {
        io_entered = 1;
        pthread_cond_signal(&io_condition);
        while (io_block) pthread_cond_wait(&io_condition, &io_mutex);
    }
    pthread_mutex_unlock(&io_mutex);
}
static void block_storage(void) {
    pthread_mutex_lock(&io_mutex); io_block = 1; io_entered = 0; pthread_mutex_unlock(&io_mutex);
}
static void await_blocked_storage(void) {
    pthread_mutex_lock(&io_mutex);
    while (!io_entered) pthread_cond_wait(&io_condition, &io_mutex);
    pthread_mutex_unlock(&io_mutex);
}
static void release_storage(void) {
    pthread_mutex_lock(&io_mutex); io_block = 0;
    pthread_cond_signal(&io_condition); pthread_mutex_unlock(&io_mutex);
}
static pthread_mutex_t budget_mutex = PTHREAD_MUTEX_INITIALIZER;
static pxa_memory_budget_t read_budget;
static void budget_lock(void *ctx) { assert(!pthread_mutex_lock(ctx)); }
static void budget_unlock(void *ctx) { assert(!pthread_mutex_unlock(ctx)); }
static void *raw_allocate(void *ctx,size_t n) { (void)ctx; return malloc(n); }
static void raw_release(void *ctx,void *p) { (void)ctx; free(p); }
static pxa_status_t backend_read(void *ctx,pxa_component_t owner,pxa_bytes_t path,uint32_t offset,uint32_t n,uint64_t *ticket) {
    return pxa_posix_asset_worker_read(ctx,owner,path,offset,n,ticket);
}
static pxa_status_t backend_result(void *ctx,pxa_component_t owner,uint64_t ticket,pxa_bytes_t *out) {
    return pxa_posix_asset_worker_read_result(ctx,owner,ticket,out);
}
static void backend_release(void *ctx,pxa_component_t owner,uint64_t ticket) {
    pxa_status_t status=pxa_posix_asset_worker_read_release(ctx,owner,ticket);
    assert(!status || status==PXA_STATUS_NOT_FOUND);
}
static pthread_t guest_thread;
static struct { void *memory; size_t bytes; uint8_t cls; } allocations[32];
static size_t live[2], peak[2];
static unsigned load_count, notifications;
static void *allocate(void *context, size_t bytes, uint8_t cls, uint8_t kind) {
    (void)context; (void)kind;
    assert(!pthread_equal(pthread_self(), guest_thread));
    void *memory = malloc(bytes);
    assert(memory);
    pthread_mutex_lock(&allocation_mutex);
    unsigned i;
    for (i = 0; i < 32 && allocations[i].memory; ++i) {}
    assert(i < 32);
    allocations[i].memory = memory; allocations[i].bytes = bytes; allocations[i].cls = cls;
    live[cls] += bytes;
    if (live[cls] > peak[cls]) peak[cls] = live[cls];
    assert(live[1] <= 512 * 1024 && live[0] <= 64 * 1024);
    ++load_count;
    pthread_mutex_unlock(&allocation_mutex);
    return memory;
}
static void deallocate(void *context, void *memory) {
    (void)context;
    assert(!pthread_equal(pthread_self(), guest_thread));
    pthread_mutex_lock(&allocation_mutex);
    unsigned i;
    for (i = 0; i < 32 && allocations[i].memory != memory; ++i) {}
    assert(i < 32);
    live[allocations[i].cls] -= allocations[i].bytes;
    allocations[i].memory = NULL;
    pthread_mutex_unlock(&allocation_mutex);
    free(memory);
}
static void notify(void *context) { (void)context; __atomic_add_fetch(&notifications, 1, __ATOMIC_RELAXED); }
static void delay(void) { const struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static pxa_bytes_t bytes(const char *text) { return (pxa_bytes_t){(const uint8_t *)text, strlen(text)}; }
static pxa_raster_asset_t *await_asset(pxa_posix_asset_worker_t *worker, pxa_asset_ticket_t ticket) {
    pxa_raster_asset_t *asset = NULL;
    for (unsigned i = 0; i < 10000; ++i) {
        pxa_status_t status = pxa_posix_asset_worker_acquire(worker, 1, ticket, &asset);
        if (status == PXA_STATUS_OK) return asset;
        assert(status == PXA_STATUS_WOULD_BLOCK);
        delay();
    }
    assert(!"worker timed out"); return NULL;
}
static void draw_scene(pxa_raster_bindings_t *bindings, unsigned first_texture) {
    uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + 4 * PXA_RASTER_SPRITE_BYTES] = {0};
    uint16_t pixels[16];
    pxa_raster_resources_t view;
    pxa_raster_target_t target = {0};
    pxa_raster_draw_list_view_t list;
    pxa_raster_bindings_view(bindings, PXA_RASTER_CAP_KNOWN_MASK, &view);
    pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC); pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR); pxa_write_u32(draw + 8, sizeof(draw));
    pxa_write_u32(draw + 16, 4); pxa_write_u64(draw + 20, 1);
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t *r = draw + PXA_RASTER_DRAW_HEADER_BYTES + i * PXA_RASTER_SPRITE_BYTES;
        r[0] = PXA_RASTER_RECORD_SPRITE; r[4] = (uint8_t)i;
        pxa_write_u16(r + 2, PXA_RASTER_SPRITE_BYTES);
        pxa_write_u16(r + 8, (uint16_t)((i % 2) * 2));
        pxa_write_u16(r + 10, (uint16_t)((i / 2) * 2));
        pxa_write_u16(r + 12, 2); pxa_write_u16(r + 14, 2);
        pxa_write_u16(r + 20, 256); pxa_write_u16(r + 22, 256);
    }
    target.pixels = pixels; target.width = target.height = target.stride_pixels = 4;
    target.scratch_mode = PXA_RASTER_SCRATCH_NONE;
    assert(pxa_raster_validate_draw_list(draw, sizeof(draw), &target, &view, &list) == 0);
    pxa_raster_execute_draw_list(draw, &list, &target, &view, NULL);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            assert(pixels[y * 4 + x] == (first_texture + y / 2 * 2 + x / 2 + 1) * 251);
}

int main(int argc, char **argv) {
    pxa_package_manifest_t manifest = {0};
    pxa_package_file_t files[26];
    char paths[26][64]; uint8_t digests[26][32];
    pxa_posix_asset_worker_config_t config = {0};
    pxa_posix_asset_worker_t *worker;
    pxa_asset_ticket_t a, b, palette_ticket, tickets[4];
    pxa_raster_asset_t *palette, *asset;
    pxa_asset_cache_stats_t stats;
    size_t metadata;
    assert(argc == 2);
    guest_thread = pthread_self();
    /* The fixture manifest is preauthenticated by this test. The separate
     * package test covers actual ECDSA signature and inventory verification. */
    for (unsigned i = 0; i < 26; ++i) {
        char full[1024]; FILE *file; long size;
        if (i == 0) strcpy(paths[i], "assets/p.pxr");
        else if (i == 1) strcpy(paths[i], "assets/resources.pxi");
        else if (i == 22) strcpy(paths[i], "assets/zmap.bin");
        else if (i == 23) strcpy(paths[i], "assets/zsound.pcm");
        else if (i == 24) strcpy(paths[i], "assets/zzui-bgra.pxr");
        else if (i == 25) strcpy(paths[i], "assets/zzui-rgb.pxr");
        else snprintf(paths[i], sizeof(paths[i]), "assets/t%02u.pxr", i - 2);
        snprintf(full, sizeof(full), "%s/%s", argv[1], paths[i]);
        file = fopen(full, "rb"); assert(file);
        assert(!fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET));
        uint8_t *data = malloc((size_t)size); assert(data);
        assert(fread(data, 1, (size_t)size, file) == (size_t)size); fclose(file);
        assert(pxa_openssl_sha256(data, (size_t)size, digests[i]) == 0); free(data);
        files[i] = (pxa_package_file_t){bytes(paths[i]), (uint64_t)size, digests[i]};
    }
    manifest.files = files; manifest.file_count = 26; manifest.encoded = bytes("authenticated fixture v1");
    config.package_root = argv[1]; config.manifest = &manifest;
    config.cache = (pxa_asset_cache_config_t){16, 24, 24, 8, {65536, 512 * 1024}, {65536, 512 * 1024}};
    config.max_catalog_bytes = 8192;
    config.allocate = allocate; config.release = deallocate; config.notify = notify;
    config.read_delay_us = 200; config.before_read = before_read;
    pxa_memory_budget_config_t bc = {.limit={0,32768},.temporary_limit={0,32768},
        .lock_context=&budget_mutex,.lock=budget_lock,.unlock=budget_unlock};
    pxa_memory_owner_t owner; const size_t limit[2]={0,32768};
    assert(!pxa_memory_budget_init(&read_budget,&bc));
    assert(!pxa_memory_owner_open(&read_budget,limit,&owner));
    pxa_memory_allocator_t temporary={&read_budget,owner,1,PXA_MEMORY_TEMPORARY,NULL,raw_allocate,raw_release};
    config.temporary_allocator=&temporary;
    assert(pxa_posix_asset_worker_create(&config, &worker) == 0);
    assert(!pxa_posix_asset_worker_stack_bytes(worker));
    pxa_assets_backend_t blob_backend={.context=worker,.read=backend_read,.read_result=backend_result,.read_release=backend_release};
    check_worker_blob_reads(&blob_backend,argv[1],block_storage,await_blocked_storage,release_storage,delay);
    assert(pxa_posix_asset_worker_stack_bytes(worker)==128*1024);
    /* Fill temporary quota independently of the raster cache, then recover. */
    pxa_memory_stats_t ms;
    for (unsigned i=0;;++i) {
        assert(!pxa_memory_budget_stats(&read_budget,owner,&ms));
        if (!ms.charged[1]) break;
        assert(i<15000); delay();
    }
    void *pressure=pxa_memory_allocate(&temporary,32768-(pxa_memory_allocation_bytes(1)-1)); assert(pressure);
    uint64_t read_ticket=begin_blob(&blob_backend,0,128,delay); pxa_bytes_t blob;
    assert(await_blob(&blob_backend,read_ticket,&blob,delay)==PXA_STATUS_RESOURCE_LIMIT);
    backend_release(worker,1,read_ticket); pxa_memory_release(pressure);
    read_ticket=begin_blob(&blob_backend,0,128,delay);
    check_blob_result(&blob_backend,read_ticket,0,128,delay);
    pxa_asset_ticket_t sound_ticket;
    assert(!pxa_posix_asset_worker_request(worker,1,bytes(paths[23]),1,&sound_ticket));
    pxa_asset_object_t *sound=await_asset(worker,sound_ticket);
    pxa_asset_request_state_t sound_state;
    assert(!pxa_posix_asset_worker_inspect(worker,1,bytes(paths[23]),&sound_state));
    assert(sound_state.state==PXA_ASSET_REQUEST_READY && !sound_state.status);
    pxa_asset_object_view_t sound_view; pxa_asset_object_view(sound,&sound_view);
    assert(sound_view.kind==PXA_ASSET_AUDIO && sound_view.bytes==160);
    for(unsigned i=0;i<160;++i) assert(sound_view.data[i]==255);
    assert(!pxa_posix_asset_worker_release(worker,1,sound_ticket));
    pxa_asset_object_release_pinned(sound);
    pxa_asset_request_state_t snapshot;
    assert(!pxa_posix_asset_worker_inspect(worker,1,bytes(paths[2]),&snapshot) && snapshot.state == PXA_ASSET_REQUEST_ABSENT);
    assert(pxa_posix_asset_worker_prefetch(worker, 1, bytes(paths[2]), 1, &a) == 0);
    assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[2]), 1, &b) == 0);
    assert(pxa_posix_asset_worker_cancel(worker, 1, a) == 0);
    assert(pxa_posix_asset_worker_release(worker, 1, a) == 0);
    asset = await_asset(worker, b); pxa_raster_asset_release(asset);
    assert(!pxa_posix_asset_worker_inspect(worker,1,bytes(paths[2]),&snapshot) && snapshot.state == PXA_ASSET_REQUEST_READY);
    assert(!pxa_posix_asset_worker_inspect(worker,2,bytes(paths[2]),&snapshot) && snapshot.state == PXA_ASSET_REQUEST_ABSENT);
    assert(pxa_posix_asset_worker_release(worker, 1, b) == 0);
    /* Cancel the only subscriber during actual file I/O, then retry the same
     * path. The late completion must not publish into the new request. */
    block_storage();
    assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[3]), 1, &a) == 0);
    await_blocked_storage();
    assert(pxa_posix_asset_worker_release(worker, 1, a) == 0);
    assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[3]), 1, &b) == 0 && a != b);
    release_storage();
    asset = await_asset(worker, b);
    pxa_raster_asset_release(asset);
    assert(pxa_posix_asset_worker_release(worker, 1, b) == 0);
    assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[0]), 0, &palette_ticket) == 0);
    palette = await_asset(worker, palette_ticket);
    assert(pxa_posix_asset_worker_release(worker, 1, palette_ticket) == 0);
    for (unsigned scene = 0; scene < 100; ++scene) {
        pxa_raster_bindings_t frame = {0};
        unsigned first = (scene % 5) * 4;
        for (unsigned i = 0; i < 4; ++i)
            assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[first + i + 2]), 1, &tickets[i]) == 0);
        assert(pxa_raster_bindings_replace(&frame, 0, palette) == NULL);
        for (unsigned i = 0; i < 4; ++i) {
            asset = await_asset(worker, tickets[i]);
            assert(pxa_raster_bindings_replace(&frame, (uint8_t)i, asset) == NULL);
            pxa_raster_asset_release(asset);
            assert(pxa_posix_asset_worker_release(worker, 1, tickets[i]) == 0);
        }
        draw_scene(&frame, first);
        pxa_raster_bindings_release(&frame);
        pxa_posix_asset_worker_stats(worker, &stats, &metadata);
        assert(stats.charged[1] <= 512 * 1024 && stats.queued == 0);
    }
    /* UI images use this same worker and a single immutable pixel allocation. */
    for(unsigned image=0;image<2;++image) {
        block_storage();
        assert(!pxa_posix_asset_worker_request(worker,1,bytes(paths[24+image]),1,&a));
        await_blocked_storage();
        assert(!pxa_posix_asset_worker_request(worker,1,bytes(paths[24+image]),1,&b));
        pxa_raster_asset_t *not_ready=NULL;
        assert(pxa_posix_asset_worker_acquire(worker,1,a,&not_ready)==PXA_STATUS_WOULD_BLOCK && !not_ready);
        release_storage();
        asset=await_asset(worker,a); pxa_asset_object_t *same=await_asset(worker,b);
        assert(asset==same);
        pxa_asset_object_view_t view; pxa_asset_object_view(asset,&view);
        assert(view.kind==PXA_ASSET_IMAGE && view.width==320 && view.height==16);
        assert(view.encoding==(image ? PXA_ASSET_ENCODING_RGB565 : PXA_ASSET_ENCODING_BGRA8888));
        assert(view.bytes==320*16*(image ? 2u : 4u));
        if(image) for(size_t i=0;i<320*16;++i) assert(((const uint16_t *)view.data)[i]==0xfc00);
        else for(size_t i=0;i<view.bytes;i+=4) assert(!memcmp(view.data+i,"\x59\x2b\x11\x7b",4));
        assert(!pxa_posix_asset_worker_release(worker,1,a) && !pxa_posix_asset_worker_release(worker,1,b));
        pxa_asset_object_release(same); pxa_asset_object_release(asset);
    }
    puts("asset worker: cold UI images, coalesced loads, native RGB565 and BGRA alpha pixels passed");
    /* Stop with both real I/O and the palette's external reference alive. */
    block_storage();
    assert(pxa_posix_asset_worker_request(worker, 1, bytes(paths[2]), 1, &a) == 0);
    await_blocked_storage();
    /* Destroy must not free a live frame reference or join behind that frame. */
    assert(pxa_posix_asset_worker_destroy(worker) == PXA_STATUS_WOULD_BLOCK);
    release_storage();
    pxa_raster_asset_release(palette);
    for (unsigned i = 0;; ++i) {
        pxa_status_t status = pxa_posix_asset_worker_destroy(worker);
        if (status == 0) break;
        assert(status == PXA_STATUS_WOULD_BLOCK && i < 10000);
        delay();
    }
    assert(live[0] == 0 && live[1] == 0 && notifications > 0);
    printf("asset worker: 20 files,100 scenes,resident peak=%zu B,palette peak=%zu B,metadata=%zu B,loads=%u\n",
           peak[1], peak[0], metadata, load_count);
    assert(!pxa_memory_budget_stats(&read_budget,owner,&ms) && !ms.charged[1]);
    assert(!pxa_memory_owner_close(&read_budget,owner));
    pthread_mutex_destroy(&budget_mutex);
    pthread_mutex_destroy(&allocation_mutex);
    pthread_mutex_destroy(&io_mutex); pthread_cond_destroy(&io_condition);
    return 0;
}
