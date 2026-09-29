#include "pxa/asset_cache.h"
#include "common/checked_math.h"
#include "core/slot_token.h"

#include <string.h>

#define CACHE_MAGIC UINT32_C(0x50584348)
#define ENTRY_NONE UINT16_MAX
#define ENTRY_RELEASING 6u

typedef struct {
    pxa_asset_info_t info;
    uint8_t package_key[32];
    uint64_t owner;
    uint64_t age;
    size_t charge;
    pxa_raster_asset_t *asset;
    pxa_status_t status;
    uint32_t generation;
    uint32_t pins;
    uint8_t state;
    uint8_t memory_class;
    uint8_t drop;
    uint8_t retired;
    uint8_t pressure_retried;
    uint16_t foreground_pins;
} cache_entry_t;

typedef struct {
    uint64_t owner;
    uint32_t generation;
    uint16_t entry;
    uint8_t active;
    uint8_t retired;
} cache_request_t;

struct pxa_asset_cache {
    uint32_t magic;
    pxa_asset_cache_config_t config;
    pxa_asset_cache_stats_t stats;
    cache_entry_t *entries;
    cache_request_t *requests;
    uint64_t clock;
    uint16_t worker;
    uint8_t closing;
    uint8_t pressure;
};

static int valid(const pxa_asset_cache_t *cache) {
    return cache != NULL && cache->magic == CACHE_MAGIC;
}

size_t pxa_asset_cache_workspace_size(const pxa_asset_cache_config_t *config) {
    size_t bytes;
    if (config == NULL || config->max_entries == 0 || config->max_requests == 0 ||
        config->max_requests_per_owner == 0 ||
        config->max_requests_per_owner > config->max_requests ||
        config->max_pending == 0 || config->max_pending > config->max_entries)
        return 0;
    bytes = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1 + sizeof(pxa_asset_cache_t);
    if ((size_t)config->max_entries > (SIZE_MAX - bytes) / sizeof(cache_entry_t)) return 0;
    bytes += (size_t)config->max_entries * sizeof(cache_entry_t);
    if ((size_t)config->max_requests > (SIZE_MAX - bytes) / sizeof(cache_request_t)) return 0;
    return bytes + (size_t)config->max_requests * sizeof(cache_request_t);
}

