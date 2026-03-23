/**
 * crypto_backend.c - ECDSA (secp256k1), Falcon-512, and ML-DSA-44 implementations
 *
 * Compile-time selection via SIG_SCHEME (1=ECDSA, 2=Falcon-512, 3=Hybrid, 4=ML-DSA-44).
 * In hybrid mode both ECDSA and Falcon backends are compiled in, with runtime dispatch
 * based on ctx->sig_type.
 */

#include "../include/crypto_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==================================================================
 * Determine which backends to compile
 * ================================================================== */
#if SIG_SCHEME == SIG_ECDSA || SIG_SCHEME == SIG_HYBRID
  #define COMPILE_ECDSA 1
#else
  #define COMPILE_ECDSA 0
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
#if COMPILE_ECDSA
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#endif

#if COMPILE_FALCON || COMPILE_ML_DSA
#include <oqs/oqs.h>
#endif

/* ==================================================================
 * Unified struct crypto_ctx
 * ================================================================== */
struct crypto_ctx {
    uint8_t sig_type;
#if COMPILE_ECDSA
    EVP_PKEY *pkey;
#endif
#if COMPILE_FALCON
    OQS_SIG *sig;
#endif
#if COMPILE_ML_DSA
    OQS_SIG *ml_dsa_sig;
#endif
};

/* ==================================================================
 * ECDSA static helpers
 * ================================================================== */
#if COMPILE_ECDSA

static bool ecdsa_init_ctx(crypto_ctx_t *ctx) {
    ctx->pkey = NULL;
    return true;
}

static bool ecdsa_keygen(crypto_ctx_t *ctx,
                         uint8_t *pubkey, size_t *pubkey_len,
                         uint8_t *seckey, size_t *seckey_len) {
    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!kctx) return false;

    bool ok = false;

    if (EVP_PKEY_keygen_init(kctx) <= 0) goto cleanup;
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_secp256k1) <= 0) goto cleanup;

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(kctx, &pkey) <= 0) goto cleanup;

    /* Extract uncompressed public key (65 bytes) */
    size_t pk_len = 0;
    if (EVP_PKEY_get_octet_string_param(pkey, "pub", NULL, 0, &pk_len) != 1)
        goto cleanup_key;
    if (pk_len > CRYPTO_PUBKEY_MAX) goto cleanup_key;
    if (EVP_PKEY_get_octet_string_param(pkey, "pub", pubkey, pk_len, &pk_len) != 1)
        goto cleanup_key;
    *pubkey_len = pk_len;

    /* Extract raw private key scalar (32 bytes) */
    {
        BIGNUM *bn = NULL;
        if (EVP_PKEY_get_bn_param(pkey, "priv", &bn) != 1)
            goto cleanup_key;
        int bn_len = BN_num_bytes(bn);
        if (bn_len > (int)CRYPTO_SECKEY_MAX) {
            BN_free(bn);
            goto cleanup_key;
        }
        memset(seckey, 0, CRYPTO_SECKEY_MAX);
        BN_bn2binpad(bn, seckey, 32);
        *seckey_len = 32;
        BN_free(bn);
    }

    /* Store EVP_PKEY in context for signing */
    if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
    ctx->pkey = pkey;
    pkey = NULL;
    ok = true;

cleanup_key:
    if (pkey) EVP_PKEY_free(pkey);
cleanup:
    EVP_PKEY_CTX_free(kctx);
    return ok;
}

static bool ecdsa_sign(crypto_ctx_t *ctx,
                       uint8_t *sig_out, size_t *sig_len,
                       const uint8_t *msg, size_t msg_len) {
    if (!ctx->pkey) return false;

    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return false;

    bool ok = false;

    if (EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, ctx->pkey) != 1)
        goto done;

    size_t slen = 0;
    if (EVP_DigestSign(mctx, NULL, &slen, msg, msg_len) != 1)
        goto done;
    if (slen > CRYPTO_SIG_MAX) goto done;

    if (EVP_DigestSign(mctx, sig_out, &slen, msg, msg_len) != 1)
        goto done;

    *sig_len = slen;
    ok = true;

done:
    EVP_MD_CTX_free(mctx);
    return ok;
}

/**
 * Reconstruct EVP_PKEY from raw 65-byte uncompressed secp256k1 pubkey.
 * Caller must EVP_PKEY_free() the returned key.
 */
