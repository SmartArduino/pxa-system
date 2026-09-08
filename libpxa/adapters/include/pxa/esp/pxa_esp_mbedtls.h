#ifndef PXA_ESP_MBEDTLS_H
#define PXA_ESP_MBEDTLS_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/package.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: signature verification and hashing over the
 * ESP-IDF mbedTLS build. The adapter is not part of the portable Core;
 * hosts supply their own pxa_package_signature_verify_fn implementation.
 * Mirrors the legacy C++ MbedTlsSignatureVerifier semantics. */

#define PXA_ESP_MBEDTLS_SHA256_BYTES ((size_t)32)

typedef struct {
    const uint8_t *spki;
    size_t spki_size;
} pxa_esp_mbedtls_publisher_key_t;

typedef struct {
    uint32_t struct_size;
    const pxa_esp_mbedtls_publisher_key_t *keys;
    size_t key_count;
} pxa_esp_mbedtls_trust_t;

/* SHA-256 of `data`. `output` must hold at least
 * PXA_ESP_MBEDTLS_SHA256_BYTES. */
pxa_status_t pxa_esp_mbedtls_sha256(const uint8_t *data, size_t size,
                                    uint8_t *output);

/* Implements pxa_package_signature_verify_fn over ECDSA P-256 / SHA-256.
 * `context` must point to a pxa_esp_mbedtls_trust_t. The signed message is
 * the concatenation of `domain` (which carries its trailing NUL, per the Core
 * convention) and `manifest`. Enforces canonical SPKI identity, curve
 * secp256r1, scalar-range and low-S signature rules. */
pxa_status_t pxa_esp_mbedtls_p256_verify(void *context,
                                         pxa_bytes_t publisher_key_id,
                                         pxa_bytes_t domain,
                                         pxa_bytes_t manifest,
                                         pxa_bytes_t signature);

#ifdef __cplusplus
}
#endif

#endif
