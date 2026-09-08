#include "pxsys/task_manager.h"

#include <string.h>

#include "pxsys/intent_wire.h"

#define PXSYS_TASK_MANAGER_MAGIC UINT32_C(0x5058544d)

struct pxsys_task_manager {
    uint32_t magic;
    size_t capacity;
    size_t count;
    size_t wire_capacity;
    pxsys_intent_resolver_t* intents;
    pxsys_runtime_t* runtime;
    void* policy_context;
    pxsys_navigation_policy_fn authorize;
    pxsys_allocator_t allocator;
    pxsys_instance_ref_t* stack;
    uint8_t* wire;
};

static int manager_valid(const pxsys_task_manager_t* manager) {
    return manager != NULL && manager->magic == PXSYS_TASK_MANAGER_MAGIC;
}

static int config_valid(const pxsys_task_manager_config_t* config) {
    return config != NULL && config->struct_size >= sizeof(*config) && config->max_tasks != 0 &&
           config->max_tasks <= SIZE_MAX / sizeof(pxsys_instance_ref_t) &&
           config->max_intent_wire_bytes != 0 && config->intents != NULL &&
           config->runtime != NULL && config->allocator.struct_size >= sizeof(config->allocator) &&
           config->allocator.allocate != NULL && config->allocator.release != NULL;
}

