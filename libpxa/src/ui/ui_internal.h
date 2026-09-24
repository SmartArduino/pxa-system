#ifndef PXA_UI_INTERNAL_H
#define PXA_UI_INTERNAL_H

#include "pxa/ui.h"

#define PXA_UI_MAGIC UINT32_C(0x50585532)
#define PXA_UI_NODE_CHUNK_COUNT 32u
#define PXA_UI_INITIAL_BUCKETS 16u
#define PXA_UI_NODES_PER_BUCKET 2u
#define PXA_UI_MAX_DIRTY_RECTS 4u
#define PXA_UI_ENVIRONMENT_WIRE_BYTES                                      \
    (5u * (PXA_RECORD_HEADER_SIZE + 4u) +                                 \
     (PXA_RECORD_HEADER_SIZE + 16u) +                                     \
     2u * (PXA_RECORD_HEADER_SIZE + 1u) +                                 \
     2u * (PXA_RECORD_HEADER_SIZE + 8u) +                                 \
     (PXA_RECORD_HEADER_SIZE + 4u) +                                     \
     (PXA_RECORD_HEADER_SIZE + 20u))
#define PXA_UI_SURFACE_READY_PREFIX_BYTES 12u
#define PXA_UI_SURFACE_READY_PAYLOAD_BYTES                                \
    (PXA_UI_SURFACE_READY_PREFIX_BYTES + PXA_UI_ENVIRONMENT_WIRE_BYTES)

typedef struct pxa_ui_alloc_header {
    size_t size;
} pxa_ui_alloc_header_t;

typedef struct pxa_ui_node pxa_ui_node_t;
typedef struct pxa_ui_node_chunk pxa_ui_node_chunk_t;
typedef struct pxa_ui_entry pxa_ui_entry_t;
typedef struct pxa_ui_change pxa_ui_change_t;

struct pxa_ui_node {
    uint32_t id;
    uint32_t surface;
    uint32_t parent;
    uint32_t first_child;
    uint32_t next_sibling;
    pxa_ui_node_t *hash_next;
    uint64_t event_mask;
    void *backend_handle;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    uint8_t occupied;
    uint8_t chunk_index;
};

struct pxa_ui_node_chunk {
    pxa_ui_node_chunk_t *next;
    uint32_t base_index;
    uint32_t used;
    pxa_ui_node_t nodes[PXA_UI_NODE_CHUNK_COUNT];
};

typedef struct pxa_ui_surface {
    struct pxa_ui_surface *next;
    uint32_t id;
    uint32_t generation;
    pxa_ui_environment_t environment;
    uint8_t occupied;
} pxa_ui_surface_t;

typedef struct {
    uint32_t id;
    uint32_t surface;
    uint32_t node;
    uint32_t generation;
    pxa_ui_transaction_kind_t kind;
    uint8_t flags;
    uint8_t active;
    uint8_t failed;
    uint8_t *bytes;
    size_t size;
    size_t capacity;
    pxa_ui_change_t *changes;
    pxa_ui_change_t *changes_tail;
    pxa_ui_change_t **change_buckets;
    size_t change_bucket_count;
    size_t change_count;
} pxa_ui_transaction_t;

typedef struct pxa_ui_canvas {
    struct pxa_ui_canvas *next;
    pxa_ui_service_t *service;
    uint8_t *spare_bytes;
    size_t spare_capacity;
    size_t outstanding_frames;
    uint8_t detached;
    uint32_t surface;
    uint32_t node;
    uint64_t identity;
    uint32_t generation;
    uint8_t staging;
    uint8_t *bytes;
    size_t size;
    size_t capacity;
} pxa_ui_canvas_t;

struct pxa_ui_entry {
    pxa_ui_entry_t *next;
    pxa_component_t component;
    pxa_ui_backend_t backend;
    pxa_ui_surface_t primary;
    pxa_ui_surface_t *surfaces;
    uint32_t next_surface;
    size_t surface_count;
    pxa_ui_node_chunk_t *chunks;
    pxa_ui_node_t *free_nodes;
    size_t free_node_count;
    pxa_ui_node_t **buckets;
    size_t bucket_count;
    size_t node_count;
    pxa_ui_transaction_t transaction;
    pxa_ui_canvas_t *canvases;
    pxa_ui_pressure_t pressure;
    uint64_t commit_count;
    uint64_t last_commit_us;
    uint64_t max_commit_us;
};

struct pxa_ui_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_ui_config_t config;
    pxa_ui_theme_snapshot_t theme;
    pxa_ui_entry_t *entries;
    uint64_t next_canvas_identity;
    size_t current_bytes;
    size_t peak_bytes;
    uint8_t registered;
};

