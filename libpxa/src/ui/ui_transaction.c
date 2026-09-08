#include "ui/ui_internal.h"

#include <limits.h>
#include <string.h>

typedef struct {
    uint32_t id;
    uint32_t parent;
    uint64_t event_mask;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    void *backend_handle;
} virtual_node_t;

static size_t change_hash(uint32_t node, size_t bucket_count) {
    uint32_t value = node;
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    return (size_t)value & (bucket_count - 1u);
}

static pxa_status_t change_rehash(pxa_ui_service_t *service,
                                  pxa_ui_transaction_t *transaction,
                                  size_t bucket_count) {
    pxa_ui_change_t **buckets;
    pxa_ui_change_t *change;
    size_t bytes;
    if (bucket_count < PXA_UI_INITIAL_BUCKETS ||
        (bucket_count & (bucket_count - 1u)) != 0 ||
        bucket_count > SIZE_MAX / sizeof(*buckets))
        return PXA_STATUS_RESOURCE_LIMIT;
    bytes = bucket_count * sizeof(*buckets);
    buckets = (pxa_ui_change_t **)pxa_ui_alloc(service, bytes);
    if (buckets == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(buckets, 0, bytes);
    for (change = transaction->changes; change != NULL; change = change->next) {
        size_t bucket = change_hash(change->node, bucket_count);
        change->hash_next = buckets[bucket];
        buckets[bucket] = change;
    }
    pxa_ui_free(service, transaction->change_buckets);
    transaction->change_buckets = buckets;
    transaction->change_bucket_count = bucket_count;
    return PXA_STATUS_OK;
}

static pxa_ui_change_t *find_change(pxa_ui_transaction_t *transaction,
                                       uint32_t node) {
    pxa_ui_change_t *change;
    size_t bucket;
    if (transaction->change_buckets == NULL || node == 0) return NULL;
    bucket = change_hash(node, transaction->change_bucket_count);
    for (change = transaction->change_buckets[bucket]; change != NULL;
         change = change->hash_next)
        if (change->node == node) return change;
    return NULL;
}

static int virtual_find(const pxa_ui_entry_t *entry,
                        pxa_ui_transaction_t *transaction, uint32_t node,
                        virtual_node_t *output) {
    pxa_ui_change_t *change = find_change(transaction, node);
    const pxa_ui_node_t *active = NULL;
    if (node == 0 || output == NULL) return 0;
    if (change != NULL && (change->flags & PXA_UI_CHANGE_REMOVED) != 0)
        return 0;
    if (transaction->kind == PXA_UI_PATCH)
        active = pxa_ui_registry_find_const(entry, transaction->surface,
                                                node);
    if (change == NULL && active == NULL) return 0;
    if (change != NULL && (change->flags & PXA_UI_CHANGE_CREATED) != 0) {
        output->id = change->node;
        output->parent = change->parent;
        output->event_mask = change->event_mask;
        output->type = change->type;
        output->subtype = change->subtype;
        output->backend_handle = change->backend_handle;
        return 1;
    }
    if (active == NULL) return 0;
    output->id = active->id;
    output->parent = change != NULL &&
                             (change->flags & PXA_UI_CHANGE_MOVED) != 0
                         ? change->parent : active->parent;
    output->event_mask =
        change != NULL && (change->flags & PXA_UI_CHANGE_EVENT_MASK) != 0
            ? change->event_mask : active->event_mask;
    output->type = active->type;
    output->subtype = active->subtype;
    output->backend_handle = active->backend_handle;
    return 1;
}

static pxa_ui_change_t *get_change(pxa_ui_service_t *service,
                                      pxa_ui_entry_t *entry,
                                      uint32_t node, int create) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    pxa_ui_change_t *change = find_change(transaction, node);
    const pxa_ui_node_t *active;
    size_t bucket;
    pxa_status_t status;
    if (change != NULL || !create) return change;
    if (transaction->change_bucket_count == 0) {
        status = change_rehash(service, transaction,
                               PXA_UI_INITIAL_BUCKETS);
        if (status != PXA_STATUS_OK) return NULL;
    } else if (transaction->change_count >=
               transaction->change_bucket_count -
                   transaction->change_bucket_count / 4u) {
        if (transaction->change_bucket_count > SIZE_MAX / 2u) return NULL;
        status = change_rehash(service, transaction,
                               transaction->change_bucket_count * 2u);
        if (status != PXA_STATUS_OK) return NULL;
    }
    change = (pxa_ui_change_t *)pxa_ui_alloc(service, sizeof(*change));
    if (change == NULL) return NULL;
    memset(change, 0, sizeof(*change));
    change->node = node;
    change->surface = transaction->surface;
    active = pxa_ui_registry_find_const(entry, transaction->surface, node);
    if (active != NULL) {
        change->parent = active->parent;
        change->event_mask = active->event_mask;
        change->backend_handle = active->backend_handle;
        change->type = active->type;
        change->subtype = active->subtype;
    }
    if (transaction->changes_tail == NULL)
        transaction->changes = change;
    else
        transaction->changes_tail->next = change;
    transaction->changes_tail = change;
    bucket = change_hash(node, transaction->change_bucket_count);
    change->hash_next = transaction->change_buckets[bucket];
    transaction->change_buckets[bucket] = change;
    ++transaction->change_count;
    return change;
}

