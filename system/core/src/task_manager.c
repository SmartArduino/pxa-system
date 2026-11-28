#include "pxsys/task_manager.h"

#include <string.h>

#include "pxsys/intent_wire.h"
#include "runtime_internal.h"

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

static size_t find_app(const pxsys_task_manager_t* manager,
                       const pxsys_app_identity_t* identity, size_t first) {
    for (size_t index = manager->count; index > first;) {
        --index;
        const pxsys_app_descriptor_t* app =
            pxsys_runtime_instance_app(manager->runtime, manager->stack[index]);
        if (app != NULL && pxsys_app_identity_equal(&app->identity, identity))
            return index;
    }
    return manager->count;
}

static size_t find_instance(const pxsys_task_manager_t* manager, pxsys_instance_ref_t instance) {
    size_t index;
    for (index = 0; index < manager->count; ++index) {
        if (manager->stack[index].slot == instance.slot &&
            manager->stack[index].generation == instance.generation)
            break;
    }
    return index;
}

static void promote_task(pxsys_task_manager_t* manager, size_t index) {
    if (index + 1u == manager->count) return;
    pxsys_instance_ref_t instance = manager->stack[index];
    memmove(&manager->stack[index], &manager->stack[index + 1u],
            (manager->count - index - 1u) * sizeof(*manager->stack));
    manager->stack[manager->count - 1u] = instance;
}

static pxsys_status_t create_message(pxsys_task_manager_t* manager, const pxsys_intent_t* intent,
                                     const pxsys_caller_t* caller, uint32_t operation,
                                     pxsys_message_t* message) {
    size_t written;
    pxsys_status_t status =
        pxsys_intent_wire_encode(intent, manager->wire, manager->wire_capacity, &written);
    if (status != PXSYS_STATUS_OK)
        return status;
    *message = (pxsys_message_t){
        .struct_size = sizeof(*message),
        .interface_id = {PXSYS_INTENT_INTERFACE_ID, sizeof(PXSYS_INTENT_INTERFACE_ID) - 1u},
        .version = {1, 0},
        .operation = operation,
        .request_id = intent->correlation_id,
        .flags = intent->flags,
        .payload = {manager->wire, written},
        .caller = caller,
    };
    return PXSYS_STATUS_OK;
}

static pxsys_status_t foreground_after_top(pxsys_task_manager_t* manager) {
    for (size_t index = manager->count; index != 0;) {
        pxsys_instance_ref_t ref = manager->stack[--index];
        pxsys_status_t status = pxsys_runtime_foreground(manager->runtime, ref);
        if (status == PXSYS_STATUS_OK) {
            promote_task(manager, index);
            return status;
        }
        pxsys_instance_snapshot_t snapshot = {.struct_size = sizeof(snapshot)};
        if (status != PXSYS_STATUS_BAD_STATE ||
            pxsys_runtime_snapshot(manager->runtime, ref, &snapshot) != PXSYS_STATUS_OK ||
            snapshot.lifecycle.state != PXSYS_APP_STOPPING)
            return status;
    }
    return PXSYS_STATUS_OK;
}

