#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/activation.h"
#include "pxa/package.h"
#include "pxa/runtime.h"
#include "pxa/slot_transaction.h"

typedef struct {
    pxa_slot_state_t states[3];
    size_t operations;
    size_t fail_operation;
    size_t barriers;
} slot_store_t;

static size_t slot_index(pxa_package_slot_t slot) {
    assert(slot >= PXA_PACKAGE_SLOT_CURRENT &&
           slot <= PXA_PACKAGE_SLOT_INCOMING);
    return (size_t)slot - 1u;
}

static pxa_status_t maybe_fail(slot_store_t *store) {
    store->operations++;
    return store->fail_operation == store->operations
               ? PXA_STATUS_INTERNAL
               : PXA_STATUS_OK;
}

static pxa_status_t inspect_slot(void *context, pxa_package_slot_t slot,
                                 pxa_slot_state_t *state) {
    slot_store_t *store = (slot_store_t *)context;
    pxa_status_t status = maybe_fail(store);
    if (status == PXA_STATUS_OK) *state = store->states[slot_index(slot)];
    return status;
}

static pxa_status_t remove_slot(void *context, pxa_package_slot_t slot) {
    slot_store_t *store = (slot_store_t *)context;
    pxa_status_t status = maybe_fail(store);
    if (status == PXA_STATUS_OK) store->states[slot_index(slot)] = PXA_SLOT_ABSENT;
    return status;
}

static pxa_status_t rename_slot(void *context, pxa_package_slot_t from,
                                pxa_package_slot_t to) {
    slot_store_t *store = (slot_store_t *)context;
    pxa_status_t status = maybe_fail(store);
    if (status != PXA_STATUS_OK) return status;
    assert(store->states[slot_index(from)] != PXA_SLOT_ABSENT &&
           store->states[slot_index(to)] == PXA_SLOT_ABSENT);
    store->states[slot_index(to)] = store->states[slot_index(from)];
    store->states[slot_index(from)] = PXA_SLOT_ABSENT;
    return PXA_STATUS_OK;
}

static pxa_status_t slot_barrier(void *context) {
    slot_store_t *store = (slot_store_t *)context;
    pxa_status_t status = maybe_fail(store);
    if (status == PXA_STATUS_OK) store->barriers++;
    return status;
}

static pxa_slot_storage_t make_storage(slot_store_t *store) {
    pxa_slot_storage_t storage;
    storage.struct_size = sizeof(storage);
    storage.context = store;
    storage.inspect = inspect_slot;
    storage.remove = remove_slot;
    storage.rename = rename_slot;
    storage.barrier = slot_barrier;
    return storage;
}

static void test_recovery_states(void) {
    pxa_slot_state_t values[] = {
        PXA_SLOT_ABSENT, PXA_SLOT_VERIFIED, PXA_SLOT_CORRUPT,
    };
    size_t current;
    size_t previous;
    size_t incoming;
    for (current = 0; current < 3; ++current) {
        for (previous = 0; previous < 3; ++previous) {
            for (incoming = 0; incoming < 3; ++incoming) {
                slot_store_t store = {
                    {values[current], values[previous], values[incoming]},
                    0, 0, 0,
                };
                pxa_slot_storage_t storage = make_storage(&store);
                pxa_status_t status = pxa_slots_recover(&storage);
                assert(status == PXA_STATUS_OK || status == PXA_STATUS_DENIED);
                if (status != PXA_STATUS_OK) continue;
                assert(store.states[2] == PXA_SLOT_ABSENT);
                assert(store.states[0] != PXA_SLOT_CORRUPT &&
                       store.states[1] != PXA_SLOT_CORRUPT);
                store.operations = 0;
                assert(pxa_slots_recover(&storage) == PXA_STATUS_OK);
                assert(store.states[2] == PXA_SLOT_ABSENT);
            }
        }
    }
}

