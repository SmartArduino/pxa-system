#undef NDEBUG
#include "pxa/asset_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pxa_asset_cache_t *cache;
static size_t live_bytes, peak_bytes;
static unsigned loads, frees;
static const uint8_t package_key[32] = {0x83};
static void *allocate(void *context, size_t bytes) {
    pxa_asset_cache_stats_t stats;
    (void)context;
    size_t *p = malloc(sizeof(size_t) + bytes);
    assert(p);
    *p = bytes;
    live_bytes += bytes;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    pxa_asset_cache_stats(cache, &stats);
    assert(live_bytes <= stats.charged[0] + stats.charged[1]);
    ++loads;
    return p + 1;
}
static void deallocate(void *context, void *memory) {
    pxa_asset_cache_stats_t stats;
    (void)context;
    size_t *p = (size_t *)memory - 1;
    pxa_asset_cache_stats(cache, &stats);
    /* Credit has NOT yet been returned while the old allocation still exists. */
    assert(live_bytes <= stats.charged[0] + stats.charged[1]);
    live_bytes -= *p;
    ++frees;
    free(p);
}
/* Model immutable installed metadata: lookup structs are temporary, their
 * backing records remain valid until every cache job and reference is gone. */
static struct { char path[64]; uint8_t digest[32]; pxa_package_file_t file; } catalog[32];
static void init_catalog(void) {
    for (unsigned i=0;i<32;++i) {
        snprintf(catalog[i].path,sizeof(catalog[i].path),"assets/t%02u.pxr",i);
        catalog[i].digest[0]=(uint8_t)i;
        catalog[i].file=(pxa_package_file_t){
            {(const uint8_t *)catalog[i].path,strlen(catalog[i].path)},32+65536,catalog[i].digest};
    }
}
static pxa_status_t access_asset(unsigned texture, uint64_t owner, uint8_t cls,
                            pxa_asset_ticket_t *ticket, int prefetch, pxa_asset_request_state_t *state) {
    assert(texture<32);
    pxa_asset_info_t info = {0};
    info.path = catalog[texture].file.path;
    info.file = &catalog[texture].file;
    info.kind = PXA_ASSET_TEXTURE; info.encoding = PXA_ASSET_ENCODING_INDEX8;
    info.width = info.height = 256;
    info.stored_bytes = 32 + 65536; info.decoded_bytes = 65536;
    info.payload_offset = 32; info.format_version = 1;
    if (state) return pxa_asset_cache_inspect(cache, owner, package_key, &info, cls, state);
    return (prefetch ? pxa_asset_cache_prefetch : pxa_asset_cache_request)(cache, owner, package_key, &info, cls, ticket);
}
static pxa_status_t request(unsigned texture, uint64_t owner, uint8_t cls, pxa_asset_ticket_t *ticket) {
    return access_asset(texture, owner, cls, ticket, 0, NULL);
}
static void inspect_state(unsigned texture, uint64_t owner, uint8_t expected) {
    pxa_asset_request_state_t state;
    assert(!access_asset(texture, owner, 1, NULL, 0, &state));
    assert(state.state == expected);
}
static void release_job(pxa_asset_job_t *job) {
    assert(job->kind == PXA_ASSET_JOB_RELEASE);
    pxa_raster_asset_release(job->asset);
    assert(pxa_asset_cache_finish_release(cache, job->token) == 0);
}
static pxa_raster_asset_t *load_job(pxa_asset_job_t *job) {
    pxa_raster_asset_t *asset;
    uint8_t *payload;
    assert(job->kind == PXA_ASSET_JOB_LOAD);
    assert(pxa_raster_asset_create(&job->info, allocate, deallocate, NULL, &asset, &payload) == 0);
    memset(payload, job->info.file->sha256[0], job->info.decoded_bytes);
    pxa_raster_asset_finish_loading(asset);
    return asset;
}
static void pump(void) {
    pxa_asset_job_t job, discard;
    unsigned steps = 0;
    pxa_status_t status;
    while ((status = pxa_asset_cache_next_job(cache, &job)) == 0) {
        assert(++steps < 1000);
        if (job.kind == PXA_ASSET_JOB_LOAD) {
            pxa_raster_asset_t *asset = load_job(&job);
            assert(pxa_asset_cache_finish_load(cache, job.token, 0, asset, &discard) == 0);
            if (discard.kind) release_job(&discard);
        } else if (job.kind == PXA_ASSET_JOB_RELEASE) release_job(&job);
        else assert(job.kind == PXA_ASSET_JOB_NOTIFY);
    }
    assert(status == PXA_STATUS_NOT_FOUND);
}
static void close_ticket(pxa_asset_ticket_t ticket, uint64_t owner) {
    assert(pxa_asset_cache_release(cache, owner, ticket) == 0);
}

