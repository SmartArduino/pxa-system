#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/wasi.h"

#define PXA_TEST_PATH_CAPACITY 1024
#define PXA_PACKAGE_TOOL_INVENTORY_COUNT 6u

static int bytes_equal_text(pxa_bytes_t value, const char *text) {
    size_t size = strlen(text);
    return value.size == size && memcmp(value.data, text, size) == 0;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long file_size;
    uint8_t *data;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    data = malloc((size_t)file_size);
    if (data == NULL || fread(data, 1, (size_t)file_size, file) !=
                            (size_t)file_size ||
        fclose(file) != 0) {
        free(data);
        return NULL;
    }
    *size = (size_t)file_size;
    return data;
}

static const pxa_package_component_t *find_component(
    const pxa_package_manifest_t *manifest, const char *id) {
    uint16_t index;
    for (index = 0; index < manifest->component_count; ++index) {
        if (bytes_equal_text(manifest->components[index].id, id)) {
            return &manifest->components[index];
        }
    }
    return NULL;
}

static const pxa_package_service_requirement_t *find_service(
    const pxa_package_component_t *component, uint16_t service) {
    uint16_t index;
    for (index = 0; index < component->service_count; ++index) {
        if (component->services[index].service == service)
            return &component->services[index];
    }
    return NULL;
}

