#include "pxa/openssl/pxa_openssl.h"

#include <string.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#define PXA_OPENSSL_HALF_SIGNATURE ((size_t)32)

static int constant_time_equal(const uint8_t *left, const uint8_t *right,
                               size_t size) {
    uint8_t difference = 0;
    size_t index;
    for (index = 0; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0;
}

pxa_status_t pxa_openssl_sha256(const uint8_t *data, size_t size,
                                uint8_t *output) {
    if (output == NULL || (data == NULL && size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (SHA256(data, size, output) == NULL) return PXA_STATUS_INTERNAL;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_begin(pxa_openssl_sha256_stream_t *stream) {
    if (stream == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    stream->state = NULL;
    stream->state = EVP_MD_CTX_new();
    if (stream->state == NULL) return PXA_STATUS_INTERNAL;
    if (EVP_DigestInit_ex((EVP_MD_CTX *)stream->state, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free((EVP_MD_CTX *)stream->state);
        stream->state = NULL;
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_update(pxa_openssl_sha256_stream_t *stream,
                                              const uint8_t *data, size_t size) {
    if (stream == NULL || stream->state == NULL ||
        (data == NULL && size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (size == 0) return PXA_STATUS_OK;
    if (EVP_DigestUpdate((EVP_MD_CTX *)stream->state, data, size) != 1) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_openssl_sha256_stream_finish(pxa_openssl_sha256_stream_t *stream,
                                              uint8_t *output) {
    unsigned int digest_size = 0;
    pxa_status_t status = PXA_STATUS_INTERNAL;
    if (stream == NULL || stream->state == NULL || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (EVP_DigestFinal_ex((EVP_MD_CTX *)stream->state, output,
                           &digest_size) == 1 &&
        digest_size == PXA_OPENSSL_SHA256_BYTES) {
        status = PXA_STATUS_OK;
    }
    EVP_MD_CTX_free((EVP_MD_CTX *)stream->state);
    stream->state = NULL;
    return status;
}

void pxa_openssl_sha256_stream_abort(pxa_openssl_sha256_stream_t *stream) {
    if (stream == NULL) return;
    if (stream->state != NULL) {
        EVP_MD_CTX_free((EVP_MD_CTX *)stream->state);
        stream->state = NULL;
    }
}

pxa_status_t pxa_openssl_p256_verify(void *context, pxa_bytes_t publisher_key_id,
                                     pxa_bytes_t domain, pxa_bytes_t manifest,
                                     pxa_bytes_t signature) {
    pxa_openssl_trust_t *trust = (pxa_openssl_trust_t *)context;
    size_t key_index;
    const uint8_t *spki = NULL;
    size_t spki_size = 0;
    EVP_PKEY *key = NULL;
    BIGNUM *raw_order = NULL;
    BIGNUM *half_order = NULL;
    BIGNUM *r = NULL;
    BIGNUM *s = NULL;
    ECDSA_SIG *ec_signature = NULL;
    EVP_MD_CTX *md = NULL;
    unsigned char *der_signature = NULL;
    int der_size;
    unsigned char *cursor;
    const unsigned char *spki_cursor;
    char group_name[32];
    size_t group_name_size = 0;
    uint8_t computed_key_id[PXA_OPENSSL_SHA256_BYTES];
    uint8_t *signed_message = NULL;
    size_t signed_size;
    int verified;
    pxa_status_t status = PXA_STATUS_DENIED;

    if (trust == NULL || trust->struct_size < sizeof(*trust) ||
        publisher_key_id.data == NULL || publisher_key_id.size == 0 ||
        publisher_key_id.size != PXA_OPENSSL_SHA256_BYTES ||
        signature.data == NULL || signature.size != PXA_PACKAGE_SIGNATURE_BYTES ||
        (manifest.data == NULL && manifest.size != 0) ||
        (domain.data == NULL && domain.size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }

    for (key_index = 0; key_index < trust->key_count; ++key_index) {
        const pxa_openssl_publisher_key_t *candidate =
            &trust->keys[key_index];
        if (candidate == NULL || candidate->spki == NULL ||
            candidate->spki_size == 0) {
            continue;
        }
        if (pxa_openssl_sha256(candidate->spki, candidate->spki_size,
                               computed_key_id) != PXA_STATUS_OK) {
            return PXA_STATUS_INTERNAL;
        }
        if (constant_time_equal(computed_key_id, publisher_key_id.data,
                                publisher_key_id.size)) {
            spki = candidate->spki;
            spki_size = candidate->spki_size;
            break;
        }
    }
    if (spki == NULL) return PXA_STATUS_DENIED;

    spki_cursor = spki;
    key = d2i_PUBKEY(NULL, &spki_cursor, (long)spki_size);
    if (key == NULL || spki_cursor != spki + spki_size ||
        EVP_PKEY_is_a(key, "EC") != 1) {
        goto done;
    }
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME,
                                       group_name, sizeof(group_name),
                                       &group_name_size) != 1 ||
        strcmp(group_name, "prime256v1") != 0) {
        goto done;
    }
    if (EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_ORDER, &raw_order) != 1) {
        goto done;
    }
    half_order = BN_dup(raw_order);
    r = BN_bin2bn(signature.data, (int)PXA_OPENSSL_HALF_SIGNATURE, NULL);
    s = BN_bin2bn(signature.data + PXA_OPENSSL_HALF_SIGNATURE,
                  (int)PXA_OPENSSL_HALF_SIGNATURE, NULL);
    if (half_order == NULL || r == NULL || s == NULL ||
        BN_rshift1(half_order, half_order) != 1 || BN_is_zero(r) ||
        BN_is_negative(r) || BN_cmp(r, raw_order) >= 0 || BN_is_zero(s) ||
        BN_is_negative(s) || BN_cmp(s, raw_order) >= 0 ||
        BN_cmp(s, half_order) > 0) {
        goto done;
    }
    ec_signature = ECDSA_SIG_new();
    if (ec_signature == NULL || ECDSA_SIG_set0(ec_signature, r, s) != 1) {
        goto done;
    }
    r = NULL;
    s = NULL;
    der_size = i2d_ECDSA_SIG(ec_signature, NULL);
    if (der_size <= 0) goto done;
    der_signature = (unsigned char *)OPENSSL_malloc((size_t)der_size);
    if (der_signature == NULL) goto done;
    cursor = der_signature;
    if (i2d_ECDSA_SIG(ec_signature, &cursor) != der_size) goto done;

    md = EVP_MD_CTX_new();
    if (md == NULL || EVP_DigestVerifyInit(md, NULL, EVP_sha256(), NULL, key) != 1) {
        goto done;
    }
    if (domain.size > SIZE_MAX - manifest.size) goto done;
    signed_size = domain.size + manifest.size;
    signed_message = (uint8_t *)OPENSSL_malloc(signed_size == 0 ? 1 : signed_size);
    if (signed_message == NULL) goto done;
    if (domain.size != 0) memcpy(signed_message, domain.data, domain.size);
    if (manifest.size != 0) {
        memcpy(signed_message + domain.size, manifest.data, manifest.size);
    }
    verified = EVP_DigestVerify(md, der_signature, (size_t)der_size,
                                signed_message, signed_size);
    status = verified == 1 ? PXA_STATUS_OK : PXA_STATUS_DENIED;

done:
    if (signed_message != NULL) OPENSSL_free(signed_message);
    if (der_signature != NULL) OPENSSL_free(der_signature);
    if (md != NULL) EVP_MD_CTX_free(md);
    if (ec_signature != NULL) ECDSA_SIG_free(ec_signature);
    if (r != NULL) BN_free(r);
    if (s != NULL) BN_free(s);
    if (half_order != NULL) BN_free(half_order);
    if (raw_order != NULL) BN_free(raw_order);
    if (key != NULL) EVP_PKEY_free(key);
    return status;
}