static void test_limits_and_pressure(void) {
    pxa_asset_cache_config_t cfg = {2, 4, 2, 1, {0, 70000}, {0, 70000}};
    size_t bytes = pxa_asset_cache_workspace_size(&cfg);
    void *workspace = malloc(bytes);
    pxa_asset_ticket_t a, b, c;
    pxa_raster_asset_t *held;
    pxa_asset_request_state_t state;
    pxa_asset_job_t job, discard;
    assert(workspace && pxa_asset_cache_init(workspace, bytes, &cfg, &cache) == 0);
    assert(request(0, 1, 1, &a) == 0);
    assert(request(1, 1, 1, &b) == PXA_STATUS_WOULD_BLOCK); /* queue ceiling */
    assert(request(0, 1, 1, &b) == 0); /* coalescing consumes no queue slot */
    assert(request(0, 1, 1, &c) == PXA_STATUS_QUOTA_EXCEEDED); /* request quota */
    pxa_asset_cache_cancel_owner(cache, 1);
    assert(pxa_asset_cache_query(cache, 1, b, &state) == PXA_STATUS_NOT_FOUND);
    pump();
    assert(request(0, 1, 1, &a) == 0);
    assert(pxa_asset_cache_next_job(cache, &job) == 0 && job.kind == PXA_ASSET_JOB_LOAD);
    held = load_job(&job);
    pxa_raster_asset_release(held); /* loader frees a partial allocation first */
    assert(pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_IO_ERROR, NULL, &discard) == 0);
    assert(pxa_asset_cache_query(cache, 1, a, &state) == 0 && state.status == PXA_STATUS_IO_ERROR);
    close_ticket(a, 1);
    assert(request(0, 1, 1, &a) == 0); pump();
    assert(pxa_asset_cache_acquire(cache, 1, a, &held) == 0);
    assert(request(1, 2, 1, &b) == 0); pump();
    assert(pxa_asset_cache_query(cache, 2, b, &state) == 0 && state.status == PXA_STATUS_RESOURCE_LIMIT);
    close_ticket(a, 1); close_ticket(b, 2);
    assert(request(1, 2, 1, &b) == 0); pump();
    assert(pxa_asset_cache_query(cache, 2, b, &state) == 0 && state.status == PXA_STATUS_RESOURCE_LIMIT);
    close_ticket(b, 2); /* frame-held data cannot be evicted */
    pxa_raster_asset_release(held);
    assert(request(1, 2, 1, &b) == 0);
    assert(pxa_asset_cache_next_job(cache, &job) == 0 && job.kind == PXA_ASSET_JOB_RELEASE);
    assert(pxa_asset_cache_next_job(cache, &discard) == PXA_STATUS_BUSY);
    release_job(&job); pump();
    assert(pxa_asset_cache_query(cache, 2, b, &state) == 0 && state.state == PXA_ASSET_REQUEST_READY);
    pxa_asset_cache_shutdown(cache); pump();
    assert(pxa_asset_cache_drained(cache) && live_bytes == 0);

    /* Metadata pressure triggers asynchronous eviction, never a free in request(). */
    cfg.max_entries = 1;
    assert(pxa_asset_cache_init(workspace, bytes, &cfg, &cache) == 0);
    assert(request(0, 1, 1, &a) == 0); pump();
    assert(pxa_asset_cache_acquire(cache, 1, a, &held) == 0);
    close_ticket(a, 1);
    assert(request(1, 1, 1, &b) == PXA_STATUS_WOULD_BLOCK); pump();
    assert(live_bytes != 0);
    pxa_raster_asset_release(held); pump();
    assert(live_bytes == 0);
    assert(request(1, 1, 1, &b) == 0); pump();
    assert(pxa_asset_cache_query(cache, 1, a, &state) == PXA_STATUS_NOT_FOUND);
    pxa_asset_cache_shutdown(cache); pump();
    assert(pxa_asset_cache_drained(cache) && live_bytes == 0);
    free(workspace);
}