static void test_commit_and_recovery(void) {
    size_t failure;
    slot_store_t completed = {
        {PXA_SLOT_VERIFIED, PXA_SLOT_VERIFIED, PXA_SLOT_VERIFIED},
        0, 0, 0,
    };
    pxa_slot_storage_t storage = make_storage(&completed);
    assert(pxa_slots_commit_incoming(&storage, NULL) == PXA_STATUS_OK);
    assert(completed.states[0] == PXA_SLOT_VERIFIED &&
           completed.states[1] == PXA_SLOT_VERIFIED &&
           completed.states[2] == PXA_SLOT_ABSENT && completed.barriers == 3);

    for (failure = 1; failure <= 9; ++failure) {
        slot_store_t interrupted = {
            {PXA_SLOT_VERIFIED, PXA_SLOT_VERIFIED, PXA_SLOT_VERIFIED},
            0, failure, 0,
        };
        pxa_slot_storage_t interrupted_storage = make_storage(&interrupted);
        pxa_status_t status =
            pxa_slots_commit_incoming(&interrupted_storage, NULL);
        if (status == PXA_STATUS_OK) continue;
        interrupted.fail_operation = 0;
        interrupted.operations = 0;
        assert(pxa_slots_recover(&interrupted_storage) == PXA_STATUS_OK);
        assert(interrupted.states[0] == PXA_SLOT_VERIFIED &&
               interrupted.states[2] == PXA_SLOT_ABSENT);
    }
}

static uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f') {
        return (uint8_t)(value - 'a' + 10);
    }
    assert(0);
    return 0;
}

static uint8_t *load_golden_hex(const char *field, size_t *size) {
    FILE *file = fopen(PXA_TEST_GOLDEN_PATH, "rb");
    long file_size;
    char *json;
    char pattern[64];
    char *start;
    char *end;
    uint8_t *bytes;
    size_t index;
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    file_size = ftell(file);
    assert(file_size > 0 && fseek(file, 0, SEEK_SET) == 0);
    json = (char *)malloc((size_t)file_size + 1u);
    assert(json != NULL);
    assert(fread(json, 1, (size_t)file_size, file) == (size_t)file_size);
    assert(fclose(file) == 0);
    json[file_size] = '\0';
    assert(snprintf(pattern, sizeof(pattern), "\"%s\": \"", field) > 0);
    start = strstr(json, pattern);
    assert(start != NULL);
    start += strlen(pattern);
    end = strchr(start, '"');
    assert(end != NULL && ((size_t)(end - start) & 1u) == 0);
    *size = (size_t)(end - start) / 2u;
    bytes = (uint8_t *)malloc(*size);
    assert(bytes != NULL);
    for (index = 0; index < *size; ++index) {
        bytes[index] = (uint8_t)((hex_nibble(start[index * 2u]) << 4) |
                                 hex_nibble(start[index * 2u + 1u]));
    }
    free(json);
    return bytes;
}

static int bytes_are(pxa_bytes_t bytes, const char *text) {
    size_t size = strlen(text);
    return bytes.size == size && memcmp(bytes.data, text, size) == 0;
}

typedef struct {
    pxa_runtime_t *runtime;
    pxa_status_t instantiate_status;
    pxa_status_t start_status;
    unsigned instantiates;
    unsigned starts;
    unsigned stops;
    unsigned destroys;
    pxa_stop_reason_t last_stop_reason;
} test_component_engine_t;

static pxa_status_t engine_instantiate(
    void *context, pxa_bytes_t package_root,
    const pxa_activation_entry_t *entry, pxa_component_t component,
    uint64_t instance_id) {
    test_component_engine_t *engine = (test_component_engine_t *)context;
    assert(bytes_are(package_root, "/verified/package"));
    assert(entry != NULL && entry->component != NULL &&
           entry->artifact != NULL && component != PXA_COMPONENT_INVALID &&
           instance_id != 0);
    if (bytes_are(entry->component->id, "main")) {
        assert(bytes_are(entry->artifact->path,
                         "artifacts/main.esp32s3.aot"));
    } else {
        assert(bytes_are(entry->component->id, "sync") &&
               bytes_are(entry->artifact->path, "artifacts/sync.wasm"));
    }
    engine->instantiates++;
    return engine->instantiate_status;
}

static pxa_status_t engine_start(void *context, pxa_component_t component) {
    test_component_engine_t *engine = (test_component_engine_t *)context;
    assert(component != PXA_COMPONENT_INVALID);
    engine->starts++;
    return engine->start_status;
}

