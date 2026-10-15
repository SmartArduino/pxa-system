#include "pxa/lease.h"
#include "common/checked_math.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_LEASE_MAGIC UINT32_C(0x50584c53)
#define PXA_LEASE_SLOT_NONE UINT16_MAX

typedef uint8_t pxa_lease_state_t;
#define PXA_LEASE_ACTIVE ((pxa_lease_state_t)1)
#define PXA_LEASE_REVOKING ((pxa_lease_state_t)2)
#define PXA_LEASE_PENDING_EVENT ((pxa_lease_state_t)3)
#define PXA_LEASE_WAITING ((pxa_lease_state_t)4)

typedef struct {
    union { struct pxa_lease_service *service; uint32_t identity_low; } owner;
    pxa_component_t component;
    pxa_handle64_t handle;
    union { uint64_t expires_at_ms; pxa_request_token_t token; } value;
    union { pxa_status_t revoke_reason; uint32_t identity_high; } detail;
    uint16_t next;
    pxa_lease_state_t state;
} pxa_lease_entry_t;

/* The four readiness bits share the existing state byte. Split a wait's
 * request identity across two otherwise lease-only fields: a uint64_t in the
 * pointer union would grow every slot on 32-bit targets because of alignment.
 * These helpers keep that storage decision out of scheduling code. */
static int entry_waiting(const pxa_lease_entry_t *entry) {
    return (entry->state & 15u) == PXA_LEASE_WAITING;
}
static uint32_t wait_interests(const pxa_lease_entry_t *entry) {
    return entry->state >> 4;
}
static uint64_t wait_identity(const pxa_lease_entry_t *entry) {
    return ((uint64_t)entry->detail.identity_high << 32) | entry->owner.identity_low;
}
static void set_wait(pxa_lease_entry_t *entry, uint32_t interests, uint64_t identity) {
    entry->state = (pxa_lease_state_t)(PXA_LEASE_WAITING | (interests << 4));
    entry->owner.identity_low = (uint32_t)identity;
    entry->detail.identity_high = (uint32_t)(identity >> 32);
}

struct pxa_lease_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_lease_limits_t limits;
    pxa_lease_entry_t *entries;
    uint16_t free_head;
    uint16_t active_head;
    uint8_t registered;
};

static int service_valid(const pxa_lease_service_t *service) {
    return service != NULL && service->magic == PXA_LEASE_MAGIC;
}

void pxa_lease_limits_init(pxa_lease_limits_t *limits) {
    if (limits == NULL) return;
    memset(limits, 0, sizeof(*limits));
    limits->struct_size = sizeof(*limits);
    limits->allowed_kinds = (UINT32_C(1) << 6) - 1u;
    limits->max_leases_per_component = 2;
    limits->max_leases = 16;
    limits->default_duration_ms = 30000;
    limits->max_duration_ms = 600000;
}

static int limits_valid(const pxa_lease_limits_t *limits) {
    return limits != NULL && limits->struct_size >= sizeof(*limits) &&
           limits->allowed_kinds != 0 &&
           limits->max_leases_per_component != 0 && limits->max_leases != 0 &&
           limits->default_duration_ms != 0 && limits->max_duration_ms != 0 &&
           limits->default_duration_ms <= limits->max_duration_ms;
}

size_t pxa_lease_service_workspace_size(const pxa_lease_limits_t *limits) {
    size_t size;
    if (!limits_valid(limits)) return 0;
    size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    size += sizeof(pxa_lease_service_t);
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    size += (size_t)limits->max_leases * sizeof(pxa_lease_entry_t);
    return size;
}