static void test_shared_pressure(void) {
    pxa_asset_cache_config_t cfg = {8, 8, 8, 8, {70000, 400000}, {70000, 400000}};
    size_t bytes = pxa_asset_cache_workspace_size(&cfg);
    void *workspace = malloc(bytes);
    pxa_asset_ticket_t tickets[4]; pxa_raster_asset_t *held;
    pxa_asset_job_t job;
    assert(!pxa_asset_cache_init(workspace, bytes, &cfg, &cache));
    for (unsigned i = 0; i < 4; ++i) assert(!request(i, 1, i == 3 ? 0 : 1, &tickets[i]));
    pump();
    assert(!pxa_asset_cache_acquire(cache, 1, tickets[0], &held));
    close_ticket(tickets[0], 1); /* frame-held, must survive */
    close_ticket(tickets[2], 1); /* only external eviction candidate */
    close_ticket(tickets[3], 1); /* different memory class, must survive */
    const size_t object = pxa_raster_asset_allocation_bytes(held);
    const size_t before = live_bytes;
    assert(pxa_asset_cache_trim(cache, 1, object) == object);
    assert(pxa_asset_cache_trim(cache, 1, object) == object); /* coalesced, no extra victims */
    assert(live_bytes == before); /* no synchronous free */
    assert(!pxa_asset_cache_next_job(cache, &job) && job.kind == PXA_ASSET_JOB_RELEASE);
    assert(job.info.file->sha256[0] == 2);
    assert(pxa_asset_cache_trim(cache, 1, object) == object); /* actual free still pending */
    release_job(&job);
    assert(pxa_asset_cache_trim(cache, 1, SIZE_MAX) == 0); /* held frame + live ticket */
    assert(live_bytes == before - object);
    pxa_raster_asset_release(held);
    assert(pxa_asset_cache_trim(cache, 1, 1) == object); pump();
    close_ticket(tickets[1], 1);
    assert(pxa_asset_cache_trim(cache, 1, SIZE_MAX) == object); pump();
    assert(live_bytes == object); /* internal allocation unaffected */
    assert(pxa_asset_cache_trim(cache, 0, 1) == object); pump();
    assert(!live_bytes && pxa_asset_cache_drained(cache));
    free(workspace);
}

