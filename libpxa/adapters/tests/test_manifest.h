#ifndef PXA_TEST_MANIFEST_H
#define PXA_TEST_MANIFEST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pxa/package.h"
#include "pxa/wire.h"

/* Builds a minimal valid manifest: one UI component "main" with one portable
 * Wasm artifact and matching file entry. The publisher key id is carried
 * verbatim. Returns the encoded size, or 0 on failure. */
static inline size_t pxa_test_encode_manifest(uint8_t *output, size_t capacity,
                                              const uint8_t publisher_key_id[32],
                                              const char *app_id,
                                              const char *version,
                                              const char *file_path,
                                              uint64_t file_size,
                                              const uint8_t file_sha[32]) {
    pxa_writer_t writer;
    uint8_t version_bytes[4];
    uint8_t u64_bytes[8];
    uint8_t component_payload[256];
    uint8_t artifact_payload[192];
    uint8_t file_payload[192];
    pxa_writer_t component_writer;
    pxa_writer_t artifact_writer;
    pxa_writer_t file_writer;
    size_t component_size;
    size_t artifact_size;
    size_t file_size_out;
    if (output == NULL || capacity <= 12 || publisher_key_id == NULL ||
        app_id == NULL || version == NULL || file_path == NULL ||
        file_sha == NULL) {
        return 0;
    }
    pxa_writer_init(&writer, output + 12, capacity - 12);
    pxa_writer_record(&writer, 1, publisher_key_id, 32);
    pxa_writer_record(&writer, 2, app_id, strlen(app_id));
    pxa_writer_record(&writer, 3, version, strlen(version));
    pxa_writer_record(&writer, 4, "Test App", 8);
    pxa_write_u16(version_bytes, PXA_CORE_VERSION_MAJOR);
    pxa_write_u16(version_bytes + 2, PXA_CORE_VERSION_MINOR);
    pxa_writer_record(&writer, 7, version_bytes, 4);
    pxa_writer_record(&writer, 8, version_bytes, 4);
    pxa_writer_init(&artifact_writer, artifact_payload,
                    sizeof(artifact_payload));
    {
        uint8_t kind = PXA_ARTIFACT_WASM;
        uint8_t memory_model = PXA_MEMORY_WASM32;
        memset(u64_bytes, 0, sizeof(u64_bytes));
        pxa_writer_record(&artifact_writer, 1, &kind, 1);
        pxa_writer_record(&artifact_writer, 2, file_path, strlen(file_path));
        pxa_writer_record(&artifact_writer, 6, u64_bytes, 8);
        pxa_writer_record(&artifact_writer, 7, &memory_model, 1);
    }
    artifact_size = artifact_writer.size;
    pxa_writer_init(&component_writer, component_payload,
                    sizeof(component_payload));
    {
        uint8_t kind = PXA_COMPONENT_KIND_UI;
        pxa_writer_record(&component_writer, 1, "main", 4);
        pxa_writer_record(&component_writer, 2, &kind, 1);
        pxa_writer_record(&component_writer, 4, artifact_payload,
                          artifact_size);
    }
    component_size = component_writer.size;
    pxa_writer_init(&file_writer, file_payload, sizeof(file_payload));
    pxa_writer_record(&file_writer, 1, file_path, strlen(file_path));
    pxa_write_u64(u64_bytes, file_size);
    pxa_writer_record(&file_writer, 2, u64_bytes, 8);
    pxa_writer_record(&file_writer, 3, file_sha, 32);
    file_size_out = file_writer.size;
    pxa_writer_record(&writer, 16, component_payload, component_size);
    pxa_writer_record(&writer, 17, file_payload, file_size_out);
    if (writer.status != PXA_STATUS_OK) return 0;
    {
        static const uint8_t magic[] = {'P', 'X', 'A', 'M'};
        size_t body_size = writer.size;
        memcpy(output, magic, 4);
        pxa_write_u16(output + 4, PXA_PACKAGE_MANIFEST_FORMAT_MAJOR);
        pxa_write_u16(output + 6, 1);
        pxa_write_u32(output + 8, (uint32_t)body_size);
        return 12 + body_size;
    }
}

#endif

/* Multi-component variant: each entry has an id, kind and artifact path.
 * All artifacts must reference files declared in `files` (path -> sha/size
 * must match the manifest inventory). Components must be sorted by id. */
