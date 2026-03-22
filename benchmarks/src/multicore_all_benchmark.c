/*
 * multicore_all_benchmark.c -- All 7 Algorithms Multicore Verification Scaling
 *
 * Part of the qMEMO project (IIT Chicago): measures how verification
 * throughput scales from 1 to 10 threads for every algorithm in the suite.
 *
 * Algorithms (in order):
 *   1. Falcon-512           (liboqs)  -- NIST Level 1, lattice
 *   2. Falcon-1024          (liboqs)  -- NIST Level 5, lattice
 *   3. ML-DSA-44            (liboqs)  -- NIST Level 2, module lattice
 *   4. ML-DSA-65            (liboqs)  -- NIST Level 3, module lattice
 *   5. SLH-DSA (SHA2-128f)  (liboqs)  -- NIST Level 1, hash-based
 *   6. ECDSA secp256k1      (OpenSSL) -- classical, Bitcoin/Ethereum
 *   7. Ed25519              (OpenSSL) -- classical, EdDSA
 *
 * Methodology (same as multicore_benchmark.c):
 *   - One keypair and signature per algorithm, generated in main thread.
 *   - Per-thread warm-up before timed section.
 *   - Barrier synchronization so all threads start simultaneously.
 *   - Each thread performs VERIF_PER_THREAD verifications (reduced for SLH-DSA).
 *   - Total throughput = (N x iters_per_thread) / wall_time.
 *
 * Compile:
 *   make multicore_all
 *
 * Run:
 *   ./benchmarks/bin/multicore_all_benchmark
 */

#include "bench_common.h"

#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN            256
#define MSG_FILL           0x42
#define WARMUP_PER_THREAD  100
#define VERIF_PER_THREAD   1000
#define VERIF_SLOW         100    /* SLH-DSA: ~732 ops/sec single-thread */
#define WARMUP_SLOW        10

enum { NUM_THREAD_CONFIGS = 6 };
static const int THREAD_COUNTS[NUM_THREAD_CONFIGS] = { 1, 2, 4, 6, 8, 10 };

/* ── Algorithm type enum ──────────────────────────────────────────────────── */

typedef enum { ALG_OQS, ALG_OPENSSL_EC, ALG_OPENSSL_ED25519 } alg_type_t;

typedef struct {
    const char *name;
    alg_type_t  type;
    /* OQS fields */
    const char *oqs_alg_id;
    int         nist_level;
    /* OpenSSL fields */
    int         pkey_type;
    int         curve_nid;
} alg_config_t;

static const alg_config_t ALGORITHMS[] = {
    { "Falcon-512",        ALG_OQS,            OQS_SIG_alg_falcon_512,            1, 0, 0 },
    { "Falcon-1024",       ALG_OQS,            OQS_SIG_alg_falcon_1024,           5, 0, 0 },
    { "ML-DSA-44",         ALG_OQS,            OQS_SIG_alg_ml_dsa_44,             2, 0, 0 },
    { "ML-DSA-65",         ALG_OQS,            OQS_SIG_alg_ml_dsa_65,             3, 0, 0 },
    { "SLH-DSA-SHA2-128f", ALG_OQS,            OQS_SIG_alg_slh_dsa_pure_sha2_128f, 1, 0, 0 },
    { "ECDSA secp256k1",   ALG_OPENSSL_EC,     NULL,                              0, EVP_PKEY_EC, NID_secp256k1 },
    { "Ed25519",           ALG_OPENSSL_ED25519, NULL,                              0, EVP_PKEY_ED25519, 0 },
};
enum { NUM_ALGORITHMS = 7 };

/* ── Per-thread context ───────────────────────────────────────────────────── */

typedef struct {
    int         id;
    barrier_t  *barrier;
    int         warmup_iters;
    int         timed_iters;
    /* OQS verify data (NULL if OpenSSL) */
    OQS_SIG    *oqs_sig;
    uint8_t    *oqs_pubkey;
    uint8_t    *oqs_message;
    uint8_t    *oqs_signature;
    size_t      oqs_sig_len;
    /* OpenSSL verify data (NULL if OQS) */
    EVP_PKEY   *evp_pkey;
    const EVP_MD *evp_md;
    uint8_t    *evp_message;
    uint8_t    *evp_signature;
    size_t      evp_sig_len;
    /* Result */
    int         ok;
} thread_arg_t;

/* ── Thread function ──────────────────────────────────────────────────────── */