#define PXA_UI_CHANGE_CREATED UINT8_C(1)
#define PXA_UI_CHANGE_REMOVED UINT8_C(2)
#define PXA_UI_CHANGE_MOVED UINT8_C(4)
#define PXA_UI_CHANGE_EVENT_MASK UINT8_C(8)

struct pxa_ui_change {
    pxa_ui_change_t *next;
    pxa_ui_change_t *hash_next;
    uint32_t node;
    uint32_t surface;
    uint32_t parent;
    uint64_t event_mask;
    void *backend_handle;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    uint8_t flags;
    uint8_t reserved;
};

void *pxa_ui_alloc(pxa_ui_service_t *service, size_t size);
void pxa_ui_free(pxa_ui_service_t *service, void *memory);
void *pxa_ui_grow(pxa_ui_service_t *service, void *memory,
                     size_t used, size_t *capacity, size_t required,
                     size_t maximum);

pxa_ui_entry_t *pxa_ui_find_entry(pxa_ui_service_t *service,
                                        pxa_component_t component);
const pxa_ui_entry_t *pxa_ui_find_entry_const(
    const pxa_ui_service_t *service, pxa_component_t component);
pxa_ui_surface_t *pxa_ui_find_surface(pxa_ui_entry_t *entry,
                                            uint32_t surface);
const pxa_ui_surface_t *pxa_ui_find_surface_const(
    const pxa_ui_entry_t *entry, uint32_t surface);

pxa_status_t pxa_ui_registry_init(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry);
void pxa_ui_registry_clear(pxa_ui_service_t *service,
                              pxa_ui_entry_t *entry);
pxa_ui_node_t *pxa_ui_registry_find(pxa_ui_entry_t *entry,
                                          uint32_t surface, uint32_t node);
const pxa_ui_node_t *pxa_ui_registry_find_const(
    const pxa_ui_entry_t *entry, uint32_t surface, uint32_t node);
pxa_status_t pxa_ui_registry_insert(pxa_ui_service_t *service,
                                       pxa_ui_entry_t *entry,
                                       const pxa_ui_node_t *node,
                                       pxa_ui_node_t **output);
pxa_status_t pxa_ui_registry_move(pxa_ui_entry_t *entry,
                                     pxa_ui_node_t *node,
                                     uint32_t parent);
pxa_status_t pxa_ui_registry_detach(pxa_ui_entry_t *entry,
                                       pxa_ui_node_t *node);
pxa_status_t pxa_ui_registry_reserve(pxa_ui_service_t *service,
                                        pxa_ui_entry_t *entry,
                                        size_t additional_nodes);
pxa_status_t pxa_ui_registry_remove_subtree(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    uint32_t surface, uint32_t node);

int pxa_ui_node_type_valid(uint8_t type, uint8_t subtype,
                              pxa_ui_features_t features);
pxa_status_t pxa_ui_validate_property(
    pxa_ui_features_t features, uint16_t property,
    pxa_ui_node_type_t node_type, pxa_ui_control_type_t subtype,
    pxa_bytes_t value);
pxa_status_t pxa_ui_validate_clear_property(
    pxa_ui_features_t features, uint16_t property,
    pxa_ui_node_type_t node_type, pxa_ui_control_type_t subtype);
pxa_status_t pxa_ui_validate_canvas(pxa_ui_features_t features,
                                       const uint8_t *data, size_t size);

void pxa_ui_transaction_reset(pxa_ui_service_t *service,
                                 pxa_ui_transaction_t *transaction);
pxa_status_t pxa_ui_transaction_commit(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry);
pxa_status_t pxa_ui_canvas_begin_frame(pxa_ui_service_t *service,
                                          pxa_ui_entry_t *entry,
                                          const pxa_message_view_t *message);
pxa_status_t pxa_ui_canvas_write_frame(pxa_ui_service_t *service,
                                          pxa_ui_entry_t *entry,
                                          const pxa_message_view_t *message);
pxa_status_t pxa_ui_canvas_present_frame(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    const pxa_message_view_t *message);
pxa_status_t pxa_ui_canvas_open_stream(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    const pxa_message_view_t *message);
void pxa_ui_canvas_clear(pxa_ui_service_t *service,
                            pxa_ui_entry_t *entry);
void pxa_ui_canvas_prune(pxa_ui_service_t *service,
                            pxa_ui_entry_t *entry);
void pxa_ui_canvas_clear_surface(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    uint32_t surface);
pxa_status_t pxa_ui_surface_open(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    const pxa_message_view_t *message);
pxa_status_t pxa_ui_surface_close(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry,
                                     const pxa_message_view_t *message);

#endif
