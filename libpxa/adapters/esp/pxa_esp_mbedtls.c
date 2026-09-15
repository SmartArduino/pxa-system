/* ESP-IDF Layer 2 adapter: ECDSA P-256 / SHA-256 package signature
 * verification over mbedTLS. */

#include "pxa/esp/pxa_esp_mbedtls.h"

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#define PXA_ESP_MBEDTLS_TAG "PxaSignature"

static int constant_time_equal(const uint8_t *left, const uint8_t *right,
                               size_t size) {
    uint8_t difference = 0;
    size_t index;
    for (index = 0; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0;
}

static int is_container_digest_domain(pxa_bytes_t domain) {
    static const uint8_t expected[] = "PXA-PACKAGE-CONTAINER-DIGEST\0";
    return domain.size == sizeof(expected) - 1 &&
           domain.data != NULL &&
           memcmp(domain.data, expected, sizeof(expected) - 1) == 0;
}

/* This intentionally parses only the outer record framing. The portable
 * manifest parser still validates all semantic fields before installation;
 * this adapter only needs the signed public key needed to verify a new
 * open-distribution signer. */
static const uint8_t *manifest_publisher_spki(
    pxa_bytes_t manifest, pxa_bytes_t publisher_key_id, size_t *size_output) {
    size_t offset = 12;
    if (size_output == NULL || manifest.data == NULL || manifest.size < 12 ||
        memcmp(manifest.data, "PXAM", 4) != 0 ||
        pxa_read_u16(manifest.data + 4) !=
            PXA_PACKAGE_MANIFEST_FORMAT_MAJOR ||
        pxa_read_u16(manifest.data + 6) < 5 ||
        pxa_read_u16(manifest.data + 6) >
            PXA_PACKAGE_MANIFEST_FORMAT_MINOR ||
        pxa_read_u32(manifest.data + 8) != manifest.size - 12) {
        return NULL;
    }
    while (offset < manifest.size) {
        uint16_t raw_tag;
        uint16_t size;
        const uint8_t *value;
        uint8_t key_id[PXA_ESP_MBEDTLS_SHA256_BYTES];
        if (manifest.size - offset < 4) return NULL;
        raw_tag = pxa_read_u16(manifest.data + offset);
        size = pxa_read_u16(manifest.data + offset + 2);
        offset += 4;
        if (size > manifest.size - offset) return NULL;
        value = manifest.data + offset;
        offset += size;
        if (raw_tag != 11 || size == 0 ||
            size > PXA_PACKAGE_MAX_PUBLISHER_SPKI_BYTES) {
            continue;
        }
        if (mbedtls_sha256(value, size, key_id, 0) != 0 ||
            !constant_time_equal(key_id, publisher_key_id.data,
                                 PXA_ESP_MBEDTLS_SHA256_BYTES)) {
            return NULL;
        }
        *size_output = size;
        return value;
    }
    return NULL;
}

static pxa_status_t verify_key(const uint8_t *spki, size_t spki_size,
                               pxa_bytes_t domain, pxa_bytes_t manifest,
                               pxa_bytes_t signature) {
    mbedtls_pk_context key;
    const mbedtls_ecp_keypair *ec;
    const mbedtls_ecp_group *group;
    const mbedtls_ecp_point *public_point;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi half_order;
    uint8_t message_hash[PXA_ESP_MBEDTLS_SHA256_BYTES];
    uint8_t *canonical_buffer;
    int canonical_size;
    int result;
    const char *failure_stage;
    mbedtls_sha256_context sha;
    if (spki == NULL || spki_size == 0) return PXA_STATUS_DENIED;

    mbedtls_pk_init(&key);
    result = mbedtls_pk_parse_public_key(&key, spki, spki_size);
    if (result != 0) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG,
                 "Publisher public key parse failed: -0x%04x",
                 (unsigned)(-result));
        mbedtls_pk_free(&key);
        return PXA_STATUS_DENIED;
    }
    if (!mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA)) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG, "Publisher public key is not ECDSA");
        mbedtls_pk_free(&key);
        return PXA_STATUS_DENIED;
    }
    canonical_buffer = malloc(spki_size + 32);
    if (canonical_buffer == NULL) {
        mbedtls_pk_free(&key);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    canonical_size = mbedtls_pk_write_pubkey_der(&key, canonical_buffer,
                                                 spki_size + 32);
    if (canonical_size <= 0 || (size_t)canonical_size != spki_size ||
        memcmp(spki, canonical_buffer + spki_size + 32 - canonical_size,
               spki_size) != 0) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG,
                 "Publisher public key is not canonical DER: %d",
                 canonical_size);
        free(canonical_buffer);
        mbedtls_pk_free(&key);
        return PXA_STATUS_DENIED;
    }
    free(canonical_buffer);

    ec = mbedtls_pk_ec(key);
    group = &ec->MBEDTLS_PRIVATE(grp);
    public_point = &ec->MBEDTLS_PRIVATE(Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&half_order);
    failure_stage = "P-256 curve validation";
    result = group->id == MBEDTLS_ECP_DP_SECP256R1 ? 0 : -1;
    if (result == 0) {
        failure_stage = "signature scalar decoding";
        result = mbedtls_mpi_read_binary(&r, signature.data, 32);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&s, signature.data + 32, 32);
    }
    if (result == 0) {
        failure_stage = "low-S policy validation";
        result = mbedtls_mpi_copy(&half_order, &group->N);
    }
    if (result == 0) result = mbedtls_mpi_shift_r(&half_order, 1);
    if (result == 0 &&
        (mbedtls_mpi_cmp_int(&r, 1) < 0 ||
         mbedtls_mpi_cmp_mpi(&r, &group->N) >= 0 ||
         mbedtls_mpi_cmp_int(&s, 1) < 0 ||
         mbedtls_mpi_cmp_mpi(&s, &half_order) > 0)) {
        result = -1;
    }

    if (result == 0) {
        failure_stage = "signed message hashing";
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        if (domain.size != 0) {
            mbedtls_sha256_update(&sha, domain.data, domain.size);
        }
        mbedtls_sha256_update(&sha, manifest.data, manifest.size);
        mbedtls_sha256_finish(&sha, message_hash);
        mbedtls_sha256_free(&sha);
    }
    if (result == 0) {
        failure_stage = "ECDSA verification";
        result = mbedtls_ecdsa_verify(group, message_hash,
                                      sizeof(message_hash), public_point, &r,
                                      &s);
    }
    if (result != 0) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG, "%s failed: -0x%04x", failure_stage,
                 (unsigned)(-result));
    }
    mbedtls_mpi_free(&half_order);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_pk_free(&key);
    return result == 0 ? PXA_STATUS_OK : PXA_STATUS_DENIED;
}

