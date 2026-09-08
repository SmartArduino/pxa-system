#include "pxsys/role_host.h"

#include <string.h>

#include "pxsys/intent_wire.h"

#define PXSYS_ROLE_HOST_MAGIC UINT32_C(0x50585248)

typedef struct {
    char* role_id;
    size_t role_id_size;
    pxsys_instance_ref_t current;
    pxsys_instance_ref_t incoming;
    pxsys_instance_ref_t retiring;
    uint8_t occupied;
} role_instance_t;

struct pxsys_role_host {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t max_role_id_bytes;
    size_t wire_capacity;
    pxsys_role_registry_t* roles;
    pxsys_runtime_t* runtime;
    void* policy_context;
    pxsys_navigation_policy_fn authorize;
    pxsys_allocator_t allocator;
    role_instance_t* entries;
    uint8_t* wire;
};

static int ref_valid(pxsys_instance_ref_t ref) {
    return ref.slot != PXSYS_INSTANCE_REF_INVALID_SLOT;
}

static int ref_equal(pxsys_instance_ref_t left, pxsys_instance_ref_t right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static int host_valid(const pxsys_role_host_t* host) {
    return host != NULL && host->magic == PXSYS_ROLE_HOST_MAGIC;
}

static role_instance_t* find_role(const pxsys_role_host_t* host,
                                  pxsys_string_t role_id) {
    size_t index;
    for (index = 0; index < host->capacity; ++index) {
        role_instance_t* entry = &host->entries[index];
        if (entry->occupied && entry->role_id_size == role_id.size &&
            memcmp(entry->role_id, role_id.data, role_id.size) == 0) {
            return entry;
        }
    }
    return NULL;
}

static role_instance_t* find_instance(const pxsys_role_host_t* host,
                                      pxsys_instance_ref_t instance) {
    size_t index;
    for (index = 0; index < host->capacity; ++index) {
        role_instance_t* entry = &host->entries[index];
        if (entry->occupied &&
            (ref_equal(entry->current, instance) ||
             ref_equal(entry->incoming, instance) ||
             ref_equal(entry->retiring, instance))) {
            return entry;
        }
    }
    return NULL;
}

static void release_if_empty(pxsys_role_host_t* host, role_instance_t* entry) {
    if (ref_valid(entry->current) || ref_valid(entry->incoming) ||
        ref_valid(entry->retiring)) {
        return;
    }
    host->allocator.release(host->allocator.context, entry->role_id);
    memset(entry, 0, sizeof(*entry));
    host->count--;
}

void pxsys_role_host_config_init(pxsys_role_host_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_active_roles = 8;
    config->max_role_id_bytes = 96;
    config->max_intent_wire_bytes = 4096;
    config->allocator.struct_size = sizeof(config->allocator);
}

static int config_valid(const pxsys_role_host_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_active_roles != 0 &&
           config->max_active_roles <= SIZE_MAX / sizeof(role_instance_t) &&
           config->max_role_id_bytes != 0 && config->max_role_id_bytes != SIZE_MAX &&
           config->max_intent_wire_bytes != 0 && config->roles != NULL &&
           config->runtime != NULL &&
           config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

pxsys_status_t pxsys_role_host_create(const pxsys_role_host_config_t* config,
                                      pxsys_role_host_t** output) {
    pxsys_role_host_t* host;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    host = (pxsys_role_host_t*)config->allocator.allocate(config->allocator.context,
                                                          sizeof(*host));
    if (host == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(host, 0, sizeof(*host));
    host->entries = (role_instance_t*)config->allocator.allocate(
        config->allocator.context, config->max_active_roles * sizeof(*host->entries));
    if (host->entries == NULL) {
        config->allocator.release(config->allocator.context, host);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(host->entries, 0, config->max_active_roles * sizeof(*host->entries));
    host->wire = (uint8_t*)config->allocator.allocate(config->allocator.context,
                                                       config->max_intent_wire_bytes);
    if (host->wire == NULL) {
        config->allocator.release(config->allocator.context, host->entries);
        config->allocator.release(config->allocator.context, host);
        return PXSYS_STATUS_NO_MEMORY;
    }
    host->capacity = config->max_active_roles;
    host->max_role_id_bytes = config->max_role_id_bytes;
    host->wire_capacity = config->max_intent_wire_bytes;
    host->roles = config->roles;
    host->runtime = config->runtime;
    host->policy_context = config->policy_context;
    host->authorize = config->authorize;
    host->allocator = config->allocator;
    host->magic = PXSYS_ROLE_HOST_MAGIC;
    *output = host;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_host_destroy(pxsys_role_host_t* host) {
    pxsys_allocator_t allocator;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (host->count != 0)
        return PXSYS_STATUS_BUSY;
    allocator = host->allocator;
    host->magic = 0;
    allocator.release(allocator.context, host->wire);
    allocator.release(allocator.context, host->entries);
    allocator.release(allocator.context, host);
    return PXSYS_STATUS_OK;
}

static role_instance_t* allocate_role(pxsys_role_host_t* host,
                                      pxsys_string_t role_id) {
    size_t index;
    char* copy;
    if (host->count == host->capacity)
        return NULL;
    copy = (char*)host->allocator.allocate(host->allocator.context, role_id.size + 1u);
    if (copy == NULL)
        return NULL;
    memcpy(copy, role_id.data, role_id.size);
    copy[role_id.size] = '\0';
    for (index = 0; index < host->capacity; ++index) {
        role_instance_t* entry = &host->entries[index];
        if (!entry->occupied) {
            memset(entry, 0, sizeof(*entry));
            entry->role_id = copy;
            entry->role_id_size = role_id.size;
            entry->current = pxsys_instance_ref_invalid();
            entry->incoming = pxsys_instance_ref_invalid();
            entry->retiring = pxsys_instance_ref_invalid();
            entry->occupied = 1;
            host->count++;
            return entry;
        }
    }
    host->allocator.release(host->allocator.context, copy);
    return NULL;
}

static pxsys_status_t build_message(pxsys_role_host_t* host,
                                    const pxsys_caller_t* caller,
                                    const pxsys_intent_t* intent,
                                    const pxsys_app_identity_t* target,
                                    uint32_t operation,
                                    pxsys_message_t* message) {
    pxsys_intent_t resolved = *intent;
    size_t written;
    pxsys_status_t status;
    resolved.target = target;
    status = pxsys_intent_wire_encode(&resolved, host->wire, host->wire_capacity,
                                      &written);
    if (status != PXSYS_STATUS_OK)
        return status;
    memset(message, 0, sizeof(*message));
    message->struct_size = sizeof(*message);
    message->interface_id = pxsys_string_from_cstr(PXSYS_INTENT_INTERFACE_ID);
    message->version = (pxsys_version_t){1, 0};
    message->operation = operation;
    message->request_id = intent->correlation_id;
    message->flags = intent->flags;
    message->payload = pxsys_bytes(host->wire, written);
    message->caller = caller;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t adopt_started(pxsys_role_host_t* host,
                                    role_instance_t* entry,
                                    pxsys_instance_ref_t started) {
    pxsys_status_t status;
    if (ref_valid(entry->current)) {
        status = pxsys_runtime_stop(host->runtime, entry->current,
                                    PXSYS_STOP_REPLACED);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING) {
            pxsys_status_t rollback =
                pxsys_runtime_stop(host->runtime, started, PXSYS_STOP_FAULT);
            entry->incoming = rollback == PXSYS_STATUS_OK
                                  ? pxsys_instance_ref_invalid()
                                  : started;
            return status;
        }
        if (status == PXSYS_STATUS_PENDING)
            entry->retiring = entry->current;
    }
    entry->current = started;
    entry->incoming = pxsys_instance_ref_invalid();
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_host_start(pxsys_role_host_t* host,
                                     const pxsys_caller_t* caller,
                                     pxsys_string_t role_id,
                                     const pxsys_intent_t* intent,
                                     pxsys_instance_ref_t* instance) {
    const pxsys_app_descriptor_t* app;
    pxsys_instance_snapshot_t current = {0};
    pxsys_message_t message;
    role_instance_t* entry;
    pxsys_status_t status;
    int allocated = 0;
    if (instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    if (!host_valid(host) || intent == NULL ||
        !pxsys_identifier_validate(role_id, host->max_role_id_bytes) ||
        (caller != NULL &&
         (caller->struct_size < sizeof(*caller) ||
          pxsys_app_identity_validate(&caller->app, 64) != PXSYS_STATUS_OK ||
          !pxsys_identifier_validate(caller->component_id, 64)))) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    status = pxsys_role_resolve(host->roles, role_id, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    entry = find_role(host, role_id);
    if (entry != NULL && (ref_valid(entry->incoming) || ref_valid(entry->retiring)))
        return PXSYS_STATUS_BUSY;
    if (entry != NULL && ref_valid(entry->current)) {
        current.struct_size = sizeof(current);
        status = pxsys_runtime_snapshot(host->runtime, entry->current, &current);
        if (status != PXSYS_STATUS_OK)
            return status;
        if (pxsys_app_identity_equal(&current.app->identity, &app->identity)) {
            if (host->authorize != NULL &&
                (status = host->authorize(host->policy_context, caller,
                                          PXSYS_INTENT_OPERATION_DELIVER,
                                          intent, app)) != PXSYS_STATUS_OK) {
                return status;
            }
            status = build_message(host, caller, intent, &app->identity,
                                   PXSYS_INTENT_OPERATION_DELIVER, &message);
            if (status != PXSYS_STATUS_OK)
                return status;
            *instance = entry->current;
            return pxsys_runtime_deliver(host->runtime, entry->current, &message);
        }
    }
    if (host->authorize != NULL &&
        (status = host->authorize(host->policy_context, caller,
                                  PXSYS_INTENT_OPERATION_START,
                                  intent, app)) != PXSYS_STATUS_OK) {
        return status;
    }
    status = build_message(host, caller, intent, &app->identity,
                           PXSYS_INTENT_OPERATION_START, &message);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (entry == NULL) {
        entry = allocate_role(host, role_id);
        if (entry == NULL)
            return host->count == host->capacity ? PXSYS_STATUS_RESOURCE_LIMIT
                                                 : PXSYS_STATUS_NO_MEMORY;
        allocated = 1;
    }
    status = pxsys_runtime_launch(host->runtime, &app->identity, &message, 1,
                                  instance);
    if (status == PXSYS_STATUS_PENDING) {
        entry->incoming = *instance;
        return status;
    }
    if (status != PXSYS_STATUS_OK) {
        if (allocated)
            release_if_empty(host, entry);
        return status;
    }
    status = adopt_started(host, entry, *instance);
    return status;
}

pxsys_status_t pxsys_role_host_complete_start(pxsys_role_host_t* host,
                                              pxsys_instance_ref_t instance,
                                              pxsys_status_t result) {
    role_instance_t* entry;
    pxsys_status_t status;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    entry = find_instance(host, instance);
    if (entry == NULL || !ref_equal(entry->incoming, instance))
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_complete_start(host->runtime, instance, result);
    if (status != PXSYS_STATUS_OK) {
        entry->incoming = pxsys_instance_ref_invalid();
        release_if_empty(host, entry);
        return status;
    }
    return adopt_started(host, entry, instance);
}

pxsys_status_t pxsys_role_host_report_stopped(pxsys_role_host_t* host,
                                              pxsys_instance_ref_t instance,
                                              pxsys_stop_reason_t reason) {
    role_instance_t* entry;
    pxsys_status_t status;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    entry = find_instance(host, instance);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_report_stopped(host->runtime, instance, reason);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (ref_equal(entry->current, instance))
        entry->current = pxsys_instance_ref_invalid();
    if (ref_equal(entry->incoming, instance))
        entry->incoming = pxsys_instance_ref_invalid();
    if (ref_equal(entry->retiring, instance))
        entry->retiring = pxsys_instance_ref_invalid();
    release_if_empty(host, entry);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_role_host_stop(pxsys_role_host_t* host,
                                    pxsys_string_t role_id,
                                    pxsys_stop_reason_t reason) {
    role_instance_t* entry;
    pxsys_status_t status = PXSYS_STATUS_OK;
    if (!host_valid(host) ||
        !pxsys_identifier_validate(role_id, host->max_role_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_role(host, role_id);
    if (entry == NULL)
        return PXSYS_STATUS_NOT_FOUND;
    if (ref_valid(entry->retiring))
        return PXSYS_STATUS_BUSY;
    if (ref_valid(entry->incoming)) {
        status = pxsys_runtime_stop(host->runtime, entry->incoming, reason);
        if (status == PXSYS_STATUS_PENDING) {
            entry->retiring = entry->incoming;
            entry->incoming = pxsys_instance_ref_invalid();
            return PXSYS_STATUS_PENDING;
        } else if (status == PXSYS_STATUS_OK) {
            entry->incoming = pxsys_instance_ref_invalid();
        } else {
            return status;
        }
    }
    if (ref_valid(entry->current)) {
        pxsys_status_t current_status =
            pxsys_runtime_stop(host->runtime, entry->current, reason);
        if (current_status == PXSYS_STATUS_PENDING) {
            if (ref_valid(entry->retiring))
                return PXSYS_STATUS_BUSY;
            entry->retiring = entry->current;
            status = PXSYS_STATUS_PENDING;
        } else if (current_status != PXSYS_STATUS_OK) {
            return current_status;
        }
        entry->current = pxsys_instance_ref_invalid();
    }
    release_if_empty(host, entry);
    return status;
}

pxsys_status_t pxsys_role_host_stop_all(pxsys_role_host_t* host,
                                        pxsys_stop_reason_t reason) {
    size_t index;
    pxsys_status_t aggregate = PXSYS_STATUS_OK;
    if (!host_valid(host))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < host->capacity; ++index) {
        role_instance_t* entry = &host->entries[index];
        pxsys_status_t status;
        pxsys_string_t role_id;
        if (!entry->occupied)
            continue;
        role_id = pxsys_string(entry->role_id, entry->role_id_size);
        status = pxsys_role_host_stop(host, role_id, reason);
        if (status == PXSYS_STATUS_PENDING || status == PXSYS_STATUS_BUSY)
            aggregate = PXSYS_STATUS_PENDING;
        else if (status != PXSYS_STATUS_OK && aggregate == PXSYS_STATUS_OK)
            aggregate = status;
    }
    return aggregate;
}

pxsys_status_t pxsys_role_host_current(const pxsys_role_host_t* host,
                                       pxsys_string_t role_id,
                                       pxsys_instance_ref_t* instance) {
    role_instance_t* entry;
    if (instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    if (!host_valid(host) ||
        !pxsys_identifier_validate(role_id, host->max_role_id_bytes)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    entry = find_role(host, role_id);
    if (entry == NULL || !ref_valid(entry->current))
        return PXSYS_STATUS_NOT_FOUND;
    *instance = entry->current;
    return PXSYS_STATUS_OK;
}

size_t pxsys_role_host_count(const pxsys_role_host_t* host) {
    return host_valid(host) ? host->count : 0;
}