void pxa_ui_transaction_reset(pxa_ui_service_t *service,
                                 pxa_ui_transaction_t *transaction) {
    pxa_ui_change_t *change;
    pxa_ui_change_t *next;
    if (service == NULL || transaction == NULL) return;
    for (change = transaction->changes; change != NULL; change = next) {
        next = change->next;
        pxa_ui_free(service, change);
    }
    pxa_ui_free(service, transaction->change_buckets);
    pxa_ui_free(service, transaction->bytes);
    memset(transaction, 0, sizeof(*transaction));
}

static int creates_cycle(const pxa_ui_entry_t *entry,
                         pxa_ui_transaction_t *transaction,
                         uint32_t node, uint32_t parent) {
    size_t remaining = entry->node_count + transaction->change_count + 1u;
    while (parent != 0 && remaining-- != 0) {
        virtual_node_t value;
        if (parent == node) return 1;
        if (!virtual_find(entry, transaction, parent, &value)) return 0;
        parent = value.parent;
    }
    return parent != 0;
}

static int is_descendant(const pxa_ui_entry_t *entry,
                         pxa_ui_transaction_t *transaction,
                         uint32_t node, uint32_t ancestor) {
    size_t remaining = entry->node_count + transaction->change_count + 1u;
    while (node != 0 && remaining-- != 0) {
        virtual_node_t value;
        if (node == ancestor) return 1;
        if (!virtual_find(entry, transaction, node, &value)) return 0;
        node = value.parent;
    }
    return 0;
}