pxa_status_t pxa_esp_mbedtls_sha256(const uint8_t *data, size_t size,
                                    uint8_t *output) {
    if (data == NULL || output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    return mbedtls_sha256(data, size, output, 0) == 0 ? PXA_STATUS_OK
                                                      : PXA_STATUS_INTERNAL;
}

pxa_status_t pxa_esp_mbedtls_p256_verify(void *context,
                                         pxa_bytes_t publisher_key_id,
                                         pxa_bytes_t domain,
                                         pxa_bytes_t manifest,
                                         pxa_bytes_t signature) {
    pxa_esp_mbedtls_trust_t *trust;
    const pxa_esp_mbedtls_publisher_key_t *key = NULL;
    const uint8_t *open_distribution_spki = NULL;
    size_t open_distribution_spki_size = 0;
    size_t index;
    if (context == NULL || publisher_key_id.data == NULL ||
        publisher_key_id.size != PXA_ESP_MBEDTLS_SHA256_BYTES ||
        signature.data == NULL || signature.size != 64 ||
        manifest.data == NULL || manifest.size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    trust = (pxa_esp_mbedtls_trust_t *)context;
    for (index = 0; index < trust->key_count; ++index) {
        uint8_t computed_key_id[PXA_ESP_MBEDTLS_SHA256_BYTES];
        if (trust->keys[index].spki == NULL) continue;
        if (mbedtls_sha256(trust->keys[index].spki,
                           trust->keys[index].spki_size, computed_key_id,
                           0) != 0) {
            continue;
        }
        if (constant_time_equal(computed_key_id, publisher_key_id.data,
                                publisher_key_id.size)) {
            key = &trust->keys[index];
            break;
        }
    }
    if (key == NULL) {
        open_distribution_spki = manifest_publisher_spki(
            manifest, publisher_key_id, &open_distribution_spki_size);
        if (open_distribution_spki != NULL) {
            trust->open_distribution_spki = open_distribution_spki;
            trust->open_distribution_spki_size = open_distribution_spki_size;
            memcpy(trust->open_distribution_key_id, publisher_key_id.data,
                   PXA_ESP_MBEDTLS_SHA256_BYTES);
            trust->has_open_distribution_key = 1;
        /* The cached pointer is valid only between the package-signature and
         * container-signature checks for one installer operation. Never use
         * it to verify a later package manifest. */
        } else if (is_container_digest_domain(domain) &&
                   trust->has_open_distribution_key &&
                   trust->open_distribution_spki != NULL &&
                   constant_time_equal(trust->open_distribution_key_id,
                                       publisher_key_id.data,
                                       PXA_ESP_MBEDTLS_SHA256_BYTES)) {
            open_distribution_spki = trust->open_distribution_spki;
            open_distribution_spki_size = trust->open_distribution_spki_size;
        }
    }
    if (key == NULL && open_distribution_spki == NULL) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG, "Publisher key lookup failed");
        return PXA_STATUS_DENIED;
    }
    if (open_distribution_spki != NULL) {
        return verify_key(open_distribution_spki, open_distribution_spki_size,
                          domain, manifest, signature);
    }
    return verify_key(key->spki, key->spki_size, domain, manifest, signature);
}

