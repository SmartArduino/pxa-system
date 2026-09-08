#include "pxa/package.h"

#include "common/bytes_internal.h"

static size_t feature_count(uint64_t value) {
    size_t count = 0;
    while (value != 0) {
        value &= value - 1u;
        count++;
    }
    return count;
}

pxa_status_t pxa_package_artifact_select(
    const pxa_package_component_t *component,
    const pxa_package_host_profile_t *host,
    const pxa_package_artifact_t **output) {
    uint16_t index;
    size_t best_features = 0;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (component == NULL || host == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < component->artifact_count; ++index) {
        const pxa_package_artifact_t *artifact = &component->artifacts[index];
        size_t features;
        if (artifact->memory_model != host->memory_model ||
            (artifact->required_features & ~host->supported_features) != 0) {
            continue;
        }
        if (artifact->kind == PXA_ARTIFACT_AOT &&
            (!pxa_bytes_equal_internal(artifact->target, host->target) ||
             !pxa_bytes_equal_internal(artifact->engine, host->engine) ||
             !pxa_bytes_equal_internal(artifact->engine_abi,
                                       host->engine_abi))) {
            continue;
        }
        features = feature_count(artifact->required_features);
        if (*output == NULL ||
            (artifact->kind == PXA_ARTIFACT_AOT &&
             (*output)->kind != PXA_ARTIFACT_AOT) ||
            (artifact->kind == (*output)->kind &&
             features > best_features) ||
            (artifact->kind == (*output)->kind &&
             features == best_features &&
             pxa_bytes_compare_internal(artifact->path, (*output)->path) < 0)) {
            *output = artifact;
            best_features = features;
        }
    }
    return *output == NULL ? PXA_STATUS_UNSUPPORTED : PXA_STATUS_OK;
}