static pxa_status_t mark_removed(pxa_ui_service_t *service,
                                 pxa_ui_entry_t *entry, uint32_t node) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    pxa_ui_node_chunk_t *chunk;
    pxa_ui_change_t *change;
    uint32_t index;
    change = get_change(service, entry, node, 1);
    if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    change->flags |= PXA_UI_CHANGE_REMOVED;
    for (chunk = entry->chunks; chunk != NULL; chunk = chunk->next) {
        for (index = 0; index < PXA_UI_NODE_CHUNK_COUNT; ++index) {
            pxa_ui_node_t *candidate = &chunk->nodes[index];
            if (candidate->occupied && candidate->surface == transaction->surface &&
                candidate->id != node &&
                is_descendant(entry, transaction, candidate->id, node)) {
                change = get_change(service, entry, candidate->id, 1);
                if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
                change->flags |= PXA_UI_CHANGE_REMOVED;
            }
        }
    }
    for (change = transaction->changes; change != NULL; change = change->next) {
        if (change->node != node &&
            (change->flags & PXA_UI_CHANGE_CREATED) != 0 &&
            is_descendant(entry, transaction, change->node, node))
            change->flags |= PXA_UI_CHANGE_REMOVED;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t validate_before(const pxa_ui_entry_t *entry,
                                    pxa_ui_transaction_t *transaction,
                                    uint32_t parent, uint32_t before) {
    virtual_node_t sibling;
    if (before == 0) return PXA_STATUS_OK;
    if (!virtual_find(entry, transaction, before, &sibling)) {
        const pxa_ui_node_t *active = pxa_ui_registry_find_const(
            entry, transaction->surface, before);
        if (transaction->kind != PXA_UI_REPLACE_SUBTREE || active == NULL)
            return PXA_STATUS_NOT_FOUND;
        sibling.id = active->id;
        sibling.parent = active->parent;
    }
    return sibling.parent == parent ? PXA_STATUS_OK
                                    : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t validate_create(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    const uint8_t *payload, size_t size) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    virtual_node_t existing;
    virtual_node_t parent_value;
    uint32_t node;
    uint32_t parent;
    uint32_t before;
    uint8_t type;
    uint8_t subtype;
    pxa_ui_change_t *change;
    pxa_status_t status;
    if (size != 16 || payload[14] != 0 || payload[15] != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    node = pxa_read_u32(payload);
    parent = pxa_read_u32(payload + 4);
    before = pxa_read_u32(payload + 8);
    type = payload[12];
    subtype = payload[13];
    if (node == 0 || !pxa_ui_node_type_valid(type, subtype,
                                                service->config.features) ||
        virtual_find(entry, transaction, node, &existing) ||
        (transaction->kind == PXA_UI_PATCH &&
         pxa_ui_registry_find_const(entry, transaction->surface, node) !=
             NULL))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (type == PXA_UI_NODE_ROOT) {
        if (parent != 0 || before != 0) return PXA_STATUS_INVALID_ARGUMENT;
    } else if (parent == 0) {
        return PXA_STATUS_NOT_FOUND;
    } else if (!virtual_find(entry, transaction, parent, &parent_value)) {
        const pxa_ui_node_t *active_parent =
            pxa_ui_registry_find_const(entry, transaction->surface, parent);
        if (transaction->kind != PXA_UI_REPLACE_SUBTREE ||
            node != transaction->node || active_parent == NULL)
            return PXA_STATUS_NOT_FOUND;
    }
    status = validate_before(entry, transaction, parent, before);
    if (status != PXA_STATUS_OK) return status;
    change = get_change(service, entry, node, 1);
    if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    change->parent = parent;
    change->type = type;
    change->subtype = subtype;
    change->flags = PXA_UI_CHANGE_CREATED;
    return PXA_STATUS_OK;
}

static pxa_status_t validate_set(pxa_ui_service_t *service,
                                 pxa_ui_entry_t *entry,
                                 const uint8_t *payload, size_t size) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    virtual_node_t node;
    uint32_t id;
    uint16_t property;
    pxa_bytes_t value;
    pxa_status_t status;
    if (size < 6) return PXA_STATUS_INVALID_ARGUMENT;
    id = pxa_read_u32(payload);
    property = pxa_read_u16(payload + 4);
    if (!virtual_find(entry, transaction, id, &node))
        return PXA_STATUS_NOT_FOUND;
    value.data = payload + 6;
    value.size = size - 6u;
    status = pxa_ui_validate_property(service->config.features, property,
                                         node.type, node.subtype, value);
    if (status == PXA_STATUS_OK && property == PXA_UI_PROPERTY_EVENT_MASK) {
        pxa_ui_change_t *change = get_change(service, entry, id, 1);
        if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        change->event_mask = pxa_read_u64(value.data);
        change->flags |= PXA_UI_CHANGE_EVENT_MASK;
    }
    return status;
}

static pxa_status_t validate_clear(pxa_ui_service_t *service,
                                   pxa_ui_entry_t *entry,
                                   const uint8_t *payload, size_t size) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    virtual_node_t node;
    uint32_t id;
    uint16_t property;
    pxa_status_t status;
    if (size != 6) return PXA_STATUS_INVALID_ARGUMENT;
    id = pxa_read_u32(payload);
    property = pxa_read_u16(payload + 4);
    if (!virtual_find(entry, transaction, id, &node))
        return PXA_STATUS_NOT_FOUND;
    status = pxa_ui_validate_clear_property(
        service->config.features, property, node.type, node.subtype);
    if (status == PXA_STATUS_OK && property == PXA_UI_PROPERTY_EVENT_MASK) {
        pxa_ui_change_t *change = get_change(service, entry, id, 1);
        if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        change->event_mask = 0;
        change->flags |= PXA_UI_CHANGE_EVENT_MASK;
    }
    return status;
}