/* Streaming SHA-256 implementing the OpenSSL adapter API over mbedTLS so
 * the C installer can hash in bounded chunks on ESP builds. */
#include "pxa/openssl/pxa_openssl.h"

pxa_status_t pxa_openssl_sha256_stream_begin(
    pxa_openssl_sha256_stream_t *stream) {
    mbedtls_sha256_context *context;
    if (stream == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    context = malloc(sizeof(*context));
    if (context == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    mbedtls_sha256_init(context);
    if (mbedtls_sha256_starts(context, 0) != 0) {
        free(context);
        return PXA_STATUS_INTERNAL;
    }
    stream->state = context;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_update(
    pxa_openssl_sha256_stream_t *stream, const uint8_t *data, size_t size) {
    mbedtls_sha256_context *context;
    if (stream == NULL || stream->state == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    context = (mbedtls_sha256_context *)stream->state;
    if (size != 0 && mbedtls_sha256_update(context, data, size) != 0) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_finish(
    pxa_openssl_sha256_stream_t *stream, uint8_t *output) {
    mbedtls_sha256_context *context;
    if (stream == NULL || stream->state == NULL || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    context = (mbedtls_sha256_context *)stream->state;
    if (mbedtls_sha256_finish(context, output) != 0) {
        return PXA_STATUS_INTERNAL;
    }
    mbedtls_sha256_free(context);
    free(context);
    stream->state = NULL;
    return PXA_STATUS_OK;
}

void pxa_openssl_sha256_stream_abort(pxa_openssl_sha256_stream_t *stream) {
    if (stream == NULL || stream->state == NULL) return;
    mbedtls_sha256_free((mbedtls_sha256_context *)stream->state);
    free(stream->state);
    stream->state = NULL;
}

#endif /* ESP_PLATFORM */
