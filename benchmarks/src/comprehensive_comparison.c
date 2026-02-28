/*
 * comprehensive_comparison.c -- All 7 Algorithms Side-by-Side
 *
 * Part of the qMEMO project (IIT Chicago): single-threaded benchmark
 * comparing post-quantum and classical signature schemes across keygen,
 * sign, and verify throughput with a unified output table.
 *
 * Algorithms (in order):
 *   1. Falcon-512           (liboqs)  -- NIST Level 1, lattice
 *   2. Falcon-1024          (liboqs)  -- NIST Level 5, lattice
 *   3. ML-DSA-44            (liboqs)  -- NIST Level 2, module lattice
 *   4. ML-DSA-65            (liboqs)  -- NIST Level 3, module lattice
 *   5. SLH-DSA (SHA2-128f)  (liboqs)  -- NIST Level 1, hash-based (fast)
 *   6. ECDSA secp256k1      (OpenSSL) -- classical, Bitcoin/Ethereum curve
 *   7. Ed25519              (OpenSSL) -- classical, EdDSA
 *
 * Methodology:
 *   Per algorithm: 1000 iterations of keygen, 1000 of sign, 1000 of verify,
 *   timed separately.  Warm-up: 100 iterations before each timed block
 *   (10 for SLH-DSA which is significantly slower to sign).
 *   Message: 256 bytes of 0x42 (consistent with all qMEMO benchmarks).
 *
 * Output: aligned text table + JSON (copy-paste into docs).
 *
 * Compile:
 *   make comprehensive
 *
 * Run:
 *   ./benchmarks/bin/comprehensive_comparison
 */

#include "bench_common.h"   /* get_time, get_timestamp -- must be first */

#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN       256
#define MSG_FILL      0x42
#define BENCH_ITERS   1000
#define WARMUP_ITERS  100
#define WARMUP_SLOW   10    /* for SLH-DSA sign (very slow) */

/* ── Result struct ────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    int         nist_level;
    size_t      pubkey_bytes;
    size_t      seckey_bytes;
    size_t      sig_bytes;      /* max/typical */
    double      keygen_ops;
    double      sign_ops;
    double      verify_ops;
} result_t;

/* ── OQS helper ──────────────────────────────────────────────────────────── */

/*
 * Benchmark one OQS algorithm.  Warm-up counts differ between keygen/verify
 * (WARMUP_ITERS) and sign (warmup_sign, passed in to allow reduction for
 * slow schemes like SLH-DSA).
 */
static int bench_oqs(const char *alg_id,
                     int         nist_level,
                     const char *display_name,
                     int         warmup_sign,
                     result_t   *out)
{
    OQS_SIG *sig = OQS_SIG_new(alg_id);
    if (!sig) {
        fprintf(stderr, "  ERROR: cannot instantiate %s (%s)\n",
                display_name, alg_id);
        return -1;
    }

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);
    uint8_t *message    = malloc(MSG_LEN);

    if (!public_key || !secret_key || !signature || !message) {
        fprintf(stderr, "  ERROR: malloc failed for %s\n", display_name);
        free(public_key); free(secret_key);
        free(signature); free(message);
        OQS_SIG_free(sig);
        return -1;
    }
    memset(message, MSG_FILL, MSG_LEN);

    out->name         = display_name;
    out->nist_level   = nist_level;
    out->pubkey_bytes = sig->length_public_key;
    out->seckey_bytes = sig->length_secret_key;
    out->sig_bytes    = sig->length_signature;

    /* Generate one keypair for warm-up and timed phases */
    if (OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS) {
        fprintf(stderr, "  ERROR: initial keygen failed for %s\n", display_name);
        goto fail;
    }

    /* ── keygen warm-up + timed ─────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        if (OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS)
            goto fail;
    }
    double t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        if (OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS)
            goto fail;
    }
    out->keygen_ops = (double)BENCH_ITERS / (get_time() - t0);

    /* ── sign warm-up + timed ───────────────────────────────────────────── */
    size_t sig_len = 0;
    for (int i = 0; i < warmup_sign; i++) {
        sig_len = 0;
        if (OQS_SIG_sign(sig, signature, &sig_len, message, MSG_LEN, secret_key) != OQS_SUCCESS)
            goto fail;
    }
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        sig_len = 0;
        if (OQS_SIG_sign(sig, signature, &sig_len, message, MSG_LEN, secret_key) != OQS_SUCCESS)
            goto fail;
    }
    out->sign_ops = (double)BENCH_ITERS / (get_time() - t0);

    /* Use last signed output for verify phase */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        if (OQS_SIG_verify(sig, message, MSG_LEN,
                           signature, sig_len, public_key) != OQS_SUCCESS)
            goto fail;
    }
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        if (OQS_SIG_verify(sig, message, MSG_LEN,
                           signature, sig_len, public_key) != OQS_SUCCESS)
            goto fail;
    }
    out->verify_ops = (double)BENCH_ITERS / (get_time() - t0);

    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    return 0;