static pxa_status_t validate_move(pxa_ui_service_t *service,
                                  pxa_ui_entry_t *entry,
                                  const uint8_t *payload, size_t size) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    virtual_node_t node_value;
    virtual_node_t parent_value;
    uint32_t node;
    uint32_t parent;
    uint32_t before;
    pxa_ui_change_t *change;
    pxa_status_t status;
    if (transaction->kind != PXA_UI_PATCH || size != 12)
        return PXA_STATUS_INVALID_ARGUMENT;
    node = pxa_read_u32(payload);
    parent = pxa_read_u32(payload + 4);
    before = pxa_read_u32(payload + 8);
    if (!virtual_find(entry, transaction, node, &node_value) ||
        node_value.type == PXA_UI_NODE_ROOT ||
        !virtual_find(entry, transaction, parent, &parent_value) ||
        creates_cycle(entry, transaction, node, parent))
        return PXA_STATUS_INVALID_ARGUMENT;
    status = validate_before(entry, transaction, parent, before);
    if (status != PXA_STATUS_OK) return status;
    change = get_change(service, entry, node, 1);
    if (change == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    change->parent = parent;
    change->flags |= PXA_UI_CHANGE_MOVED;
    return PXA_STATUS_OK;
}

static pxa_status_t validate_remove(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    const uint8_t *payload, size_t size) {
    virtual_node_t node;
    uint32_t id;
    if (entry->transaction.kind != PXA_UI_PATCH || size != 4)
        return PXA_STATUS_INVALID_ARGUMENT;
    id = pxa_read_u32(payload);
    if (!virtual_find(entry, &entry->transaction, id, &node))
        return PXA_STATUS_NOT_FOUND;
    if (node.type == PXA_UI_NODE_ROOT)
        return PXA_STATUS_INVALID_ARGUMENT;
    return mark_removed(service, entry, id);
}

typedef pxa_status_t (*command_fn)(pxa_ui_service_t *, pxa_ui_entry_t *,
                                  uint8_t, uint8_t, const uint8_t *, size_t,
                                  void *);

