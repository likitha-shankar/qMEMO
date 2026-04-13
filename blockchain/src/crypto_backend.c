/**
 * crypto_backend.c - Ed25519, Falcon-512, and ML-DSA-44 implementations
 *
 * Compile-time selection via SIG_SCHEME (1=Ed25519, 2=Falcon-512, 3=Hybrid, 4=ML-DSA-44).
 * In hybrid mode all three backends are compiled in, with runtime dispatch on ctx->sig_type.
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

/* ==================================================================
 * Helper: keygen from deterministic seed (used by wallet_create_named)
 * Exposed as a non-header internal helper.
 * ================================================================== */
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
