/**
 * crypto_backend.c - ECDSA (secp256k1) and Falcon-512 implementations
 *
 * Compile-time selection via SIG_SCHEME (1=ECDSA, 2=Falcon-512).
 * This is the ONLY file that includes <oqs/oqs.h> or OpenSSL signing headers.
 */

#include "../include/crypto_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==================================================================
 * FALCON-512 BACKEND
 * ================================================================== */
#if SIG_SCHEME == SIG_FALCON512

#include <oqs/oqs.h>

struct crypto_ctx {
    OQS_SIG *sig;
};

crypto_ctx_t *crypto_ctx_new(void) {
    crypto_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (!ctx->sig) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void crypto_ctx_free(crypto_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->sig) OQS_SIG_free(ctx->sig);
    free(ctx);
}

bool crypto_keygen(crypto_ctx_t *ctx,
                   uint8_t *pubkey, size_t *pubkey_len,
                   uint8_t *seckey, size_t *seckey_len) {
    if (!ctx || !ctx->sig) return false;

    if (OQS_SIG_keypair(ctx->sig, pubkey, seckey) != OQS_SUCCESS)
        return false;

    *pubkey_len = ctx->sig->length_public_key;   /* 897 */
    *seckey_len = ctx->sig->length_secret_key;    /* 1281 */
    return true;
}

bool crypto_sign(crypto_ctx_t *ctx,
                 uint8_t *sig_out, size_t *sig_len,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *seckey, size_t seckey_len) {
    if (!ctx || !ctx->sig) return false;
    (void)seckey_len;

    return OQS_SIG_sign(ctx->sig, sig_out, sig_len,
                         msg, msg_len, seckey) == OQS_SUCCESS;
}

bool crypto_verify(crypto_ctx_t *ctx,
                   const uint8_t *sig, size_t sig_len,
                   const uint8_t *msg, size_t msg_len,
                   const uint8_t *pubkey, size_t pubkey_len) {
    if (!ctx || !ctx->sig) return false;
    (void)pubkey_len;

    return OQS_SIG_verify(ctx->sig, msg, msg_len,
                           sig, sig_len, pubkey) == OQS_SUCCESS;
}

void crypto_thread_cleanup(void) {
    OQS_destroy();
}

/* ==================================================================
 * ECDSA BACKEND (secp256k1 via OpenSSL 3.x)
 * ================================================================== */
#else /* SIG_ECDSA */

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>

struct crypto_ctx {
    EVP_PKEY *pkey;   /* stored after keygen for efficient sign */
};

crypto_ctx_t *crypto_ctx_new(void) {
    crypto_ctx_t *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void crypto_ctx_free(crypto_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->pkey) EVP_PKEY_free(ctx->pkey);
    free(ctx);
}

bool crypto_keygen(crypto_ctx_t *ctx,
                   uint8_t *pubkey, size_t *pubkey_len,
                   uint8_t *seckey, size_t *seckey_len) {
    if (!ctx) return false;

    /* Generate secp256k1 key */
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
    pkey = NULL;  /* ownership transferred */
    ok = true;

cleanup_key:
    if (pkey) EVP_PKEY_free(pkey);
cleanup:
    EVP_PKEY_CTX_free(kctx);
    return ok;
}

bool crypto_sign(crypto_ctx_t *ctx,
                 uint8_t *sig_out, size_t *sig_len,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *seckey, size_t seckey_len) {
    if (!ctx || !ctx->pkey) return false;
    (void)seckey; (void)seckey_len;  /* ECDSA uses stored EVP_PKEY */

    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return false;

    bool ok = false;

    if (EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, ctx->pkey) != 1)
        goto done;

    /* Get required buffer size */
    size_t slen = 0;
    if (EVP_DigestSign(mctx, NULL, &slen, msg, msg_len) != 1)
        goto done;
    if (slen > CRYPTO_SIG_MAX) goto done;

    /* Produce signature */
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

bool crypto_verify(crypto_ctx_t *ctx,
                   const uint8_t *sig, size_t sig_len,
                   const uint8_t *msg, size_t msg_len,
                   const uint8_t *pubkey, size_t pubkey_len) {
    (void)ctx;  /* ECDSA verify is stateless once we have the pubkey */

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

void crypto_thread_cleanup(void) {
    /* No-op for OpenSSL */
}

#endif /* SIG_SCHEME */