pxa_status_t pxa_asset_cache_init(void *workspace, size_t bytes,
    const pxa_asset_cache_config_t *config, pxa_asset_cache_t **output) {
    uintptr_t cursor;
    pxa_asset_cache_t *cache;
    size_t required = pxa_asset_cache_workspace_size(config);
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (workspace == NULL || required == 0 || bytes < required ||
        (uintptr_t)workspace > UINTPTR_MAX - bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    cache = (pxa_asset_cache_t *)cursor;
    memset(cache, 0, sizeof(*cache));
    cursor += sizeof(*cache);
    cache->config = *config;
    cache->entries = (cache_entry_t *)cursor;
    cursor += (size_t)config->max_entries * sizeof(cache_entry_t);
    cache->requests = (cache_request_t *)cursor;
    memset(cache->entries, 0, (size_t)config->max_entries * sizeof(cache_entry_t));
    memset(cache->requests, 0, (size_t)config->max_requests * sizeof(cache_request_t));
    for (uint32_t i = 0; i < config->max_entries; ++i) cache->entries[i].generation = 1;
    for (uint32_t i = 0; i < config->max_requests; ++i) cache->requests[i].generation = 1;
    cache->worker = ENTRY_NONE;
    cache->magic = CACHE_MAGIC;
    *output = cache;
    return PXA_STATUS_OK;
}

static uint64_t tick(pxa_asset_cache_t *cache) {
    if (cache->clock == UINT64_MAX) {
        for (uint32_t i = 0; i < cache->config.max_entries; ++i)
            cache->entries[i].age >>= 1;
        cache->clock >>= 1;
    }
    return ++cache->clock;
}

static void clear_entry(cache_entry_t *entry) {
    uint32_t generation = entry->generation;
    memset(entry, 0, sizeof(*entry));
    entry->generation = generation == UINT32_MAX ? generation : generation + 1;
    entry->retired = generation == UINT32_MAX;
}

static cache_request_t *find_request(pxa_asset_cache_t *cache, uint64_t owner,
                                      pxa_asset_ticket_t ticket) {
    uint32_t index, generation;
    cache_request_t *request;
    if (!valid(cache) || !pxa_internal_handle64_token_decode(ticket,
            cache->config.max_requests, &index, &generation)) return NULL;
    request = &cache->requests[index];
    return request->active && request->owner == owner &&
           request->generation == generation ? request : NULL;
}

static cache_entry_t *find_job(pxa_asset_cache_t *cache, uint64_t token,
                                uint8_t expected) {
    uint32_t index, generation;
    if (!valid(cache) || !pxa_internal_handle64_token_decode(token,
            cache->config.max_entries, &index, &generation) ||
        cache->worker != index) return NULL;
    cache_entry_t *entry = &cache->entries[index];
    return entry->state == expected && entry->generation == generation ? entry : NULL;
}

static pxa_status_t request_asset(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_ticket_t *ticket, int prefetch) {
    uint16_t free_request = ENTRY_NONE, free_entry = ENTRY_NONE, match = ENTRY_NONE;
    uint32_t owner_requests = 0;
    if (ticket == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *ticket = 0;
    if (!valid(cache) || owner == 0 || package_key == NULL ||
        memory_class >= PXA_ASSET_MEMORY_CLASSES ||
        pxa_asset_object_required_bytes(info) == 0 ||
        !pxa_asset_path_valid(info->path) || info->file == NULL ||
        info->file->sha256 == NULL || info->file->size != info->stored_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (cache->closing) return PXA_STATUS_BAD_STATE;
    for (uint32_t i = 0; i < cache->config.max_requests; ++i) {
        cache_request_t *r = &cache->requests[i];
        if (r->active && r->owner == owner) ++owner_requests;
        if (!r->active && !r->retired && free_request == ENTRY_NONE) free_request = (uint16_t)i;
    }
    if (owner_requests >= cache->config.max_requests_per_owner) return PXA_STATUS_QUOTA_EXCEEDED;
    if (free_request == ENTRY_NONE) return PXA_STATUS_WOULD_BLOCK;
    for (uint32_t i = 0; i < cache->config.max_entries; ++i) {
        cache_entry_t *e = &cache->entries[i];
        if (!e->state && !e->retired && free_entry == ENTRY_NONE) free_entry = (uint16_t)i;
        if (e->state >= PXA_ASSET_REQUEST_QUEUED && e->state <= PXA_ASSET_REQUEST_READY &&
            !e->drop && e->owner == owner && e->memory_class == memory_class &&
            memcmp(e->package_key, package_key, 32) == 0 &&
            memcmp(e->info.file->sha256, info->file->sha256, 32) == 0 &&
            pxa_asset_info_equal(&e->info, info)) { match = (uint16_t)i; break; }
    }
    if (match == ENTRY_NONE) {
        if (cache->stats.queued >= cache->config.max_pending) return PXA_STATUS_WOULD_BLOCK;
        if (free_entry == ENTRY_NONE) {
            cache->pressure = 1;
            return PXA_STATUS_WOULD_BLOCK;
        }
        cache->pressure = 0;
        match = free_entry;
        cache_entry_t *e = &cache->entries[match];
        /* The installed catalog and manifest outlive the drained cache.
         * Retain their immutable metadata, not one path/file/digest per slot. */
        e->info = *info;
        memcpy(e->package_key, package_key, 32);
        e->owner = owner;
        e->memory_class = memory_class;
        e->state = PXA_ASSET_REQUEST_QUEUED;
        e->age = tick(cache);
        ++cache->stats.queued;
    } else if (cache->entries[match].state == PXA_ASSET_REQUEST_READY) {
        ++cache->stats.cache_hits;
        cache->entries[match].age = tick(cache);
    } else ++cache->stats.coalesced_requests;
    ++cache->entries[match].pins;
    if (!prefetch) ++cache->entries[match].foreground_pins;
    cache_request_t *r = &cache->requests[free_request];
    r->active = prefetch ? 2 : 1;
    r->owner = owner;
    r->entry = match;
    ++cache->stats.requests;
    if (prefetch) ++cache->stats.prefetch_requests;
    *ticket = pxa_internal_handle64_token_encode(free_request, r->generation);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_request(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_ticket_t *ticket) {
    return request_asset(cache, owner, package_key, info, memory_class, ticket, 0);
}
pxa_status_t pxa_asset_cache_prefetch(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_ticket_t *ticket) {
    return request_asset(cache, owner, package_key, info, memory_class, ticket, 1);
}
pxa_status_t pxa_asset_cache_inspect(pxa_asset_cache_t *cache, uint64_t owner,
    const uint8_t package_key[32], const pxa_asset_info_t *info,
    uint8_t memory_class, pxa_asset_request_state_t *state) {
    if (!state) return PXA_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    if (!valid(cache) || !owner || !package_key || memory_class >= PXA_ASSET_MEMORY_CLASSES ||
        !pxa_asset_object_required_bytes(info) || !pxa_asset_path_valid(info->path) ||
        !info->file || !info->file->sha256 || info->file->size != info->stored_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (cache->closing) return PXA_STATUS_BAD_STATE;
    for (uint32_t i = 0; i < cache->config.max_entries; ++i) {
        cache_entry_t *e = &cache->entries[i];
        if (!e->state || e->state == ENTRY_RELEASING || e->drop ||
            e->owner != owner || e->memory_class != memory_class ||
            memcmp(e->package_key, package_key, 32) ||
            memcmp(e->info.file->sha256, info->file->sha256, 32) || !pxa_asset_info_equal(&e->info, info)) continue;
        state->state = e->state;
        state->status = e->status;
        /* A failed ticket may coexist with a retry. Prefer live work or a
         * resident result to a previous request's retained failure. */
        if (e->state != PXA_ASSET_REQUEST_FAILED) break;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_query(pxa_asset_cache_t *cache, uint64_t owner,
    pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state) {
    cache_request_t *r = find_request(cache, owner, ticket);
    if (state == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    if (r == NULL) return PXA_STATUS_NOT_FOUND;
    if (r->entry == ENTRY_NONE) {
        state->state = PXA_ASSET_REQUEST_CANCELLED;
        state->status = PXA_STATUS_CANCELLED;
    } else {
        cache_entry_t *e = &cache->entries[r->entry];
        state->state = e->state;
        state->status = e->status;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_acquire(pxa_asset_cache_t *cache, uint64_t owner,
    pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset) {
    pxa_asset_request_state_t state;
    pxa_status_t status;
    if (asset == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *asset = NULL;
    status = pxa_asset_cache_query(cache, owner, ticket, &state);
    if (status != PXA_STATUS_OK) return status;
    if (state.state == PXA_ASSET_REQUEST_FAILED || state.state == PXA_ASSET_REQUEST_CANCELLED)
        return state.status;
    if (state.state != PXA_ASSET_REQUEST_READY) return PXA_STATUS_WOULD_BLOCK;
    cache_request_t *r = find_request(cache, owner, ticket);
    *asset = cache->entries[r->entry].asset;
    pxa_raster_asset_retain(*asset);
    cache->entries[r->entry].age = tick(cache);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_cancel(pxa_asset_cache_t *cache, uint64_t owner,
                                    pxa_asset_ticket_t ticket) {
    cache_request_t *r = find_request(cache, owner, ticket);
    if (r == NULL) return PXA_STATUS_NOT_FOUND;
    if (r->entry != ENTRY_NONE) {
        cache_entry_t *e = &cache->entries[r->entry];
        r->entry = ENTRY_NONE;
        if (r->active == 1) --e->foreground_pins;
        if (--e->pins == 0) {
            if (e->state == PXA_ASSET_REQUEST_QUEUED) { --cache->stats.queued; clear_entry(e); }
            else if (e->state == PXA_ASSET_REQUEST_FAILED) clear_entry(e);
            else if (e->state == PXA_ASSET_REQUEST_LOADING) e->drop = 1;
        }
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_release(pxa_asset_cache_t *cache, uint64_t owner,
                                     pxa_asset_ticket_t ticket) {
    cache_request_t *r = find_request(cache, owner, ticket);
    if (r == NULL) return PXA_STATUS_NOT_FOUND;
    (void)pxa_asset_cache_cancel(cache, owner, ticket);
    r->active = 0;
    if (r->generation == UINT32_MAX) r->retired = 1;
    else ++r->generation;
    --cache->stats.requests;
    return PXA_STATUS_OK;
}

void pxa_asset_cache_cancel_owner(pxa_asset_cache_t *cache, uint64_t owner) {
    if (!valid(cache)) return;
    for (uint32_t i = 0; i < cache->config.max_requests; ++i) {
        cache_request_t *r = &cache->requests[i];
        if (r->active && r->owner == owner)
            (void)pxa_asset_cache_release(cache, owner,
                pxa_internal_handle64_token_encode(i, r->generation));
    }
    for (uint32_t i = 0; i < cache->config.max_entries; ++i)
        if (cache->entries[i].state && cache->entries[i].owner == owner)
            cache->entries[i].drop = 1;
}

static void make_job(pxa_asset_cache_t *cache, uint16_t index, uint8_t kind,
                       pxa_asset_job_t *job) {
    cache_entry_t *e = &cache->entries[index];
    memset(job, 0, sizeof(*job));
    job->kind = kind;
    job->memory_class = e->memory_class;
    job->owner = e->owner;
    job->reserved_bytes = e->charge;
    job->info = e->info;
    job->token = pxa_internal_handle64_token_encode(index, e->generation);
    if (kind == PXA_ASSET_JOB_RELEASE) { job->asset = e->asset; e->asset = NULL; }
}

static uint16_t evictable(pxa_asset_cache_t *cache, int memory_class,
                           uint64_t owner, int only_dropped) {
    uint16_t best = ENTRY_NONE;
    for (uint32_t i = 0; i < cache->config.max_entries; ++i) {
        cache_entry_t *e = &cache->entries[i];
        if (e->state != PXA_ASSET_REQUEST_READY || e->pins != 0 ||
            (memory_class >= 0 && e->memory_class != memory_class) ||
            (owner != 0 && e->owner != owner) || (only_dropped > 0 && !e->drop) ||
            (only_dropped < 0 && e->drop) ||
            pxa_raster_asset_reference_count(e->asset) != 1) continue;
        if (best == ENTRY_NONE || e->age < cache->entries[best].age) best = (uint16_t)i;
    }
    return best;
}

size_t pxa_asset_cache_trim(pxa_asset_cache_t *cache, uint8_t cls, size_t needed) {
    size_t planned = 0;
    if (!valid(cache) || cls >= PXA_ASSET_MEMORY_CLASSES || !needed) return 0;
    for (uint32_t i = 0; i < cache->config.max_entries; ++i) {
        cache_entry_t *e = &cache->entries[i];
        if (e->memory_class == cls && (e->state == ENTRY_RELEASING ||
            (e->state == PXA_ASSET_REQUEST_READY && e->drop && !e->pins &&
             pxa_raster_asset_reference_count(e->asset) == 1))) planned += e->charge;
    }
    while (planned < needed) {
        uint16_t victim = evictable(cache, cls, 0, -1);
        if (victim == ENTRY_NONE) break;
        cache->entries[victim].drop = 1;
        planned += cache->entries[victim].charge;
    }
    return planned;
}

static void release_job(pxa_asset_cache_t *cache, uint16_t index, pxa_asset_job_t *job) {
    cache->entries[index].state = ENTRY_RELEASING;
    --cache->stats.ready;
    cache->worker = index;
    cache->stats.worker_busy = 1;
    make_job(cache, index, PXA_ASSET_JOB_RELEASE, job);
}

static size_t owner_charge(pxa_asset_cache_t *cache, uint64_t owner, uint8_t memory_class) {
    size_t bytes = 0;
    for (uint32_t i = 0; i < cache->config.max_entries; ++i) {
        cache_entry_t *e = &cache->entries[i];
        if (e->owner == owner && e->memory_class == memory_class) bytes += e->charge;
    }
    return bytes;
}

pxa_status_t pxa_asset_cache_next_job(pxa_asset_cache_t *cache, pxa_asset_job_t *job) {
    uint16_t next = ENTRY_NONE, victim;
    size_t need, owner_used;
    uint8_t cls;
    int owner_full, global_full;
    if (!valid(cache) || job == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(job, 0, sizeof(*job));
    if (cache->worker != ENTRY_NONE) return PXA_STATUS_BUSY;
    victim = evictable(cache, -1, 0, 1);
    if (victim != ENTRY_NONE) { release_job(cache, victim, job); return PXA_STATUS_OK; }
    for (uint32_t i = 0; i < cache->config.max_entries; ++i)
        if (cache->entries[i].state == PXA_ASSET_REQUEST_QUEUED &&
            (next == ENTRY_NONE ||
             (!!cache->entries[i].foreground_pins > !!cache->entries[next].foreground_pins) ||
             (!!cache->entries[i].foreground_pins == !!cache->entries[next].foreground_pins &&
              cache->entries[i].age < cache->entries[next].age))) next = (uint16_t)i;
    if (next == ENTRY_NONE) {
        if (cache->pressure && (victim = evictable(cache, -1, 0, 0)) != ENTRY_NONE) {
            cache->pressure = 0;
            release_job(cache, victim, job);
            return PXA_STATUS_OK;
        }
        return PXA_STATUS_NOT_FOUND;
    }
    cache_entry_t *e = &cache->entries[next];
    cls = e->memory_class;
    need = pxa_asset_object_required_bytes(&e->info);
    owner_used = owner_charge(cache, e->owner, cls);
    owner_full = need > cache->config.owner_limit[cls] ||
        owner_used > cache->config.owner_limit[cls] - need;
    global_full = need > cache->config.resident_limit[cls] ||
        cache->stats.charged[cls] > cache->config.resident_limit[cls] - need;
    if (owner_full || global_full) {
        victim = evictable(cache, cls, owner_full ? e->owner : 0, 0);
        /* Do not evict for a request that cannot fit even into an empty cache. */
        if (need <= cache->config.owner_limit[cls] &&
            need <= cache->config.resident_limit[cls] && victim != ENTRY_NONE) {
            release_job(cache, victim, job);
            return PXA_STATUS_OK;
        }
        e->state = PXA_ASSET_REQUEST_FAILED;
        e->status = owner_full ? PXA_STATUS_QUOTA_EXCEEDED : PXA_STATUS_RESOURCE_LIMIT;
        --cache->stats.queued;
        ++cache->stats.load_failures;
        if (!e->foreground_pins) ++cache->stats.prefetch_failures;
        make_job(cache, next, PXA_ASSET_JOB_NOTIFY, job);
        return PXA_STATUS_OK;
    }
    e->state = PXA_ASSET_REQUEST_LOADING;
    e->charge = need;
    --cache->stats.queued;
    cache->stats.charged[cls] += need;
    cache->stats.loading[cls] += need;
    if (cache->stats.charged[cls] > cache->stats.peak_charged[cls])
        cache->stats.peak_charged[cls] = cache->stats.charged[cls];
    cache->worker = next;
    cache->stats.worker_busy = 1;
    make_job(cache, next, PXA_ASSET_JOB_LOAD, job);
    return PXA_STATUS_OK;
}

int pxa_asset_cache_job_cancelled(pxa_asset_cache_t *cache, uint64_t token) {
    cache_entry_t *e = find_job(cache, token, PXA_ASSET_REQUEST_LOADING);
    return e == NULL || e->drop || e->pins == 0;
}

pxa_status_t pxa_asset_cache_finish_load(pxa_asset_cache_t *cache,
    uint64_t token, pxa_status_t status, pxa_raster_asset_t *asset,
    pxa_asset_job_t *discard) {
    cache_entry_t *e = find_job(cache, token, PXA_ASSET_REQUEST_LOADING);
    if (discard == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(discard, 0, sizeof(*discard));
    if (e == NULL) return PXA_STATUS_NOT_FOUND;
    if ((status == PXA_STATUS_OK && (asset == NULL ||
          pxa_raster_asset_allocation_bytes(asset) != e->charge)) ||
        (status != PXA_STATUS_OK && asset != NULL)) return PXA_STATUS_INVALID_ARGUMENT;
    cache->stats.loading[e->memory_class] -= e->charge;
    if (status != PXA_STATUS_OK) {
        cache->stats.charged[e->memory_class] -= e->charge;
        e->charge = 0;
        if (status == PXA_STATUS_RESOURCE_LIMIT && !e->drop && e->pins &&
            !e->pressure_retried && cache->stats.queued < cache->config.max_pending &&
            evictable(cache, e->memory_class, 0, 1) != ENTRY_NONE) {
            /* The shared budget already scheduled unused victims. Complete
             * their real frees first, then retry this accepted load once.
             * No Guest failure is published until the bounded retry fails. */
            e->state = PXA_ASSET_REQUEST_QUEUED;
            e->pressure_retried = 1;
            ++cache->stats.queued;
            ++cache->stats.pressure_retries;
        } else {
            e->state = PXA_ASSET_REQUEST_FAILED;
            e->status = status;
            ++cache->stats.load_failures;
            if (!e->foreground_pins) ++cache->stats.prefetch_failures;
            if (e->pins == 0) clear_entry(e);
        }
    } else {
        e->asset = asset;
        e->state = PXA_ASSET_REQUEST_READY;
        e->age = tick(cache);
        ++cache->stats.ready;
        if (e->drop || e->pins == 0) {
            release_job(cache, cache->worker, discard);
            return PXA_STATUS_OK;
        }
    }
    cache->worker = ENTRY_NONE;
    cache->stats.worker_busy = 0;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_cache_finish_release(pxa_asset_cache_t *cache, uint64_t token) {
    cache_entry_t *e = find_job(cache, token, ENTRY_RELEASING);
    if (e == NULL) return PXA_STATUS_NOT_FOUND;
    cache->stats.charged[e->memory_class] -= e->charge;
    ++cache->stats.evictions;
    clear_entry(e);
    cache->worker = ENTRY_NONE;
    cache->stats.worker_busy = 0;
    return PXA_STATUS_OK;
}

void pxa_asset_cache_stats(const pxa_asset_cache_t *cache, pxa_asset_cache_stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
        if (valid(cache)) *stats = cache->stats;
    }
}

void pxa_asset_cache_shutdown(pxa_asset_cache_t *cache) {
    if (!valid(cache)) return;
    cache->closing = 1;
    for (uint32_t i = 0; i < cache->config.max_requests; ++i) {
        cache_request_t *r = &cache->requests[i];
        if (r->active) (void)pxa_asset_cache_release(cache, r->owner,
            pxa_internal_handle64_token_encode(i, r->generation));
    }
    for (uint32_t i = 0; i < cache->config.max_entries; ++i)
        if (cache->entries[i].state) cache->entries[i].drop = 1;
}

int pxa_asset_cache_drained(const pxa_asset_cache_t *cache) {
    return valid(cache) && cache->stats.requests == 0 && cache->stats.queued == 0 &&
           cache->stats.ready == 0 && cache->worker == ENTRY_NONE &&
           cache->stats.charged[0] == 0 && cache->stats.charged[1] == 0;
}
