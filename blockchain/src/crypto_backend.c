/**
 * crypto_backend.c — Ed25519, Falcon-512, and ML-DSA-44 implementations
 *
 * This file provides the unified cryptographic backend for the qMEMO blockchain.
 * It implements three post-quantum / classical signature schemes behind a single
 * compile-time-selected API so the rest of the codebase never calls liboqs or
 * OpenSSL directly.
 *
 * Scheme selection (set via -DSIG_SCHEME=N at compile time):
 *   SIG_SCHEME=1  Ed25519 (default)  — classical, OpenSSL EVP
 *   SIG_SCHEME=2  Falcon-512         — NIST FIPS 206, liboqs, NTRU lattice
 *   SIG_SCHEME=3  Hybrid             — all three compiled, runtime dispatch on sig_type
 *   SIG_SCHEME=4  ML-DSA-44          — NIST FIPS 204, liboqs, module lattice
 *
 * Key size constants (CRYPTO_PUBKEY_MAX / CRYPTO_SECKEY_MAX / CRYPTO_SIG_MAX)
 * are set to ML-DSA-44 maximums regardless of the active scheme. This ensures
 * every node can deserialize every transaction type without overflow — see
 * crypto_backend.h for rationale.
 *
 * Dependencies:
 *   OpenSSL 3.x (EVP API) for Ed25519
 *   liboqs 0.15.0 for Falcon-512 and ML-DSA-44
 *
 * Thread safety:
 *   crypto_verify_typed() is re-entrant — it creates a temporary OQS_SIG context
 *   per call and does not touch shared state. Use it freely in OpenMP verify loops.
 *   crypto_sign() is NOT re-entrant for Falcon: each thread must own its own
 *   crypto_ctx_t because Falcon's Gaussian sampler has per-context PRNG state.
 *   Call crypto_thread_cleanup() before thread exit in liboqs builds.
 */

#include "../include/crypto_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==================================================================
 * Determine which backends to compile
 * ================================================================== */
#if SIG_SCHEME == SIG_ED25519 || SIG_SCHEME == SIG_HYBRID
  #define COMPILE_ED25519 1
#else
  #define COMPILE_ED25519 0
#endif

#if SIG_SCHEME == SIG_FALCON512 || SIG_SCHEME == SIG_HYBRID
  #define COMPILE_FALCON 1
#else
  #define COMPILE_FALCON 0
#endif

#if SIG_SCHEME == SIG_ML_DSA44 || SIG_SCHEME == SIG_HYBRID
  #define COMPILE_ML_DSA 1
#else
  #define COMPILE_ML_DSA 0
#endif

/* ==================================================================
 * Include headers for compiled backends
 * ================================================================== */
#if COMPILE_ED25519
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#if COMPILE_FALCON || COMPILE_ML_DSA
#include <oqs/oqs.h>
#endif

/* ==================================================================
 * Unified struct crypto_ctx
 * ================================================================== */
struct crypto_ctx {
    uint8_t sig_type;
#if COMPILE_ED25519
    EVP_PKEY *pkey;       /* holds keypair for Ed25519 sign/verify */
#endif
#if COMPILE_FALCON
    OQS_SIG *falcon_sig;
#endif
#if COMPILE_ML_DSA
    OQS_SIG *mldsa_sig;
#endif
};

/* ==================================================================
 * Ed25519 helpers
 * ================================================================== */
#if COMPILE_ED25519

/**
 * ed25519_keygen — Generate an Ed25519 keypair and store it in ctx.
 *
 * Generates 32 random bytes via OpenSSL RAND_bytes, constructs an EVP_PKEY,
 * extracts the 32-byte public key, and stores the raw seed as the secret key.
 * The EVP_PKEY is kept in ctx->pkey for subsequent signing without re-deriving.
 *
 * @param ctx         Allocated context — ctx->pkey will be overwritten.
 * @param pubkey      Output buffer, at least 32 bytes. Receives the public key.
 * @param pubkey_len  Receives 32.
 * @param seckey      Output buffer, at least 32 bytes. Receives the 32-byte seed.
 * @param seckey_len  Receives 32.
 * @return true on success, false on RAND_bytes / EVP failure.
 */