static void *verify_thread(void *arg)
{
    thread_arg_t *a = (thread_arg_t *)arg;
    a->ok = 1;

    if (a->oqs_sig) {
        /* OQS verify path */
        for (int i = 0; i < a->warmup_iters; i++) {
            if (OQS_SIG_verify(a->oqs_sig, a->oqs_message, MSG_LEN,
                               a->oqs_signature, a->oqs_sig_len,
                               a->oqs_pubkey) != OQS_SUCCESS) {
                a->ok = 0;
                return NULL;
            }
        }
        barrier_wait(a->barrier);
        for (int i = 0; i < a->timed_iters; i++) {
            if (OQS_SIG_verify(a->oqs_sig, a->oqs_message, MSG_LEN,
                               a->oqs_signature, a->oqs_sig_len,
                               a->oqs_pubkey) != OQS_SUCCESS) {
                a->ok = 0;
                return NULL;
            }
        }
    } else {
        /* OpenSSL verify path */
        for (int i = 0; i < a->warmup_iters; i++) {
            EVP_MD_CTX *mctx = EVP_MD_CTX_new();
            if (!mctx) { a->ok = 0; return NULL; }
            if (EVP_DigestVerifyInit(mctx, NULL, a->evp_md, NULL, a->evp_pkey) <= 0) {
                EVP_MD_CTX_free(mctx); a->ok = 0; return NULL;
            }
            int rc = EVP_DigestVerify(mctx, a->evp_signature, a->evp_sig_len,
                                      a->evp_message, MSG_LEN);
            EVP_MD_CTX_free(mctx);
            if (rc != 1) { a->ok = 0; return NULL; }
        }
        barrier_wait(a->barrier);
        for (int i = 0; i < a->timed_iters; i++) {
            EVP_MD_CTX *mctx = EVP_MD_CTX_new();
            if (!mctx) { a->ok = 0; return NULL; }
            if (EVP_DigestVerifyInit(mctx, NULL, a->evp_md, NULL, a->evp_pkey) <= 0) {
                EVP_MD_CTX_free(mctx); a->ok = 0; return NULL;
            }
            int rc = EVP_DigestVerify(mctx, a->evp_signature, a->evp_sig_len,
                                      a->evp_message, MSG_LEN);
            EVP_MD_CTX_free(mctx);
            if (rc != 1) { a->ok = 0; return NULL; }
        }
    }
    return NULL;
}

/* ── OpenSSL helpers (same as comprehensive_comparison.c) ─────────────────── */