static pxsys_status_t remove_task(pxsys_task_manager_t* manager, size_t index) {
    manager->count--;
    if (index == manager->count)
        return foreground_after_top(manager);
    memmove(&manager->stack[index], &manager->stack[index + 1u],
            (manager->count - index) * sizeof(*manager->stack));
    return PXSYS_STATUS_OK;
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
    if (instance == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = pxsys_instance_ref_invalid();
    if (intent == NULL || intent->struct_size < sizeof(*intent))
        return PXSYS_STATUS_INVALID_ARGUMENT;
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
    size_t existing;
    uint32_t operation;
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
    existing = manager->count;
    if ((app->flags & PXSYS_APP_FLAG_SINGLE_INSTANCE) ||
        (intent->flags & PXSYS_INTENT_FLAG_CLEAR_TOP)) {
        existing = find_app(manager, &app->identity, 0);
    } else if ((intent->flags & PXSYS_INTENT_FLAG_SINGLE_TOP) && manager->count != 0) {
        existing = find_app(manager, &app->identity, manager->count - 1u);
    }
    reuse = existing != manager->count;
    operation = reuse ? PXSYS_INTENT_OPERATION_DELIVER : PXSYS_INTENT_OPERATION_START;
    if (manager->authorize != NULL) {
        status = manager->authorize(manager->policy_context, caller, operation, intent, app);
        if (status != PXSYS_STATUS_OK)
            return status;
    }
    if (!reuse && manager->count == manager->capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    status = create_message(manager, intent, caller, operation, &message);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (reuse) {
        if ((intent->flags & PXSYS_INTENT_FLAG_CLEAR_TOP) != 0) {
            status = remove_above(manager, existing);
            if (status != PXSYS_STATUS_OK) {
                if (status != PXSYS_STATUS_PENDING)
                    (void)foreground_after_top(manager);
                return status;
            }
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
        promote_task(manager, existing);
        return PXSYS_STATUS_OK;
    }
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
    index = find_instance(manager, instance);
    if (index == manager->count)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_complete_start(manager->runtime, instance, result);
    if (status != PXSYS_STATUS_OK &&
        pxsys_runtime_instance_app(manager->runtime, instance) == NULL)
        (void)remove_task(manager, index);
    return status;
}

pxsys_status_t pxsys_task_manager_report_stopped(pxsys_task_manager_t* manager,
                                                 pxsys_instance_ref_t instance,
                                                 pxsys_stop_reason_t reason) {
    pxsys_status_t status;
    size_t index;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    index = find_instance(manager, instance);
    if (index == manager->count)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_report_stopped(manager->runtime, instance, reason);
    if (status != PXSYS_STATUS_OK)
        return status;
    return remove_task(manager, index);
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
    const pxsys_instance_ref_t top = manager->stack[manager->count - 1u];
    status = pxsys_runtime_back(manager->runtime, top, result);
    if (status == PXSYS_STATUS_BAD_STATE) {
        pxsys_instance_snapshot_t snapshot = {.struct_size = sizeof(snapshot)};
        if (pxsys_runtime_snapshot(manager->runtime, top, &snapshot) == PXSYS_STATUS_OK &&
            (snapshot.lifecycle.state == PXSYS_APP_STARTING ||
             snapshot.lifecycle.state == PXSYS_APP_STOPPING)) {
            status = pxsys_task_manager_finish_top(manager, PXSYS_STOP_NORMAL);
            if (status == PXSYS_STATUS_OK || status == PXSYS_STATUS_PENDING)
                *result = PXSYS_BACK_HANDLED;
            return status;
        }
    }
    if (status != PXSYS_STATUS_OK || *result == PXSYS_BACK_HANDLED)
        return status;
    if (manager->count == 1)
        return PXSYS_STATUS_OK;
    status = pxsys_task_manager_finish_top(manager, PXSYS_STOP_NORMAL);
    if (status == PXSYS_STATUS_OK || status == PXSYS_STATUS_PENDING)
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

pxsys_status_t pxsys_task_manager_finish_instance(pxsys_task_manager_t* manager,
                                                  pxsys_instance_ref_t instance,
                                                  pxsys_stop_reason_t reason) {
    size_t index;
    pxsys_status_t status;
    if (!manager_valid(manager))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    index = find_instance(manager, instance);
    if (index == manager->count)
        return PXSYS_STATUS_NOT_FOUND;
    status = pxsys_runtime_stop(manager->runtime, manager->stack[index], reason);
    if (status != PXSYS_STATUS_OK)
        return status;
    return remove_task(manager, index);
}

pxsys_status_t pxsys_task_manager_finish_all(pxsys_task_manager_t* manager,
                                             pxsys_stop_reason_t reason) {
    if (!manager_valid(manager) || reason > PXSYS_STOP_SHUTDOWN)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    pxsys_status_t result = PXSYS_STATUS_OK;
    for (size_t index = manager->count; index != 0;) {
        pxsys_status_t status = pxsys_runtime_stop(manager->runtime, manager->stack[--index], reason);
        if (status == PXSYS_STATUS_PENDING) {
            result = status;
            continue;
        }
        if (status != PXSYS_STATUS_OK) {
            (void)foreground_after_top(manager);
            return status;
        }
        manager->count--;
        if (index != manager->count)
            memmove(&manager->stack[index], &manager->stack[index + 1u],
                    (manager->count - index) * sizeof(*manager->stack));
    }
    return result;
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

static pxsys_status_t activate_task(pxsys_task_manager_t* manager, size_t index) {
    const pxsys_instance_ref_t instance = manager->stack[index];
    pxsys_instance_ref_t previous;
    pxsys_status_t status;
    if (index + 1u == manager->count)
        return pxsys_runtime_foreground(manager->runtime, instance);
    previous = manager->stack[manager->count - 1u];
    status = pxsys_runtime_background(manager->runtime, previous);
    if (status != PXSYS_STATUS_OK) return status;
    status = pxsys_runtime_foreground(manager->runtime, instance);
    if (status != PXSYS_STATUS_OK) {
        (void)pxsys_runtime_foreground(manager->runtime, previous);
        return status;
    }
    promote_task(manager, index);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_task_manager_activate(
    pxsys_task_manager_t* manager, pxsys_instance_ref_t instance) {
    if (!manager_valid(manager)) return PXSYS_STATUS_INVALID_ARGUMENT;
    const size_t index = find_instance(manager, instance);
    return index == manager->count ? PXSYS_STATUS_NOT_FOUND : activate_task(manager, index);
}

pxsys_status_t pxsys_task_manager_resume_app(pxsys_task_manager_t* manager,
                                            const pxsys_app_identity_t* app) {
    if (!manager_valid(manager) || pxsys_app_identity_validate(app, 64) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    const size_t index = find_app(manager, app, 0);
    return index == manager->count ? PXSYS_STATUS_NOT_FOUND : activate_task(manager, index);
}