static bool ed25519_keygen(crypto_ctx_t *ctx,
                            uint8_t *pubkey, size_t *pubkey_len,
                            uint8_t *seckey, size_t *seckey_len) {
    /* Generate random 32-byte seed */
    uint8_t seed[32];
    if (RAND_bytes(seed, 32) != 1) return false;

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!pkey) return false;

    /* Extract public key (32 bytes) */
    size_t pk_len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pk_len) != 1) {
        EVP_PKEY_free(pkey);
        return false;
    }
    *pubkey_len = pk_len;

    /* Store seed as secret key */
    memcpy(seckey, seed, 32);
    *seckey_len = 32;

    /* Store EVP_PKEY in context for signing */
    if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
    ctx->pkey = pkey;
    return true;
}

/**
 * ed25519_keygen_from_seed — Reconstruct an Ed25519 keypair from a known seed.
 *
 * Used by wallet_create_named() to derive deterministic keys from a user-supplied
 * passphrase/seed. Produces the same public key for the same 32-byte seed every
 * time, enabling wallet recovery without storing the secret key on-chain.
 *
 * @param ctx        Allocated context — ctx->pkey will be overwritten.
 * @param seed       32-byte seed (caller responsible for secure generation).
 * @param seed_len   Must be >= 32; only the first 32 bytes are used.
 * @param pubkey     Output buffer, at least 32 bytes. Receives the public key.
 * @param pubkey_len Receives 32.
 * @return true on success, false if seed_len < 32 or EVP fails.
 */
static bool ed25519_keygen_from_seed(crypto_ctx_t *ctx,
                                      const uint8_t *seed, size_t seed_len,
                                      uint8_t *pubkey, size_t *pubkey_len) {
    if (seed_len < 32) return false;
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!pkey) return false;
    size_t pk_len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pk_len) != 1) {
        EVP_PKEY_free(pkey);
        return false;
    }
    *pubkey_len = pk_len;
    if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
    ctx->pkey = pkey;
    return true;
}

/**
 * ed25519_sign — Sign a message with Ed25519.
 *
 * Uses the EVP_PKEY stored in ctx if available; otherwise reconstructs it
 * from the raw 32-byte seckey bytes (for use after wallet_load()). This
 * lazy-load path avoids storing an EVP_PKEY in wallet files on disk.
 *
 * @param ctx         Context — may or may not have ctx->pkey set.
 * @param sig_out     Output buffer, at least CRYPTO_SIG_MAX bytes. Receives the signature.
 * @param sig_len     Receives the actual signature length (always 64 for Ed25519).
 * @param msg         Message to sign. Ed25519 hashes internally; no pre-hashing needed.
 * @param msg_len     Length of msg in bytes.
 * @param seckey      32-byte raw private key (seed). Used only if ctx->pkey is NULL.
 * @param seckey_len  Must be >= 32.
 * @return true on success, false on EVP init or sign failure.
 */
static bool ed25519_sign(crypto_ctx_t *ctx,
                          uint8_t *sig_out, size_t *sig_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *seckey, size_t seckey_len) {
    EVP_PKEY *pkey = ctx->pkey;
    bool own_pkey = false;

    /* If context has no key, reconstruct from seckey bytes */
    if (!pkey && seckey && seckey_len >= 32) {
        pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seckey, 32);
        if (!pkey) return false;
        own_pkey = true;
    }
    if (!pkey) return false;

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) { if (own_pkey) EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestSignInit(md_ctx, NULL, NULL, NULL, pkey) == 1) {
        size_t slen = CRYPTO_SIG_MAX;
        if (EVP_DigestSign(md_ctx, sig_out, &slen, msg, msg_len) == 1) {
            *sig_len = slen;
            ok = true;
        }
    }
    EVP_MD_CTX_free(md_ctx);
    if (own_pkey) EVP_PKEY_free(pkey);
    return ok;
}

/**
 * ed25519_verify — Verify an Ed25519 signature.
 *
 * Stateless: constructs a temporary EVP_PKEY from the raw public key bytes,
 * verifies, then frees it. Safe to call concurrently from multiple threads
 * since no shared state is accessed.
 *
 * @param sig        The signature bytes (must be exactly 64 bytes for Ed25519).
 * @param sig_len    Length of sig; returns false if 0.
 * @param msg        The original message that was signed.
 * @param msg_len    Length of msg.
 * @param pubkey     32-byte raw Ed25519 public key.
 * @param pubkey_len Must be >= 32; only the first 32 bytes are used.
 * @return true if the signature is valid, false otherwise.
 */
