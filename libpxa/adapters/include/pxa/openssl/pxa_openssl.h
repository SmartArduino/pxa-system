#ifndef PXA_OPENSSL_H
#define PXA_OPENSSL_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/package.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: signature verification and hashing over OpenSSL.
 * The adapter is not part of the portable Core; hosts without OpenSSL must
 * supply their own pxa_package_signature_verify_fn implementation.
 */

#define PXA_OPENSSL_SHA256_BYTES ((size_t)32)

typedef struct {
    const uint8_t *spki;
    size_t spki_size;
} pxa_openssl_publisher_key_t;

typedef struct {
    uint32_t struct_size;
    const pxa_openssl_publisher_key_t *keys;
    size_t key_count;
} pxa_openssl_trust_t;

/* SHA-256 of `data`. `output` must hold at least PXA_OPENSSL_SHA256_BYTES. */
pxa_status_t pxa_openssl_sha256(const uint8_t *data, size_t size,
                                uint8_t *output);

/* Streaming SHA-256 for bounded-copy verification. The stream owns no
 * resources after finish or abort; begin allocates a provider context. */
typedef struct {
    void *state;
} pxa_openssl_sha256_stream_t;

pxa_status_t pxa_openssl_sha256_stream_begin(pxa_openssl_sha256_stream_t *stream);
pxa_status_t pxa_openssl_sha256_stream_update(pxa_openssl_sha256_stream_t *stream,
                                              const uint8_t *data, size_t size);
pxa_status_t pxa_openssl_sha256_stream_finish(pxa_openssl_sha256_stream_t *stream,
                                              uint8_t *output);
void pxa_openssl_sha256_stream_abort(pxa_openssl_sha256_stream_t *stream);

/* Implements pxa_package_signature_verify_fn over ECDSA P-256 / SHA-256.
 * `context` must point to a pxa_openssl_trust_t. The signed message is the
 * concatenation of `domain` (which carries its trailing NUL, per the Core
 * convention) and `manifest`. Enforces canonical SPKI identity, curve
 * prime256v1, scalar-range and low-S signature rules. */
pxa_status_t pxa_openssl_p256_verify(void *context, pxa_bytes_t publisher_key_id,
                                     pxa_bytes_t domain, pxa_bytes_t manifest,
                                     pxa_bytes_t signature);

#ifdef __cplusplus
}
#endif

#endif
