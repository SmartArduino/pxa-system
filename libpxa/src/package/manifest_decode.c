#include "package/manifest_internal.h"

#include "common/bytes_internal.h"
#include "package/package_internal.h"

#include <string.h>

static int safe_id(pxa_bytes_t value, size_t max_size) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > max_size ||
        value.data[0] < 'a' || value.data[0] > 'z') {
        return 0;
    }
    for (index = 0; index < value.size; ++index) {
        uint8_t c = value.data[index];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int version_string(pxa_bytes_t value) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > 64 ||
        value.data[0] < '0' || value.data[0] > '9') {
        return 0;
    }
    for (index = 0; index < value.size; ++index) {
        uint8_t c = value.data[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')) {
            return 0;
        }
    }
    return 1;
}

static int compat_id(pxa_bytes_t value) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > 96) return 0;
    for (index = 0; index < value.size; ++index) {
        uint8_t c = value.data[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
              c == '+' || c == '~')) {
            return 0;
        }
    }
    return 1;
}

static int valid_utf8(pxa_bytes_t value, size_t max_size) {
    if ((value.data == NULL && value.size != 0) || value.size > max_size) {
        return 0;
    }
    return pxa_utf8_validate(
        value.data, value.size,
        PXA_UTF8_REJECT_C0 | PXA_UTF8_REJECT_DEL | PXA_UTF8_REJECT_C1);
}