static void engine_stop(void *context, pxa_component_t component,
                        pxa_stop_reason_t reason) {
    test_component_engine_t *engine = (test_component_engine_t *)context;
    assert(component != PXA_COMPONENT_INVALID);
    engine->stops++;
    engine->last_stop_reason = reason;
}

static void engine_destroy(void *context, pxa_component_t component) {
    test_component_engine_t *engine = (test_component_engine_t *)context;
    pxa_component_snapshot_t snapshot;
    assert(component != PXA_COMPONENT_INVALID);
    assert(pxa_component_snapshot(engine->runtime, component, &snapshot) ==
               PXA_STATUS_OK &&
           snapshot.state == PXA_COMPONENT_STOPPED);
    engine->destroys++;
}

static void test_activation(
    const pxa_package_manifest_t *manifest,
    const pxa_package_activation_profile_t *activation,
    const pxa_package_host_profile_t *host) {
    static const uint8_t main_id[] = "main";
    static const uint8_t sync_id[] = "sync";
    pxa_runtime_limits_t runtime_limits;
    pxa_runtime_t *runtime = NULL;
    pxa_activation_plan_t *plan = NULL;
    pxa_activation_coordinator_t *coordinator = NULL;
    pxa_component_t main_component;
    pxa_component_t sync_component;
    pxa_component_t found_component;
    uint64_t found_instance;
    size_t runtime_size;
    size_t plan_size;
    size_t coordinator_size;
    void *runtime_workspace;
    void *plan_workspace;
    void *coordinator_workspace;
    test_component_engine_t engine_state = {
        NULL, PXA_STATUS_OK, PXA_STATUS_OK, 0, 0, 0, 0, PXA_STOP_NORMAL,
    };
    pxa_component_engine_t engine = {
        sizeof(engine), &engine_state, engine_instantiate, engine_start,
        engine_stop, engine_destroy,
    };
    pxa_bytes_t main = {main_id, sizeof(main_id) - 1u};
    pxa_bytes_t sync = {sync_id, sizeof(sync_id) - 1u};

    pxa_runtime_limits_init(&runtime_limits);
    runtime_limits.max_components = 2;
    runtime_size = pxa_runtime_workspace_size(&runtime_limits);
    assert(runtime_size != 0);
    runtime_workspace = malloc(runtime_size);
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace, runtime_size, &runtime_limits,
                            &runtime) == PXA_STATUS_OK);
    engine_state.runtime = runtime;

    plan_size = pxa_activation_plan_workspace_size(manifest->component_count);
    plan_workspace = malloc(plan_size);
    assert(plan_workspace != NULL);
    assert(pxa_activation_plan_prepare(
               plan_workspace, plan_size, manifest, activation, host,
               (pxa_bytes_t){(const uint8_t *)"/verified/package", 17},
               &plan) == PXA_STATUS_OK);
    assert(plan != NULL && plan->entry_count == 2 &&
           bytes_are(plan->entries[0].artifact->path,
                     "artifacts/main.esp32s3.aot") &&
           bytes_are(plan->entries[1].artifact->path,
                     "artifacts/sync.wasm"));
    {
        pxa_activation_plan_t invalid_plan = {0};
        pxa_activation_entry_t reversed[2] = {
            plan->entries[1], plan->entries[0],
        };
        assert(pxa_activation_coordinator_workspace_size(&invalid_plan) == 0);
        invalid_plan = *plan;
        invalid_plan.entries = reversed;
        assert(pxa_activation_coordinator_workspace_size(&invalid_plan) == 0);
    }

    coordinator_size = pxa_activation_coordinator_workspace_size(plan);
    coordinator_workspace = malloc(coordinator_size);
    assert(coordinator_workspace != NULL);
    assert(pxa_activation_coordinator_init(
               coordinator_workspace, coordinator_size, runtime, plan, &engine,
               &coordinator) == PXA_STATUS_OK);

    assert(pxa_activation_activate(coordinator, main, 101, &main_component) ==
           PXA_STATUS_OK);
    assert(pxa_activation_activate(coordinator, sync, 102, &sync_component) ==
           PXA_STATUS_OK);
    assert(main_component != sync_component && engine_state.instantiates == 2 &&
           engine_state.starts == 2);
    assert(pxa_activation_activate(coordinator, main, 103, &found_component) ==
           PXA_STATUS_BUSY);
    assert(pxa_activation_activate(coordinator, main, 102, &found_component) ==
           PXA_STATUS_BUSY);
    assert(pxa_activation_find(coordinator, sync, &found_instance,
                               &found_component) == PXA_STATUS_OK &&
           found_instance == 102 && found_component == sync_component);

    assert(pxa_activation_deactivate(coordinator, main, PXA_STOP_REPLACED) ==
           PXA_STATUS_OK);
    assert(engine_state.stops == 1 && engine_state.destroys == 1 &&
           engine_state.last_stop_reason == PXA_STOP_REPLACED);

    engine_state.instantiate_status = PXA_STATUS_DENIED;
    assert(pxa_activation_activate(coordinator, main, 103, &found_component) ==
           PXA_STATUS_DENIED);
    assert(engine_state.instantiates == 3 && engine_state.starts == 2 &&
           engine_state.stops == 1 && engine_state.destroys == 2);
    engine_state.instantiate_status = PXA_STATUS_OK;
    engine_state.start_status = PXA_STATUS_INTERNAL;
    assert(pxa_activation_activate(coordinator, main, 104, &found_component) ==
           PXA_STATUS_INTERNAL);
    assert(engine_state.instantiates == 4 && engine_state.starts == 3 &&
           engine_state.stops == 2 && engine_state.destroys == 3 &&
           engine_state.last_stop_reason == PXA_STOP_FAULT);
    assert(pxa_activation_find(coordinator, main, &found_instance,
                               &found_component) == PXA_STATUS_NOT_FOUND);

    engine_state.start_status = PXA_STATUS_OK;
    pxa_activation_deactivate_all(coordinator, PXA_STOP_SHUTDOWN);
    assert(engine_state.stops == 3 && engine_state.destroys == 4 &&
           engine_state.last_stop_reason == PXA_STOP_SHUTDOWN);
    assert(pxa_activation_find(coordinator, sync, &found_instance,
                               &found_component) == PXA_STATUS_NOT_FOUND);

    pxa_runtime_deinit(runtime);
    free(coordinator_workspace);
    free(plan_workspace);
    free(runtime_workspace);
}