static pxa_status_t iterate_commands(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry,
                                     command_fn callback, void *context) {
    const uint8_t *bytes = entry->transaction.bytes;
    size_t size = entry->transaction.size;
    size_t offset = 0;
    size_t commands = 0;
    while (offset < size) {
        uint8_t command;
        uint8_t flags;
        uint16_t length;
        pxa_status_t status;
        if (size - offset < 4) return PXA_STATUS_INVALID_ARGUMENT;
        command = bytes[offset];
        flags = bytes[offset + 1u];
        length = pxa_read_u16(bytes + offset + 2u);
        offset += 4;
        if ((flags & ~PXA_UI_COMMAND_OPTIONAL) != 0 || length > size - offset)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (command < PXA_UI_COMMAND_CREATE ||
            command > PXA_UI_COMMAND_REMOVE) {
            if ((flags & PXA_UI_COMMAND_OPTIONAL) == 0)
                return PXA_STATUS_UNSUPPORTED;
        } else {
            status = callback(service, entry, command, flags, bytes + offset,
                              length, context);
            if (status != PXA_STATUS_OK) return status;
        }
        offset += length;
        ++commands;
    }
    return commands != 0 ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t validate_command(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry,
                                     uint8_t command, uint8_t flags,
                                     const uint8_t *payload, size_t size,
                                     void *context) {
    (void)flags;
    (void)context;
    switch (command) {
        case PXA_UI_COMMAND_CREATE:
            return validate_create(service, entry, payload, size);
        case PXA_UI_COMMAND_SET_PROPERTY:
            return validate_set(service, entry, payload, size);
        case PXA_UI_COMMAND_CLEAR_PROPERTY:
            return validate_clear(service, entry, payload, size);
        case PXA_UI_COMMAND_MOVE:
            return validate_move(service, entry, payload, size);
        case PXA_UI_COMMAND_REMOVE:
            return validate_remove(service, entry, payload, size);
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

static pxa_status_t validate_replacement(const pxa_ui_entry_t *entry) {
    const pxa_ui_transaction_t *transaction = &entry->transaction;
    const pxa_ui_change_t *change;
    const pxa_ui_node_t *target = NULL;
    size_t roots = 0;
    uint32_t expected_parent = 0;
    if (transaction->kind == PXA_UI_REPLACE_SUBTREE) {
        target = pxa_ui_registry_find_const(entry, transaction->surface,
                                               transaction->node);
        if (target == NULL) return PXA_STATUS_NOT_FOUND;
        expected_parent = target->parent;
    }
    for (change = transaction->changes; change != NULL; change = change->next) {
        if ((change->flags & PXA_UI_CHANGE_CREATED) == 0 ||
            (change->flags & PXA_UI_CHANGE_REMOVED) != 0)
            continue;
        if (change->type == PXA_UI_NODE_ROOT ||
            change->node == transaction->node) {
            ++roots;
            if (transaction->kind == PXA_UI_REPLACE_SURFACE) {
                if (change->type != PXA_UI_NODE_ROOT || change->parent != 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
            } else if (change->node != transaction->node ||
                       change->parent != expected_parent ||
                       change->type == PXA_UI_NODE_ROOT) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    return roots == 1 ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t backend_command(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    uint8_t opcode, uint8_t flags,
                                    const uint8_t *payload, size_t size,
                                    void *context) {
    pxa_ui_command_view_t command;
    virtual_node_t node;
    virtual_node_t parent;
    virtual_node_t before;
    void *created = NULL;
    pxa_status_t status;
    (void)service;
    memset(&command, 0, sizeof(command));
    command.command = opcode;
    command.flags = flags;
    command.node = pxa_read_u32(payload);
    if (opcode == PXA_UI_COMMAND_CREATE) {
        command.parent = pxa_read_u32(payload + 4);
        command.before = pxa_read_u32(payload + 8);
        command.type = payload[12];
        command.subtype = payload[13];
    } else if (opcode == PXA_UI_COMMAND_SET_PROPERTY) {
        command.property = pxa_read_u16(payload + 4);
        command.value.data = payload + 6;
        command.value.size = size - 6u;
    } else if (opcode == PXA_UI_COMMAND_CLEAR_PROPERTY) {
        command.property = pxa_read_u16(payload + 4);
    } else if (opcode == PXA_UI_COMMAND_MOVE) {
        command.parent = pxa_read_u32(payload + 4);
        command.before = pxa_read_u32(payload + 8);
    }
    if (opcode != PXA_UI_COMMAND_CREATE) {
        pxa_ui_change_t *change = find_change(&entry->transaction,
                                                 command.node);
        const pxa_ui_node_t *active = pxa_ui_registry_find_const(
            entry, entry->transaction.surface, command.node);
        if (change != NULL &&
            (change->flags & PXA_UI_CHANGE_CREATED) != 0)
            command.node_handle = change->backend_handle;
        else if (active != NULL)
            command.node_handle = active->backend_handle;
        else if (virtual_find(entry, &entry->transaction, command.node, &node))
            command.node_handle = node.backend_handle;
    }
    if (command.parent != 0 &&
        virtual_find(entry, &entry->transaction, command.parent, &parent))
        command.parent_handle = parent.backend_handle;
    else if (command.parent != 0) {
        const pxa_ui_node_t *active = pxa_ui_registry_find_const(
            entry, entry->transaction.surface, command.parent);
        if (active != NULL) command.parent_handle = active->backend_handle;
    }
    if (command.before != 0 &&
        virtual_find(entry, &entry->transaction, command.before, &before))
        command.before_handle = before.backend_handle;
    else if (command.before != 0) {
        const pxa_ui_node_t *active = pxa_ui_registry_find_const(
            entry, entry->transaction.surface, command.before);
        if (active != NULL) command.before_handle = active->backend_handle;
    }
    status = entry->backend.apply(entry->backend.context, context, &command,
                                  &created);
    if (status == PXA_STATUS_OK && opcode == PXA_UI_COMMAND_CREATE) {
        pxa_ui_change_t *change = find_change(&entry->transaction,
                                                 command.node);
        if (change == NULL) return PXA_STATUS_INTERNAL;
        change->backend_handle = created;
    }
    return status;
}

static pxa_ui_node_t *surface_root(pxa_ui_entry_t *entry,
                                      uint32_t surface) {
    pxa_ui_node_chunk_t *chunk;
    uint32_t index;
    for (chunk = entry->chunks; chunk != NULL; chunk = chunk->next)
        for (index = 0; index < PXA_UI_NODE_CHUNK_COUNT; ++index) {
            pxa_ui_node_t *node = &chunk->nodes[index];
            if (node->occupied && node->surface == surface &&
                node->type == PXA_UI_NODE_ROOT && node->parent == 0)
                return node;
        }
    return NULL;
}

static pxa_status_t publish_registry(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    pxa_ui_change_t *change;
    pxa_status_t status;
    for (change = transaction->changes; change != NULL; change = change->next) {
        pxa_ui_node_t *active;
        if ((change->flags & PXA_UI_CHANGE_MOVED) == 0 ||
            (change->flags & (PXA_UI_CHANGE_CREATED | PXA_UI_CHANGE_REMOVED)) !=
                0)
            continue;
        active = pxa_ui_registry_find(entry, transaction->surface,
                                      change->node);
        if (active != NULL && active->parent != change->parent) {
            status = pxa_ui_registry_detach(entry, active);
            if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
        }
    }
    if (transaction->kind == PXA_UI_REPLACE_SURFACE) {
        pxa_ui_node_t *root = surface_root(entry, transaction->surface);
        if (root != NULL)
            (void)pxa_ui_registry_remove_subtree(
                service, entry, transaction->surface, root->id);
    } else if (transaction->kind == PXA_UI_REPLACE_SUBTREE) {
        status = pxa_ui_registry_remove_subtree(
            service, entry, transaction->surface, transaction->node);
        if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    } else {
        for (change = transaction->changes; change != NULL;
             change = change->next) {
            if ((change->flags & PXA_UI_CHANGE_REMOVED) != 0 &&
                (change->flags & PXA_UI_CHANGE_CREATED) == 0 &&
                pxa_ui_registry_find(entry, transaction->surface,
                                        change->node) != NULL) {
                status = pxa_ui_registry_remove_subtree(
                    service, entry, transaction->surface, change->node);
                if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
            }
        }
    }
    for (change = transaction->changes; change != NULL; change = change->next) {
        pxa_ui_node_t *active;
        if ((change->flags & PXA_UI_CHANGE_CREATED) != 0) {
            pxa_ui_node_t value;
            if ((change->flags & PXA_UI_CHANGE_REMOVED) != 0) continue;
            memset(&value, 0, sizeof(value));
            value.id = change->node;
            value.surface = transaction->surface;
            value.parent = change->parent;
            value.event_mask = change->event_mask;
            value.backend_handle = change->backend_handle;
            value.type = change->type;
            value.subtype = change->subtype;
            status = pxa_ui_registry_insert(service, entry, &value, NULL);
            if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
            continue;
        }
        active = pxa_ui_registry_find(entry, transaction->surface,
                                         change->node);
        if (active == NULL || (change->flags & PXA_UI_CHANGE_REMOVED) != 0)
            continue;
        if ((change->flags & PXA_UI_CHANGE_MOVED) != 0) {
            status = pxa_ui_registry_move(entry, active, change->parent);
            if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
        }
        if ((change->flags & PXA_UI_CHANGE_EVENT_MASK) != 0)
            active->event_mask = change->event_mask;
    }
    {
        pxa_ui_surface_t *surface =
            pxa_ui_find_surface(entry, transaction->surface);
        if (surface == NULL) return PXA_STATUS_INTERNAL;
        surface->generation = transaction->generation;
        surface->environment.surface = surface->id;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t finish_commit(pxa_ui_service_t *service,
                                  pxa_ui_entry_t *entry,
                                  uint64_t started_us,
                                  pxa_status_t status) {
    ++entry->commit_count;
    if (service->config.now_us != NULL) {
        uint64_t finished_us = service->config.now_us(
            service->config.clock_context);
        entry->last_commit_us = finished_us >= started_us
                                    ? finished_us - started_us : 0;
        if (entry->last_commit_us > entry->max_commit_us)
            entry->max_commit_us = entry->last_commit_us;
    }
    return status;
}

pxa_status_t pxa_ui_transaction_commit(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry) {
    pxa_ui_transaction_t *transaction;
    pxa_ui_transaction_info_t info;
    pxa_ui_change_t *change;
    void *backend_transaction = NULL;
    size_t creates = 0;
    uint64_t started_us = 0;
    pxa_status_t status;
    if (service == NULL || entry == NULL || !entry->transaction.active)
        return PXA_STATUS_BAD_STATE;
    if (service->config.now_us != NULL)
        started_us = service->config.now_us(service->config.clock_context);
    transaction = &entry->transaction;
    status = iterate_commands(service, entry, validate_command, NULL);
    if (status == PXA_STATUS_OK && transaction->kind != PXA_UI_PATCH)
        status = validate_replacement(entry);
    if (status != PXA_STATUS_OK)
        return finish_commit(service, entry, started_us, status);
    for (change = transaction->changes; change != NULL; change = change->next)
        if ((change->flags & PXA_UI_CHANGE_CREATED) != 0 &&
            (change->flags & PXA_UI_CHANGE_REMOVED) == 0)
            ++creates;
    status = pxa_ui_registry_reserve(service, entry, creates);
    if (status != PXA_STATUS_OK)
        return finish_commit(service, entry, started_us, status);
    memset(&info, 0, sizeof(info));
    info.surface = transaction->surface;
    info.transaction = transaction->id;
    info.generation = transaction->generation;
    info.target = transaction->node;
    info.kind = transaction->kind;
    info.flags = transaction->flags;
    if (transaction->kind == PXA_UI_REPLACE_SUBTREE) {
        const pxa_ui_node_t *target = pxa_ui_registry_find_const(
            entry, transaction->surface, transaction->node);
        info.target_handle = target == NULL ? NULL : target->backend_handle;
    } else if (transaction->kind == PXA_UI_REPLACE_SURFACE) {
        pxa_ui_node_t *root = surface_root(entry, transaction->surface);
        info.target_handle = root == NULL ? NULL : root->backend_handle;
    }
    status = entry->backend.begin(entry->backend.context, &info,
                                  &backend_transaction);
    if (status == PXA_STATUS_OK)
        status = iterate_commands(service, entry, backend_command,
                                  backend_transaction);
    if (status == PXA_STATUS_OK)
        status = entry->backend.commit(entry->backend.context,
                                       backend_transaction);
    if (status != PXA_STATUS_OK) {
        if (backend_transaction != NULL)
            entry->backend.cancel(entry->backend.context, backend_transaction);
        return finish_commit(service, entry, started_us, status);
    }
    status = publish_registry(service, entry);
    if (status == PXA_STATUS_OK) pxa_ui_canvas_prune(service, entry);
    return finish_commit(service, entry, started_us, status);
}