typedef struct {
    const char *id;
    uint8_t kind;
    const char *artifact_path;
} pxa_test_component_t;

typedef struct {
    const char *path;
    uint64_t size;
    const uint8_t *sha256;
} pxa_test_file_t;

static inline size_t pxa_test_encode_manifest_multi(
    uint8_t *output, size_t capacity, const uint8_t publisher_key_id[32],
    const char *app_id, const char *version,
    const pxa_test_component_t *components, size_t component_count,
    const pxa_test_file_t *files, size_t file_count) {
    pxa_writer_t writer;
    uint8_t version_bytes[4];
    uint8_t u64_bytes[8];
    size_t index;
    if (output == NULL || capacity <= 12 || publisher_key_id == NULL ||
        app_id == NULL || version == NULL || component_count == 0 ||
        file_count == 0 || components == NULL || files == NULL) {
        return 0;
    }
    pxa_writer_init(&writer, output + 12, capacity - 12);
    pxa_writer_record(&writer, 1, publisher_key_id, 32);
    pxa_writer_record(&writer, 2, app_id, strlen(app_id));
    pxa_writer_record(&writer, 3, version, strlen(version));
    pxa_writer_record(&writer, 4, "Test App", 8);
    pxa_write_u16(version_bytes, PXA_CORE_VERSION_MAJOR);
    pxa_write_u16(version_bytes + 2, PXA_CORE_VERSION_MINOR);
    pxa_writer_record(&writer, 7, version_bytes, 4);
    pxa_writer_record(&writer, 8, version_bytes, 4);
    for (index = 0; index < component_count; ++index) {
        uint8_t component_payload[256];
        uint8_t artifact_payload[192];
        pxa_writer_t component_writer;
        pxa_writer_t artifact_writer;
        const char *artifact_path = components[index].artifact_path;
        if (artifact_path == NULL || strlen(artifact_path) == 0) return 0;
        pxa_writer_init(&artifact_writer, artifact_payload,
                        sizeof(artifact_payload));
        {
            uint8_t kind = PXA_ARTIFACT_WASM;
            uint8_t memory_model = PXA_MEMORY_WASM32;
            memset(u64_bytes, 0, sizeof(u64_bytes));
            pxa_writer_record(&artifact_writer, 1, &kind, 1);
            pxa_writer_record(&artifact_writer, 2, artifact_path,
                              strlen(artifact_path));
            pxa_writer_record(&artifact_writer, 6, u64_bytes, 8);
            pxa_writer_record(&artifact_writer, 7, &memory_model, 1);
        }
        pxa_writer_init(&component_writer, component_payload,
                        sizeof(component_payload));
        pxa_writer_record(&component_writer, 1, components[index].id,
                          strlen(components[index].id));
        pxa_writer_record(&component_writer, 2, &components[index].kind, 1);
        pxa_writer_record(&component_writer, 4, artifact_payload,
                          artifact_writer.size);
        if (pxa_writer_record(&writer, 16, component_payload,
                              component_writer.size) != PXA_STATUS_OK) {
            return 0;
        }
    }
    for (index = 0; index < file_count; ++index) {
        uint8_t file_payload[192];
        pxa_writer_t file_writer;
        pxa_writer_init(&file_writer, file_payload, sizeof(file_payload));
        pxa_writer_record(&file_writer, 1, files[index].path,
                          strlen(files[index].path));
        pxa_write_u64(u64_bytes, files[index].size);
        pxa_writer_record(&file_writer, 2, u64_bytes, 8);
        pxa_writer_record(&file_writer, 3, files[index].sha256, 32);
        if (pxa_writer_record(&writer, 17, file_payload, file_writer.size) !=
            PXA_STATUS_OK) {
            return 0;
        }
    }
    if (writer.status != PXA_STATUS_OK) return 0;
    {
        static const uint8_t magic[] = {'P', 'X', 'A', 'M'};
        size_t body_size = writer.size;
        memcpy(output, magic, 4);
        pxa_write_u16(output + 4, PXA_PACKAGE_MANIFEST_FORMAT_MAJOR);
        pxa_write_u16(output + 6, 1);
        pxa_write_u32(output + 8, (uint32_t)body_size);
        return 12 + body_size;
    }
}