static bool ed25519_verify(const uint8_t *sig, size_t sig_len,
                            const uint8_t *msg, size_t msg_len,
                            const uint8_t *pubkey, size_t pubkey_len) {
    if (pubkey_len < 32 || sig_len == 0) return false;
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pubkey, 32);
    if (!pkey) return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1) {
        ok = (EVP_DigestVerify(ctx, sig, sig_len, msg, msg_len) == 1);
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}
#endif /* COMPILE_ED25519 */

/* ==================================================================
 * Falcon-512 helpers
 * ================================================================== */
#if COMPILE_FALCON

/**
 * falcon_keygen — Generate a Falcon-512 keypair via liboqs.
 *
 * Lazily allocates the OQS_SIG object if not already present in ctx.
 * Key sizes are fixed by FIPS 206 (NIST Level 1): pubkey=897B, seckey=1281B.
 *
 * NOTE: Falcon key generation is slow (~148 keygen/sec on M2 Pro, ~153/sec on Xeon)
 * due to NTRU lattice reduction. This is a one-time cost — signing and verification
 * are fast (4K–5K sign/sec, 23K–31K verify/sec respectively).
 *
 * @param ctx        Context — ctx->falcon_sig is allocated here if NULL.
 * @param pubkey     Output buffer, at least 897 bytes (CRYPTO_PUBKEY_MAX is sufficient).
 * @param pubkey_len Receives 897.
 * @param seckey     Output buffer, at least 1281 bytes (CRYPTO_SECKEY_MAX is sufficient).
 * @param seckey_len Receives 1281.
 * @return true on success, false if OQS_SIG allocation or keypair generation fails.
 */
static bool falcon_keygen(crypto_ctx_t *ctx,
                           uint8_t *pubkey, size_t *pubkey_len,
                           uint8_t *seckey, size_t *seckey_len) {
    if (!ctx->falcon_sig) {
        ctx->falcon_sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!ctx->falcon_sig) return false;
    }
    OQS_STATUS rc = OQS_SIG_keypair(ctx->falcon_sig, pubkey, seckey);
    if (rc != OQS_SUCCESS) return false;
    *pubkey_len = ctx->falcon_sig->length_public_key;
    *seckey_len = ctx->falcon_sig->length_secret_key;
    return true;
}

/**
 * falcon_sign — Sign a message with Falcon-512 via liboqs.
 *
 * Falcon signatures are variable-length (mean ~655B, max 666B per FIPS 206).
 * The OQS API writes the actual length to *sig_len. Callers must allocate at
 * least CRYPTO_SIG_MAX (2420B, ML-DSA-44 maximum) to be safe in hybrid mode.
 *
 * IMPORTANT — thread safety: ctx->falcon_sig contains per-context PRNG state.
 * Each signing thread must own a distinct crypto_ctx_t. Never share a single
 * ctx across threads for signing.
 *
 * @param ctx        Context with an initialised ctx->falcon_sig.
 * @param sig_out    Output buffer, at least 666 bytes (or CRYPTO_SIG_MAX).
 * @param sig_len    Receives the actual signature length (typically 620–666B).
 * @param msg        Message bytes to sign.
 * @param msg_len    Length of msg.
 * @param seckey     Falcon-512 secret key (1281 bytes).
 * @param seckey_len Must match the Falcon-512 secret key size (1281 bytes).
 * @return true on success, false if OQS_SIG allocation or sign fails.
 */
static bool falcon_sign(crypto_ctx_t *ctx,
                         uint8_t *sig_out, size_t *sig_len,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *seckey, size_t seckey_len) {
    if (!ctx->falcon_sig) {
        ctx->falcon_sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!ctx->falcon_sig) return false;
    }
    OQS_STATUS rc = OQS_SIG_sign(ctx->falcon_sig, sig_out, sig_len, msg, msg_len, seckey);
    return (rc == OQS_SUCCESS);
}

