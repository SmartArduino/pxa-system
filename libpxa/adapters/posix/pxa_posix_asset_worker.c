#define _POSIX_C_SOURCE 200809L
#include "pxa/posix/pxa_posix_asset_worker.h"
#include "pxa/posix/pxa_posix_asset.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct pxa_posix_asset_worker {
    pxa_posix_asset_worker_config_t config;
    char *root;
    pxa_asset_catalog_t catalog;
    uint8_t *index;
    uint8_t package_key[32];
    void *cache_workspace;
    pxa_asset_cache_t *cache;
    size_t metadata_bytes;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint8_t shutdown;
    uint8_t thread_started;
    uint8_t allocation_class, allocation_kind;
    uint8_t last_read;
    uint8_t blob_reading;
    pxa_asset_read_queue_t reads;
};

typedef struct {
    pxa_posix_asset_worker_t *worker;
    pxa_asset_input_t raw;
    uint64_t token;
    uint8_t blob;
} load_input_t;

static void *pixel_allocate(void *context, size_t bytes) {
    pxa_posix_asset_worker_t *w = context;
    return w->config.allocate(w->config.allocator_context, bytes, w->allocation_class, w->allocation_kind);
}
static void pixel_release(void *context, void *memory) {
    pxa_posix_asset_worker_t *w = context;
    w->config.release(w->config.allocator_context, memory);
}
static pxa_status_t load_read(void *context, uint8_t *output, size_t capacity, size_t *read_bytes) {
    load_input_t *input = context;
    if (input->blob) {
        pthread_mutex_lock(&input->worker->mutex);
        input->worker->blob_reading=1;
        pthread_mutex_unlock(&input->worker->mutex);
    }
    if (input->worker->config.before_read)
        input->worker->config.before_read(input->worker->config.io_context);
    if (input->worker->config.read_delay_us) {
        uint32_t us = input->worker->config.read_delay_us;
        struct timespec delay = {us / 1000000u, (long)(us % 1000000u) * 1000};
        nanosleep(&delay, NULL);
    }
    pxa_status_t status=input->raw.read(input->raw.context, output, capacity, read_bytes);
    if (input->blob) {
        pthread_mutex_lock(&input->worker->mutex);
        input->worker->blob_reading=0;
        pthread_mutex_unlock(&input->worker->mutex);
    }
    return status;
}

int pxa_posix_asset_worker_reading_blob(pxa_posix_asset_worker_t *w) {
    if (!w) return 0;
    pthread_mutex_lock(&w->mutex);
    int reading=w->blob_reading;
    pthread_mutex_unlock(&w->mutex);
    return reading;
}
static int load_cancelled(void *context) {
    load_input_t *input = context;
    pthread_mutex_lock(&input->worker->mutex);
    int cancelled = input->blob ? pxa_asset_read_cancelled(&input->worker->reads,input->token) :
        pxa_asset_cache_job_cancelled(input->worker->cache, input->token);
    pthread_mutex_unlock(&input->worker->mutex);
    return cancelled;
}
static void finish_release(pxa_posix_asset_worker_t *w, pxa_asset_job_t *job) {
    pxa_raster_asset_release(job->asset);
    pthread_mutex_lock(&w->mutex);
    (void)pxa_asset_cache_finish_release(w->cache, job->token);
    pthread_mutex_unlock(&w->mutex);
}
static void *run_worker(void *context) {
    pxa_posix_asset_worker_t *w = context;
    for (;;) {
        pxa_asset_job_t job, discard;
        pxa_asset_read_job_t read_job;
        pthread_mutex_lock(&w->mutex);
        pxa_status_t next;
        for (;;) {
            /* Alternate job classes when both are ready. Reads are bounded
             * blocks, not a new thread competing with the audio decoder. */
            memset(&read_job,0,sizeof(read_job));
            if (!w->last_read && pxa_asset_read_next(&w->reads,&read_job) == PXA_STATUS_OK) break;
            next = pxa_asset_cache_next_job(w->cache,&job);
            if (next == PXA_STATUS_OK) break;
            if (pxa_asset_read_next(&w->reads,&read_job) == PXA_STATUS_OK) break;
            if (w->shutdown && pxa_asset_cache_drained(w->cache) && pxa_asset_read_drained(&w->reads)) {
                pthread_mutex_unlock(&w->mutex);
                return NULL;
            }
            pthread_cond_wait(&w->condition, &w->mutex);
        }
        w->last_read = read_job.kind != 0;
        pthread_mutex_unlock(&w->mutex);
        if (read_job.kind == PXA_ASSET_READ_JOB_RELEASE) {
            pxa_memory_release(read_job.buffer);
            pthread_mutex_lock(&w->mutex);
            pxa_asset_read_freed(&w->reads,read_job.token);
            pthread_mutex_unlock(&w->mutex);
        } else if (read_job.kind == PXA_ASSET_READ_JOB_LOAD) {
            pxa_posix_asset_input_t file;
            load_input_t wrapped = {w,{0},read_job.token,1};
            uint8_t *buffer = NULL; size_t bytes = 0;
            pxa_status_t status = pxa_posix_asset_open_range(&file,w->root,
                read_job.block.info.path,read_job.block.info.stored_bytes,
                read_job.block.offset,read_job.block.bytes,&wrapped.raw);
            if (!status) {
                file.gate=w->config.storage_gate; file.cancel_context=&wrapped; file.cancelled=load_cancelled;
                pxa_asset_input_t input = {&wrapped,load_read,load_cancelled,NULL};
                status = pxa_asset_read_load(&read_job,&input,w->reads.allocator,&buffer,&bytes);
                pxa_posix_asset_close(&file);
            }
            pthread_mutex_lock(&w->mutex);
            pxa_asset_read_finish(&w->reads,read_job.token,status,buffer,bytes);
            pthread_mutex_unlock(&w->mutex);
        } else if (job.kind == PXA_ASSET_JOB_RELEASE) finish_release(w, &job);
        else if (job.kind == PXA_ASSET_JOB_LOAD) {
            pxa_posix_asset_input_t file;
            load_input_t wrapped = {w, {0}, job.token,0};
            pxa_raster_asset_t *asset = NULL;
            pxa_status_t status = pxa_posix_asset_open(&file, w->root, job.info.path,
                                                       job.info.stored_bytes, &wrapped.raw);
            if (status == PXA_STATUS_OK) {
                file.gate=w->config.storage_gate; file.cancel_context=&wrapped; file.cancelled=load_cancelled;
                pxa_asset_input_t input = {&wrapped, load_read, load_cancelled, NULL};
                w->allocation_class = job.memory_class; w->allocation_kind = job.info.kind;
                status = pxa_asset_load_resident(&job.info, &input, pixel_allocate,
                                                pixel_release, w, &asset);
                pxa_posix_asset_close(&file);
            }
            pthread_mutex_lock(&w->mutex);
            (void)pxa_asset_cache_finish_load(w->cache, job.token, status, asset, &discard);
            pthread_mutex_unlock(&w->mutex);
            if (discard.kind == PXA_ASSET_JOB_RELEASE) finish_release(w, &discard);
        }
        if (w->config.notify) w->config.notify(w->config.notify_context);
    }
}

