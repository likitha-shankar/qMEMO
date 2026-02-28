/*
 * classical_benchmark.c — ECDSA secp256k1 and Ed25519 Baseline Benchmarks
 *
 * Part of the qMEMO project (IIT Chicago): measures classical signature
 * scheme performance for comparison against Falcon-512 and ML-DSA.
 *
 * Uses OpenSSL 3.x at /opt/homebrew/opt/openssl/ via the high-level
 * EVP_PKEY / EVP_PKEY_CTX / EVP_DigestSign API — not the deprecated
 * EC_ / ECDSA_ low-level API.
 *
 * Schemes:
 *   ECDSA secp256k1  — the curve used by Bitcoin and Ethereum.
 *                      EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL)
 *                      Signatures are DER-encoded (variable length).
 *   Ed25519          — deterministic EdDSA over Curve25519 (RFC 8032).
 *                      EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL)
 *                      One-shot sign — no digest initialisation needed.
 *
 * Methodology:
 *   10,000 iterations each of keygen, sign, verify (timed separately).
 *   Message: 256 bytes of 0x42 (same as qMEMO PQC benchmarks).
 *   100-iteration warm-up before each timed block.
 *
 * Metrics: keygen time, sign time, verify time, average sig size,
 *          throughput (ops/sec) for each phase.
 *
 * Compile:
 *   make classical
 *
 * Run:
 *   ./benchmarks/bin/classical_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp — must be first */

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN      256
#define MSG_FILL     0x42
#define WARMUP_ITERS 100
#define BENCH_ITERS  10000

/* ── OpenSSL error printer ────────────────────────────────────────────────── */

static void print_ssl_error(const char *ctx)
{
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    fprintf(stderr, "ERROR [%s]: %s\n", ctx, buf);
}

/* ── ECDSA secp256k1 ─────────────────────────────────────────────────────── */

/*
 * Generate one ECDSA secp256k1 keypair using the EVP high-level API.
 * Returns a new EVP_PKEY on success, NULL on failure.
 * Caller must call EVP_PKEY_free().
 */
static EVP_PKEY *ecdsa_keygen(void)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!ctx) { print_ssl_error("EVP_PKEY_CTX_new_id"); return NULL; }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        print_ssl_error("EVP_PKEY_keygen_init");
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp256k1) <= 0) {
        print_ssl_error("set_ec_paramgen_curve_nid");
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        print_ssl_error("EVP_PKEY_keygen");
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/*
 * Sign with ECDSA (secp256k1, SHA-256 digest).
 * sig_out must be at least EVP_PKEY_size(pkey) bytes.
 * Returns actual signature length or 0 on error.
 */
static size_t ecdsa_sign(EVP_PKEY *pkey,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t *sig_out, size_t sig_max)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) { print_ssl_error("EVP_MD_CTX_new"); return 0; }

    if (EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        print_ssl_error("EVP_DigestSignInit");
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    size_t sig_len = sig_max;
    if (EVP_DigestSign(mctx, sig_out, &sig_len, msg, msg_len) <= 0) {
        print_ssl_error("EVP_DigestSign");
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    EVP_MD_CTX_free(mctx);
    return sig_len;
}

/*
 * Verify ECDSA signature.  Returns 1 on success, 0 on failure.
 */
static int ecdsa_verify(EVP_PKEY *pkey,
                        const uint8_t *msg, size_t msg_len,
                        const uint8_t *sig, size_t sig_len)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;

    if (EVP_DigestVerifyInit(mctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len);
    EVP_MD_CTX_free(mctx);
    return (rc == 1) ? 1 : 0;
}

/* ── Ed25519 ─────────────────────────────────────────────────────────────── */

static EVP_PKEY *ed25519_keygen(void)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!ctx) { print_ssl_error("ed25519 ctx"); return NULL; }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        print_ssl_error("ed25519 keygen_init");
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        print_ssl_error("ed25519 keygen");
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/*
 * Ed25519 is one-shot — no digest context needed, no EVP_DigestSignInit
 * with a digest argument (pass NULL).
 */
