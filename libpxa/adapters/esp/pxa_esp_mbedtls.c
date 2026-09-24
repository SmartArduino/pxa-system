/* ESP-IDF Layer 2 adapter: ECDSA P-256 / SHA-256 package signature
 * verification over mbedTLS. */

#include "pxa/esp/pxa_esp_mbedtls.h"

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"
#include "psa/crypto.h"

#define PXA_ESP_MBEDTLS_TAG "PxaSignature"

typedef mbedtls_md_context_t pxa_esp_sha256_context_t;

static int sha256_begin(pxa_esp_sha256_context_t *context) {
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(context);
    if (info == NULL || mbedtls_md_setup(context, info, 0) != 0 ||
        mbedtls_md_starts(context) != 0) {
        mbedtls_md_free(context);
        return 0;
    }
    return 1;
}

static int sha256_finish(pxa_esp_sha256_context_t *context,
                         uint8_t *output) {
    int result = mbedtls_md_finish(context, output);
    mbedtls_md_free(context);
    return result == 0;
}

static int sha256_bytes(const uint8_t *data, size_t size, uint8_t *output) {
    pxa_esp_sha256_context_t context;
    if (!sha256_begin(&context)) return 0;
    if (size != 0 && mbedtls_md_update(&context, data, size) != 0) {
        mbedtls_md_free(&context);
        return 0;
    }
    return sha256_finish(&context, output);
}

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

static int signature_scalar_is_valid(const uint8_t *scalar,
                                     const uint8_t *maximum) {
    static const uint8_t zero[PXA_ESP_MBEDTLS_SHA256_BYTES];
    return memcmp(scalar, zero, sizeof(zero)) > 0 &&
           memcmp(scalar, maximum, sizeof(zero)) < 0;
}

static int signature_is_low_s(pxa_bytes_t signature) {
    static const uint8_t order[PXA_ESP_MBEDTLS_SHA256_BYTES] = {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
        0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51,
    };
    static const uint8_t half_order[PXA_ESP_MBEDTLS_SHA256_BYTES] = {
        0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
        0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
        0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8,
    };
    return signature_scalar_is_valid(signature.data, order) &&
           signature_scalar_is_valid(signature.data + 32, order) &&
           memcmp(signature.data + 32, half_order, sizeof(half_order)) <= 0;
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
        if (!sha256_bytes(value, size, key_id) ||
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
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t public_key[65];
    uint8_t message_hash[PXA_ESP_MBEDTLS_SHA256_BYTES];
    uint8_t *canonical_buffer;
    size_t public_key_size = 0;
    int canonical_size;
    int result;
    const char *failure_stage;
    pxa_esp_sha256_context_t sha;
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
    if (mbedtls_pk_get_bitlen(&key) != 256 ||
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
        !mbedtls_pk_can_do_psa(&key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                               PSA_KEY_USAGE_VERIFY_HASH)) {
#else
        !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA)) {
#endif
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

    failure_stage = "low-S policy validation";
    result = signature_is_low_s(signature) ? 0 : -1;
    if (result == 0 &&
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
        mbedtls_pk_write_pubkey_psa(&key, public_key, sizeof(public_key),
                                    &public_key_size) != 0) {
#else
        mbedtls_ecp_point_write_binary(&mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(grp),
            &mbedtls_pk_ec(key)->MBEDTLS_PRIVATE(Q),
            MBEDTLS_ECP_PF_UNCOMPRESSED, &public_key_size,
            public_key, sizeof(public_key)) != 0) {
#endif
        failure_stage = "P-256 public key export";
        result = -1;
    }
    if (result == 0 && (public_key_size != sizeof(public_key) ||
                        public_key[0] != 0x04)) {
        failure_stage = "P-256 public key validation";
        result = -1;
    }

    if (result == 0) {
        failure_stage = "signed message hashing";
        if (!sha256_begin(&sha)) {
            result = -1;
        } else if ((domain.size != 0 &&
                    mbedtls_md_update(&sha, domain.data, domain.size) != 0) ||
                   mbedtls_md_update(&sha, manifest.data, manifest.size) != 0) {
            mbedtls_md_free(&sha);
            result = -1;
        } else if (!sha256_finish(&sha, message_hash)) {
            result = -1;
        }
    }
    if (result == 0) {
        failure_stage = "ECDSA verification";
        if (psa_crypto_init() != PSA_SUCCESS) {
            result = -1;
        } else {
            psa_set_key_type(&attributes,
                             PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
            psa_set_key_bits(&attributes, 256);
            psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
            psa_set_key_algorithm(&attributes,
                                  PSA_ALG_ECDSA(PSA_ALG_SHA_256));
            if (psa_import_key(&attributes, public_key, public_key_size,
                               &key_id) != PSA_SUCCESS ||
                psa_verify_hash(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                message_hash, sizeof(message_hash),
                                signature.data, signature.size) != PSA_SUCCESS) {
                result = -1;
            }
        }
    }
    if (result != 0) {
        ESP_LOGW(PXA_ESP_MBEDTLS_TAG, "%s failed: -0x%04x", failure_stage,
                 (unsigned)(-result));
    }
    if (key_id != MBEDTLS_SVC_KEY_ID_INIT) {
        (void)psa_destroy_key(key_id);
    }
    psa_reset_key_attributes(&attributes);
    mbedtls_pk_free(&key);
    return result == 0 ? PXA_STATUS_OK : PXA_STATUS_DENIED;
}

pxa_status_t pxa_esp_mbedtls_sha256(const uint8_t *data, size_t size,
                                    uint8_t *output) {
    if (data == NULL || output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    return sha256_bytes(data, size, output) ? PXA_STATUS_OK
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
        if (!sha256_bytes(trust->keys[index].spki,
                          trust->keys[index].spki_size, computed_key_id)) {
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
    pxa_esp_sha256_context_t *context;
    if (stream == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    context = malloc(sizeof(*context));
    if (context == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    if (!sha256_begin(context)) {
        free(context);
        return PXA_STATUS_INTERNAL;
    }
    stream->state = context;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_update(
    pxa_openssl_sha256_stream_t *stream, const uint8_t *data, size_t size) {
    pxa_esp_sha256_context_t *context;
    if (stream == NULL || stream->state == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    context = (pxa_esp_sha256_context_t *)stream->state;
    if (size != 0 && mbedtls_md_update(context, data, size) != 0) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_finish(
    pxa_openssl_sha256_stream_t *stream, uint8_t *output) {
    pxa_esp_sha256_context_t *context;
    if (stream == NULL || stream->state == NULL || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    context = (pxa_esp_sha256_context_t *)stream->state;
    if (!sha256_finish(context, output)) {
        return PXA_STATUS_INTERNAL;
    }
    free(context);
    stream->state = NULL;
    return PXA_STATUS_OK;
}

void pxa_openssl_sha256_stream_abort(pxa_openssl_sha256_stream_t *stream) {
    if (stream == NULL || stream->state == NULL) return;
    mbedtls_md_free((pxa_esp_sha256_context_t *)stream->state);
    free(stream->state);
    stream->state = NULL;
}

#endif /* ESP_PLATFORM */