static EVP_PKEY *pkey_from_raw_pubkey(const uint8_t *pubkey, size_t pubkey_len) {
    EVP_PKEY *pkey = NULL;

    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    if (!bld) return NULL;

    if (!OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0))
        goto fail;
    if (!OSSL_PARAM_BLD_push_octet_string(bld, "pub", pubkey, pubkey_len))
        goto fail;

    OSSL_PARAM *params = OSSL_PARAM_BLD_to_param(bld);
    if (!params) goto fail;

    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!pctx) { OSSL_PARAM_free(params); goto fail; }

    if (EVP_PKEY_fromdata_init(pctx) <= 0 ||
        EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
        pkey = NULL;
    }

    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);

fail:
    OSSL_PARAM_BLD_free(bld);
    return pkey;
}

static bool ecdsa_verify(const uint8_t *sig, size_t sig_len,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *pubkey, size_t pubkey_len) {
    EVP_PKEY *pkey = pkey_from_raw_pubkey(pubkey, pubkey_len);
    if (!pkey) return false;

    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;

    if (EVP_DigestVerifyInit(mctx, NULL, EVP_sha256(), NULL, pkey) != 1)
        goto done;

    int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len);
    ok = (rc == 1);

done:
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    return ok;
}

#endif /* COMPILE_ECDSA */

/* ==================================================================
 * Falcon-512 static helpers
 * ================================================================== */
#if COMPILE_FALCON

static bool falcon_init_ctx(crypto_ctx_t *ctx) {
    ctx->sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    return ctx->sig != NULL;
}

static bool falcon_keygen(crypto_ctx_t *ctx,
                          uint8_t *pubkey, size_t *pubkey_len,
                          uint8_t *seckey, size_t *seckey_len) {
    if (!ctx->sig) return false;

    if (OQS_SIG_keypair(ctx->sig, pubkey, seckey) != OQS_SUCCESS)
        return false;

    *pubkey_len = ctx->sig->length_public_key;   /* 897 */
    *seckey_len = ctx->sig->length_secret_key;    /* 1281 */
    return true;
}

static bool falcon_sign(crypto_ctx_t *ctx,
                        uint8_t *sig_out, size_t *sig_len,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *seckey) {
    if (!ctx->sig) return false;
    return OQS_SIG_sign(ctx->sig, sig_out, sig_len,
                         msg, msg_len, seckey) == OQS_SUCCESS;
}

static bool falcon_verify(crypto_ctx_t *ctx,
                          const uint8_t *sig, size_t sig_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *pubkey) {
    if (!ctx->sig) return false;
    return OQS_SIG_verify(ctx->sig, msg, msg_len,
                           sig, sig_len, pubkey) == OQS_SUCCESS;
}

#endif /* COMPILE_FALCON */

/* ==================================================================
 * ML-DSA-44 static helpers
 * ================================================================== */
#if COMPILE_ML_DSA

static bool ml_dsa_init_ctx(crypto_ctx_t *ctx) {
    ctx->ml_dsa_sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    return ctx->ml_dsa_sig != NULL;
}

static bool ml_dsa_keygen(crypto_ctx_t *ctx,
                          uint8_t *pubkey, size_t *pubkey_len,
                          uint8_t *seckey, size_t *seckey_len) {
    if (!ctx->ml_dsa_sig) return false;

    if (OQS_SIG_keypair(ctx->ml_dsa_sig, pubkey, seckey) != OQS_SUCCESS)
        return false;

    *pubkey_len = ctx->ml_dsa_sig->length_public_key;   /* 1312 */
    *seckey_len = ctx->ml_dsa_sig->length_secret_key;    /* 2560 */
    return true;
}

static bool ml_dsa_sign(crypto_ctx_t *ctx,
                        uint8_t *sig_out, size_t *sig_len,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *seckey) {
    if (!ctx->ml_dsa_sig) return false;
    return OQS_SIG_sign(ctx->ml_dsa_sig, sig_out, sig_len,
                         msg, msg_len, seckey) == OQS_SUCCESS;
}

static bool ml_dsa_verify(crypto_ctx_t *ctx,
                          const uint8_t *sig, size_t sig_len,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *pubkey) {
    if (!ctx->ml_dsa_sig) return false;
    return OQS_SIG_verify(ctx->ml_dsa_sig, msg, msg_len,
                           sig, sig_len, pubkey) == OQS_SUCCESS;
}

#endif /* COMPILE_ML_DSA */

/* ==================================================================
 * Public API — runtime dispatch
 * ================================================================== */