int main(int argc, char **argv) {
    static const char *const inventory_paths[PXA_PACKAGE_TOOL_INVENTORY_COUNT] = {
        "artifacts/main.wasm",
        "artifacts/main.linux-x86_64.aot",
        "artifacts/main.esp32-s3.aot",
        "artifacts/responder.wasm",
        "assets/SOURCES.md",
        "assets/flappy-bird/icon.png",
    };
    pxa_package_limits_t limits;
    pxa_package_manifest_t *manifest = NULL;
    pxa_package_signature_t signature;
    pxa_package_inventory_entry_t inventory[PXA_PACKAGE_TOOL_INVENTORY_COUNT];
    uint8_t digests[PXA_PACKAGE_TOOL_INVENTORY_COUNT][PXA_OPENSSL_SHA256_BYTES];
    pxa_openssl_publisher_key_t key;
    pxa_openssl_trust_t trust;
    const pxa_package_component_t *main_component;
    const pxa_package_component_t *responder_component;
    const pxa_package_service_requirement_t *ui_requirement;
    const pxa_package_service_requirement_t *net_requirement;
    const pxa_package_service_requirement_t *wasi_requirement;
    const pxa_package_artifact_t *artifact = NULL;
    pxa_package_host_profile_t host;
    pxa_package_metadata_t chinese_metadata;
    void *workspace = NULL;
    uint8_t *manifest_data = NULL;
    uint8_t *signature_data = NULL;
    uint8_t *key_data = NULL;
    size_t manifest_size = 0;
    size_t signature_size = 0;
    size_t key_size = 0;
    size_t index;
    int result = 1;

    if (argc != 3) return 2;
    {
        char path[PXA_TEST_PATH_CAPACITY];
        if (snprintf(path, sizeof(path), "%s/manifest.pxm", argv[1]) >=
            (int)sizeof(path)) {
            return 3;
        }
        manifest_data = read_file(path, &manifest_size);
        if (snprintf(path, sizeof(path), "%s/signature.pxs", argv[1]) >=
            (int)sizeof(path)) {
            free(manifest_data);
            return 3;
        }
        signature_data = read_file(path, &signature_size);
    }
    key_data = read_file(argv[2], &key_size);
    if (manifest_data == NULL || signature_data == NULL || key_data == NULL) {
        result = 4;
        goto done;
    }

    pxa_package_limits_init(&limits);
    workspace = malloc(pxa_package_manifest_workspace_size(&limits));
    if (workspace == NULL ||
        pxa_package_manifest_parse(
            workspace, pxa_package_manifest_workspace_size(&limits),
            (pxa_bytes_t){manifest_data, manifest_size}, &limits,
            &manifest) != PXA_STATUS_OK ||
        pxa_package_signature_parse((pxa_bytes_t){signature_data, signature_size},
                                    &signature) != PXA_STATUS_OK ||
        pxa_package_signature_identity_validate(manifest, &signature) !=
            PXA_STATUS_OK) {
        result = 5;
        goto done;
    }

    key = (pxa_openssl_publisher_key_t){key_data, key_size};
    trust = (pxa_openssl_trust_t){sizeof(trust), &key, 1};
    if (pxa_package_signature_verify(manifest, &signature, &trust,
                                     pxa_openssl_p256_verify) != PXA_STATUS_OK) {
        result = 6;
        goto done;
    }

    for (index = 0; index < PXA_PACKAGE_TOOL_INVENTORY_COUNT; ++index) {
        char path[PXA_TEST_PATH_CAPACITY];
        uint8_t *data;
        size_t size;
        if (snprintf(path, sizeof(path), "%s/%s", argv[1],
                     inventory_paths[index]) >= (int)sizeof(path)) {
            result = 7;
            goto done;
        }
        data = read_file(path, &size);
        if (data == NULL ||
            pxa_openssl_sha256(data, size, digests[index]) != PXA_STATUS_OK) {
            free(data);
            result = 7;
            goto done;
        }
        free(data);
        inventory[index] = (pxa_package_inventory_entry_t){
            {(const uint8_t *)inventory_paths[index], strlen(inventory_paths[index])},
            size, digests[index]};
    }
    if (manifest->file_count != (uint16_t)PXA_PACKAGE_TOOL_INVENTORY_COUNT ||
        pxa_package_inventory_validate(manifest, inventory,
                                       PXA_PACKAGE_TOOL_INVENTORY_COUNT) !=
            PXA_STATUS_OK) {
        result = 8;
        goto done;
    }
    if (pxa_package_file_find(
            manifest,
            (pxa_bytes_t){(const uint8_t *)"assets/SOURCES.md",
                          sizeof("assets/SOURCES.md") - 1u}) == NULL ||
        pxa_package_file_find(
            manifest,
            (pxa_bytes_t){(const uint8_t *)"assets/sources.md",
                          sizeof("assets/sources.md") - 1u}) != NULL) {
        result = 8;
        goto done;
    }

    main_component = find_component(manifest, "main");
    responder_component = find_component(manifest, "responder");
    ui_requirement = main_component == NULL ? NULL : find_service(main_component, 3);
    net_requirement = main_component == NULL ? NULL : find_service(main_component, 9);
    wasi_requirement = main_component == NULL ? NULL : find_service(main_component, 14);
    if (manifest->format_minor != PXA_PACKAGE_MANIFEST_FORMAT_MINOR ||
        manifest->component_count != 2 || manifest->permission_count != 1 ||
        manifest->ipc_endpoint_count != 1 || main_component == NULL ||
        responder_component == NULL || ui_requirement == NULL ||
        net_requirement == NULL || wasi_requirement == NULL ||
        ui_requirement->min_version.major != 0 ||
        ui_requirement->min_version.minor != 3 ||
        ui_requirement->max_version.major != 0 ||
        ui_requirement->max_version.minor != 3 ||
        net_requirement->min_version.major != 0 ||
        net_requirement->min_version.minor != 2 ||
        net_requirement->max_version.major != 0 ||
        net_requirement->max_version.minor != 2 ||
        wasi_requirement->min_version.major != 0 ||
        wasi_requirement->min_version.minor != 1 ||
        wasi_requirement->max_version.major != 0 ||
        wasi_requirement->max_version.minor != 1 ||
        wasi_requirement->required_features !=
            (PXA_WASI_FEATURE_STDIO | PXA_WASI_FEATURE_MONOTONIC_CLOCK) ||
        main_component->flags != PXA_PACKAGE_COMPONENT_FLAG_PINNED_MEMORY ||
        responder_component->flags != PXA_PACKAGE_COMPONENT_FLAG_PINNED_MEMORY ||
        main_component->artifact_count != 3 ||
        responder_component->artifact_count != 1 ||
        !bytes_equal_text(manifest->permissions[0].name, "net.client") ||
        !bytes_equal_text(manifest->permissions[0].scope, "api.example") ||
        !bytes_equal_text(manifest->ipc_endpoints[0].name, "demo.echo") ||
        !bytes_equal_text(manifest->ipc_endpoints[0].component_id, "responder") ||
        !bytes_equal_text(manifest->name, "PXA Arcade") ||
        !bytes_equal_text(manifest->description,
                          "A collection of compact arcade games") ||
        !bytes_equal_text(manifest->icon_path,
                          "assets/flappy-bird/icon.png") ||
        pxa_package_metadata_resolve(
            manifest,
            (pxa_bytes_t){(const uint8_t *)"zh-CN", 5},
            &chinese_metadata) != PXA_STATUS_OK ||
        !bytes_equal_text(chinese_metadata.name, "PXA 游戏厅") ||
        !bytes_equal_text(chinese_metadata.description,
                          "多款轻量街机游戏合集")) {
        result = 9;
        goto done;
    }

    host = (pxa_package_host_profile_t){
        {(const uint8_t *)"linux-x86_64", sizeof("linux-x86_64") - 1u},
        {(const uint8_t *)"wamr", sizeof("wamr") - 1u},
        {(const uint8_t *)PXSYS_WAMR_ENGINE_ABI,
         sizeof(PXSYS_WAMR_ENGINE_ABI) - 1u},
        0, PXA_MEMORY_WASM32};
    if (pxa_package_artifact_select(main_component, &host, &artifact) !=
            PXA_STATUS_OK ||
        artifact == NULL || artifact->kind != PXA_ARTIFACT_AOT) {
        result = 10;
        goto done;
    }
    host.target = (pxa_bytes_t){(const uint8_t *)"unsupported-target",
                                 sizeof("unsupported-target") - 1u};
    if (pxa_package_artifact_select(main_component, &host, &artifact) !=
            PXA_STATUS_OK ||
        artifact == NULL || artifact->kind != PXA_ARTIFACT_WASM) {
        result = 11;
        goto done;
    }
    host.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64",
                                 sizeof("linux-x86_64") - 1u};
    if (pxa_package_artifact_select(responder_component, &host, &artifact) !=
            PXA_STATUS_OK ||
        artifact == NULL || artifact->kind != PXA_ARTIFACT_WASM) {
        result = 12;
        goto done;
    }

    result = 0;
done:
    free(key_data);
    free(signature_data);
    free(manifest_data);
    free(workspace);
    return result;
}