typedef struct {
    const uint8_t *manifest;
    size_t manifest_size;
    unsigned calls;
} signature_verifier_t;

static pxa_status_t verify_signature(
    void *context, pxa_bytes_t publisher_key_id, pxa_bytes_t domain,
    pxa_bytes_t manifest, pxa_bytes_t signature) {
    static const uint8_t expected_domain[] = "PXA-PACKAGE-MANIFEST\0";
    signature_verifier_t *verifier = (signature_verifier_t *)context;
    assert(publisher_key_id.size == PXA_PACKAGE_DIGEST_BYTES &&
           publisher_key_id.data[0] == 1 && publisher_key_id.data[31] == 32);
    assert(domain.size == sizeof(expected_domain) - 1 &&
           memcmp(domain.data, expected_domain, domain.size) == 0);
    assert(manifest.data == verifier->manifest &&
           manifest.size == verifier->manifest_size);
    assert(signature.size == PXA_PACKAGE_SIGNATURE_BYTES);
    verifier->calls++;
    return PXA_STATUS_OK;
}

static void test_package_golden(void) {
    size_t manifest_size;
    size_t signature_size;
    uint8_t *manifest_bytes = load_golden_hex("manifest_hex", &manifest_size);
    uint8_t *signature_bytes =
        load_golden_hex("signature_hex", &signature_size);
    pxa_package_limits_t limits;
    size_t workspace_size;
    void *workspace;
    pxa_package_manifest_t *manifest = NULL;
    pxa_package_signature_t signature;
    signature_verifier_t verifier;
    pxa_package_inventory_entry_t inventory[5];
    pxa_package_service_capability_t capabilities[3];
    pxa_package_activation_profile_t activation;
    pxa_package_host_profile_t host;
    const pxa_package_artifact_t *artifact = NULL;
    uint8_t *mutated;
    uint8_t *manifest_v02;
    pxa_package_limits_t default_limits;
    pxa_package_limits_t measured_limits;
    void *measured_workspace;
    size_t measured_workspace_size;
    size_t insert_offset;
    uint16_t index;
    size_t mutation_index;
    uint32_t mutation_state = UINT32_C(0x91e10da5);

    pxa_package_limits_init(&limits);
    limits.max_components = 2;
    limits.max_artifacts = 4;
    limits.max_services = 3;
    limits.max_files = 5;
    limits.max_permissions = 2;
    limits.max_ipc_endpoints = 1;
    pxa_package_limits_init(&default_limits);
    assert(pxa_package_manifest_measure(
               (pxa_bytes_t){manifest_bytes, manifest_size},
               &default_limits, &measured_limits) == PXA_STATUS_OK);
    assert(measured_limits.max_components == 2 &&
           measured_limits.max_artifacts == 4 &&
           measured_limits.max_services == 3 &&
           measured_limits.max_files == 5 &&
           measured_limits.max_permissions == 2 &&
           measured_limits.max_ipc_endpoints == 0);
    measured_workspace_size =
        pxa_package_manifest_workspace_size(&measured_limits);
    assert(measured_workspace_size != 0 &&
           measured_workspace_size <
               pxa_package_manifest_workspace_size(&default_limits));
    measured_workspace = malloc(measured_workspace_size);
    assert(measured_workspace != NULL);
    assert(pxa_package_manifest_parse(
               measured_workspace, measured_workspace_size,
               (pxa_bytes_t){manifest_bytes, manifest_size},
               &measured_limits, &manifest) == PXA_STATUS_OK);
    free(measured_workspace);
    workspace_size = pxa_package_manifest_workspace_size(&limits);
    assert(workspace_size != 0);
    workspace = malloc(workspace_size);
    assert(workspace != NULL);
    memset(workspace, 0xa5, workspace_size);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){manifest_bytes, manifest_size}, &limits,
               &manifest) == PXA_STATUS_OK);
    assert(manifest != NULL && manifest->component_count == 2 &&
           manifest->artifact_count == 4 && manifest->service_count == 3 &&
           manifest->file_count == 5 && manifest->permission_count == 2 &&
           bytes_are(manifest->app_id, "com.example.reader") &&
           bytes_are(manifest->version, "0.3.0") &&
           bytes_are(manifest->components[0].id, "main") &&
           bytes_are(manifest->components[1].id, "sync"));

    insert_offset = 12;
    while (insert_offset < manifest_size &&
           pxa_read_u16(manifest_bytes + insert_offset) < 16) {
        insert_offset += 4 + pxa_read_u16(manifest_bytes + insert_offset + 2);
    }
    assert(insert_offset < manifest_size);
    manifest_v02 = malloc(manifest_size + 12);
    assert(manifest_v02 != NULL);
    memcpy(manifest_v02, manifest_bytes, insert_offset);
    pxa_write_u16(manifest_v02 + 6, 2);
    pxa_write_u32(manifest_v02 + 8, (uint32_t)(manifest_size - 12 + 12));
    pxa_write_u16(manifest_v02 + insert_offset, 9);
    pxa_write_u16(manifest_v02 + insert_offset + 2, 8);
    pxa_write_u64(manifest_v02 + insert_offset + 4, 7);
    memcpy(manifest_v02 + insert_offset + 12,
           manifest_bytes + insert_offset, manifest_size - insert_offset);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){manifest_v02, manifest_size + 12}, &limits,
               &manifest) == PXA_STATUS_OK);
    assert(manifest->format_minor == 2 && manifest->has_release_sequence &&
           manifest->release_sequence == 7);
    pxa_write_u64(manifest_v02 + insert_offset + 4, 0);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){manifest_v02, manifest_size + 12}, &limits,
               &manifest) == PXA_STATUS_INVALID_ARGUMENT);
    free(manifest_v02);

    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){manifest_bytes, manifest_size}, &limits,
               &manifest) == PXA_STATUS_OK);

    assert(pxa_package_signature_parse(
               (pxa_bytes_t){signature_bytes, signature_size}, &signature) ==
           PXA_STATUS_OK);
    assert(pxa_package_signature_identity_validate(manifest, &signature) ==
           PXA_STATUS_OK);
    verifier.manifest = manifest_bytes;
    verifier.manifest_size = manifest_size;
    verifier.calls = 0;
    assert(pxa_package_signature_verify(manifest, &signature, &verifier,
                                        verify_signature) == PXA_STATUS_OK &&
           verifier.calls == 1);

    for (index = 0; index < manifest->file_count; ++index) {
        inventory[index].path = manifest->files[index].path;
        inventory[index].size = manifest->files[index].size;
        inventory[index].sha256 = manifest->files[index].sha256;
    }
    assert(pxa_package_inventory_validate(manifest, inventory, 5) ==
           PXA_STATUS_OK);
    inventory[0].size++;
    assert(pxa_package_inventory_validate(manifest, inventory, 5) ==
           PXA_STATUS_DENIED);
    inventory[0].size--;

    capabilities[0] = (pxa_package_service_capability_t){
        2, {0, 1}, 0,
    };
    capabilities[1] = (pxa_package_service_capability_t){
        3, {0, 1}, 1,
    };
    capabilities[2] = (pxa_package_service_capability_t){
        7, {0, 1}, 0,
    };
    activation.core_version = (pxa_package_version_t){0, 1};
    activation.services = capabilities;
    activation.service_count = 3;
    assert(pxa_package_requirements_validate(
               manifest, &manifest->components[0], &activation) ==
           PXA_STATUS_OK);
    host.target = (pxa_bytes_t){(const uint8_t *)"esp32-s3", 8};
    host.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    host.engine_abi =
        (pxa_bytes_t){(const uint8_t *)"wamr-2.4.0-aot-v1-pxa0", 22};
    host.supported_features = 1;
    host.memory_model = PXA_MEMORY_WASM32;
    assert(pxa_package_artifact_select(&manifest->components[0], &host,
                                       &artifact) == PXA_STATUS_OK);
    assert(artifact != NULL &&
           bytes_are(artifact->path, "artifacts/main.esp32s3.aot"));
    assert(pxa_package_same_identity(manifest, manifest));
    test_activation(manifest, &activation, &host);

    mutated = (uint8_t *)malloc(manifest_size);
    assert(mutated != NULL);
    memcpy(mutated, manifest_bytes, manifest_size);
    mutated[13] |= UINT8_C(0x80);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){mutated, manifest_size}, &limits,
               &manifest) == PXA_STATUS_INVALID_ARGUMENT);
    memcpy(mutated, manifest_bytes, manifest_size);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){mutated, manifest_size - 1}, &limits,
               &manifest) == PXA_STATUS_INVALID_ARGUMENT);
    for (mutation_index = 0; mutation_index < manifest_size; ++mutation_index) {
        assert(pxa_package_manifest_parse(
                   workspace, workspace_size,
                   (pxa_bytes_t){manifest_bytes, mutation_index}, &limits,
                   &manifest) == PXA_STATUS_INVALID_ARGUMENT &&
               manifest == NULL);
    }
    for (mutation_index = 0; mutation_index < 2000; ++mutation_index) {
        size_t flip;
        size_t flip_count;
        pxa_status_t status;
        memcpy(mutated, manifest_bytes, manifest_size);
        mutation_state = mutation_state * UINT32_C(1664525) +
                         UINT32_C(1013904223);
        flip_count = 1u + mutation_state % 4u;
        for (flip = 0; flip < flip_count; ++flip) {
            size_t position;
            mutation_state = mutation_state * UINT32_C(1664525) +
                             UINT32_C(1013904223);
            position = mutation_state % manifest_size;
            mutated[position] ^=
                (uint8_t)(UINT8_C(1) << ((mutation_state >> 16) & 7u));
        }
        status = pxa_package_manifest_parse(
            workspace, workspace_size,
            (pxa_bytes_t){mutated, manifest_size}, &limits, &manifest);
        assert(status == PXA_STATUS_OK ||
               status == PXA_STATUS_INVALID_ARGUMENT ||
               status == PXA_STATUS_UNSUPPORTED ||
               status == PXA_STATUS_RESOURCE_LIMIT);
        if (status == PXA_STATUS_OK) {
            assert(manifest != NULL && manifest->encoded.data == mutated &&
                   manifest->encoded.size == manifest_size &&
                   manifest->component_count <= limits.max_components &&
                   manifest->artifact_count <= limits.max_artifacts &&
                   manifest->service_count <= limits.max_services &&
                   manifest->file_count <= limits.max_files &&
                   manifest->permission_count <= limits.max_permissions &&
                   manifest->ipc_endpoint_count <= limits.max_ipc_endpoints);
        } else {
            assert(manifest == NULL);
        }
    }
    limits.max_files = 4;
    assert(pxa_package_manifest_measure(
               (pxa_bytes_t){manifest_bytes, manifest_size}, &limits,
               &measured_limits) == PXA_STATUS_RESOURCE_LIMIT);
    assert(pxa_package_manifest_parse(
               workspace, workspace_size,
               (pxa_bytes_t){manifest_bytes, manifest_size}, &limits,
               &manifest) == PXA_STATUS_RESOURCE_LIMIT);

    signature_bytes[0] = 'X';
    assert(pxa_package_signature_parse(
               (pxa_bytes_t){signature_bytes, signature_size}, &signature) ==
           PXA_STATUS_INVALID_ARGUMENT);
    free(mutated);
    free(workspace);
    free(signature_bytes);
    free(manifest_bytes);
}