/**
 * falcon_verify — Verify a Falcon-512 signature (stateless, thread-safe).
 *
 * Allocates a temporary OQS_SIG context, verifies the signature, then frees it.
 * Creating a new OQS_SIG per call costs ~µs but eliminates all shared state,
 * making this function safe for parallel verification in OpenMP loops.
 *
 * Performance note: At 23,877 verify/sec (Xeon 6242 single-core), the context
 * allocation overhead is negligible (<1% of total time). At 10 cores, throughput
 * scales to ~184K verify/sec with 96.4% efficiency (see docs/RESULTS.md §5.2).
 *
 * @param sig        Falcon-512 signature bytes (variable length, max 666B).
 * @param sig_len    Actual signature length (as returned by falcon_sign).
 * @param msg        The original message that was signed.
 * @param msg_len    Length of msg.
 * @param pubkey     Falcon-512 public key (897 bytes, FIPS 206 §3.3).
 * @param pubkey_len Must be exactly 897 bytes.
 * @return true if signature is valid, false otherwise or on OQS error.
 */
static bool falcon_verify(const uint8_t *sig, size_t sig_len,
                           const uint8_t *msg, size_t msg_len,
                           const uint8_t *pubkey, size_t pubkey_len) {
    OQS_SIG *s = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (!s) return false;
    bool ok = (OQS_SIG_verify(s, msg, msg_len, sig, sig_len, pubkey) == OQS_SUCCESS);
    OQS_SIG_free(s);
    return ok;
}
#endif /* COMPILE_FALCON */

/* ==================================================================
 * ML-DSA-44 helpers
 * ================================================================== */
#if COMPILE_ML_DSA

/**
 * mldsa_keygen — Generate an ML-DSA-44 keypair via liboqs.
 *
 * ML-DSA-44 (CRYSTALS-Dilithium, NIST FIPS 204 §5, parameter set ML-DSA-44)
 * provides NIST Security Level 2 (~AES-128 post-quantum). Key sizes:
 *   pubkey = 1,312 bytes (FIPS 204 Table 1, row ML-DSA-44)
 *   seckey = 2,560 bytes
 *
 * ML-DSA keygen is fast (47K–52K keygen/sec on Xeon with AVX-512), unlike
 * Falcon (148–153/sec). Prefer ML-DSA when wallets need to be created frequently.
 *
 * @param ctx        Context — ctx->mldsa_sig is allocated here if NULL.
 * @param pubkey     Output buffer, at least 1312 bytes (CRYPTO_PUBKEY_MAX is sufficient).
 * @param pubkey_len Receives 1312.
 * @param seckey     Output buffer, at least 2560 bytes (CRYPTO_SECKEY_MAX is sufficient).
 * @param seckey_len Receives 2560.
 * @return true on success, false if OQS allocation or keypair generation fails.
 */
static bool mldsa_keygen(crypto_ctx_t *ctx,
                          uint8_t *pubkey, size_t *pubkey_len,
                          uint8_t *seckey, size_t *seckey_len) {
    if (!ctx->mldsa_sig) {
        ctx->mldsa_sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
        if (!ctx->mldsa_sig) return false;
    }
    OQS_STATUS rc = OQS_SIG_keypair(ctx->mldsa_sig, pubkey, seckey);
    if (rc != OQS_SUCCESS) return false;
    *pubkey_len = ctx->mldsa_sig->length_public_key;
    *seckey_len = ctx->mldsa_sig->length_secret_key;
    return true;
}

/**
 * mldsa_sign — Sign a message with ML-DSA-44 via liboqs.
 *
 * ML-DSA-44 produces fixed-length 2,420-byte signatures (FIPS 204 §5.2, λ=128).
 * Unlike Falcon, ML-DSA uses rejection sampling (no floating-point), which makes
 * it easier to verify for timing side-channels.
 *
 * Signing throughput: 10K–16K sign/sec single-core (14K on Skylake-SP Xeon).
 * This is 2–3× faster than Falcon-512 signing (4K–5K/sec) due to the simpler
 * module lattice arithmetic without NTRU key generation overhead.
 *
 * @param ctx        Context with an initialised ctx->mldsa_sig.
 * @param sig_out    Output buffer, exactly 2420 bytes required (CRYPTO_SIG_MAX is sufficient).
 * @param sig_len    Receives 2420 (fixed; ML-DSA signatures are not variable-length).
 * @param msg        Message bytes to sign.
 * @param msg_len    Length of msg.
 * @param seckey     ML-DSA-44 secret key (2560 bytes).
 * @param seckey_len Must match ML-DSA-44 secret key size (2560 bytes).
 * @return true on success, false if OQS_SIG allocation or sign fails.
 */