static void test_bounded_pressure_retry(void) {
    pxa_asset_cache_config_t cfg = {4, 4, 4, 4, {0, 400000}, {0, 400000}};
    size_t bytes = pxa_asset_cache_workspace_size(&cfg);
    void *workspace = malloc(bytes);
    pxa_asset_ticket_t a, b, c; pxa_asset_job_t job, discard;
    pxa_asset_request_state_t state; pxa_asset_cache_stats_t stats;
    assert(!pxa_asset_cache_init(workspace, bytes, &cfg, &cache));
    assert(!request(0, 1, 1, &a) && !request(1, 1, 1, &b)); pump();
    close_ticket(a, 1); close_ticket(b, 1);
    assert(!request(2, 1, 1, &c));
    assert(!pxa_asset_cache_next_job(cache, &job) && job.kind == PXA_ASSET_JOB_LOAD);
    assert(pxa_asset_cache_trim(cache, 1, 1) > 0);
    assert(!pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_RESOURCE_LIMIT, NULL, &discard));
    assert(!pxa_asset_cache_query(cache, 1, c, &state) && state.state == PXA_ASSET_REQUEST_QUEUED);
    assert(!pxa_asset_cache_next_job(cache, &job) && job.kind == PXA_ASSET_JOB_RELEASE);
    release_job(&job);
    assert(!pxa_asset_cache_next_job(cache, &job) && job.kind == PXA_ASSET_JOB_LOAD);
    assert(pxa_asset_cache_trim(cache, 1, 1) > 0);
    assert(!pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_RESOURCE_LIMIT, NULL, &discard));
    assert(!pxa_asset_cache_query(cache, 1, c, &state) && state.state == PXA_ASSET_REQUEST_FAILED);
    assert(state.status == PXA_STATUS_RESOURCE_LIMIT);
    pxa_asset_cache_stats(cache, &stats);
    assert(stats.pressure_retries == 1 && stats.load_failures == 1);
    close_ticket(c, 1); pump();
    assert(pxa_asset_cache_drained(cache) && !live_bytes);
    /* Requeue cannot exceed max_pending if callers filled it during the load. */
    cfg.max_pending = 1;
    assert(!pxa_asset_cache_init(workspace, bytes, &cfg, &cache));
    assert(!request(0, 1, 1, &a)); pump(); close_ticket(a, 1);
    assert(!request(1, 1, 1, &b));
    assert(!pxa_asset_cache_next_job(cache, &job) && job.kind == PXA_ASSET_JOB_LOAD);
    assert(!request(2, 1, 1, &c));
    assert(pxa_asset_cache_trim(cache, 1, 1) > 0);
    assert(!pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_RESOURCE_LIMIT, NULL, &discard));
    pxa_asset_cache_stats(cache, &stats);
    assert(stats.queued == 1 && !stats.pressure_retries);
    assert(!pxa_asset_cache_query(cache, 1, b, &state) && state.state == PXA_ASSET_REQUEST_FAILED);
    pxa_asset_cache_shutdown(cache); pump();
    assert(pxa_asset_cache_drained(cache) && !live_bytes);
    free(workspace);
}

static void test_prefetch(void) {
    pxa_asset_cache_config_t cfg = {8, 8, 8, 8, {0, 512*1024}, {0, 512*1024}};
    size_t bytes = pxa_asset_cache_workspace_size(&cfg); void *workspace = malloc(bytes);
    assert(!pxa_asset_cache_init(workspace, bytes, &cfg, &cache));
    pxa_asset_ticket_t pre, front, promotion, second;
    pxa_asset_job_t job, discard;
    inspect_state(1, 1, PXA_ASSET_REQUEST_ABSENT);
    assert(!access_asset(1, 1, 1, &pre, 1, NULL));
    inspect_state(1, 1, PXA_ASSET_REQUEST_QUEUED);
    inspect_state(1, 2, PXA_ASSET_REQUEST_ABSENT);
    assert(!request(2, 1, 1, &front));
    assert(!pxa_asset_cache_next_job(cache, &job) && job.info.file->sha256[0] == 2);
    assert(!pxa_asset_cache_finish_load(cache, job.token, 0, load_job(&job), &discard));
    close_ticket(front, 1);
    assert(!request(1, 1, 1, &promotion)); // shares prefetch, now foreground
    assert(!request(3, 1, 1, &front));
    assert(!pxa_asset_cache_next_job(cache, &job) && job.info.file->sha256[0] == 1);
    inspect_state(1, 1, PXA_ASSET_REQUEST_LOADING);
    assert(!pxa_asset_cache_finish_load(cache, job.token, 0, load_job(&job), &discard));
    close_ticket(promotion, 1); close_ticket(pre, 1);
    inspect_state(1, 1, PXA_ASSET_REQUEST_READY);
    assert(!pxa_asset_cache_next_job(cache, &job) && job.info.file->sha256[0] == 3);
    assert(!pxa_asset_cache_finish_load(cache, job.token, 0, load_job(&job), &discard));
    close_ticket(front, 1);
    assert(!access_asset(4, 1, 1, &pre, 1, NULL));
    assert(!request(4, 1, 1, &promotion));
    assert(!request(5, 1, 1, &front));
    close_ticket(promotion, 1); // cancellation demotes back to prefetch
    assert(!pxa_asset_cache_next_job(cache, &job) && job.info.file->sha256[0] == 5);
    assert(!pxa_asset_cache_finish_load(cache, job.token, 0, load_job(&job), &discard));
    close_ticket(front, 1); close_ticket(pre, 1);
    inspect_state(4, 1, PXA_ASSET_REQUEST_ABSENT);
    // Inspect and completed prefetch pins do not prevent reclamation.
    assert(pxa_asset_cache_trim(cache, 1, SIZE_MAX)); pump();
    inspect_state(1, 1, PXA_ASSET_REQUEST_ABSENT);
    assert(!access_asset(6, 1, 1, &pre, 1, NULL));
    assert(!pxa_asset_cache_next_job(cache, &job));
    assert(!pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_IO_ERROR, NULL, &discard));
    inspect_state(6, 1, PXA_ASSET_REQUEST_FAILED);
    pxa_asset_cache_stats_t stats; pxa_asset_cache_stats(cache, &stats);
    assert(stats.prefetch_requests == 3 && stats.load_failures == 1 && stats.prefetch_failures == 1);
    assert(!request(6, 1, 1, &second));
    inspect_state(6, 1, PXA_ASSET_REQUEST_QUEUED); // old failure cannot mask a retry
    close_ticket(pre, 1); close_ticket(second, 1);
    pxa_asset_cache_shutdown(cache); pump();
    assert(pxa_asset_cache_drained(cache) && !live_bytes); free(workspace);
}