static size_t ed25519_sign(EVP_PKEY *pkey,
                           const uint8_t *msg, size_t msg_len,
                           uint8_t *sig_out, size_t sig_max)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;

    if (EVP_DigestSignInit(mctx, NULL, NULL, NULL, pkey) <= 0) {
        print_ssl_error("ed25519 DigestSignInit");
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    size_t sig_len = sig_max;
    if (EVP_DigestSign(mctx, sig_out, &sig_len, msg, msg_len) <= 0) {
        print_ssl_error("ed25519 DigestSign");
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    EVP_MD_CTX_free(mctx);
    return sig_len;
}

static int ed25519_verify(EVP_PKEY *pkey,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig, size_t sig_len)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;

    if (EVP_DigestVerifyInit(mctx, NULL, NULL, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx);
        return 0;
    }
    int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len);
    EVP_MD_CTX_free(mctx);
    return (rc == 1) ? 1 : 0;
}

/* ── Generic benchmark runner ────────────────────────────────────────────── */

typedef struct {
    const char *name;
    int         is_ecdsa;   /* 1 = ECDSA secp256k1, 0 = Ed25519 */
} scheme_t;

static const scheme_t SCHEMES[] = {
    { "ECDSA secp256k1", 1 },
    { "Ed25519",         0 },
};
enum { NUM_SCHEMES = (int)(sizeof(SCHEMES) / sizeof(SCHEMES[0])) };

typedef struct {
    double keygen_ops;
    double sign_ops;
    double verify_ops;
    double avg_sig_bytes;
} result_t;