static void test_localized_metadata(void) {
    static const uint8_t default_name[] = "Weather";
    static const uint8_t default_description[] = "Weather near you";
    static const uint8_t english_description[] = "Nearby conditions";
    static const uint8_t english_name[] = "Local Weather";
    static const uint8_t chinese_name[] = "\xe5\xa4\xa9\xe6\xb0\x94";
    static pxa_package_localization_t localizations[] = {
        {{(const uint8_t *)"en", 2},
         {NULL, 0},
         {english_description, sizeof(english_description) - 1u},
         {NULL, 0}},
        {{(const uint8_t *)"en-US", 5},
         {english_name, sizeof(english_name) - 1u},
         {NULL, 0},
         {NULL, 0}},
        {{(const uint8_t *)"zh-CN", 5},
         {chinese_name, sizeof(chinese_name) - 1u},
         {NULL, 0},
         {NULL, 0}},
    };
    pxa_package_manifest_t manifest;
    pxa_package_metadata_t metadata;

    memset(&manifest, 0, sizeof(manifest));
    manifest.name =
        (pxa_bytes_t){default_name, sizeof(default_name) - 1u};
    manifest.description = (pxa_bytes_t){
        default_description, sizeof(default_description) - 1u};
    manifest.localizations = localizations;
    manifest.localization_count = 3;

    assert(pxa_package_metadata_resolve(
               &manifest, (pxa_bytes_t){(const uint8_t *)"en-US", 5},
               &metadata) == PXA_STATUS_OK);
    assert(bytes_are(metadata.name, "Local Weather") &&
           bytes_are(metadata.description, "Nearby conditions") &&
           metadata.localized_fields ==
               (PXA_PACKAGE_METADATA_NAME |
                PXA_PACKAGE_METADATA_DESCRIPTION));
    assert(pxa_package_metadata_resolve(
               &manifest, (pxa_bytes_t){(const uint8_t *)"zh-CN", 5},
               &metadata) == PXA_STATUS_OK);
    assert(metadata.name.size == sizeof(chinese_name) - 1u &&
           memcmp(metadata.name.data, chinese_name, metadata.name.size) == 0);
    assert(pxa_package_metadata_resolve(
               &manifest, (pxa_bytes_t){(const uint8_t *)"fr-FR", 5},
               &metadata) == PXA_STATUS_OK);
    assert(bytes_are(metadata.name, "Weather") &&
           metadata.localized_fields == 0);
    assert(pxa_package_metadata_resolve(
               &manifest, (pxa_bytes_t){(const uint8_t *)"EN-us", 5},
               &metadata) == PXA_STATUS_INVALID_ARGUMENT);
}

int main(void) {
    test_recovery_states();
    test_commit_and_recovery();
    test_package_golden();
    test_localized_metadata();
    return 0;
}
