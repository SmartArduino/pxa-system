#include "pxa/package.h"
#include "common/bytes_internal.h"
#include "common/status_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_SIGNATURE_HEADER_SIZE ((size_t)44)

static pxa_status_t parse_publisher_lineage(
    const pxa_package_manifest_t *manifest,
    pxa_package_publisher_lineage_t *output, const uint8_t **root_output) {
    static const uint8_t magic[] = {'P', 'X', 'K', 'L'};
    const uint8_t *root_key_id;
    const uint8_t *previous_new_key_id = NULL;
    size_t offset;
    uint16_t link_count;
    uint16_t index;
    if (manifest == NULL || manifest->publisher_key_id == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    root_key_id = manifest->publisher_key_id;
    if (output != NULL) {
        memset(output, 0, sizeof(*output));
        output->root_key_id = manifest->publisher_key_id;
        output->current_key_id = manifest->publisher_key_id;
    }
    if (root_output != NULL) *root_output = root_key_id;
    if (manifest->publisher_lineage.size == 0) return PXA_STATUS_OK;
    if (manifest->publisher_lineage.data == NULL ||
        manifest->publisher_lineage.size < 16 ||
        memcmp(manifest->publisher_lineage.data, magic, sizeof(magic)) != 0 ||
        pxa_read_u16(manifest->publisher_lineage.data + 4) !=
            PXA_PUBLISHER_LINEAGE_FORMAT_MAJOR ||
        pxa_read_u16(manifest->publisher_lineage.data + 6) !=
            PXA_PUBLISHER_LINEAGE_FORMAT_MINOR ||
        pxa_read_u16(manifest->publisher_lineage.data + 8) != 16 ||
        pxa_read_u16(manifest->publisher_lineage.data + 10) == 0 ||
        pxa_read_u16(manifest->publisher_lineage.data + 10) >
            PXA_PACKAGE_MAX_LINEAGE_LINKS ||
        pxa_read_u32(manifest->publisher_lineage.data + 12) !=
            manifest->publisher_lineage.size - 16) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    link_count = pxa_read_u16(manifest->publisher_lineage.data + 10);
    if (output != NULL) output->link_count = link_count;
    offset = 16;
    for (index = 0; index < link_count; ++index) {
        const uint8_t *bytes;
        const uint8_t *old_key_id;
        const uint8_t *new_key_id;
        uint16_t spki_size;
        size_t link_size;
        if (offset > manifest->publisher_lineage.size ||
            80 > manifest->publisher_lineage.size - offset) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        bytes = manifest->publisher_lineage.data + offset;
        spki_size = pxa_read_u16(bytes + 72);
        if (pxa_read_u32(bytes) != (uint32_t)index + 1u ||
            (pxa_read_u32(bytes + 4) & ~UINT32_C(1)) != 0 ||
            spki_size == 0 || spki_size > PXA_PACKAGE_MAX_LINEAGE_SPKI_BYTES ||
            pxa_read_u16(bytes + 74) != PXA_PACKAGE_SIGNATURE_BYTES ||
            pxa_read_u32(bytes + 76) != 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        link_size = 80u + spki_size + PXA_PACKAGE_SIGNATURE_BYTES;
        if (link_size > manifest->publisher_lineage.size - offset) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        old_key_id = bytes + 8;
        new_key_id = bytes + 40;
        if (index != 0 && memcmp(old_key_id, previous_new_key_id,
                                 PXA_PACKAGE_DIGEST_BYTES) != 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (index == 0) root_key_id = old_key_id;
        previous_new_key_id = new_key_id;
        if (output != NULL) {
            pxa_package_lineage_link_t *link = &output->links[index];
            link->generation = pxa_read_u32(bytes);
            link->flags = pxa_read_u32(bytes + 4);
            link->old_key_id = old_key_id;
            link->new_key_id = new_key_id;
            link->new_spki = (pxa_bytes_t){bytes + 80, spki_size};
            link->signature = bytes + 80 + spki_size;
        }
        offset += link_size;
    }
    if (offset != manifest->publisher_lineage.size ||
        memcmp(previous_new_key_id, manifest->publisher_key_id,
               PXA_PACKAGE_DIGEST_BYTES) != 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (output != NULL) output->root_key_id = root_key_id;
    if (root_output != NULL) *root_output = root_key_id;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_package_publisher_lineage_parse(
    const pxa_package_manifest_t *manifest,
    pxa_package_publisher_lineage_t *output) {
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    return parse_publisher_lineage(manifest, output, NULL);
}

pxa_status_t pxa_package_publisher_lineage_root(
    const pxa_package_manifest_t *manifest, const uint8_t **output) {
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    return parse_publisher_lineage(manifest, NULL, output);
}

pxa_status_t pxa_package_signature_parse(
    pxa_bytes_t encoded, pxa_package_signature_t *output) {
    static const uint8_t magic[] = {'P', 'X', 'A', 'S'};
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (encoded.data == NULL ||
        encoded.size != PXA_SIGNATURE_HEADER_SIZE +
                            PXA_PACKAGE_SIGNATURE_BYTES ||
        memcmp(encoded.data, magic, sizeof(magic)) != 0 ||
        pxa_read_u16(encoded.data + 4) !=
            PXA_PACKAGE_SIGNATURE_FORMAT_VERSION ||
        pxa_read_u16(encoded.data + 6) != 1 ||
        pxa_read_u16(encoded.data + 40) != PXA_PACKAGE_SIGNATURE_BYTES ||
        pxa_read_u16(encoded.data + 42) != 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    output->algorithm = 1;
    output->publisher_key_id = encoded.data + 8;
    output->signature = encoded.data + PXA_SIGNATURE_HEADER_SIZE;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_package_signature_identity_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_signature_t *signature) {
    if (manifest == NULL || signature == NULL ||
        manifest->publisher_key_id == NULL ||
        signature->publisher_key_id == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return memcmp(manifest->publisher_key_id, signature->publisher_key_id,
                  PXA_PACKAGE_DIGEST_BYTES) == 0
               ? PXA_STATUS_OK
               : PXA_STATUS_DENIED;
}

pxa_status_t pxa_package_signature_verify(
    const pxa_package_manifest_t *manifest,
    const pxa_package_signature_t *signature, void *context,
    pxa_package_signature_verify_fn verify) {
    static const uint8_t domain[] = "PXA-PACKAGE-MANIFEST\0";
    pxa_status_t status;
    if (verify == NULL || manifest == NULL || signature == NULL ||
        manifest->encoded.data == NULL || signature->signature == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = pxa_package_signature_identity_validate(manifest, signature);
    if (status != PXA_STATUS_OK) return status;
    status = verify(
        context,
        (pxa_bytes_t){signature->publisher_key_id,
                      PXA_PACKAGE_DIGEST_BYTES},
        (pxa_bytes_t){domain, sizeof(domain) - 1}, manifest->encoded,
        (pxa_bytes_t){signature->signature, PXA_PACKAGE_SIGNATURE_BYTES});
    return pxa_status_normalize(status);
}

int pxa_package_same_identity(const pxa_package_manifest_t *left,
                              const pxa_package_manifest_t *right) {
    return left != NULL && right != NULL && left->management_key_id != NULL &&
           right->management_key_id != NULL &&
           memcmp(left->management_key_id, right->management_key_id,
                  PXA_PACKAGE_DIGEST_BYTES) == 0 &&
           pxa_bytes_equal_internal(left->app_id, right->app_id);
}