static bool mldsa_sign(crypto_ctx_t *ctx,
                        uint8_t *sig_out, size_t *sig_len,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *seckey, size_t seckey_len) {
    if (!ctx->mldsa_sig) {
        ctx->mldsa_sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
        if (!ctx->mldsa_sig) return false;
    }
    OQS_STATUS rc = OQS_SIG_sign(ctx->mldsa_sig, sig_out, sig_len, msg, msg_len, seckey);
    return (rc == OQS_SUCCESS);
}

/**
 * mldsa_verify — Verify an ML-DSA-44 signature (stateless, thread-safe).
 *
 * Allocates a temporary OQS_SIG, verifies, frees. Same thread-safety guarantee
 * as falcon_verify — safe for concurrent use in OpenMP loops.
 *
 * Performance: 46K–49K verify/sec on Xeon with AVX-512 (liboqs NTT path),
 * 25K/sec on M2 Pro ARM. ML-DSA is the fastest NIST PQC scheme for verification
 * on x86-64 hardware due to AVX-512 NTT acceleration (see docs/RESULTS.md §1).
 * Despite this, ML-DSA delivers lower end-to-end blockchain TPS than Falcon-512
 * because its fixed 2,420B signatures are 3.5× larger, increasing serialization
 * and I/O cost (see docs/FINDINGS.md §3, docs/RESULTS.md §10.4).
 *
 * @param sig        ML-DSA-44 signature bytes (must be exactly 2420 bytes).
 * @param sig_len    Must be 2420.
 * @param msg        The original message that was signed.
 * @param msg_len    Length of msg.
 * @param pubkey     ML-DSA-44 public key (1312 bytes, FIPS 204 Table 1).
 * @param pubkey_len Must be exactly 1312 bytes.
 * @return true if signature is valid, false otherwise or on OQS error.
 */
static bool mldsa_verify(const uint8_t *sig, size_t sig_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *pubkey, size_t pubkey_len) {
    OQS_SIG *s = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!s) return false;
    bool ok = (OQS_SIG_verify(s, msg, msg_len, sig, sig_len, pubkey) == OQS_SUCCESS);
    OQS_SIG_free(s);
    return ok;
}
#endif /* COMPILE_ML_DSA */

/* ==================================================================
 * Public API
 * ================================================================== */

/**
 * crypto_ctx_new — Allocate and initialise a crypto context for a given scheme.
 *
 * Pre-allocates the liboqs OQS_SIG object for the chosen scheme so that
 * subsequent sign/verify calls avoid repeated allocation overhead. For Ed25519,
 * no pre-allocation is needed (EVP_PKEY is created on keygen/sign).
 *
 * @param sig_type  SIG_ED25519 (1), SIG_FALCON512 (2), or SIG_ML_DSA44 (4).
 *                  SIG_HYBRID (3) is valid only in SIG_SCHEME=3 builds.
 * @return Allocated context, or NULL on memory or liboqs initialisation failure.
 *         Caller must free with crypto_ctx_free().
 */
crypto_ctx_t *crypto_ctx_new(uint8_t sig_type) {
    crypto_ctx_t *ctx = calloc(1, sizeof(crypto_ctx_t));
    if (!ctx) return NULL;
    ctx->sig_type = sig_type;

#if COMPILE_FALCON
    if (sig_type == SIG_FALCON512) {
        ctx->falcon_sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!ctx->falcon_sig) { free(ctx); return NULL; }
    }
#endif
#if COMPILE_ML_DSA
    if (sig_type == SIG_ML_DSA44) {
        ctx->mldsa_sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
        if (!ctx->mldsa_sig) { free(ctx); return NULL; }
    }
#endif
    return ctx;
}