static int canonical_locale(pxa_bytes_t value) {
    size_t subtag_start = 0;
    size_t index;
    unsigned subtag = 0;
    if (value.data == NULL || value.size < 2 || value.size > 63) return 0;
    for (index = 0; index <= value.size; ++index) {
        size_t cursor;
        size_t length;
        if (index != value.size && value.data[index] != '-') continue;
        length = index - subtag_start;
        if (length == 0 || length > 8 ||
            (subtag == 0 && (length < 2 || length > 8))) {
            return 0;
        }
        for (cursor = subtag_start; cursor < index; ++cursor) {
            uint8_t ch = value.data[cursor];
            int alpha = (ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z');
            int digit = ch >= '0' && ch <= '9';
            if ((!alpha && !digit) || (subtag == 0 && !alpha)) return 0;
            if (subtag == 0 || (length != 2 && length != 4)) {
                if (alpha && (ch < 'a' || ch > 'z')) return 0;
            } else if (length == 2) {
                if (alpha && (ch < 'A' || ch > 'Z')) return 0;
            } else if (alpha &&
                       (cursor == subtag_start
                            ? (ch < 'A' || ch > 'Z')
                            : (ch < 'a' || ch > 'z'))) {
                return 0;
            }
        }
        subtag_start = index + 1;
        ++subtag;
    }
    return 1;
}

static int version_less(pxa_package_version_t left,
                        pxa_package_version_t right) {
    return left.major < right.major ||
           (left.major == right.major && left.minor < right.minor);
}

static uint16_t max_localizations(const pxa_package_limits_t *limits) {
    return limits->struct_size >= sizeof(*limits)
               ? limits->max_localizations
               : 0;
}

static pxa_status_t parse_version(pxa_bytes_t bytes,
                                  pxa_package_version_t *output) {
    if (bytes.size != 4) return PXA_STATUS_INVALID_ARGUMENT;
    output->major = pxa_read_u16(bytes.data);
    output->minor = pxa_read_u16(bytes.data + 2);
    return PXA_STATUS_OK;
}

static pxa_status_t parse_artifact(pxa_bytes_t bytes,
                                   pxa_package_artifact_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint32_t seen = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        uint32_t bit;
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 7) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag > 7) {
            if (!record.optional) return PXA_STATUS_UNSUPPORTED;
            continue;
        }
        bit = UINT32_C(1) << record.tag;
        if ((seen & bit) != 0) return PXA_STATUS_INVALID_ARGUMENT;
        seen |= bit;
        if (record.tag == 1) {
            if (record.payload.size != 1 ||
                (record.payload.data[0] != PXA_ARTIFACT_WASM &&
                 record.payload.data[0] != PXA_ARTIFACT_AOT)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->kind = record.payload.data[0];
        } else if (record.tag == 2) {
            if (!pxa_package_path_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->path = record.payload;
        } else if (record.tag == 3) {
            if (!compat_id(record.payload)) return PXA_STATUS_INVALID_ARGUMENT;
            output->target = record.payload;
        } else if (record.tag == 4) {
            if (!compat_id(record.payload)) return PXA_STATUS_INVALID_ARGUMENT;
            output->engine = record.payload;
        } else if (record.tag == 5) {
            if (!compat_id(record.payload)) return PXA_STATUS_INVALID_ARGUMENT;
            output->engine_abi = record.payload;
        } else if (record.tag == 6) {
            if (record.payload.size != 8) return PXA_STATUS_INVALID_ARGUMENT;
            output->required_features = pxa_read_u64(record.payload.data);
        } else {
            if (record.payload.size != 1 ||
                (record.payload.data[0] != PXA_MEMORY_WASM32 &&
                 record.payload.data[0] != PXA_MEMORY_WASM32_SHARED)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->memory_model = record.payload.data[0];
        }
    }
    if ((seen & UINT32_C(0x00c6)) != UINT32_C(0x00c6)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (output->kind == PXA_ARTIFACT_WASM) {
        if (output->target.size != 0 || output->engine.size != 0 ||
            output->engine_abi.size != 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    } else if (output->target.size == 0 || output->engine.size == 0 ||
               output->engine_abi.size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t parse_service_requirement(
    pxa_bytes_t bytes, pxa_package_service_requirement_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint32_t seen = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        uint32_t bit;
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 4) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag > 4) {
            if (!record.optional) return PXA_STATUS_UNSUPPORTED;
            continue;
        }
        bit = UINT32_C(1) << record.tag;
        if ((seen & bit) != 0) return PXA_STATUS_INVALID_ARGUMENT;
        seen |= bit;
        if (record.tag == 1) {
            if (record.payload.size != 2) return PXA_STATUS_INVALID_ARGUMENT;
            output->service = pxa_read_u16(record.payload.data);
            if (output->service == 0) return PXA_STATUS_INVALID_ARGUMENT;
        } else if (record.tag == 2) {
            status = parse_version(record.payload, &output->min_version);
            if (status != PXA_STATUS_OK) return status;
        } else if (record.tag == 3) {
            status = parse_version(record.payload, &output->max_version);
            if (status != PXA_STATUS_OK) return status;
        } else {
            if (record.payload.size != 8) return PXA_STATUS_INVALID_ARGUMENT;
            output->required_features = pxa_read_u64(record.payload.data);
        }
    }
    if (seen != UINT32_C(0x1e) ||
        output->min_version.major != output->max_version.major ||
        version_less(output->max_version, output->min_version)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t parse_component(pxa_manifest_parser_t *parser,
                                    pxa_bytes_t bytes,
                                    pxa_package_component_t *output) {
    pxa_package_manifest_t *manifest = parser->manifest;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen_id = 0;
    uint8_t seen_kind = 0;
    uint8_t seen_flags = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    output->artifacts = &manifest->artifacts[manifest->artifact_count];
    output->services = manifest->services == NULL
                           ? NULL
                           : &manifest->services[manifest->service_count];
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional &&
            (record.tag == 1 || record.tag == 2 || record.tag == 3 ||
             record.tag == 4 ||
             record.tag == 5)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag == 1) {
            if (seen_id || !safe_id(record.payload, 64)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            seen_id = 1;
            output->id = record.payload;
        } else if (record.tag == 2) {
            if (seen_kind || record.payload.size != 1 ||
                record.payload.data[0] < PXA_COMPONENT_KIND_UI ||
                record.payload.data[0] > PXA_COMPONENT_KIND_JOB) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            seen_kind = 1;
            output->kind = record.payload.data[0];
        } else if (record.tag == 3) {
            if (seen_flags || parser->manifest->format_minor < 7 ||
                record.payload.size != 1 ||
                (record.payload.data[0] &
                 ~PXA_PACKAGE_COMPONENT_FLAG_KNOWN_MASK) != 0) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            seen_flags = 1;
            output->flags = record.payload.data[0];
        } else if (record.tag == 4) {
            pxa_package_artifact_t *artifact;
            if (output->artifact_count >=
                    PXA_PACKAGE_MAX_ARTIFACTS_PER_COMPONENT ||
                manifest->artifact_count >= parser->limits->max_artifacts) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            artifact = &manifest->artifacts[manifest->artifact_count];
            status = parse_artifact(record.payload, artifact);
            if (status != PXA_STATUS_OK) return status;
            if (output->artifact_count != 0 &&
                pxa_bytes_compare_internal(
                    output->artifacts[output->artifact_count - 1].path,
                    artifact->path) >= 0) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->artifact_count++;
            manifest->artifact_count++;
        } else if (record.tag == 5) {
            pxa_package_service_requirement_t *requirement;
            if (output->service_count >=
                    PXA_PACKAGE_MAX_SERVICES_PER_COMPONENT ||
                manifest->service_count >= parser->limits->max_services) {
                return PXA_STATUS_RESOURCE_LIMIT;
            }
            requirement = &manifest->services[manifest->service_count];
            status = parse_service_requirement(record.payload, requirement);
            if (status != PXA_STATUS_OK) return status;
            if (output->service_count != 0 &&
                output->services[output->service_count - 1].service >=
                    requirement->service) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->service_count++;
            manifest->service_count++;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    return seen_id && seen_kind && output->artifact_count != 0 &&
                   (parser->manifest->format_minor < 7 || seen_flags)
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_file(pxa_bytes_t bytes,
                               pxa_package_file_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint32_t seen = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        uint32_t bit;
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 3) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag > 3) {
            if (!record.optional) return PXA_STATUS_UNSUPPORTED;
            continue;
        }
        bit = UINT32_C(1) << record.tag;
        if ((seen & bit) != 0) return PXA_STATUS_INVALID_ARGUMENT;
        seen |= bit;
        if (record.tag == 1) {
            if (!pxa_package_path_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->path = record.payload;
        } else if (record.tag == 2) {
            if (record.payload.size != 8) return PXA_STATUS_INVALID_ARGUMENT;
            output->size = pxa_read_u64(record.payload.data);
        } else {
            if (record.payload.size != PXA_PACKAGE_DIGEST_BYTES) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->sha256 = record.payload.data;
        }
    }
    return seen == UINT32_C(0x0e) ? PXA_STATUS_OK
                                  : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_permission(pxa_bytes_t bytes,
                                     pxa_package_permission_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen_name = 0;
    uint8_t seen_required = 0;
    uint8_t seen_scope = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 3) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag == 1) {
            size_t index;
            int dot = 0;
            if (seen_name || !safe_id(record.payload, 96)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            for (index = 0; index < record.payload.size; ++index) {
                if (record.payload.data[index] == '.') dot = 1;
            }
            if (!dot) return PXA_STATUS_INVALID_ARGUMENT;
            seen_name = 1;
            output->name = record.payload;
        } else if (record.tag == 2) {
            if (seen_required || record.payload.size != 1 ||
                record.payload.data[0] > 1) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            seen_required = 1;
            output->required = record.payload.data[0];
        } else if (record.tag == 3) {
            if (seen_scope || record.payload.size > 1024) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            seen_scope = 1;
            output->scope = record.payload;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    return seen_name && seen_required ? PXA_STATUS_OK
                                      : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_ipc_endpoint(
    pxa_bytes_t bytes, pxa_package_ipc_endpoint_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 2) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag == 1 && (seen & 1u) == 0 &&
            safe_id(record.payload, 64)) {
            output->name = record.payload;
            seen |= 1u;
        } else if (record.tag == 2 && (seen & 2u) == 0 &&
                   safe_id(record.payload, 64)) {
            output->component_id = record.payload;
            seen |= 2u;
        } else if (!record.optional) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return seen == 3u ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_localization(
    pxa_bytes_t bytes, pxa_package_localization_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, bytes);
    for (;;) {
        uint8_t bit;
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional && record.tag >= 1 && record.tag <= 4) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (record.tag < 1 || record.tag > 4) {
            if (!record.optional) return PXA_STATUS_UNSUPPORTED;
            continue;
        }
        bit = (uint8_t)(UINT8_C(1) << (record.tag - 1u));
        if ((seen & bit) != 0) return PXA_STATUS_INVALID_ARGUMENT;
        seen = (uint8_t)(seen | bit);
        if (record.tag == 1) {
            if (!canonical_locale(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->locale = record.payload;
        } else if (record.tag == 2) {
            if (record.payload.size == 0 ||
                !valid_utf8(record.payload, 128)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->name = record.payload;
        } else if (record.tag == 3) {
            if (record.payload.size == 0 ||
                !valid_utf8(record.payload, 512)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->description = record.payload;
        } else {
            if (!pxa_package_path_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->icon_path = record.payload;
        }
    }
    return (seen & UINT8_C(1)) != 0 && (seen & UINT8_C(0x0e)) != 0
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

const pxa_package_file_t *pxa_package_file_find(
    const pxa_package_manifest_t *manifest, pxa_bytes_t path) {
    uint16_t begin = 0;
    uint16_t end = manifest->file_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        int comparison =
            pxa_bytes_compare_internal(manifest->files[middle].path, path);
        if (comparison < 0) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin < manifest->file_count &&
                   pxa_bytes_equal_internal(manifest->files[begin].path, path)
               ? &manifest->files[begin]
               : NULL;
}

static int locale_is_candidate(pxa_bytes_t requested, pxa_bytes_t candidate) {
    return candidate.data != NULL && candidate.size != 0 &&
           candidate.size <= requested.size &&
           memcmp(requested.data, candidate.data, candidate.size) == 0 &&
           (candidate.size == requested.size ||
            requested.data[candidate.size] == '-');
}

pxa_status_t pxa_package_metadata_resolve(
    const pxa_package_manifest_t *manifest, pxa_bytes_t locale,
    pxa_package_metadata_t *metadata) {
    const pxa_package_localization_t *best_name = NULL;
    const pxa_package_localization_t *best_description = NULL;
    const pxa_package_localization_t *best_icon = NULL;
    uint16_t index;
    if (manifest == NULL || metadata == NULL || !canonical_locale(locale) ||
        manifest->name.data == NULL || manifest->name.size == 0 ||
        (manifest->localization_count != 0 &&
         manifest->localizations == NULL)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->name = manifest->name;
    metadata->description = manifest->description;
    metadata->icon_path = manifest->icon_path;
    for (index = 0; index < manifest->localization_count; ++index) {
        const pxa_package_localization_t *candidate =
            &manifest->localizations[index];
        if (!locale_is_candidate(locale, candidate->locale)) continue;
        if (candidate->name.size != 0 &&
            (best_name == NULL ||
             candidate->locale.size > best_name->locale.size))
            best_name = candidate;
        if (candidate->description.size != 0 &&
            (best_description == NULL ||
             candidate->locale.size > best_description->locale.size))
            best_description = candidate;
        if (candidate->icon_path.size != 0 &&
            (best_icon == NULL ||
             candidate->locale.size > best_icon->locale.size))
            best_icon = candidate;
    }
    if (best_name != NULL) {
        metadata->name = best_name->name;
        metadata->localized_fields |= PXA_PACKAGE_METADATA_NAME;
    }
    if (best_description != NULL) {
        metadata->description = best_description->description;
        metadata->localized_fields |= PXA_PACKAGE_METADATA_DESCRIPTION;
    }
    if (best_icon != NULL) {
        metadata->icon_path = best_icon->icon_path;
        metadata->localized_fields |= PXA_PACKAGE_METADATA_ICON;
    }
    return PXA_STATUS_OK;
}

static const pxa_package_component_t *find_component(
    const pxa_package_manifest_t *manifest, pxa_bytes_t id) {
    uint16_t begin = 0;
    uint16_t end = manifest->component_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        int comparison =
            pxa_bytes_compare_internal(manifest->components[middle].id, id);
        if (comparison < 0) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin < manifest->component_count &&
                   pxa_bytes_equal_internal(manifest->components[begin].id, id)
               ? &manifest->components[begin]
               : NULL;
}

static pxa_status_t validate_manifest_links(
    const pxa_package_manifest_t *manifest) {
    uint16_t component_index;
    uint16_t endpoint_index;
    uint16_t localization_index;
    uint16_t ui_count = 0;
    if (manifest->icon_path.size != 0 &&
        pxa_package_file_find(manifest, manifest->icon_path) == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    for (localization_index = 0;
         localization_index < manifest->localization_count;
         ++localization_index) {
        pxa_bytes_t icon = manifest->localizations[localization_index].icon_path;
        if (icon.size != 0 && pxa_package_file_find(manifest, icon) == NULL) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    for (component_index = 0;
         component_index < manifest->component_count; ++component_index) {
        const pxa_package_component_t *component =
            &manifest->components[component_index];
        uint16_t artifact_index;
        if (component->kind == PXA_COMPONENT_KIND_UI && ++ui_count > 1) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        for (artifact_index = 0;
             artifact_index < component->artifact_count; ++artifact_index) {
            const pxa_package_file_t *file = pxa_package_file_find(
                manifest, component->artifacts[artifact_index].path);
            if (file == NULL || file->size == 0) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    for (endpoint_index = 0;
         endpoint_index < manifest->ipc_endpoint_count; ++endpoint_index) {
        if (find_component(
                manifest,
                manifest->ipc_endpoints[endpoint_index].component_id) == NULL) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return PXA_STATUS_OK;
}

static pxa_status_t decode_scalar(pxa_package_manifest_t *manifest,
                                  const pxa_record_view_t *record) {
    pxa_status_t status;
    if (record->tag == 1) {
        if (record->payload.size != PXA_PACKAGE_DIGEST_BYTES) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->publisher_key_id = record->payload.data;
    } else if (record->tag == 2) {
        if (!safe_id(record->payload, 64)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->app_id = record->payload;
    } else if (record->tag == 3) {
        if (!version_string(record->payload)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->version = record->payload;
    } else if (record->tag == 4) {
        if (!valid_utf8(record->payload, 128)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->name = record->payload;
    } else if (record->tag == 5) {
        if (!valid_utf8(record->payload, 512)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->description = record->payload;
    } else if (record->tag == 6) {
        if (!pxa_package_path_is_valid(record->payload)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->icon_path = record->payload;
    } else if (record->tag == 7) {
        status = parse_version(record->payload, &manifest->min_sdk);
        if (status != PXA_STATUS_OK) return status;
    } else if (record->tag == 8) {
        status = parse_version(record->payload, &manifest->target_sdk);
        if (status != PXA_STATUS_OK) return status;
    } else if (record->tag == 9) {
        if (manifest->format_minor < 2 || record->payload.size != 8 ||
            pxa_read_u64(record->payload.data) == 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->release_sequence = pxa_read_u64(record->payload.data);
        manifest->has_release_sequence = 1;
    } else if (record->tag == 10) {
        if (manifest->format_minor < 2 || record->payload.size == 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->publisher_lineage = record->payload;
    } else if (record->tag == 11) {
        if (manifest->format_minor < 5 || record->payload.size == 0 ||
            record->payload.size > PXA_PACKAGE_MAX_PUBLISHER_SPKI_BYTES) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->publisher_spki = record->payload;
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_manifest_decode_record(
    pxa_manifest_parser_t *parser, const pxa_record_view_t *record,
    uint32_t *singleton_seen) {
    pxa_package_manifest_t *manifest;
    pxa_status_t status;
    if (parser == NULL || parser->manifest == NULL || parser->limits == NULL ||
        record == NULL || singleton_seen == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    manifest = parser->manifest;
    if (record->optional &&
        ((record->tag >= 1 && record->tag <= 11) ||
         (record->tag >= 16 && record->tag <= 20))) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (record->tag <= 11) {
        uint32_t bit = UINT32_C(1) << record->tag;
        if ((*singleton_seen & bit) != 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        *singleton_seen |= bit;
        return decode_scalar(manifest, record);
    }
    if (record->tag == 16) {
        pxa_package_component_t *component;
        if (manifest->component_count >= parser->limits->max_components) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        component = &manifest->components[manifest->component_count];
        status = parse_component(parser, record->payload, component);
        if (status != PXA_STATUS_OK) return status;
        if (manifest->component_count != 0 &&
            pxa_bytes_compare_internal(
                manifest->components[manifest->component_count - 1].id,
                component->id) >= 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->component_count++;
    } else if (record->tag == 17) {
        pxa_package_file_t *file;
        if (manifest->file_count >= parser->limits->max_files) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        file = &manifest->files[manifest->file_count];
        status = parse_file(record->payload, file);
        if (status != PXA_STATUS_OK) return status;
        if (manifest->file_count != 0 &&
            pxa_bytes_compare_internal(
                manifest->files[manifest->file_count - 1].path,
                file->path) >= 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->file_count++;
    } else if (record->tag == 18) {
        pxa_package_permission_t *permission;
        if (manifest->permission_count >= parser->limits->max_permissions) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        permission = &manifest->permissions[manifest->permission_count];
        status = parse_permission(record->payload, permission);
        if (status != PXA_STATUS_OK) return status;
        if (manifest->permission_count != 0) {
            const pxa_package_permission_t *prior =
                &manifest->permissions[manifest->permission_count - 1];
            int comparison =
                pxa_bytes_compare_internal(prior->name, permission->name);
            if (comparison > 0 ||
                (comparison == 0 &&
                 pxa_bytes_compare_internal(prior->scope,
                                            permission->scope) >= 0)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
        }
        manifest->permission_count++;
    } else if (record->tag == 19) {
        pxa_package_ipc_endpoint_t *endpoint;
        if (manifest->ipc_endpoint_count >=
            parser->limits->max_ipc_endpoints) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        endpoint = &manifest->ipc_endpoints[manifest->ipc_endpoint_count];
        status = parse_ipc_endpoint(record->payload, endpoint);
        if (status != PXA_STATUS_OK) return status;
        if (manifest->ipc_endpoint_count != 0 &&
            pxa_bytes_compare_internal(
                manifest->ipc_endpoints[
                    manifest->ipc_endpoint_count - 1].name,
                endpoint->name) >= 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->ipc_endpoint_count++;
    } else if (record->tag == 20) {
        pxa_package_localization_t *localization;
        uint16_t limit = max_localizations(parser->limits);
        if (manifest->format_minor < 6) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (manifest->localization_count >= limit) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        localization =
            &manifest->localizations[manifest->localization_count];
        status = parse_localization(record->payload, localization);
        if (status != PXA_STATUS_OK) return status;
        if (manifest->localization_count != 0 &&
            pxa_bytes_compare_internal(
                manifest->localizations[
                    manifest->localization_count - 1].locale,
                localization->locale) >= 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        manifest->localization_count++;
    } else if (!record->optional) {
        return PXA_STATUS_UNSUPPORTED;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_manifest_decode_finish(
    pxa_manifest_parser_t *parser, uint32_t singleton_seen) {
    pxa_package_manifest_t *manifest;
    const uint8_t *management_key_id;
    pxa_status_t status;
    if (parser == NULL || parser->manifest == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    manifest = parser->manifest;
    if ((singleton_seen & UINT32_C(0x018e)) != UINT32_C(0x018e) ||
        (manifest->format_minor >= 2 && !manifest->has_release_sequence) ||
        (manifest->format_minor >= 5 && manifest->publisher_spki.size == 0) ||
        manifest->component_count == 0 || manifest->file_count == 0 ||
        manifest->min_sdk.major != manifest->target_sdk.major ||
        version_less(manifest->target_sdk, manifest->min_sdk)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = validate_manifest_links(manifest);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_publisher_lineage_root(manifest,
                                               &management_key_id);
    if (status != PXA_STATUS_OK) return status;
    manifest->management_key_id = management_key_id;
    return PXA_STATUS_OK;
}
