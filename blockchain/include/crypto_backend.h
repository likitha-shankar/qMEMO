/**
 * crypto_backend.h - Compile-time crypto abstraction for ECDSA / Falcon-512 / ML-DSA-44 / Hybrid
 *
 * Build with:
 *   SIG_SCHEME=1  (default) -> ECDSA secp256k1 via OpenSSL
 *   SIG_SCHEME=2            -> Falcon-512 via liboqs
 *   SIG_SCHEME=3            -> Hybrid (both ECDSA + Falcon-512, runtime dispatch)
 *   SIG_SCHEME=4            -> ML-DSA-44 via liboqs
 */
#ifndef CRYPTO_BACKEND_H
#define CRYPTO_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Signature scheme selector                                          */
/* ------------------------------------------------------------------ */
#define SIG_ECDSA      1
#define SIG_FALCON512  2
#define SIG_HYBRID     3
#define SIG_ML_DSA44   4

#ifndef SIG_SCHEME
#define SIG_SCHEME SIG_ECDSA
#endif

/* ------------------------------------------------------------------ */
/* Compile-time size constants — universal maximums                    */
/* ------------------------------------------------------------------ */
/* All buffers use ML-DSA-44 maximums (the largest scheme) regardless */
/* of SIG_SCHEME. This ensures binary compatibility: a TX serialized  */
/* by any node can be deserialized by any other node.                 */
/* ------------------------------------------------------------------ */
#define CRYPTO_PUBKEY_MAX   1312  /* ML-DSA-44 public key (was 897) */
#define CRYPTO_SECKEY_MAX   2560  /* ML-DSA-44 secret key (was 1281) */
#define CRYPTO_SIG_MAX      2420  /* ML-DSA-44 signature (was 690) */

#if SIG_SCHEME == SIG_FALCON512
  #define CRYPTO_SCHEME_NAME  "Falcon-512"
#elif SIG_SCHEME == SIG_HYBRID
  #define CRYPTO_SCHEME_NAME  "Hybrid (ECDSA + Falcon-512)"
#elif SIG_SCHEME == SIG_ML_DSA44
  #define CRYPTO_SCHEME_NAME  "ML-DSA-44"
#else  /* SIG_ECDSA */
  #define CRYPTO_SCHEME_NAME  "ECDSA-secp256k1"
#endif

/* ------------------------------------------------------------------ */
/* Opaque context                                                     */
/* ------------------------------------------------------------------ */

/**
 * crypto_ctx_t wraps backend state.
 * For ECDSA: holds EVP_PKEY* so sign/verify don't reconstruct every call.
 * For Falcon: holds OQS_SIG* (one per thread for signing).
 * For Hybrid: holds sig_type + union of either backend.
 */
typedef struct crypto_ctx crypto_ctx_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * Allocate and initialise a new context for the given sig_type.
 * sig_type must be SIG_ECDSA, SIG_FALCON512, or SIG_ML_DSA44.
 * Returns NULL on failure or if sig_type is not compiled in.
 */
crypto_ctx_t *crypto_ctx_new(uint8_t sig_type);

/** Free context and all internal resources. */
void crypto_ctx_free(crypto_ctx_t *ctx);

/* ------------------------------------------------------------------ */
/* Key generation                                                     */
/* ------------------------------------------------------------------ */

/**
 * Generate a keypair.
 * - pubkey/seckey buffers must be at least CRYPTO_PUBKEY_MAX / CRYPTO_SECKEY_MAX.
 * - Actual lengths written to *pubkey_len, *seckey_len.
 * - For ECDSA the EVP_PKEY is stored inside ctx for later sign/verify.
 */
bool crypto_keygen(crypto_ctx_t *ctx,
                   uint8_t *pubkey, size_t *pubkey_len,
                   uint8_t *seckey, size_t *seckey_len);

/* ------------------------------------------------------------------ */
/* Signing                                                            */
/* ------------------------------------------------------------------ */

/**
 * Sign a message.
 * - sig_out must be at least CRYPTO_SIG_MAX bytes.
 * - *sig_len receives the actual signature length.
 * - For ECDSA: uses the EVP_PKEY stored in ctx (seckey bytes ignored).
 * - For Falcon: uses seckey bytes directly via OQS_SIG_sign.
 */
bool crypto_sign(crypto_ctx_t *ctx,
                 uint8_t *sig_out, size_t *sig_len,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *seckey, size_t seckey_len);

/* ------------------------------------------------------------------ */
/* Verification                                                       */
/* ------------------------------------------------------------------ */

/**
 * Verify a signature given the raw public key bytes.
 * - For ECDSA: reconstructs EVP_PKEY from the 65-byte uncompressed pubkey.
 * - For Falcon: calls OQS_SIG_verify directly.
 */
bool crypto_verify(crypto_ctx_t *ctx,
                   const uint8_t *sig, size_t sig_len,
                   const uint8_t *msg, size_t msg_len,
                   const uint8_t *pubkey, size_t pubkey_len);

/**
 * Stateless verify dispatching by sig_type. Creates a temporary context,
 * verifies, and frees it. Used for TX verification without wallet context.
 */
bool crypto_verify_typed(uint8_t sig_type,
                         const uint8_t *sig, size_t sig_len,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *pubkey, size_t pubkey_len);

/* ------------------------------------------------------------------ */
/* Thread cleanup (call before thread exit in Falcon builds)          */
/* ------------------------------------------------------------------ */
void crypto_thread_cleanup(void);

#endif /* CRYPTO_BACKEND_H */
