#include "ui/ui_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static size_t node_hash(uint32_t surface, uint32_t id, size_t buckets) {
    uint32_t value = id ^ (surface * UINT32_C(0x9e3779b9));
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    return (size_t)value & (buckets - 1u);
}

static int registry_capacity_insufficient(size_t nodes, size_t buckets) {
    return buckets == 0 ||
           (buckets <= SIZE_MAX / PXA_UI_NODES_PER_BUCKET &&
            nodes > buckets * PXA_UI_NODES_PER_BUCKET);
}

static pxa_status_t rehash(pxa_ui_service_t *service,
                           pxa_ui_entry_t *entry, size_t bucket_count) {
    pxa_ui_node_t **buckets;
    pxa_ui_node_chunk_t *chunk;
    size_t bytes;
    uint32_t index;
    if (bucket_count < PXA_UI_INITIAL_BUCKETS ||
        (bucket_count & (bucket_count - 1u)) != 0 ||
        bucket_count > SIZE_MAX / sizeof(*buckets))
        return PXA_STATUS_RESOURCE_LIMIT;
    bytes = bucket_count * sizeof(*buckets);
    buckets = (pxa_ui_node_t **)pxa_ui_alloc(service, bytes);
    if (buckets == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(buckets, 0, bytes);
    for (chunk = entry->chunks; chunk != NULL; chunk = chunk->next) {
        for (index = 0; index < PXA_UI_NODE_CHUNK_COUNT; ++index) {
            pxa_ui_node_t *node = &chunk->nodes[index];
            size_t bucket;
            if (!node->occupied) continue;
            bucket = node_hash(node->surface, node->id, bucket_count);
            node->hash_next = buckets[bucket];
            buckets[bucket] = node;
        }
    }
    pxa_ui_free(service, entry->buckets);
    entry->buckets = buckets;
    entry->bucket_count = bucket_count;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_registry_init(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry) {
    if (service == NULL || entry == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    return rehash(service, entry, PXA_UI_INITIAL_BUCKETS);
}

void pxa_ui_registry_clear(pxa_ui_service_t *service,
                              pxa_ui_entry_t *entry) {
    pxa_ui_node_chunk_t *chunk;
    pxa_ui_node_chunk_t *next;
    if (service == NULL || entry == NULL) return;
    for (chunk = entry->chunks; chunk != NULL; chunk = next) {
        next = chunk->next;
        pxa_ui_free(service, chunk);
    }
    pxa_ui_free(service, entry->buckets);
    entry->chunks = NULL;
    entry->free_nodes = NULL;
    entry->free_node_count = 0;
    entry->buckets = NULL;
    entry->bucket_count = 0;
    entry->node_count = 0;
}

pxa_ui_node_t *pxa_ui_registry_find(pxa_ui_entry_t *entry,
                                          uint32_t surface, uint32_t node) {
    pxa_ui_node_t *current;
    size_t bucket;
    if (entry == NULL || entry->buckets == NULL || surface == 0 || node == 0)
        return NULL;
    bucket = node_hash(surface, node, entry->bucket_count);
    for (current = entry->buckets[bucket]; current != NULL;
         current = current->hash_next) {
        if (current->surface == surface && current->id == node) return current;
    }
    return NULL;
}

const pxa_ui_node_t *pxa_ui_registry_find_const(
    const pxa_ui_entry_t *entry, uint32_t surface, uint32_t node) {
    return pxa_ui_registry_find((pxa_ui_entry_t *)entry, surface, node);
}

static void add_free_chunk(pxa_ui_entry_t *entry,
                           pxa_ui_node_chunk_t *chunk) {
    uint32_t index;
    for (index = 0; index < PXA_UI_NODE_CHUNK_COUNT; ++index) {
        pxa_ui_node_t *node = &chunk->nodes[index];
        node->chunk_index = (uint8_t)index;
        node->hash_next = entry->free_nodes;
        entry->free_nodes = node;
    }
    entry->free_node_count += PXA_UI_NODE_CHUNK_COUNT;
}

static pxa_ui_node_chunk_t *node_chunk(pxa_ui_node_t *node) {
    return (pxa_ui_node_chunk_t *)(
        (uint8_t *)node - offsetof(pxa_ui_node_chunk_t, nodes) -
        (size_t)node->chunk_index * sizeof(*node));
}

static pxa_ui_node_t *allocate_node(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry) {
    pxa_ui_node_chunk_t *chunk;
    pxa_ui_node_t *node;
    if (entry->free_nodes == NULL) {
        chunk = (pxa_ui_node_chunk_t *)pxa_ui_alloc(service, sizeof(*chunk));
        if (chunk == NULL) return NULL;
        memset(chunk, 0, sizeof(*chunk));
        chunk->next = entry->chunks;
        entry->chunks = chunk;
        add_free_chunk(entry, chunk);
    }
    node = entry->free_nodes;
    entry->free_nodes = node->hash_next;
    node->hash_next = NULL;
    --entry->free_node_count;
    ++node_chunk(node)->used;
    return node;
}

pxa_status_t pxa_ui_registry_reserve(pxa_ui_service_t *service,
                                        pxa_ui_entry_t *entry,
                                        size_t additional_nodes) {
    pxa_ui_node_chunk_t *chunk;
    pxa_ui_node_chunk_t *pending_chunks = NULL;
    size_t chunks_needed = 0;
    size_t required_nodes;
    size_t required_buckets;
    pxa_status_t status;
    if (service == NULL || entry == NULL ||
        additional_nodes > SIZE_MAX - entry->node_count)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (additional_nodes > entry->free_node_count) {
        size_t missing = additional_nodes - entry->free_node_count;
        chunks_needed = missing / PXA_UI_NODE_CHUNK_COUNT;
        if (missing % PXA_UI_NODE_CHUNK_COUNT != 0) ++chunks_needed;
    }
    while (chunks_needed != 0) {
        chunk = (pxa_ui_node_chunk_t *)pxa_ui_alloc(service,
                                                           sizeof(*chunk));
        if (chunk == NULL) {
            status = PXA_STATUS_RESOURCE_LIMIT;
            goto fail;
        }
        memset(chunk, 0, sizeof(*chunk));
        chunk->next = pending_chunks;
        pending_chunks = chunk;
        --chunks_needed;
    }
    required_nodes = entry->node_count + additional_nodes;
    required_buckets = entry->bucket_count;
    while (registry_capacity_insufficient(required_nodes, required_buckets)) {
        if (required_buckets > SIZE_MAX / 2u)
            goto resource_limit;
        required_buckets *= 2u;
    }
    if (required_buckets != entry->bucket_count) {
        status = rehash(service, entry, required_buckets);
        if (status != PXA_STATUS_OK) goto fail;
    }
    while (pending_chunks != NULL) {
        chunk = pending_chunks;
        pending_chunks = chunk->next;
        chunk->next = entry->chunks;
        entry->chunks = chunk;
        add_free_chunk(entry, chunk);
    }
    return PXA_STATUS_OK;

resource_limit:
    status = PXA_STATUS_RESOURCE_LIMIT;
fail:
    while (pending_chunks != NULL) {
        chunk = pending_chunks;
        pending_chunks = chunk->next;
        pxa_ui_free(service, chunk);
    }
    return status;
}

pxa_status_t pxa_ui_registry_insert(pxa_ui_service_t *service,
                                       pxa_ui_entry_t *entry,
                                       const pxa_ui_node_t *value,
                                       pxa_ui_node_t **output) {
    pxa_ui_node_t *node;
    pxa_ui_node_t *parent = NULL;
    size_t bucket;
    pxa_status_t status;
    uint8_t chunk_index;
    if (output != NULL) *output = NULL;
    if (service == NULL || entry == NULL || value == NULL || value->id == 0 ||
        value->surface == 0 || value->parent == value->id ||
        entry->node_count == SIZE_MAX ||
        pxa_ui_registry_find(entry, value->surface, value->id) != NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (value->parent != 0) {
        parent = pxa_ui_registry_find(entry, value->surface, value->parent);
        if (parent == NULL) return PXA_STATUS_NOT_FOUND;
    }
    if (registry_capacity_insufficient(entry->node_count + 1u,
                                       entry->bucket_count)) {
        if (entry->bucket_count > SIZE_MAX / 2u)
            return PXA_STATUS_RESOURCE_LIMIT;
        status = rehash(service, entry, entry->bucket_count * 2u);
        if (status != PXA_STATUS_OK) return status;
    }
    node = allocate_node(service, entry);
    if (node == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    chunk_index = node->chunk_index;
    *node = *value;
    node->chunk_index = chunk_index;
    node->occupied = 1;
    if (parent != NULL) {
        node->next_sibling = parent->first_child;
        parent->first_child = node->id;
    }
    bucket = node_hash(node->surface, node->id, entry->bucket_count);
    node->hash_next = entry->buckets[bucket];
    entry->buckets[bucket] = node;
    ++entry->node_count;
    if (output != NULL) *output = node;
    return PXA_STATUS_OK;
}

static pxa_status_t unlink_from_parent(pxa_ui_entry_t *entry,
                                      pxa_ui_node_t *node) {
    pxa_ui_node_t *parent;
    pxa_ui_node_t *previous = NULL;
    uint32_t child_id;
    if (node->parent == 0) {
        node->next_sibling = 0;
        return PXA_STATUS_OK;
    }
    parent = pxa_ui_registry_find(entry, node->surface, node->parent);
    if (parent == NULL) return PXA_STATUS_INTERNAL;
    child_id = parent->first_child;
    while (child_id != 0) {
        pxa_ui_node_t *child =
            pxa_ui_registry_find(entry, node->surface, child_id);
        if (child == NULL) return PXA_STATUS_INTERNAL;
        if (child == node) {
            if (previous == NULL)
                parent->first_child = child->next_sibling;
            else
                previous->next_sibling = child->next_sibling;
            child->next_sibling = 0;
            return PXA_STATUS_OK;
        }
        previous = child;
        child_id = child->next_sibling;
    }
    return PXA_STATUS_INTERNAL;
}

pxa_status_t pxa_ui_registry_detach(pxa_ui_entry_t *entry,
                                    pxa_ui_node_t *node) {
    pxa_status_t status;
    if (entry == NULL || node == NULL || !node->occupied)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = unlink_from_parent(entry, node);
    if (status != PXA_STATUS_OK) return status;
    node->parent = 0;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_registry_move(pxa_ui_entry_t *entry,
                                  pxa_ui_node_t *node,
                                  uint32_t parent_id) {
    pxa_ui_node_t *parent;
    pxa_status_t status;
    if (entry == NULL || node == NULL || !node->occupied || parent_id == 0 ||
        parent_id == node->id)
        return PXA_STATUS_INVALID_ARGUMENT;
    parent = pxa_ui_registry_find(entry, node->surface, parent_id);
    if (parent == NULL) return PXA_STATUS_NOT_FOUND;
    if (node->parent == parent_id) return PXA_STATUS_OK;
    status = pxa_ui_registry_detach(entry, node);
    if (status != PXA_STATUS_OK) return status;
    node->parent = parent_id;
    node->next_sibling = parent->first_child;
    parent->first_child = node->id;
    return PXA_STATUS_OK;
}

static void unlink_node(pxa_ui_entry_t *entry, pxa_ui_node_t *node) {
    pxa_ui_node_t **cursor;
    size_t bucket = node_hash(node->surface, node->id, entry->bucket_count);
    for (cursor = &entry->buckets[bucket]; *cursor != NULL;
         cursor = &(*cursor)->hash_next) {
        if (*cursor == node) {
            *cursor = node->hash_next;
            return;
        }
    }
}

pxa_status_t pxa_ui_registry_remove_subtree(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    uint32_t surface, uint32_t node_id) {
    pxa_ui_node_t *root;
    uint32_t pending;
    pxa_status_t status;
    (void)service;
    if (entry == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    root = pxa_ui_registry_find(entry, surface, node_id);
    if (root == NULL) return PXA_STATUS_NOT_FOUND;
    status = unlink_from_parent(entry, root);
    if (status != PXA_STATUS_OK) return status;
    pending = root->id;
    root->next_sibling = 0;
    while (pending != 0) {
        pxa_ui_node_t *node = pxa_ui_registry_find(entry, surface, pending);
        pxa_ui_node_chunk_t *chunk;
        uint32_t child_id;
        uint8_t chunk_index;
        if (node == NULL) return PXA_STATUS_INTERNAL;
        pending = node->next_sibling;
        child_id = node->first_child;
        while (child_id != 0) {
            pxa_ui_node_t *child =
                pxa_ui_registry_find(entry, surface, child_id);
            uint32_t next_child;
            if (child == NULL) return PXA_STATUS_INTERNAL;
            next_child = child->next_sibling;
            child->next_sibling = pending;
            pending = child->id;
            child_id = next_child;
        }
        unlink_node(entry, node);
        chunk = node_chunk(node);
        chunk_index = node->chunk_index;
        memset(node, 0, sizeof(*node));
        node->chunk_index = chunk_index;
        node->hash_next = entry->free_nodes;
        entry->free_nodes = node;
        ++entry->free_node_count;
        --chunk->used;
        --entry->node_count;
    }
    return PXA_STATUS_OK;
}