/* One resource can need many entries: terminal failures remain observable
 * until their tickets close. Keep both per-owner and global request capacity. */
static void test_catalog_lifetime_and_capacity(void) {
    pxa_asset_cache_config_t cfg={64,48,32,16,{70000,512*1024},{70000,512*1024}};
    size_t bytes=pxa_asset_cache_workspace_size(&cfg);
    void *workspace=malloc(bytes);
    struct metadata { uint8_t path[255],digest[32];pxa_package_file_t file; };
    struct metadata *installed=calloc(1,sizeof(*installed));
    assert(workspace && installed);
    memset(installed->path,'a',sizeof(installed->path));
    memcpy(installed->path,"assets/",7);
    memcpy(installed->path+sizeof(installed->path)-4,".pxr",4);
    installed->digest[0]=23;
    installed->file=(pxa_package_file_t){{installed->path,sizeof(installed->path)},32+65536,installed->digest};
    assert(!pxa_asset_cache_init(workspace,bytes,&cfg,&cache));
    pxa_asset_ticket_t tickets[48],extra;
    pxa_asset_job_t job,discard;
    pxa_asset_request_state_t state;
    pxa_asset_info_t info={.path=installed->file.path,.file=&installed->file,
        .kind=PXA_ASSET_TEXTURE,.encoding=PXA_ASSET_ENCODING_INDEX8,.width=256,.height=256,
        .stored_bytes=65568,.decoded_bytes=65536,.payload_offset=32,.format_version=1};
    for(unsigned i=0;i<48;++i) {
        uint64_t owner=i<32 ? 1 : 2;
        pxa_asset_info_t lookup=info;
        assert(!pxa_asset_cache_request(cache,owner,package_key,&lookup,1,&tickets[i]));
        memset(&lookup,0xa5,sizeof(lookup)); /* The lookup result itself is not retained. */
        assert(!pxa_asset_cache_next_job(cache,&job) && job.kind==PXA_ASSET_JOB_LOAD);
        assert(job.info.path.size==255 && !memcmp(job.info.path.data,installed->path,255));
        assert(!pxa_asset_cache_finish_load(cache,job.token,PXA_STATUS_IO_ERROR,NULL,&discard));
        assert(!discard.kind);
    }
    assert(pxa_asset_cache_request(cache,1,package_key,&info,1,&extra)==PXA_STATUS_QUOTA_EXCEEDED);
    assert(pxa_asset_cache_request(cache,3,package_key,&info,1,&extra)==PXA_STATUS_WOULD_BLOCK);
    assert(!pxa_asset_cache_release(cache,1,tickets[0]));
    assert(pxa_asset_cache_query(cache,1,tickets[0],&state)==PXA_STATUS_NOT_FOUND);
    assert(!pxa_asset_cache_request(cache,1,package_key,&info,1,&extra) && extra!=tickets[0]);
    assert(!pxa_asset_cache_next_job(cache,&job));
    /* Shutdown cannot release installed metadata while its worker owns a job. */
    pxa_asset_cache_shutdown(cache);assert(!pxa_asset_cache_drained(cache));
    assert(pxa_asset_cache_job_cancelled(cache,job.token));
    assert(!memcmp(job.info.path.data,installed->path,255));
    assert(!pxa_asset_cache_finish_load(cache,job.token,PXA_STATUS_CANCELLED,NULL,&discard));
    pump();assert(pxa_asset_cache_drained(cache) && !live_bytes);
    free(workspace);free(installed);
    printf("Cache metadata: entries=64 requests=48 per_owner=32 workspace=%zu; retained failures, lookup lifetime and shutdown passed\n",bytes);
}