pxa_status_t pxa_lease_service_init(void *workspace, size_t workspace_size,
                                    pxa_runtime_t *runtime,
                                    const pxa_lease_limits_t *limits,
                                    pxa_lease_service_t **output) {
    uintptr_t cursor;
    uintptr_t end;
    size_t required;
    pxa_lease_service_t *service;
    uint16_t index;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_lease_service_workspace_size(limits);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_lease_service_t *)cursor;
    cursor = pxa_internal_align_pointer(cursor + sizeof(*service), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (cursor > end ||
        (size_t)limits->max_leases * sizeof(pxa_lease_entry_t) > end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(service, 0, sizeof(*service));
    service->runtime = runtime;
    service->limits = *limits;
    service->entries = (pxa_lease_entry_t *)cursor;
    memset(service->entries, 0,
           (size_t)limits->max_leases * sizeof(*service->entries));
    for (index = 0; index < limits->max_leases; ++index) {
        service->entries[index].owner.service = service;
        service->entries[index].next =
            index + 1u < limits->max_leases
                ? (uint16_t)(index + 1u)
                : PXA_LEASE_SLOT_NONE;
    }
    service->free_head = 0;
    service->active_head = PXA_LEASE_SLOT_NONE;
    service->magic = PXA_LEASE_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static void release_entry_at(pxa_lease_service_t *service, uint16_t index,
                             uint16_t previous) {
    pxa_lease_entry_t *entry = &service->entries[index];
    if (previous == PXA_LEASE_SLOT_NONE) {
        service->active_head = entry->next;
    } else {
        service->entries[previous].next = entry->next;
    }
    memset(entry, 0, sizeof(*entry));
    entry->owner.service = service;
    entry->next = service->free_head;
    service->free_head = index;
}

static void release_entry(pxa_lease_service_t *service,
                          pxa_lease_entry_t *entry) {
    uint16_t index = service->active_head;
    uint16_t previous = PXA_LEASE_SLOT_NONE;
    uint16_t target = (uint16_t)(entry - service->entries);
    while (index != PXA_LEASE_SLOT_NONE && index != target) {
        previous = index;
        index = service->entries[index].next;
    }
    if (index == PXA_LEASE_SLOT_NONE) return;
    release_entry_at(service, target, previous);
}

static pxa_lease_entry_t *allocate_entry(pxa_lease_service_t *service) {
    uint16_t index = service->free_head;
    pxa_lease_entry_t *entry;
    if (index == PXA_LEASE_SLOT_NONE) return NULL;
    entry = &service->entries[index];
    service->free_head = entry->next;
    memset(entry, 0, sizeof(*entry));
    entry->owner.service = service;
    entry->next = service->active_head;
    service->active_head = index;
    entry->state = PXA_LEASE_ACTIVE;
    return entry;
}

static void lease_closed(void *context) {
    pxa_lease_entry_t *entry = (pxa_lease_entry_t *)context;
    if (entry->state == PXA_LEASE_REVOKING) {
        entry->state = PXA_LEASE_PENDING_EVENT;
        return;
    }
    release_entry(entry->owner.service, entry);
}

static pxa_status_t parse_request(pxa_bytes_t payload, uint16_t *kind,
                                  uint32_t *duration_ms,
                                  uint8_t *has_duration) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t has_kind = 0;
    pxa_status_t status;
    *kind = 0;
    *duration_ms = 0;
    *has_duration = 0;
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && !has_kind && record.payload.size == 2) {
            *kind = pxa_read_u16(record.payload.data);
            if (*kind < 1 || *kind > 6) return PXA_STATUS_INVALID_ARGUMENT;
            has_kind = 1;
        } else if (record.tag == 2 && !*has_duration &&
                   record.payload.size == 4) {
            *duration_ms = pxa_read_u32(record.payload.data);
            *has_duration = 1;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return has_kind ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static uint16_t active_for_component(const pxa_lease_service_t *service,
                                     pxa_component_t component) {
    uint16_t count = 0;
    uint16_t index = service->active_head;
    while (index != PXA_LEASE_SLOT_NONE) {
        const pxa_lease_entry_t *entry = &service->entries[index];
        if (entry->state == PXA_LEASE_ACTIVE &&
            entry->component == component) {
            count++;
        }
        index = entry->next;
    }
    return count;
}

/* Readiness waits share the existing bounded Core pool with leases. No new
 * per-component table or additional entry storage is required. */
static pxa_status_t wait_control(pxa_lease_service_t *service,
                                 pxa_component_t component,
                                 const pxa_message_view_t *message) {
    pxa_status_t status;
    pxa_lease_entry_t *entry;
    uint32_t interests;
    uint64_t handle;
    int32_t ready;
    uint16_t i, count = 0, profile;
    uint8_t result[4];
    if (!message->request_token || message->payload.size != 12)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_component_wire_profile(service->runtime, component, &profile);
    if (status != PXA_STATUS_OK) return status;
    if (profile != 1) return PXA_STATUS_UNSUPPORTED;
    handle = pxa_read_u64(message->payload.data);
    interests = pxa_read_u32(message->payload.data + 8);
    if (!handle || !interests || (interests & ~UINT32_C(15)))
        return PXA_STATUS_INVALID_ARGUMENT;
    /* Prune cancelled waits before admitting a replacement. */
    i = service->active_head;
    while (i != PXA_LEASE_SLOT_NONE) {
        entry = &service->entries[i];
        i = entry->next;
        if (!entry_waiting(entry)) continue;
        if (pxa_request_identity(service->runtime, entry->component,
                entry->value.token) != wait_identity(entry))
            release_entry(service, entry);
        else if (entry->component == component) ++count;
    }
    if (count >= service->limits.max_leases_per_component)
        return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_request_begin_reserved(service->runtime, component,
        message->request_token, PXA_SERVICE_CORE, PXA_CORE_WAIT_RESOURCE, 0, 4);
    if (status != PXA_STATUS_OK) return status;
    ready = pxa_handle_poll64(service->runtime, component, handle);
    if (ready < 0 || ((uint32_t)ready & (interests | PXA_READY_END))) {
        pxa_write_u32(result, ready < 0 ? 0 : (uint32_t)ready);
        return pxa_request_complete(service->runtime, component,
            message->request_token, ready < 0 ? ready : PXA_STATUS_OK,
            ready < 0 ? NULL : result, ready < 0 ? 0 : 4);
    }
    entry = allocate_entry(service);
    if (!entry)
        return pxa_request_complete(service->runtime, component,
            message->request_token, PXA_STATUS_RESOURCE_LIMIT, NULL, 0);
    entry->component = component;
    entry->handle = handle;
    entry->value.token = message->request_token;
    set_wait(entry, interests, pxa_request_identity(service->runtime, component,
                                                    message->request_token));
    return PXA_STATUS_OK;
}

static pxa_status_t lease_control(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    pxa_lease_service_t *service = (pxa_lease_service_t *)context;
    pxa_lease_entry_t *entry = NULL;
    pxa_status_t result;
    pxa_status_t complete;
    uint16_t kind;
    uint32_t requested_duration;
    uint32_t duration;
    uint8_t has_duration;
    uint64_t now;
    pxa_handle64_t handle = PXA_HANDLE_INVALID;
    uint16_t wire_profile = 0;
    pxa_resource_t resource;
    uint8_t result_record[12];
    (void)runtime;
    if (message->opcode == PXA_CORE_WAIT_RESOURCE)
        return wait_control(service, component, message);
    if (message->opcode != PXA_LEASE_ACQUIRE || message->request_token == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    result = pxa_component_wire_profile(service->runtime, component,
                                      &wire_profile);
    if (result != PXA_STATUS_OK) return result;
    result = pxa_request_begin_reserved(service->runtime, component,
                                        message->request_token,
                                        PXA_SERVICE_CORE,
                                        PXA_LEASE_ACQUIRE, 0,
                                        wire_profile == 1 ? 12u : 8u);
    if (result != PXA_STATUS_OK) return result;
    result = parse_request(message->payload, &kind, &requested_duration,
                           &has_duration);
    duration = has_duration ? requested_duration
                            : service->limits.default_duration_ms;
    if (result == PXA_STATUS_OK &&
        ((service->limits.allowed_kinds & (UINT32_C(1) << (kind - 1u))) == 0 ||
         duration == 0 || duration > service->limits.max_duration_ms)) {
        result = PXA_STATUS_DENIED;
    }
    if (result == PXA_STATUS_OK &&
        active_for_component(service, component) >=
            service->limits.max_leases_per_component) {
        result = PXA_STATUS_RESOURCE_LIMIT;
    }
    now = service->limits.clock != NULL
              ? service->limits.clock(service->limits.clock_context)
              : 0;
    if (result == PXA_STATUS_OK && now > UINT64_MAX - duration) {
        result = PXA_STATUS_RESOURCE_LIMIT;
    }
    if (result == PXA_STATUS_OK)
        result = pxa_request_commit(service->runtime, component,
                                    message->request_token);
    if (result == PXA_STATUS_OK) {
        entry = allocate_entry(service);
        if (entry == NULL) result = PXA_STATUS_RESOURCE_LIMIT;
    }
    if (result == PXA_STATUS_OK) {
        memset(&resource, 0, sizeof(resource));
        resource.context = entry;
        resource.close = lease_closed;
        entry->component = component;
        entry->value.expires_at_ms = now + duration;
        if (wire_profile == 1)
            result = pxa_handle_open64(service->runtime, component,
                                        PXA_RESOURCE_LEASE, 0, &resource,
                                        &handle);
        else {
            pxa_handle_t handle32 = PXA_HANDLE_INVALID;
            result = pxa_handle_open(service->runtime, component,
                                     PXA_RESOURCE_LEASE, 0, &resource,
                                     &handle32);
            handle = handle32;
        }
        if (result == PXA_STATUS_OK) {
            entry->handle = handle;
            pxa_write_u16(result_record, 4);
            pxa_write_u16(result_record + 2, wire_profile == 1 ? 8 : 4);
            if (wire_profile == 1) pxa_write_u64(result_record + 4, handle);
            else pxa_write_u32(result_record + 4, (pxa_handle_t)handle);
        } else {
            release_entry(service, entry);
        }
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_token, result,
        result == PXA_STATUS_OK ? result_record : NULL,
        result == PXA_STATUS_OK ? (wire_profile == 1 ? 12u : 8u) : 0);
    if (complete != PXA_STATUS_OK) {
        if (handle != PXA_HANDLE_INVALID) {
            if (wire_profile == 1)
                (void)pxa_handle_close64(service->runtime, component, handle);
            else
                (void)pxa_handle_close(service->runtime, component,
                                       (pxa_handle_t)handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_token);
    }
    return PXA_STATUS_OK;
}

static void lease_component_stopped(void *context, pxa_runtime_t *runtime,
                                    pxa_component_t component) {
    pxa_lease_service_t *service = (pxa_lease_service_t *)context;
    uint16_t previous = PXA_LEASE_SLOT_NONE;
    uint16_t index = service->active_head;
    (void)runtime;
    while (index != PXA_LEASE_SLOT_NONE) {
        pxa_lease_entry_t *entry = &service->entries[index];
        uint16_t next = entry->next;
        if (entry->component == component) {
            release_entry_at(service, index, previous);
        } else {
            previous = index;
        }
        index = next;
    }
}

pxa_status_t pxa_lease_service_register(pxa_lease_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_SERVICE_CORE;
    operations.major = PXA_CORE_SERVICE_MAJOR;
    operations.minor = PXA_CORE_SERVICE_MINOR;
    operations.context = service;
    operations.control = lease_control;
    operations.component_stopped = lease_component_stopped;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

static pxa_status_t post_revoked(pxa_lease_service_t *service,
                                 pxa_lease_entry_t *entry) {
    uint8_t payload[12];
    pxa_status_t status;
    size_t payload_size = (entry->handle >> 32) != 0 ? 12u : 8u;
    if (payload_size == 12)
        pxa_write_u64(payload, entry->handle);
    else
        pxa_write_u32(payload, (pxa_handle_t)entry->handle);
    pxa_write_u32(payload + payload_size - 4,
                  (uint32_t)entry->detail.revoke_reason);
    status = pxa_event_post_message(
        service->runtime, entry->component, PXA_SERVICE_CORE,
        PXA_LEASE_REVOKED, 0, (pxa_bytes_t){payload, payload_size}, 1, 0);
    return status;
}

static void append_affected(pxa_component_t component,
                            pxa_component_t *affected, size_t capacity,
                            size_t *count) {
    size_t index;
    for (index = 0; index < *count && index < capacity; ++index) {
        if (affected[index] == component) return;
    }
    if (*count < capacity && affected != NULL) affected[*count] = component;
    (*count)++;
}

static pxa_status_t revoke_matching(pxa_lease_service_t *service,
                                    int expired_only, pxa_status_t reason,
                                    pxa_component_t *affected,
                                    size_t capacity, size_t *count) {
    uint64_t now = service->limits.clock != NULL
                       ? service->limits.clock(service->limits.clock_context)
                       : 0;
    uint16_t index;
    uint16_t previous = PXA_LEASE_SLOT_NONE;
    pxa_status_t final_status = PXA_STATUS_OK;
    if (!service_valid(service) || count == NULL ||
        (affected == NULL && capacity != 0) || !pxa_status_is_known(reason)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    index = service->active_head;
    while (index != PXA_LEASE_SLOT_NONE) {
        pxa_lease_entry_t *entry = &service->entries[index];
        uint16_t next = entry->next;
        int released = 0;
        if (entry_waiting(entry)) {
            uint8_t payload[4];
            int32_t ready;
            if (pxa_request_identity(service->runtime, entry->component,
                    entry->value.token) != wait_identity(entry)) {
                release_entry_at(service, index, previous);
                index = next;
                continue;
            }
            ready = expired_only ? pxa_handle_poll64(service->runtime,
                entry->component, entry->handle) : reason;
            if (ready < 0 || ((uint32_t)ready &
                    (wait_interests(entry) | PXA_READY_END))) {
                pxa_status_t completed;
                pxa_write_u32(payload, ready < 0 ? 0 : (uint32_t)ready);
                completed = pxa_request_complete(service->runtime,
                    entry->component, entry->value.token,
                    ready < 0 ? ready : PXA_STATUS_OK,
                    ready < 0 ? NULL : payload, ready < 0 ? 0 : 4);
                if (completed == PXA_STATUS_OK) {
                    append_affected(entry->component, affected, capacity, count);
                    release_entry_at(service, index, previous);
                    index = next;
                    continue;
                }
                final_status = completed;
            }
            previous = index;
            index = next;
            continue;
        }
        if (entry->state == PXA_LEASE_ACTIVE &&
            (!expired_only || now >= entry->value.expires_at_ms)) {
            pxa_component_t component = entry->component;
            entry->detail.revoke_reason = reason;
            entry->state = PXA_LEASE_REVOKING;
            if ((entry->handle >> 32) != 0)
                (void)pxa_handle_close64(service->runtime, component,
                                         entry->handle);
            else
                (void)pxa_handle_close(service->runtime, component,
                                       (pxa_handle_t)entry->handle);
            append_affected(component, affected, capacity, count);
        }
        if (entry->state == PXA_LEASE_PENDING_EVENT) {
            pxa_status_t status = post_revoked(service, entry);
            if (status == PXA_STATUS_OK || status == PXA_STATUS_BAD_STATE ||
                status == PXA_STATUS_NOT_FOUND) {
                release_entry_at(service, index, previous);
                released = 1;
            } else {
                final_status = status;
            }
        }
        if (!released) previous = index;
        index = next;
    }
    if (*count > capacity && final_status == PXA_STATUS_OK) {
        final_status = PXA_STATUS_RESOURCE_LIMIT;
    }
    return final_status;
}

int pxa_lease_has_active(const pxa_lease_service_t *service) {
    uint16_t index;
    if (!service_valid(service)) return 0;
    index = service->active_head;
    while (index != PXA_LEASE_SLOT_NONE) {
        if (service->entries[index].state == PXA_LEASE_ACTIVE ||
            entry_waiting(&service->entries[index])) return 1;
        index = service->entries[index].next;
    }
    return 0;
}

pxa_status_t pxa_lease_active_components(
    const pxa_lease_service_t *service, pxa_component_t *output,
    size_t capacity, size_t *count) {
    uint16_t index;
    if (!service_valid(service) || count == NULL ||
        (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    index = service->active_head;
    while (index != PXA_LEASE_SLOT_NONE) {
        if (service->entries[index].state == PXA_LEASE_ACTIVE) {
            append_affected(service->entries[index].component,
                            output, capacity, count);
        }
        index = service->entries[index].next;
    }
    return *count > capacity ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_OK;
}

pxa_status_t pxa_lease_revoke_expired(
    pxa_lease_service_t *service, pxa_component_t *affected,
    size_t capacity, size_t *count) {
    return revoke_matching(service, 1, PXA_STATUS_CANCELLED,
                           affected, capacity, count);
}

pxa_status_t pxa_lease_revoke_all(
    pxa_lease_service_t *service, pxa_status_t reason,
    pxa_component_t *affected, size_t capacity, size_t *count) {
    return revoke_matching(service, 0, reason, affected, capacity, count);
}