static EVP_PKEY *openssl_keygen(int pkey_type, int curve_nid)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(pkey_type, NULL);
    if (!ctx) return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return NULL; }
    if (curve_nid != 0 &&
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curve_nid) <= 0) {
        EVP_PKEY_CTX_free(ctx); return NULL;
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

/* ── Per-algorithm scaling result ─────────────────────────────────────────── */

typedef struct {
    const char *name;
    int         iters_per_thread;
    int         ops_per_sec[NUM_THREAD_CONFIGS];
    double      speedup[NUM_THREAD_CONFIGS];
    double      efficiency[NUM_THREAD_CONFIGS];
    int         ok;
} scaling_result_t;

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Multicore Verification Scaling — All 7 Algorithms\n");
    printf("  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n");
    printf("  Thread configs: 1, 2, 4, 6, 8, 10\n");
    printf("  Message: %d bytes 0x%02X\n\n", MSG_LEN, MSG_FILL);

    OQS_init();

    uint8_t message[MSG_LEN];
    memset(message, MSG_FILL, MSG_LEN);

    scaling_result_t results[NUM_ALGORITHMS];
    memset(results, 0, sizeof(results));

    for (int alg = 0; alg < NUM_ALGORITHMS; alg++) {
        const alg_config_t *cfg = &ALGORITHMS[alg];
        results[alg].name = cfg->name;

        int is_slow = (alg == 4);  /* SLH-DSA */
        int verif_per_thread = is_slow ? VERIF_SLOW : VERIF_PER_THREAD;
        int warmup = is_slow ? WARMUP_SLOW : WARMUP_PER_THREAD;
        results[alg].iters_per_thread = verif_per_thread;

        printf("  [%d/%d] %s ", alg + 1, NUM_ALGORITHMS, cfg->name);
        fflush(stdout);

        /* ── Set up key material ──────────────────────────────────────── */

        /* OQS data */
        OQS_SIG  *oqs_sig    = NULL;
        uint8_t  *oqs_pubkey = NULL;
        uint8_t  *oqs_seckey = NULL;
        uint8_t  *oqs_sigbuf = NULL;
        size_t    oqs_siglen = 0;

        /* OpenSSL data */
        EVP_PKEY *evp_pkey   = NULL;
        uint8_t   evp_sigbuf[128];
        size_t    evp_siglen = 0;
        const EVP_MD *evp_md = NULL;

        if (cfg->type == ALG_OQS) {
            oqs_sig = OQS_SIG_new(cfg->oqs_alg_id);
            if (!oqs_sig) {
                printf("FAILED (cannot instantiate)\n");
                results[alg].ok = -1;
                continue;
            }
            oqs_pubkey = malloc(oqs_sig->length_public_key);
            oqs_seckey = malloc(oqs_sig->length_secret_key);
            oqs_sigbuf = malloc(oqs_sig->length_signature);
            if (!oqs_pubkey || !oqs_seckey || !oqs_sigbuf) {
                printf("FAILED (malloc)\n");
                results[alg].ok = -1;
                goto cleanup_alg;
            }
            if (OQS_SIG_keypair(oqs_sig, oqs_pubkey, oqs_seckey) != OQS_SUCCESS) {
                printf("FAILED (keygen)\n");
                results[alg].ok = -1;
                goto cleanup_alg;
            }
            oqs_siglen = 0;
            if (OQS_SIG_sign(oqs_sig, oqs_sigbuf, &oqs_siglen,
                             message, MSG_LEN, oqs_seckey) != OQS_SUCCESS) {
                printf("FAILED (sign)\n");
                results[alg].ok = -1;
                goto cleanup_alg;
            }
        } else {
            evp_pkey = openssl_keygen(cfg->pkey_type, cfg->curve_nid);
            if (!evp_pkey) {
                printf("FAILED (keygen)\n");
                results[alg].ok = -1;
                continue;
            }
            if (cfg->type == ALG_OPENSSL_EC)
                evp_md = EVP_sha256();
            else
                evp_md = NULL;  /* Ed25519 uses NULL md */

            evp_siglen = openssl_sign(evp_pkey, evp_md, message, MSG_LEN,
                                      evp_sigbuf, sizeof(evp_sigbuf));
            if (!evp_siglen) {
                printf("FAILED (sign)\n");
                EVP_PKEY_free(evp_pkey);
                results[alg].ok = -1;
                continue;
            }
        }

        /* ── Run thread scaling ───────────────────────────────────────── */

        for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++) {
            int nthreads = THREAD_COUNTS[tc];

            barrier_t barrier;
            barrier_init(&barrier, nthreads + 1);  /* +1 for main thread */

            pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)nthreads);
            thread_arg_t *args = calloc((size_t)nthreads, sizeof(thread_arg_t));

            for (int t = 0; t < nthreads; t++) {
                args[t].id           = t;
                args[t].barrier      = &barrier;
                args[t].warmup_iters = warmup;
                args[t].timed_iters  = verif_per_thread;
                args[t].ok           = 1;

                if (cfg->type == ALG_OQS) {
                    args[t].oqs_sig       = oqs_sig;
                    args[t].oqs_pubkey    = oqs_pubkey;
                    args[t].oqs_message   = message;
                    args[t].oqs_signature = oqs_sigbuf;
                    args[t].oqs_sig_len   = oqs_siglen;
                } else {
                    args[t].evp_pkey      = evp_pkey;
                    args[t].evp_md        = evp_md;
                    args[t].evp_message   = message;
                    args[t].evp_signature = evp_sigbuf;
                    args[t].evp_sig_len   = evp_siglen;
                }
            }

            for (int t = 0; t < nthreads; t++)
                pthread_create(&threads[t], NULL, verify_thread, &args[t]);

            /* Main thread waits at barrier so all threads start simultaneously */
            barrier_wait(&barrier);
            double t_start = get_time();

            for (int t = 0; t < nthreads; t++)
                pthread_join(threads[t], NULL);

            double t_end = get_time();
            double elapsed = t_end - t_start;

            /* Check all threads succeeded */
            int all_ok = 1;
            for (int t = 0; t < nthreads; t++) {
                if (!args[t].ok) { all_ok = 0; break; }
            }

            if (all_ok && elapsed > 0.0) {
                int total_ops = nthreads * verif_per_thread;
                int ops_sec = (int)((double)total_ops / elapsed);
                results[alg].ops_per_sec[tc] = ops_sec;
            } else {
                results[alg].ok = -1;
            }

            barrier_destroy(&barrier);
            free(threads);
            free(args);
        }

        /* Compute speedup and efficiency */
        if (results[alg].ok == 0 && results[alg].ops_per_sec[0] > 0) {
            double base = (double)results[alg].ops_per_sec[0];
            for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++) {
                results[alg].speedup[tc] = (double)results[alg].ops_per_sec[tc] / base;
                results[alg].efficiency[tc] = results[alg].speedup[tc] /
                                              (double)THREAD_COUNTS[tc] * 100.0;
            }
        }

        printf("done.\n");