static int run_scheme(const scheme_t *s, result_t *out)
{
    uint8_t  msg[MSG_LEN];
    memset(msg, MSG_FILL, MSG_LEN);

    /* Maximum DER-encoded ECDSA signature: 72 bytes; Ed25519: always 64. */
    uint8_t  sig_buf[128];
    size_t   sig_len = 0;

    EVP_PKEY *pkey = NULL;
    double    t0, t1;

    /* ── keygen warm-up ─────────────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        pkey = s->is_ecdsa ? ecdsa_keygen() : ed25519_keygen();
        if (!pkey) { fprintf(stderr, "ERROR: keygen warmup failed\n"); return -1; }
        EVP_PKEY_free(pkey);
    }

    /* ── keygen timed ───────────────────────────────────────────────────── */
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        pkey = s->is_ecdsa ? ecdsa_keygen() : ed25519_keygen();
        if (!pkey) { fprintf(stderr, "ERROR: keygen failed at iter %d\n", i); return -1; }
        EVP_PKEY_free(pkey);
    }
    t1 = get_time();
    out->keygen_ops = (double)BENCH_ITERS / (t1 - t0);

    /* Generate one keypair to use for sign/verify phases */
    pkey = s->is_ecdsa ? ecdsa_keygen() : ed25519_keygen();
    if (!pkey) { fprintf(stderr, "ERROR: keygen for sign phase failed\n"); return -1; }

    /* ── sign warm-up ───────────────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        sig_len = s->is_ecdsa
            ? ecdsa_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf))
            : ed25519_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
        if (!sig_len) { EVP_PKEY_free(pkey); return -1; }
    }

    /* ── sign timed ─────────────────────────────────────────────────────── */
    double sig_total = 0.0;
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        sig_len = s->is_ecdsa
            ? ecdsa_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf))
            : ed25519_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
        if (!sig_len) { EVP_PKEY_free(pkey); return -1; }
        sig_total += (double)sig_len;
    }
    t1 = get_time();
    out->sign_ops      = (double)BENCH_ITERS / (t1 - t0);
    out->avg_sig_bytes = sig_total / (double)BENCH_ITERS;

    /* Produce one final signature for the verify phase */
    sig_len = s->is_ecdsa
        ? ecdsa_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf))
        : ed25519_sign(pkey, msg, MSG_LEN, sig_buf, sizeof(sig_buf));
    if (!sig_len) { EVP_PKEY_free(pkey); return -1; }

    /* ── verify warm-up ─────────────────────────────────────────────────── */
    for (int i = 0; i < WARMUP_ITERS; i++) {
        int ok = s->is_ecdsa
            ? ecdsa_verify(pkey, msg, MSG_LEN, sig_buf, sig_len)
            : ed25519_verify(pkey, msg, MSG_LEN, sig_buf, sig_len);
        if (!ok) { EVP_PKEY_free(pkey); return -1; }
    }

    /* ── verify timed ───────────────────────────────────────────────────── */
    t0 = get_time();
    for (int i = 0; i < BENCH_ITERS; i++) {
        int ok = s->is_ecdsa
            ? ecdsa_verify(pkey, msg, MSG_LEN, sig_buf, sig_len)
            : ed25519_verify(pkey, msg, MSG_LEN, sig_buf, sig_len);
        if (!ok) { EVP_PKEY_free(pkey); return -1; }
    }
    t1 = get_time();
    out->verify_ops = (double)BENCH_ITERS / (t1 - t0);

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
    printf("  Classical Signature Baselines  (qMEMO / IIT Chicago)\n");
    printf("  OpenSSL 3.x — EVP_PKEY high-level API\n");
    printf("================================================================\n");
    printf("  Iterations: %d (+ %d warm-up) per phase\n\n",
           BENCH_ITERS, WARMUP_ITERS);

    result_t results[NUM_SCHEMES];
    int      ok[NUM_SCHEMES];

    for (int i = 0; i < NUM_SCHEMES; i++) {
        printf("Benchmarking %-16s …", SCHEMES[i].name);
        fflush(stdout);
        ok[i] = run_scheme(&SCHEMES[i], &results[i]);
        if (ok[i] == 0)
            printf(" done.\n");
        else
            printf(" FAILED.\n");
    }

    /* ── Human-readable table ───────────────────────────────────────────── */
    printf("\n");
    printf("Scheme            Keygen (ops/s)   Sign (ops/s)   Verify (ops/s)   Avg Sig (bytes)\n");
    printf("----------------  --------------   ------------   --------------   ---------------\n");

    for (int i = 0; i < NUM_SCHEMES; i++) {
        if (ok[i] != 0) {
            printf("%-16s  (failed)\n", SCHEMES[i].name);
            continue;
        }
        result_t *r = &results[i];
        printf("%-16s  %14.0f   %12.0f   %14.0f   %15.1f\n",
               SCHEMES[i].name,
               r->keygen_ops, r->sign_ops, r->verify_ops, r->avg_sig_bytes);
    }

    /* ── JSON output ────────────────────────────────────────────────────── */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"classical_signature_baselines\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"bench_iters\": %d,\n", BENCH_ITERS);
    printf("  \"message_len\": %d,\n", MSG_LEN);
    printf("  \"schemes\": [\n");

    for (int i = 0; i < NUM_SCHEMES; i++) {
        result_t *r = &results[i];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", SCHEMES[i].name);
        if (ok[i] == 0) {
            printf("      \"keygen_ops_per_sec\": %.0f,\n",  r->keygen_ops);
            printf("      \"sign_ops_per_sec\": %.0f,\n",    r->sign_ops);
            printf("      \"verify_ops_per_sec\": %.0f,\n",  r->verify_ops);
            printf("      \"avg_sig_bytes\": %.1f\n",        r->avg_sig_bytes);
        } else {
            printf("      \"error\": true\n");
        }
        printf("    }%s\n", (i < NUM_SCHEMES - 1) ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");

    printf("\nClassical baseline benchmark complete.\n");
    return EXIT_SUCCESS;
}