void pxsys_task_manager_config_init(pxsys_task_manager_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_tasks = 16;
    config->max_intent_wire_bytes = 4096;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_task_manager_create(const pxsys_task_manager_config_t* config,
                                         pxsys_task_manager_t** output) {
    pxsys_task_manager_t* manager;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!config_valid(config))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    manager = (pxsys_task_manager_t*)config->allocator.allocate(config->allocator.context,
                                                                sizeof(*manager));
    if (manager == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(manager, 0, sizeof(*manager));
    manager->stack = (pxsys_instance_ref_t*)config->allocator.allocate(
        config->allocator.context, config->max_tasks * sizeof(*manager->stack));
    if (manager->stack == NULL) {
        config->allocator.release(config->allocator.context, manager);
        return PXSYS_STATUS_NO_MEMORY;
    }
    manager->wire = (uint8_t*)config->allocator.allocate(config->allocator.context,
                                                         config->max_intent_wire_bytes);
    if (manager->wire == NULL) {
        config->allocator.release(config->allocator.context, manager->stack);
        config->allocator.release(config->allocator.context, manager);
        return PXSYS_STATUS_NO_MEMORY;
    }
    manager->capacity = config->max_tasks;
    manager->wire_capacity = config->max_intent_wire_bytes;
    manager->intents = config->intents;
    manager->runtime = config->runtime;
    manager->policy_context = config->policy_context;
    manager->authorize = config->authorize;
    manager->allocator = config->allocator;
    manager->magic = PXSYS_TASK_MANAGER_MAGIC;
    *output = manager;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_task_manager_destroy(pxsys_task_manager_t* manager) {
    pxsys_allocator_t allocator;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (manager->count != 0)
        return PXSYS_STATUS_BUSY;
    allocator = manager->allocator;
    manager->magic = 0;
    allocator.release(allocator.context, manager->wire);
    allocator.release(allocator.context, manager->stack);
    allocator.release(allocator.context, manager);
    return PXSYS_STATUS_OK;
}

static int find_task(const pxsys_task_manager_t* manager, const pxsys_app_identity_t* identity,
                     size_t* index) {
    size_t candidate;
    for (candidate = 0; candidate < manager->count; ++candidate) {
        pxsys_instance_snapshot_t snapshot = {0};
        snapshot.struct_size = sizeof(snapshot);
        if (pxsys_runtime_snapshot(manager->runtime, manager->stack[candidate], &snapshot) ==
                PXSYS_STATUS_OK &&
            pxsys_app_identity_equal(&snapshot.app->identity, identity)) {
            *index = candidate;
            return 1;
        }
    }
    return 0;
}

static pxsys_status_t create_message(pxsys_task_manager_t* manager, const pxsys_intent_t* intent,
                                     const pxsys_caller_t* caller, uint32_t operation,
                                     pxsys_message_t* message) {
    size_t written;
    pxsys_status_t status =
        pxsys_intent_wire_encode(intent, manager->wire, manager->wire_capacity, &written);
    if (status != PXSYS_STATUS_OK)
        return status;
    memset(message, 0, sizeof(*message));
    message->struct_size = sizeof(*message);
    message->interface_id = pxsys_string_from_cstr(PXSYS_INTENT_INTERFACE_ID);
    message->version = (pxsys_version_t){1, 0};
    message->operation = operation;
    message->request_id = intent->correlation_id;
    message->flags = intent->flags;
    message->payload = pxsys_bytes(manager->wire, written);
    message->caller = caller;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t foreground_after_top(pxsys_task_manager_t* manager) {
    if (manager->count == 0)
        return PXSYS_STATUS_OK;
    return pxsys_runtime_foreground(manager->runtime, manager->stack[manager->count - 1u]);
}

static pxsys_status_t remove_above(pxsys_task_manager_t* manager, size_t index) {
    while (manager->count > index + 1u) {
        pxsys_status_t status = pxsys_runtime_stop(
            manager->runtime, manager->stack[manager->count - 1u], PXSYS_STOP_REPLACED);
        if (status != PXSYS_STATUS_OK)
            return status;
        manager->count--;
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_task_manager_start(pxsys_task_manager_t* manager, const pxsys_intent_t* intent,
                                        pxsys_instance_ref_t* instance) {
    return pxsys_task_manager_start_as(manager, NULL, intent, instance);
}

pxsys_status_t pxsys_task_manager_start_role(pxsys_task_manager_t* manager,
                                             const pxsys_role_registry_t* roles,
                                             const pxsys_caller_t* caller,
                                             pxsys_string_t role_id,
                                             const pxsys_intent_t* intent,
                                             pxsys_instance_ref_t* instance) {
    const pxsys_app_descriptor_t* app;
    pxsys_intent_t resolved;
    pxsys_status_t status;
    if (intent == NULL || instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    status = pxsys_role_resolve(roles, role_id, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    resolved = *intent;
    resolved.target = &app->identity;
    return pxsys_task_manager_start_as(manager, caller, &resolved, instance);
}

pxsys_status_t pxsys_task_manager_start_as(pxsys_task_manager_t* manager,
                                           const pxsys_caller_t* caller,
                                           const pxsys_intent_t* intent,
                                           pxsys_instance_ref_t* instance) {
    const pxsys_app_descriptor_t* app;
    pxsys_message_t message;
    pxsys_status_t status;
    size_t existing = 0;
    int has_existing;
    int reuse;
    if (instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (caller != NULL && (caller->struct_size < sizeof(*caller) ||
                           pxsys_app_identity_validate(&caller->app, 64) != PXSYS_STATUS_OK ||
                           !pxsys_identifier_validate(caller->component_id, 64))) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    status = pxsys_intent_resolve(manager->intents, intent, &app);
    if (status != PXSYS_STATUS_OK)
        return status;
    has_existing = find_task(manager, &app->identity, &existing);
    reuse = has_existing && (((app->flags & PXSYS_APP_FLAG_SINGLE_INSTANCE) != 0) ||
                             ((intent->flags & PXSYS_INTENT_FLAG_CLEAR_TOP) != 0) ||
                             (((intent->flags & PXSYS_INTENT_FLAG_SINGLE_TOP) != 0) &&
                              existing + 1u == manager->count));
    if (manager->authorize != NULL) {
        status = manager->authorize(
            manager->policy_context, caller,
            reuse ? PXSYS_INTENT_OPERATION_DELIVER : PXSYS_INTENT_OPERATION_START, intent, app);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    status = create_message(manager, intent, caller,
                            reuse ? PXSYS_INTENT_OPERATION_DELIVER : PXSYS_INTENT_OPERATION_START,
                            &message);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (reuse) {
        if ((intent->flags & PXSYS_INTENT_FLAG_CLEAR_TOP) != 0) {
            status = remove_above(manager, existing);
            if (status != PXSYS_STATUS_OK)
                return status;
        } else if (existing + 1u != manager->count) {
            status =
                pxsys_runtime_background(manager->runtime, manager->stack[manager->count - 1u]);
            if (status != PXSYS_STATUS_OK)
                return status;
        }
        *instance = manager->stack[existing];
        status = pxsys_runtime_deliver(manager->runtime, *instance, &message);
        if (status != PXSYS_STATUS_OK) {
            (void)foreground_after_top(manager);
            return status;
        }
        status = pxsys_runtime_foreground(manager->runtime, *instance);
        if (status != PXSYS_STATUS_OK) {
            (void)foreground_after_top(manager);
            return status;
        }
        if (existing + 1u != manager->count) {
            pxsys_instance_ref_t selected = manager->stack[existing];
            memmove(&manager->stack[existing], &manager->stack[existing + 1u],
                    (manager->count - existing - 1u) * sizeof(*manager->stack));
            manager->stack[manager->count - 1u] = selected;
        }
        return PXSYS_STATUS_OK;
    }
    if (manager->count == manager->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    if (manager->count != 0) {
        status = pxsys_runtime_background(manager->runtime, manager->stack[manager->count - 1u]);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    status = pxsys_runtime_launch(manager->runtime, &app->identity, &message, 1, instance);
    if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING) {
        (void)foreground_after_top(manager);
        return status;
    }
    manager->stack[manager->count++] = *instance;
    return status;
}

pxsys_status_t pxsys_task_manager_complete_start(pxsys_task_manager_t* manager,
                                                 pxsys_instance_ref_t instance,
                                                 pxsys_status_t result) {
    pxsys_status_t status;
    size_t index;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < manager->count; ++index) {
        if (manager->stack[index].slot == instance.slot &&
            manager->stack[index].generation == instance.generation)
            break;
    }
    if (index == manager->count)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_complete_start(manager->runtime, instance, result);
    if (status == PXSYS_STATUS_OK)
        return status;
    if (pxsys_runtime_snapshot(manager->runtime, instance,
                               &(pxsys_instance_snapshot_t){
                                   .struct_size = sizeof(pxsys_instance_snapshot_t),
                               }) != PXSYS_STATUS_NOT_FOUND) {
        return status;
    }
    memmove(&manager->stack[index], &manager->stack[index + 1u],
            (manager->count - index - 1u) * sizeof(*manager->stack));
    manager->count--;
    if (index == manager->count)
        (void)foreground_after_top(manager);
    return status;
}

pxsys_status_t pxsys_task_manager_report_stopped(pxsys_task_manager_t* manager,
                                                 pxsys_instance_ref_t instance,
                                                 pxsys_stop_reason_t reason) {
    pxsys_status_t status;
    size_t index;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < manager->count; ++index) {
        if (manager->stack[index].slot == instance.slot &&
            manager->stack[index].generation == instance.generation)
            break;
    }
    if (index == manager->count)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_report_stopped(manager->runtime, instance, reason);
    if (status != PXSYS_STATUS_OK)
        return status;
    memmove(&manager->stack[index], &manager->stack[index + 1u],
            (manager->count - index - 1u) * sizeof(*manager->stack));
    manager->count--;
    return index == manager->count ? foreground_after_top(manager) : PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_task_manager_back(pxsys_task_manager_t* manager, pxsys_back_result_t* result) {
    pxsys_status_t status;
    if (result == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *result = PXSYS_BACK_UNHANDLED;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (manager->count == 0)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_back(manager->runtime, manager->stack[manager->count - 1u], result);
    if (status != PXSYS_STATUS_OK || *result == PXSYS_BACK_HANDLED)
        return status;
    if (manager->count == 1)
        return PXSYS_STATUS_OK;
    status = pxsys_runtime_stop(manager->runtime, manager->stack[manager->count - 1u],
                                PXSYS_STOP_NORMAL);
    if (status == PXSYS_STATUS_PENDING) {
        *result = PXSYS_BACK_HANDLED;
        return status;
    }
    if (status != PXSYS_STATUS_OK)
        return status;
    manager->count--;
    status = foreground_after_top(manager);
    if (status == PXSYS_STATUS_OK)
        *result = PXSYS_BACK_HANDLED;
    return status;
}

pxsys_status_t pxsys_task_manager_finish_top(pxsys_task_manager_t* manager,
                                             pxsys_stop_reason_t reason) {
    pxsys_status_t status;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (manager->count == 0)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_stop(manager->runtime, manager->stack[manager->count - 1u], reason);
    if (status == PXSYS_STATUS_PENDING)
        return status;
    if (status != PXSYS_STATUS_OK)
        return status;
    manager->count--;
    return foreground_after_top(manager);
}

pxsys_status_t pxsys_task_manager_finish_all(pxsys_task_manager_t* manager,
                                             pxsys_stop_reason_t reason) {
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    while (manager->count != 0) {
        pxsys_status_t status =
            pxsys_runtime_stop(manager->runtime, manager->stack[manager->count - 1u], reason);
        if (status == PXSYS_STATUS_PENDING)
            return status;
        if (status != PXSYS_STATUS_OK)
            return status;
        manager->count--;
    }
    return PXSYS_STATUS_OK;
}

size_t pxsys_task_manager_count(const pxsys_task_manager_t* manager) {
    return manager_valid(manager) ? manager->count : 0;
}

pxsys_status_t pxsys_task_manager_current(const pxsys_task_manager_t* manager,
                                          pxsys_instance_ref_t* instance) {
    if (instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (manager->count == 0)
        return PXSYS_STATUS_NOT_FOUND;
    *instance = manager->stack[manager->count - 1u];
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_task_manager_task(
    const pxsys_task_manager_t* manager, size_t recent_index,
    pxsys_instance_ref_t* instance, pxsys_instance_snapshot_t* snapshot) {
    size_t stack_index;
    if (!manager_valid(manager) || instance == NULL || snapshot == NULL ||
        snapshot->struct_size < sizeof(*snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (recent_index >= manager->count) return PXSYS_STATUS_NOT_FOUND;
    stack_index = manager->count - recent_index - 1u;
    *instance = manager->stack[stack_index];
    return pxsys_runtime_snapshot(manager->runtime, *instance, snapshot);
}

pxsys_status_t pxsys_task_manager_activate(
    pxsys_task_manager_t* manager, pxsys_instance_ref_t instance) {
    size_t index;
    pxsys_instance_ref_t previous;
    pxsys_status_t status;
    if (!manager_valid(manager)) return PXSYS_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < manager->count; ++index) {
        if (manager->stack[index].slot == instance.slot &&
            manager->stack[index].generation == instance.generation)
            break;
    }
    if (index == manager->count) return PXSYS_STATUS_NOT_FOUND;
    if (index + 1u == manager->count) return PXSYS_STATUS_OK;
    previous = manager->stack[manager->count - 1u];
    status = pxsys_runtime_background(manager->runtime, previous);
    if (status != PXSYS_STATUS_OK) return status;
    status = pxsys_runtime_foreground(manager->runtime, instance);
    if (status != PXSYS_STATUS_OK) {
        (void)pxsys_runtime_foreground(manager->runtime, previous);
        return status;
    }
    memmove(&manager->stack[index], &manager->stack[index + 1u],
            (manager->count - index - 1u) * sizeof(*manager->stack));
    manager->stack[manager->count - 1u] = instance;
    return PXSYS_STATUS_OK;
}