cleanup_alg:
        if (oqs_seckey) OQS_MEM_secure_free(oqs_seckey, oqs_sig ? oqs_sig->length_secret_key : 0);
        if (oqs_pubkey) OQS_MEM_insecure_free(oqs_pubkey);
        if (oqs_sigbuf) OQS_MEM_insecure_free(oqs_sigbuf);
        if (oqs_sig)    OQS_SIG_free(oqs_sig);
        if (evp_pkey)   EVP_PKEY_free(evp_pkey);
    }

    OQS_destroy();

    /* ── Human-readable tables ────────────────────────────────────────────── */

    printf("\n");
    printf("================================================================\n");
    printf("  Results — Multicore Verification Scaling\n");
    printf("================================================================\n\n");

    for (int alg = 0; alg < NUM_ALGORITHMS; alg++) {
        scaling_result_t *r = &results[alg];
        if (r->ok != 0) {
            printf("%-20s  FAILED\n\n", r->name);
            continue;
        }

        printf("%-20s  (%d iters/thread)\n", r->name, r->iters_per_thread);
        printf("Threads |  Throughput (ops/sec)  |  Speedup  |  Efficiency\n");
        printf("--------|------------------------|-----------|------------\n");
        for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++) {
            printf("  %2d   |          %10d     |   %5.2f   |  %5.1f%%\n",
                   THREAD_COUNTS[tc],
                   r->ops_per_sec[tc],
                   r->speedup[tc],
                   r->efficiency[tc]);
        }
        printf("\n");
    }

    /* ── Summary table ────────────────────────────────────────────────────── */

    printf("================================================================\n");
    printf("  Summary — 10-Thread Scaling\n");
    printf("================================================================\n\n");
    printf("Algorithm            |  1-Thread   | 10-Thread   | Speedup | Efficiency\n");
    printf("---------------------|-------------|-------------|---------|----------\n");
    for (int alg = 0; alg < NUM_ALGORITHMS; alg++) {
        scaling_result_t *r = &results[alg];
        if (r->ok != 0) {
            printf("%-20s |    FAILED   |    FAILED   |    -    |     -\n", r->name);
            continue;
        }
        printf("%-20s | %9d   | %9d   |  %5.2fx |  %5.1f%%\n",
               r->name,
               r->ops_per_sec[0],
               r->ops_per_sec[NUM_THREAD_CONFIGS - 1],
               r->speedup[NUM_THREAD_CONFIGS - 1],
               r->efficiency[NUM_THREAD_CONFIGS - 1]);
    }

    /* ── JSON output ──────────────────────────────────────────────────────── */

    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"multicore_all_verify\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"message_len\": %d,\n", MSG_LEN);
    printf("  \"threads\": [1, 2, 4, 6, 8, 10],\n");
    printf("  \"algorithms\": [\n");

    for (int alg = 0; alg < NUM_ALGORITHMS; alg++) {
        scaling_result_t *r = &results[alg];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", r->name);
        printf("      \"iters_per_thread\": %d,\n", r->iters_per_thread);
        if (r->ok == 0) {
            printf("      \"ops_per_sec\": [");
            for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++)
                printf("%d%s", r->ops_per_sec[tc], tc < 5 ? ", " : "");
            printf("],\n");
            printf("      \"speedup\": [");
            for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++)
                printf("%.2f%s", r->speedup[tc], tc < 5 ? ", " : "");
            printf("],\n");
            printf("      \"efficiency_pct\": [");
            for (int tc = 0; tc < NUM_THREAD_CONFIGS; tc++)
                printf("%.1f%s", r->efficiency[tc], tc < 5 ? ", " : "");
            printf("]\n");
        } else {
            printf("      \"error\": true\n");
        }
        printf("    }%s\n", (alg < NUM_ALGORITHMS - 1) ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");

    printf("\nMulticore all-algorithm benchmark complete.\n");
    return EXIT_SUCCESS;
}