int main(void) {
    init_catalog();
    pxa_asset_cache_config_t config = {16, 24, 16, 8,
                                      {70000, 512 * 1024}, {70000, 300000}};
    size_t workspace_bytes = pxa_asset_cache_workspace_size(&config);
    void *workspace = malloc(workspace_bytes);
    pxa_asset_ticket_t a, b, tickets[4];
    pxa_asset_job_t job, discard;
    pxa_asset_request_state_t state;
    pxa_raster_asset_t *asset;
    pxa_asset_cache_stats_t stats;
    assert(workspace && pxa_asset_cache_init(workspace, workspace_bytes, &config, &cache) == 0);
    /* Queue dedup, independent cancellation, owner isolation and stale handles. */
    assert(request(0, 1, 1, &a) == 0 && request(0, 1, 1, &b) == 0 && a != b);
    assert(pxa_asset_cache_query(cache, 2, a, &state) == PXA_STATUS_NOT_FOUND);
    assert(pxa_asset_cache_cancel(cache, 1, a) == 0);
    assert(pxa_asset_cache_query(cache, 1, a, &state) == 0 && state.state == PXA_ASSET_REQUEST_CANCELLED);
    pump();
    assert(loads == 1 && pxa_asset_cache_acquire(cache, 1, b, &asset) == 0);
    close_ticket(a, 1); close_ticket(b, 1);
    assert(pxa_asset_cache_query(cache, 1, a, &state) == PXA_STATUS_NOT_FOUND);
    assert(request(0, 1, 1, &a) == 0);
    pump(); assert(loads == 1); /* Hot cache hit, no new allocation/job. */
    close_ticket(a, 1); pxa_raster_asset_release(asset);

    /* Mid-load cancellation: slot and credit remain owned until actual free. */
    assert(request(1, 1, 1, &a) == 0);
    assert(pxa_asset_cache_next_job(cache, &job) == 0 && job.kind == PXA_ASSET_JOB_LOAD);
    assert(pxa_asset_cache_next_job(cache, &discard) == PXA_STATUS_BUSY);
    asset = load_job(&job);
    close_ticket(a, 1);
    assert(pxa_asset_cache_job_cancelled(cache, job.token));
    assert(request(1, 1, 1, &b) == 0); /* Must not join a cancelled load. */
    assert(pxa_asset_cache_finish_load(cache, job.token, 0, asset, &discard) == 0);
    assert(discard.kind == PXA_ASSET_JOB_RELEASE);
    pxa_asset_cache_stats(cache, &stats);
    assert(stats.loading[1] == 0 && stats.worker_busy);
    release_job(&discard);
    assert(pxa_asset_cache_finish_load(cache, job.token, PXA_STATUS_IO_ERROR, NULL, &discard) == PXA_STATUS_NOT_FOUND);
    pump(); close_ticket(b, 1);

    /* Global vs app quota: four app-owned textures fit; a fifth must fail. */
    for (unsigned i = 0; i < 4; ++i) assert(request(i, 7, 1, &tickets[i]) == 0);
    pump();
    assert(request(5, 7, 1, &a) == 0); pump();
    assert(pxa_asset_cache_query(cache, 7, a, &state) == 0 &&
           state.state == PXA_ASSET_REQUEST_FAILED && state.status == PXA_STATUS_QUOTA_EXCEEDED);
    close_ticket(a, 7);
    pxa_asset_cache_cancel_owner(cache, 7); pump();
    for (unsigned i = 0; i < 4; ++i)
        assert(pxa_asset_cache_query(cache, 7, tickets[i], &state) == PXA_STATUS_NOT_FOUND);

    /* Internal memory has a separate hard limit. */
    assert(request(2, 2, 0, &a) == 0 && request(3, 2, 0, &b) == 0); pump();
    assert(pxa_asset_cache_query(cache, 2, b, &state) == 0 && state.status == PXA_STATUS_QUOTA_EXCEEDED);
    pxa_asset_cache_cancel_owner(cache, 2); pump();

    /* A frame remains valid after ticket cancellation and app exit. */
    assert(request(9, 9, 1, &a) == 0); pump();
    assert(pxa_asset_cache_acquire(cache, 9, a, &asset) == 0);
    pxa_asset_cache_cancel_owner(cache, 9); pump();
    pxa_raster_bindings_t frame = {0};
    pxa_raster_resources_t view;
    assert(pxa_raster_bindings_replace(&frame, 0, asset) == NULL);
    pxa_raster_asset_release(asset);
    pxa_raster_bindings_view(&frame, 0, &view);
    assert(view.textures[0].pixels[65535] == 9);
    pxa_raster_bindings_release(&frame); pump();

    /* 20 unique textures, 4 current scene pins, 100 scene transitions. Release
     * old scene first: 8 x (65536 + object header) does not fit in 512 KiB. */
    for (unsigned scene = 0; scene < 100; ++scene) {
        for (unsigned i = 0; i < 4; ++i)
            assert(request((scene % 5) * 4 + i, 1, 1, &tickets[i]) == 0);
        pump();
        memset(&frame, 0, sizeof(frame));
        for (unsigned i = 0; i < 4; ++i) {
            assert(pxa_asset_cache_acquire(cache, 1, tickets[i], &asset) == 0);
            assert(pxa_raster_bindings_replace(&frame, (uint8_t)i, asset) == NULL);
            pxa_raster_asset_release(asset); close_ticket(tickets[i], 1);
        }
        pxa_raster_bindings_view(&frame, 0, &view);
        for (unsigned i = 0; i < 4; ++i)
            assert(view.textures[i].pixels[65535] == (scene % 5) * 4 + i);
        pxa_raster_bindings_release(&frame);
        pxa_asset_cache_stats(cache, &stats);
        assert(stats.charged[1] <= 512 * 1024 && stats.loading[1] == 0);
    }
    pxa_asset_cache_stats(cache, &stats);
    assert(stats.coalesced_requests == 1 && stats.cache_hits >= 1);
    pxa_asset_cache_shutdown(cache); pump();
    assert(pxa_asset_cache_drained(cache) && live_bytes == 0 && loads == frees);
    assert(request(0, 1, 1, &a) == PXA_STATUS_BAD_STATE);
    printf("asset cache: 100 scenes, peak=%zu B, workspace=%zu B, loads=%u frees=%u\n",
           peak_bytes, workspace_bytes, loads, frees);
    free(workspace);
    test_limits_and_pressure();
    test_shared_pressure();
    test_bounded_pressure_retry();
    test_prefetch();
    test_catalog_lifetime_and_capacity();
    return 0;
}