void crypto_ctx_free(crypto_ctx_t *ctx) {
    if (!ctx) return;
#if COMPILE_ED25519
    if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
#endif
#if COMPILE_FALCON
    if (ctx->falcon_sig) OQS_SIG_free(ctx->falcon_sig);
#endif
#if COMPILE_ML_DSA
    if (ctx->mldsa_sig) OQS_SIG_free(ctx->mldsa_sig);
#endif
    free(ctx);
}

bool crypto_keygen(crypto_ctx_t *ctx,
                   uint8_t *pubkey, size_t *pubkey_len,
                   uint8_t *seckey, size_t *seckey_len) {
    if (!ctx) return false;
    switch (ctx->sig_type) {
#if COMPILE_ED25519
        case SIG_ED25519:
            return ed25519_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif
#if COMPILE_FALCON
        case SIG_FALCON512:
            return falcon_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif
#if COMPILE_ML_DSA
        case SIG_ML_DSA44:
            return mldsa_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif
        default: return false;
    }
}

bool crypto_sign(crypto_ctx_t *ctx,
                 uint8_t *sig_out, size_t *sig_len,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *seckey, size_t seckey_len) {
    if (!ctx) return false;
    switch (ctx->sig_type) {
#if COMPILE_ED25519
        case SIG_ED25519:
            return ed25519_sign(ctx, sig_out, sig_len, msg, msg_len, seckey, seckey_len);
#endif
#if COMPILE_FALCON
        case SIG_FALCON512:
            return falcon_sign(ctx, sig_out, sig_len, msg, msg_len, seckey, seckey_len);
#endif
#if COMPILE_ML_DSA
        case SIG_ML_DSA44:
            return mldsa_sign(ctx, sig_out, sig_len, msg, msg_len, seckey, seckey_len);
#endif
        default: return false;
    }
}

bool crypto_verify(crypto_ctx_t *ctx,
                   const uint8_t *sig, size_t sig_len,
                   const uint8_t *msg, size_t msg_len,
                   const uint8_t *pubkey, size_t pubkey_len) {
    if (!ctx) return false;
    return crypto_verify_typed(ctx->sig_type, sig, sig_len, msg, msg_len, pubkey, pubkey_len);
}

bool crypto_verify_typed(uint8_t sig_type,
                         const uint8_t *sig, size_t sig_len,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *pubkey, size_t pubkey_len) {
    switch (sig_type) {
#if COMPILE_ED25519
        case SIG_ED25519:
            return ed25519_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len);
#endif
#if COMPILE_FALCON
        case SIG_FALCON512:
            return falcon_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len);
#endif
#if COMPILE_ML_DSA
        case SIG_ML_DSA44:
            return mldsa_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len);
#endif
#if SIG_SCHEME == SIG_HYBRID
        case SIG_HYBRID:
            /* Hybrid: try Ed25519 first, then Falcon, then ML-DSA */
            if (ed25519_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len)) return true;
            if (falcon_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len)) return true;
            return mldsa_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len);
#endif
        default: return false;
    }
}

void crypto_thread_cleanup(void) {
#if COMPILE_FALCON || COMPILE_ML_DSA
    OQS_thread_stop();
#endif
}

/**
 * crypto_ed25519_keygen_from_seed — Deterministic Ed25519 keygen from a seed.
 *
 * Thin wrapper around ed25519_keygen_from_seed(). Exposed at the .c level (not
 * in crypto_backend.h) because it is only used by wallet_create_named() and
 * does not belong in the general-purpose crypto API.
 *
 * @param ctx        Allocated context — ctx->pkey will be overwritten.
 * @param seed       32-byte seed bytes.
 * @param seed_len   Must be >= 32.
 * @param pubkey     Output buffer >= 32 bytes. Receives the derived public key.
 * @param pubkey_len Receives 32.
 * @return true on success, false on size check or EVP failure.
 */
bool crypto_ed25519_keygen_from_seed(crypto_ctx_t *ctx,
                                      const uint8_t *seed, size_t seed_len,
                                      uint8_t *pubkey, size_t *pubkey_len) {
#if COMPILE_ED25519
    return ed25519_keygen_from_seed(ctx, seed, seed_len, pubkey, pubkey_len);
#else
    (void)ctx; (void)seed; (void)seed_len; (void)pubkey; (void)pubkey_len;
    return false;
#endif
}
