#include "pxa/activation.h"
#include "common/bytes_internal.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_COORDINATOR_MAGIC UINT32_C(0x50584143)

typedef struct {
    const pxa_activation_entry_t *entry;
    pxa_component_t component;
    uint64_t instance_id;
    uint8_t active;
} pxa_active_component_t;

struct pxa_activation_coordinator {
    uint32_t magic;
    pxa_runtime_t *runtime;
    const pxa_activation_plan_t *plan;
    pxa_component_engine_t engine;
    pxa_active_component_t *active;
    uint16_t active_count;
};

typedef struct {
    pxa_activation_plan_t plan;
} pxa_plan_workspace_t;

static int stop_reason_valid(pxa_stop_reason_t reason) {
    return reason <= PXA_STOP_SHUTDOWN;
}

size_t pxa_activation_plan_workspace_size(uint16_t max_components) {
    size_t entries_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (max_components == 0 ||
        sizeof(pxa_activation_entry_t) > SIZE_MAX / max_components) {
        return 0;
    }
    entries_size =
        (size_t)max_components * sizeof(pxa_activation_entry_t);
    if (sizeof(pxa_plan_workspace_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_plan_workspace_t);
    if (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u > SIZE_MAX - size) return 0;
    size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (entries_size > SIZE_MAX - size) return 0;
    return size + entries_size;
}

pxa_status_t pxa_activation_plan_prepare(
    void *workspace, size_t workspace_size,
    const pxa_package_manifest_t *manifest,
    const pxa_package_activation_profile_t *capabilities,
    const pxa_package_host_profile_t *host, pxa_bytes_t package_root,
    pxa_activation_plan_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_plan_workspace_t *storage;
    pxa_activation_entry_t *entries;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (manifest == NULL || capabilities == NULL || host == NULL ||
        package_root.data == NULL || package_root.size == 0 ||
        manifest->component_count == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    required = pxa_activation_plan_workspace_size(manifest->component_count);
    if (workspace == NULL || required == 0 || workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    storage = (pxa_plan_workspace_t *)cursor;
    memset(storage, 0, sizeof(*storage));
    cursor += sizeof(*storage);
    cursor = pxa_internal_align_pointer(cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (cursor > end ||
        (size_t)manifest->component_count *
                sizeof(pxa_activation_entry_t) >
            end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entries = (pxa_activation_entry_t *)cursor;
    storage->plan.entries = entries;
    storage->plan.package_root = package_root;
    storage->plan.manifest = manifest;
    storage->plan.entry_count = manifest->component_count;
    for (index = 0; index < manifest->component_count; ++index) {
        const pxa_package_artifact_t *artifact = NULL;
        pxa_status_t status = pxa_package_requirements_validate(
            manifest, &manifest->components[index], capabilities);
        if (status != PXA_STATUS_OK) return status;
        status = pxa_package_artifact_select(
            &manifest->components[index], host, &artifact);
        if (status != PXA_STATUS_OK) return status;
        entries[index].component = &manifest->components[index];
        entries[index].artifact = artifact;
    }
    *output = &storage->plan;
    return PXA_STATUS_OK;
}

static int plan_valid(const pxa_activation_plan_t *plan) {
    uint16_t index;
    if (plan == NULL || plan->manifest == NULL || plan->entries == NULL ||
        plan->entry_count == 0 || plan->package_root.data == NULL ||
        plan->package_root.size == 0) {
        return 0;
    }
    for (index = 0; index < plan->entry_count; ++index) {
        const pxa_activation_entry_t *entry = &plan->entries[index];
        if (entry->component == NULL || entry->artifact == NULL ||
            entry->component->id.data == NULL ||
            entry->component->id.size == 0 ||
            (index != 0 &&
             pxa_bytes_compare_internal(
                 plan->entries[index - 1u].component->id,
                 entry->component->id) >= 0)) {
            return 0;
        }
    }
    return 1;
}

static int engine_valid(const pxa_component_engine_t *engine) {
    return engine != NULL && engine->struct_size >= sizeof(*engine) &&
           engine->instantiate != NULL && engine->start != NULL &&
           engine->stop != NULL && engine->destroy != NULL;
}

size_t pxa_activation_coordinator_workspace_size(
    const pxa_activation_plan_t *plan) {
    size_t active_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!plan_valid(plan) ||
        sizeof(pxa_active_component_t) > SIZE_MAX / plan->entry_count) {
        return 0;
    }
    active_size = (size_t)plan->entry_count * sizeof(pxa_active_component_t);
    if (sizeof(pxa_activation_coordinator_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_activation_coordinator_t);
    if (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u > SIZE_MAX - size) return 0;
    size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (active_size > SIZE_MAX - size) return 0;
    return size + active_size;
}

pxa_status_t pxa_activation_coordinator_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_activation_plan_t *plan,
    const pxa_component_engine_t *engine,
    pxa_activation_coordinator_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    pxa_activation_coordinator_t *coordinator;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_activation_coordinator_workspace_size(plan);
    if (workspace == NULL || runtime == NULL || !engine_valid(engine) ||
        required == 0 || workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    coordinator = (pxa_activation_coordinator_t *)cursor;
    memset(coordinator, 0, sizeof(*coordinator));
    cursor += sizeof(*coordinator);
    cursor = pxa_internal_align_pointer(cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (cursor > end ||
        (size_t)plan->entry_count * sizeof(pxa_active_component_t) >
            end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    coordinator->runtime = runtime;
    coordinator->plan = plan;
    coordinator->engine = *engine;
    coordinator->active = (pxa_active_component_t *)cursor;
    memset(coordinator->active, 0,
           (size_t)plan->entry_count * sizeof(coordinator->active[0]));
    coordinator->magic = PXA_COORDINATOR_MAGIC;
    *output = coordinator;
    return PXA_STATUS_OK;
}

static int coordinator_valid(const pxa_activation_coordinator_t *coordinator) {
    return coordinator != NULL && coordinator->magic == PXA_COORDINATOR_MAGIC;
}

static const pxa_activation_entry_t *find_plan_entry(
    const pxa_activation_plan_t *plan, pxa_bytes_t component_id) {
    uint16_t begin = 0;
    uint16_t end = plan->entry_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        int comparison = pxa_bytes_compare_internal(
            plan->entries[middle].component->id, component_id);
        if (comparison < 0) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin < plan->entry_count &&
                   pxa_bytes_compare_internal(
                       plan->entries[begin].component->id, component_id) == 0
               ? &plan->entries[begin]
               : NULL;
}

static pxa_active_component_t *find_active(
    const pxa_activation_coordinator_t *coordinator,
    const pxa_activation_entry_t *entry) {
    uint16_t index;
    for (index = 0; index < coordinator->plan->entry_count; ++index) {
        if (coordinator->active[index].active &&
            coordinator->active[index].entry == entry) {
            return &coordinator->active[index];
        }
    }
    return NULL;
}

static pxa_active_component_t *free_active(
    pxa_activation_coordinator_t *coordinator) {
    uint16_t index;
    for (index = 0; index < coordinator->plan->entry_count; ++index) {
        if (!coordinator->active[index].active) {
            return &coordinator->active[index];
        }
    }
    return NULL;
}

static pxa_status_t destroy_component(
    pxa_activation_coordinator_t *coordinator, pxa_component_t component) {
    coordinator->engine.destroy(coordinator->engine.context, component);
    return pxa_component_remove(coordinator->runtime, component);
}

pxa_status_t pxa_activation_activate(
    pxa_activation_coordinator_t *coordinator, pxa_bytes_t component_id,
    uint64_t instance_id, pxa_component_t *component_output) {
    const pxa_activation_entry_t *entry;
    pxa_active_component_t *active;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_status_t status;
    uint16_t index;
    if (component_output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *component_output = PXA_COMPONENT_INVALID;
    if (!coordinator_valid(coordinator) || component_id.data == NULL ||
        component_id.size == 0 || instance_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry = find_plan_entry(coordinator->plan, component_id);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    if (find_active(coordinator, entry) != NULL) return PXA_STATUS_BUSY;
    for (index = 0; index < coordinator->plan->entry_count; ++index) {
        if (coordinator->active[index].active &&
            coordinator->active[index].instance_id == instance_id) {
            return PXA_STATUS_BUSY;
        }
    }
    active = free_active(coordinator);
    if (active == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_component_create(coordinator->runtime, instance_id,
                                  &component);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_status_normalize(coordinator->engine.instantiate(
        coordinator->engine.context, coordinator->plan->package_root, entry,
        component, instance_id));
    if (status != PXA_STATUS_OK) {
        (void)pxa_component_abort(coordinator->runtime, component,
                                  PXA_STOP_FAULT);
        (void)destroy_component(coordinator, component);
        return status;
    }
    status = pxa_component_begin_start(coordinator->runtime, component);
    if (status == PXA_STATUS_OK) {
        pxa_status_t start_status = pxa_status_normalize(
            coordinator->engine.start(coordinator->engine.context, component));
        status = pxa_component_finish_start(coordinator->runtime, component,
                                            start_status);
        if (status == PXA_STATUS_OK && start_status != PXA_STATUS_OK) {
            status = start_status;
        }
    }
    if (status != PXA_STATUS_OK) {
        coordinator->engine.stop(coordinator->engine.context, component,
                                 PXA_STOP_FAULT);
        (void)pxa_component_abort(coordinator->runtime, component,
                                  PXA_STOP_FAULT);
        (void)destroy_component(coordinator, component);
        return status;
    }
    active->entry = entry;
    active->component = component;
    active->instance_id = instance_id;
    active->active = 1;
    coordinator->active_count++;
    *component_output = component;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_activation_deactivate(
    pxa_activation_coordinator_t *coordinator, pxa_bytes_t component_id,
    pxa_stop_reason_t reason) {
    const pxa_activation_entry_t *entry;
    pxa_active_component_t *active;
    pxa_status_t status;
    pxa_status_t remove_status;
    if (!coordinator_valid(coordinator) || !stop_reason_valid(reason) ||
        component_id.data == NULL || component_id.size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry = find_plan_entry(coordinator->plan, component_id);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    active = find_active(coordinator, entry);
    if (active == NULL) return PXA_STATUS_NOT_FOUND;
    status = pxa_component_request_stop(coordinator->runtime,
                                        active->component, reason);
    if (status == PXA_STATUS_OK) {
        status = pxa_component_begin_stop(coordinator->runtime,
                                          active->component);
    }
    if (status == PXA_STATUS_OK) {
        coordinator->engine.stop(coordinator->engine.context,
                                 active->component, reason);
        status = pxa_component_finish_stop(coordinator->runtime,
                                           active->component);
    } else {
        (void)pxa_component_abort(coordinator->runtime, active->component,
                                  reason);
    }
    remove_status = destroy_component(coordinator, active->component);
    memset(active, 0, sizeof(*active));
    if (coordinator->active_count != 0) coordinator->active_count--;
    return status != PXA_STATUS_OK ? status : remove_status;
}

void pxa_activation_deactivate_all(
    pxa_activation_coordinator_t *coordinator, pxa_stop_reason_t reason) {
    uint16_t index;
    if (!coordinator_valid(coordinator) || !stop_reason_valid(reason)) return;
    for (index = 0; index < coordinator->plan->entry_count; ++index) {
        if (coordinator->active[index].active) {
            pxa_bytes_t id = coordinator->active[index].entry->component->id;
            (void)pxa_activation_deactivate(coordinator, id, reason);
        }
    }
}

pxa_status_t pxa_activation_find(
    const pxa_activation_coordinator_t *coordinator,
    pxa_bytes_t component_id, uint64_t *instance_id,
    pxa_component_t *component) {
    const pxa_activation_entry_t *entry;
    pxa_active_component_t *active;
    if (instance_id == NULL || component == NULL ||
        !coordinator_valid(coordinator) || component_id.data == NULL ||
        component_id.size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *instance_id = 0;
    *component = PXA_COMPONENT_INVALID;
    entry = find_plan_entry(coordinator->plan, component_id);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    active = find_active(coordinator, entry);
    if (active == NULL) return PXA_STATUS_NOT_FOUND;
    *instance_id = active->instance_id;
    *component = active->component;
    return PXA_STATUS_OK;
}
