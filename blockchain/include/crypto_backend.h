/**
 * crypto_backend.h - Compile-time crypto abstraction for ECDSA / Falcon-512
 *
 * Build with:
 *   SIG_SCHEME=1  (default) -> ECDSA secp256k1 via OpenSSL
 *   SIG_SCHEME=2            -> Falcon-512 via liboqs
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

#ifndef SIG_SCHEME
#define SIG_SCHEME SIG_ECDSA
#endif

/* ------------------------------------------------------------------ */
/* Compile-time size constants                                        */
/* ------------------------------------------------------------------ */
#if SIG_SCHEME == SIG_FALCON512
  #define CRYPTO_PUBKEY_MAX   897
  #define CRYPTO_SECKEY_MAX   1281
  #define CRYPTO_SIG_MAX      690   /* Falcon-512 max sig (padded) */
  #define CRYPTO_SCHEME_NAME  "Falcon-512"
#else  /* SIG_ECDSA */
  #define CRYPTO_PUBKEY_MAX   65    /* uncompressed secp256k1 */
  #define CRYPTO_SECKEY_MAX   32    /* raw 256-bit scalar */
  #define CRYPTO_SIG_MAX      72    /* DER-encoded ECDSA */
  #define CRYPTO_SCHEME_NAME  "ECDSA-secp256k1"
#endif

/* ------------------------------------------------------------------ */
/* Opaque context                                                     */
/* ------------------------------------------------------------------ */

/**
 * crypto_ctx_t wraps backend state.
 * For ECDSA: holds EVP_PKEY* so sign/verify don't reconstruct every call.
 * For Falcon: holds OQS_SIG* (one per thread for signing).
 */
typedef struct crypto_ctx crypto_ctx_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/** Allocate and initialise a new context. Returns NULL on failure. */
crypto_ctx_t *crypto_ctx_new(void);

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

/* ------------------------------------------------------------------ */
/* Thread cleanup (call before thread exit in Falcon builds)          */
/* ------------------------------------------------------------------ */
void crypto_thread_cleanup(void);

#endif /* CRYPTO_BACKEND_H */