crypto_ctx_t *crypto_ctx_new(uint8_t sig_type) {
    /* Validate sig_type is compiled in */
#if SIG_SCHEME == SIG_ECDSA
    if (sig_type != SIG_ECDSA) return NULL;
#elif SIG_SCHEME == SIG_FALCON512
    if (sig_type != SIG_FALCON512) return NULL;
#elif SIG_SCHEME == SIG_ML_DSA44
    if (sig_type != SIG_ML_DSA44) return NULL;
#elif SIG_SCHEME == SIG_HYBRID
    if (sig_type != SIG_ECDSA && sig_type != SIG_FALCON512) return NULL;
#endif

    crypto_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->sig_type = sig_type;

    bool ok = false;
#if COMPILE_ECDSA
    if (sig_type == SIG_ECDSA) {
        ok = ecdsa_init_ctx(ctx);
    }
#endif
#if COMPILE_FALCON
    if (sig_type == SIG_FALCON512) {
        ok = falcon_init_ctx(ctx);
    }
#endif
#if COMPILE_ML_DSA
    if (sig_type == SIG_ML_DSA44) {
        ok = ml_dsa_init_ctx(ctx);
    }
#endif

    if (!ok) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void crypto_ctx_free(crypto_ctx_t *ctx) {
    if (!ctx) return;
#if COMPILE_ECDSA
    if (ctx->sig_type == SIG_ECDSA && ctx->pkey) {
        EVP_PKEY_free(ctx->pkey);
    }
#endif
#if COMPILE_FALCON
    if (ctx->sig_type == SIG_FALCON512 && ctx->sig) {
        OQS_SIG_free(ctx->sig);
    }
#endif
#if COMPILE_ML_DSA
    if (ctx->sig_type == SIG_ML_DSA44 && ctx->ml_dsa_sig) {
        OQS_SIG_free(ctx->ml_dsa_sig);
    }
#endif
    free(ctx);
}

bool crypto_keygen(crypto_ctx_t *ctx,
                   uint8_t *pubkey, size_t *pubkey_len,
                   uint8_t *seckey, size_t *seckey_len) {
    if (!ctx) return false;

#if COMPILE_ECDSA
    if (ctx->sig_type == SIG_ECDSA)
        return ecdsa_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif
#if COMPILE_FALCON
    if (ctx->sig_type == SIG_FALCON512)
        return falcon_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif
#if COMPILE_ML_DSA
    if (ctx->sig_type == SIG_ML_DSA44)
        return ml_dsa_keygen(ctx, pubkey, pubkey_len, seckey, seckey_len);
#endif

    return false;
}

bool crypto_sign(crypto_ctx_t *ctx,
                 uint8_t *sig_out, size_t *sig_len,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *seckey, size_t seckey_len) {
    if (!ctx) return false;

#if COMPILE_ECDSA
    if (ctx->sig_type == SIG_ECDSA) {
        (void)seckey; (void)seckey_len;
        return ecdsa_sign(ctx, sig_out, sig_len, msg, msg_len);
    }
#endif
#if COMPILE_FALCON
    if (ctx->sig_type == SIG_FALCON512) {
        (void)seckey_len;
        return falcon_sign(ctx, sig_out, sig_len, msg, msg_len, seckey);
    }
#endif
#if COMPILE_ML_DSA
    if (ctx->sig_type == SIG_ML_DSA44) {
        (void)seckey_len;
        return ml_dsa_sign(ctx, sig_out, sig_len, msg, msg_len, seckey);
    }
#endif

    return false;
}

bool crypto_verify(crypto_ctx_t *ctx,
                   const uint8_t *sig, size_t sig_len,
                   const uint8_t *msg, size_t msg_len,
                   const uint8_t *pubkey, size_t pubkey_len) {
    if (!ctx) return false;

#if COMPILE_ECDSA
    if (ctx->sig_type == SIG_ECDSA) {
        return ecdsa_verify(sig, sig_len, msg, msg_len, pubkey, pubkey_len);
    }
#endif
#if COMPILE_FALCON
    if (ctx->sig_type == SIG_FALCON512) {
        (void)pubkey_len;
        return falcon_verify(ctx, sig, sig_len, msg, msg_len, pubkey);
    }
#endif
#if COMPILE_ML_DSA
    if (ctx->sig_type == SIG_ML_DSA44) {
        (void)pubkey_len;
        return ml_dsa_verify(ctx, sig, sig_len, msg, msg_len, pubkey);
    }
#endif

    return false;
}

bool crypto_verify_typed(uint8_t sig_type,
                         const uint8_t *sig, size_t sig_len,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *pubkey, size_t pubkey_len) {
    crypto_ctx_t *ctx = crypto_ctx_new(sig_type);
    if (!ctx) return false;

    bool ok = crypto_verify(ctx, sig, sig_len, msg, msg_len, pubkey, pubkey_len);
    crypto_ctx_free(ctx);
    return ok;
}

void crypto_thread_cleanup(void) {
#if COMPILE_FALCON || COMPILE_ML_DSA
    OQS_destroy();
#endif
}