fail:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    return -1;
}

/* ── OpenSSL helpers ─────────────────────────────────────────────────────── */

static EVP_PKEY *openssl_keygen(int pkey_type, int curve_nid)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(pkey_type, NULL);
    if (!ctx) return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return NULL; }
    if (curve_nid != 0 &&
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curve_nid) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) { EVP_PKEY_CTX_free(ctx); return NULL; }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static size_t openssl_sign(EVP_PKEY *pkey, const EVP_MD *md,
                            const uint8_t *msg, size_t msg_len,
                            uint8_t *sig_out, size_t sig_max)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;
    if (EVP_DigestSignInit(mctx, NULL, md, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    size_t sig_len = sig_max;
    if (EVP_DigestSign(mctx, sig_out, &sig_len, msg, msg_len) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    EVP_MD_CTX_free(mctx);
    return sig_len;
}

static int openssl_verify(EVP_PKEY *pkey, const EVP_MD *md,
                           const uint8_t *msg, size_t msg_len,
                           const uint8_t *sig, size_t sig_len)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;
    if (EVP_DigestVerifyInit(mctx, NULL, md, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len);
    EVP_MD_CTX_free(mctx);
    return (rc == 1) ? 1 : 0;
}

static int bench_openssl(int pkey_type, int curve_nid, const EVP_MD *md,
                          const char *display_name, int nist_level,
                          result_t *out)
{
    uint8_t  msg[MSG_LEN];
    uint8_t  sig_buf[128];
    size_t   sig_len = 0;
    EVP_PKEY *pkey = NULL;
    double    t0;

    memset(msg, MSG_FILL, MSG_LEN);

    out->name         = display_name;
    out->nist_level   = nist_level;
    out->seckey_bytes = 0;   /* Not directly exposed by EVP_PKEY */

    /*
     * Key sizes for supported algorithms:
     *   ECDSA secp256k1 -- 65-byte uncompressed public point, 32-byte scalar
     *   Ed25519         -- 32-byte public key, 32-byte seed (secret key)
     */
    out->pubkey_bytes = (pkey_type == EVP_PKEY_ED25519) ? 32u : 65u;
    out->seckey_bytes = 32u;

    /* ── keygen warm-up + timed ─────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        pkey = openssl_keygen(pkey_type, curve_nid);
        if (!pkey) return -1;
        EVP_PKEY_free(pkey);
    }
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        pkey = openssl_keygen(pkey_type, curve_nid);
        if (!pkey) return -1;
        EVP_PKEY_free(pkey);
    }
    out->keygen_ops = (double)BENCH_ITERS / (get_time() - t0);

    /* One keypair for sign/verify */
    pkey = openssl_keygen(pkey_type, curve_nid);
    if (!pkey) return -1;

    /* ── sign warm-up + timed ───────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        sig_len = openssl_sign(pkey, md, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
        if (!sig_len) { EVP_PKEY_free(pkey); return -1; }
    }
    double total_sig = 0.0;
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        sig_len = openssl_sign(pkey, md, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
        if (!sig_len) { EVP_PKEY_free(pkey); return -1; }
        total_sig += (double)sig_len;
    }
    out->sign_ops  = (double)BENCH_ITERS / (get_time() - t0);
    out->sig_bytes = (size_t)(total_sig / (double)BENCH_ITERS + 0.5);

    /* Final signature for verify */
    sig_len = openssl_sign(pkey, md, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
    if (!sig_len) { EVP_PKEY_free(pkey); return -1; }

    /* ── verify warm-up + timed ─────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        if (!openssl_verify(pkey, md, msg, MSG_LEN, sig_buf, sig_len)) {
            EVP_PKEY_free(pkey); return -1;
        }
    }
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        if (!openssl_verify(pkey, md, msg, MSG_LEN, sig_buf, sig_len)) {
            EVP_PKEY_free(pkey); return -1;
        }
    }
    out->verify_ops = (double)BENCH_ITERS / (get_time() - t0);

    EVP_PKEY_free(pkey);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Comprehensive Signature Comparison  (qMEMO / IIT Chicago)\n");
    printf("  7 algorithms: 5 PQC (liboqs) + 2 classical (OpenSSL 3.x)\n");
    printf("================================================================\n");
    printf("  %d iterations per phase  |  message: %d bytes 0x%02X\n\n",
           BENCH_ITERS, MSG_LEN, MSG_FILL);

    OQS_init();

    result_t results[7];
    int      ok[7];

    /* ── PQC schemes (liboqs) ───────────────────────────────────────────── */

    printf("  [1/7] Falcon-512 ...");           fflush(stdout);
    ok[0] = bench_oqs(OQS_SIG_alg_falcon_512, 1,
                      "Falcon-512", WARMUP_ITERS, &results[0]);
    printf(" %s\n", ok[0] == 0 ? "done." : "FAILED.");

    printf("  [2/7] Falcon-1024 ...");          fflush(stdout);
    ok[1] = bench_oqs(OQS_SIG_alg_falcon_1024, 5,
                      "Falcon-1024", WARMUP_ITERS, &results[1]);
    printf(" %s\n", ok[1] == 0 ? "done." : "FAILED.");

    printf("  [3/7] ML-DSA-44 ...");            fflush(stdout);
    ok[2] = bench_oqs(OQS_SIG_alg_ml_dsa_44, 2,
                      "ML-DSA-44", WARMUP_ITERS, &results[2]);
    printf(" %s\n", ok[2] == 0 ? "done." : "FAILED.");

    printf("  [4/7] ML-DSA-65 ...");            fflush(stdout);
    ok[3] = bench_oqs(OQS_SIG_alg_ml_dsa_65, 3,
                      "ML-DSA-65", WARMUP_ITERS, &results[3]);
    printf(" %s\n", ok[3] == 0 ? "done." : "FAILED.");

    printf("  [5/7] SLH-DSA-SHA2-128f ...");   fflush(stdout);
    ok[4] = bench_oqs(OQS_SIG_alg_slh_dsa_pure_sha2_128f, 1,
                      "SLH-DSA-SHA2-128f", WARMUP_SLOW, &results[4]);
    printf(" %s\n", ok[4] == 0 ? "done." : "FAILED.");

    /* ── Classical (OpenSSL) ────────────────────────────────────────────── */

    printf("  [6/7] ECDSA secp256k1 ...");      fflush(stdout);
    ok[5] = bench_openssl(EVP_PKEY_EC, NID_secp256k1, EVP_sha256(),
                          "ECDSA secp256k1", 0, &results[5]);
    printf(" %s\n", ok[5] == 0 ? "done." : "FAILED.");

    printf("  [7/7] Ed25519 ...");              fflush(stdout);
    ok[6] = bench_openssl(EVP_PKEY_ED25519, 0, NULL,
                          "Ed25519", 0, &results[6]);
    printf(" %s\n", ok[6] == 0 ? "done." : "FAILED.");

    OQS_destroy();

    /* ── Human-readable table ───────────────────────────────────────────── */
    printf("\n");
    printf("Algorithm           NIST  PubKey  SecKey  SigBytes  "
           "Keygen/s    Sign/s    Verify/s\n");
    printf("------------------  ----  ------  ------  --------  "
           "--------  --------  --------\n");

    for (int i = 0; i < 7; i++) {
        if (ok[i] != 0) {
            printf("%-18s  (failed)\n", results[i].name ? results[i].name : "?");
            continue;
        }
        result_t *r = &results[i];
        const char *level = (r->nist_level == 0) ? "  -"
                          : (r->nist_level == 1)  ? " L1"
                          : (r->nist_level == 2)  ? " L2"
                          : (r->nist_level == 3)  ? " L3"
                                                  : " L5";
        printf("%-18s  %s   %6zu  %6zu  %8zu  %8.0f  %8.0f  %8.0f\n",
               r->name, level,
               r->pubkey_bytes, r->seckey_bytes, r->sig_bytes,
               r->keygen_ops, r->sign_ops, r->verify_ops);
    }

    /* ── JSON output ────────────────────────────────────────────────────── */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"comprehensive_signature_comparison\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"bench_iters\": %d,\n", BENCH_ITERS);
    printf("  \"message_len\": %d,\n", MSG_LEN);
    printf("  \"algorithms\": [\n");

    for (int i = 0; i < 7; i++) {
        result_t *r = &results[i];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n",       r->name ? r->name : "unknown");
        printf("      \"nist_level\": %d,\n",     r->nist_level);
        if (ok[i] == 0) {
            printf("      \"pubkey_bytes\": %zu,\n",  r->pubkey_bytes);
            printf("      \"seckey_bytes\": %zu,\n",  r->seckey_bytes);
            printf("      \"sig_bytes\": %zu,\n",     r->sig_bytes);
            printf("      \"keygen_ops_per_sec\": %.0f,\n", r->keygen_ops);
            printf("      \"sign_ops_per_sec\": %.0f,\n",   r->sign_ops);
            printf("      \"verify_ops_per_sec\": %.0f\n",  r->verify_ops);
        } else {
            printf("      \"error\": true\n");
        }
        printf("    }%s\n", (i < 6) ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");

    printf("\nComprehensive comparison complete.\n");
    return EXIT_SUCCESS;
}