static void *metadata_allocate(const pxa_memory_allocator_t *allocator, size_t bytes) {
    return allocator ? pxa_memory_allocate(allocator, bytes) : malloc(bytes);
}
static pxa_status_t load_catalog(pxa_posix_asset_worker_t *w) {
    static const uint8_t index_path[] = PXA_ASSET_INDEX_PATH;
    pxa_bytes_t path = {index_path, sizeof(index_path) - 1};
    const pxa_package_file_t *file = pxa_package_file_find(w->config.manifest, path);
    pxa_posix_asset_input_t stream;
    pxa_asset_input_t input;
    size_t offset = 0, count;
    pxa_status_t status;
    uint8_t extra;
    if (file == NULL) return PXA_STATUS_NOT_FOUND;
    if (file->size < PXA_ASSET_INDEX_HEADER_BYTES || file->size > w->config.max_catalog_bytes)
        return PXA_STATUS_RESOURCE_LIMIT;
    w->index = metadata_allocate(w->config.metadata_allocator, (size_t)file->size);
    if (w->index == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    w->metadata_bytes += (size_t)file->size;
    status = pxa_posix_asset_open(&stream, w->root, path, file->size, &input);
    stream.gate=w->config.storage_gate;
    if (status != PXA_STATUS_OK) return status;
    while (offset < file->size) {
        size_t capacity = (size_t)file->size - offset;
        if (capacity > PXA_ASSET_READ_CHUNK_BYTES) capacity = PXA_ASSET_READ_CHUNK_BYTES;
        status = input.read(input.context, w->index + offset, capacity, &count);
        if (status != PXA_STATUS_OK || count == 0) {
            if (status == PXA_STATUS_OK) status = PXA_STATUS_IO_ERROR;
            break;
        }
        offset += count;
    }
    if (status == PXA_STATUS_OK) {
        status = input.read(input.context, &extra, 1, &count);
        if (status == PXA_STATUS_OK && count != 0) status = PXA_STATUS_PROTOCOL_ERROR;
    }
    pxa_posix_asset_close(&stream);
    if (status != PXA_STATUS_OK) return status;
    return pxa_asset_catalog_init(&w->catalog,
        (pxa_bytes_t){w->index, (size_t)file->size}, w->config.manifest);
}

static void metadata_free(const pxa_memory_allocator_t *allocator, void *memory) {
    if (allocator) pxa_memory_release(memory);
    else free(memory);
}
static void release_storage(pxa_posix_asset_worker_t *w) {
    const pxa_memory_allocator_t *allocator = w->config.metadata_allocator;
    metadata_free(allocator, w->index);
    metadata_free(allocator, w->cache_workspace);
    metadata_free(allocator, w->root);
    metadata_free(allocator, w);
}
pxa_status_t pxa_posix_asset_worker_create(const pxa_posix_asset_worker_config_t *config,
                                          pxa_posix_asset_worker_t **output) {
    pxa_posix_asset_worker_t *w;
    pthread_attr_t attributes;
    size_t workspace;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->package_root == NULL ||
        strlen(config->package_root) > 1023 || config->manifest == NULL ||
        config->manifest->encoded.data == NULL || config->manifest->encoded.size == 0 ||
        config->allocate == NULL || config->release == NULL ||
        (workspace = pxa_asset_cache_workspace_size(&config->cache)) == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    w = metadata_allocate(config->metadata_allocator, sizeof(*w));
    if (w == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(w, 0, sizeof(*w));
    w->config = *config;
    if (!w->config.stack_bytes) w->config.stack_bytes=128*1024;
    pxa_asset_read_init(&w->reads,config->temporary_allocator);
    w->root = metadata_allocate(config->metadata_allocator, strlen(config->package_root) + 1);
    if (w->root) strcpy(w->root, config->package_root);
    w->cache_workspace = metadata_allocate(config->metadata_allocator, workspace);
    w->metadata_bytes = sizeof(*w) + workspace + strlen(config->package_root) + 1;
    if (w->root == NULL || w->cache_workspace == NULL) { release_storage(w); return PXA_STATUS_RESOURCE_LIMIT; }
    /* Cache is private to this activation; no content hash is needed as namespace. */
    status = PXA_STATUS_OK;
    if (status == PXA_STATUS_OK) status = load_catalog(w);
    if (status == PXA_STATUS_OK) status = pxa_asset_cache_init(w->cache_workspace, workspace, &config->cache, &w->cache);
    if (status != PXA_STATUS_OK) { release_storage(w); return status; }
    if (pthread_mutex_init(&w->mutex, NULL) != 0) { release_storage(w); return PXA_STATUS_INTERNAL; }
    if (pthread_cond_init(&w->condition, NULL) != 0) {
        pthread_mutex_destroy(&w->mutex); release_storage(w); return PXA_STATUS_INTERNAL;
    }
    if (pthread_attr_init(&attributes) != 0) {
        pthread_cond_destroy(&w->condition); pthread_mutex_destroy(&w->mutex);
        release_storage(w); return PXA_STATUS_INTERNAL;
    }
    int failed = pthread_attr_setstacksize(&attributes, w->config.stack_bytes);
    pthread_attr_destroy(&attributes);
    if (failed) {
        pthread_cond_destroy(&w->condition); pthread_mutex_destroy(&w->mutex);
        release_storage(w); return PXA_STATUS_INTERNAL;
    }
    *output = w;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_posix_asset_worker_find(pxa_posix_asset_worker_t *w,
    pxa_bytes_t path, pxa_asset_info_t *info) {
    if (!w || !info) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_asset_catalog_find(&w->catalog, path, info);
}
pxa_status_t pxa_posix_asset_worker_block_map(pxa_posix_asset_worker_t *w,
    pxa_bytes_t path, pxa_asset_block_map_t *map) {
    if (!w || !map) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_asset_catalog_block_map(&w->catalog, path, map);
}
/* Called under the queue mutex, before accepting the first async request.
 * Catalog-only music consumers use their existing decoder task, so do not
 * pay for an idle asset worker stack. */
static pxa_status_t start_worker_locked(pxa_posix_asset_worker_t *w) {
    if (w->shutdown) return PXA_STATUS_BAD_STATE;
    if (w->thread_started) return PXA_STATUS_OK;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes)) return PXA_STATUS_RESOURCE_LIMIT;
    int failed=pthread_attr_setstacksize(&attributes,w->config.stack_bytes);
    if (!failed) failed=pthread_create(&w->thread,&attributes,run_worker,w);
    pthread_attr_destroy(&attributes);
    if (failed) return PXA_STATUS_RESOURCE_LIMIT;
    w->thread_started=1;
    return PXA_STATUS_OK;
}
size_t pxa_posix_asset_worker_stack_bytes(pxa_posix_asset_worker_t *w) {
    if (!w) return 0;
    pthread_mutex_lock(&w->mutex);
    size_t bytes=w->thread_started ? w->config.stack_bytes : 0;
    pthread_mutex_unlock(&w->mutex);
    return bytes;
}
static pxa_status_t request_asset(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_bytes_t path, uint8_t memory_class, pxa_asset_ticket_t *ticket, int prefetch) {
    pxa_asset_info_t info;
    if (w == NULL || ticket == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *ticket = 0;
    pxa_status_t status = pxa_asset_catalog_find(&w->catalog, path, &info);
    if (status != PXA_STATUS_OK) return status;
    pthread_mutex_lock(&w->mutex);
    status=start_worker_locked(w);
    if (!status) status = (prefetch ? pxa_asset_cache_prefetch : pxa_asset_cache_request)(
        w->cache, owner, w->package_key, &info, memory_class, ticket);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_request(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    return request_asset(w, owner, path, cls, ticket, 0);
}
pxa_status_t pxa_posix_asset_worker_prefetch(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    return request_asset(w, owner, path, cls, ticket, 1);
}
pxa_status_t pxa_posix_asset_worker_inspect(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_bytes_t path, pxa_asset_request_state_t *state) {
    pxa_asset_info_t info;
    if (!w || !state) return PXA_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    pxa_status_t status = pxa_asset_catalog_find(&w->catalog, path, &info);
    if (status != PXA_STATUS_OK) return status;
    if (!pxa_asset_object_required_bytes(&info)) return PXA_STATUS_UNSUPPORTED;
    pthread_mutex_lock(&w->mutex);
    status = pxa_asset_cache_inspect(w->cache, owner, w->package_key, &info,
        pxa_asset_default_memory_class(&info), state);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_query(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state) {
    if (w == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_cache_query(w->cache, owner, ticket, state);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_acquire(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset) {
    if (w == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_cache_acquire(w->cache, owner, ticket, asset);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_cancel(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_asset_ticket_t ticket) {
    if (w == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_cache_cancel(w->cache, owner, ticket);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_release(pxa_posix_asset_worker_t *w,
    uint64_t owner, pxa_asset_ticket_t ticket) {
    if (w == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_cache_release(w->cache, owner, ticket);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
void pxa_posix_asset_worker_cancel_owner(pxa_posix_asset_worker_t *w, uint64_t owner) {
    if (w == NULL) return;
    pthread_mutex_lock(&w->mutex);
    pxa_asset_cache_cancel_owner(w->cache, owner);
    pxa_asset_read_cancel_owner(&w->reads,owner);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
}
void pxa_posix_asset_worker_stats(pxa_posix_asset_worker_t *w,
    pxa_asset_cache_stats_t *stats, size_t *metadata_bytes) {
    if (w == NULL) return;
    pthread_mutex_lock(&w->mutex);
    pxa_asset_cache_stats(w->cache, stats);
    if (metadata_bytes) *metadata_bytes = w->metadata_bytes;
    pthread_mutex_unlock(&w->mutex);
}
pxa_status_t pxa_posix_asset_worker_destroy(pxa_posix_asset_worker_t *w) {
    if (w == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    w->shutdown = 1;
    pxa_asset_cache_shutdown(w->cache);
    pxa_asset_read_shutdown(&w->reads);
    int drained = pxa_asset_cache_drained(w->cache) && pxa_asset_read_drained(&w->reads);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    if (!drained) return PXA_STATUS_WOULD_BLOCK;
    if (w->thread_started) pthread_join(w->thread, NULL);
    pthread_cond_destroy(&w->condition);
    pthread_mutex_destroy(&w->mutex);
    release_storage(w);
    return PXA_STATUS_OK;
}

size_t pxa_posix_asset_worker_trim(pxa_posix_asset_worker_t *w, uint8_t cls, size_t needed) {
    if (!w) return 0;
    pthread_mutex_lock(&w->mutex);
    size_t planned = pxa_asset_cache_trim(w->cache, cls, needed);
    if (planned) pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return planned;
}

pxa_status_t pxa_posix_asset_worker_read(pxa_posix_asset_worker_t *w, uint64_t owner,
    pxa_bytes_t path, uint32_t offset, uint32_t bytes, uint64_t *ticket) {
    if (!w || !ticket) return PXA_STATUS_INVALID_ARGUMENT;
    *ticket = 0;
    pxa_asset_blob_block_t block;
    pxa_status_t status = pxa_asset_catalog_blob_block(&w->catalog,path,offset,&block);
    if (status) return status;
    pthread_mutex_lock(&w->mutex);
    status=start_worker_locked(w);
    if (!status) status = pxa_asset_read_begin(&w->reads,owner,&block,offset,bytes,ticket);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_read_result(pxa_posix_asset_worker_t *w, uint64_t owner,
    uint64_t ticket, pxa_bytes_t *result) {
    if (!w) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_read_result(&w->reads,owner,ticket,result);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
pxa_status_t pxa_posix_asset_worker_read_release(pxa_posix_asset_worker_t *w, uint64_t owner, uint64_t ticket) {
    if (!w) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&w->mutex);
    pxa_status_t status = pxa_asset_read_release(&w->reads,owner,ticket);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return status;
}
