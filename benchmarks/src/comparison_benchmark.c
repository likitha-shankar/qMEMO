/*
 * comparison_benchmark.c -- Falcon-512 vs ML-DSA-44 (Dilithium2) Comparison
 *
 * Part of the qMEMO project (IIT Chicago): benchmarking post-quantum
 * digital signatures for blockchain transaction verification.
 *
 * Purpose
 * ───────
 * Produce head-to-head numbers that justify the algorithm choice in the
 * research paper.  Both Falcon-512 and ML-DSA-44 target NIST Security
 * Level 1 (roughly equivalent to AES-128), making them a fair pair for
 * comparison.  The key trade-off in blockchain context is:
 *
 *   Falcon-512  → smaller signatures, faster verification, but more
 *                 expensive key generation and signing (uses NTRU lattices
 *                 with fast-Fourier sampling).
 *
 *   ML-DSA-44   → larger signatures and public keys, but simpler
 *                 implementation with constant-time signing and no
 *                 floating-point dependency (module lattice + Fiat-Shamir).
 *
 * For blockchain, *verification* dominates: every full node verifies every
 * transaction in every block.  Signing happens once per transaction at the
 * wallet.  Key generation happens once per address.  So:
 *
 *   Metric that matters most:  verification throughput (ops/sec)
 *   Metric that matters next:  total per-transaction overhead
 *                              = signature_bytes + pubkey_bytes
 *                              (both travel on-chain in UTXO-style systems)
 *
 * Methodology
 * ───────────
 *   - Key generation:  100 trials  (expensive, especially for Falcon)
 *   - Signing:        1,000 trials
 *   - Verification:  10,000 trials
 *   - Each operation is timed individually with CLOCK_MONOTONIC
 *   - A warm-up phase precedes each operation type
 *   - Results reported as ops/sec and µs/op
 *
 * Compile (from project root, after install_liboqs.sh):
 *
 *   cc -O3 -I./liboqs_install/include -L./liboqs_install/lib \
 *      benchmarks/src/comparison_benchmark.c -loqs \
 *      -o benchmarks/bin/comparison_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp -- must be first */

#include <oqs/oqs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define KEYGEN_TRIALS    100
#define SIGN_TRIALS      1000
#define VERIFY_TRIALS    10000
#define WARMUP_FRAC      10      /* warm-up = trials / WARMUP_FRAC */

#define MSG_LEN          256
#define MSG_FILL_BYTE    0x42

#define NUM_ALGORITHMS   2

/* ── Per-algorithm result storage ─────────────────────────────────────────── */

typedef struct {
    const char *alg_id;           /* OQS identifier string           */
    const char *display_name;     /* human-readable label for output  */

    size_t pubkey_bytes;
    size_t privkey_bytes;
    size_t sig_max_bytes;
    size_t sig_actual_bytes;      /* from one real signing            */

    double keygen_ops_sec;
    double keygen_us_op;

    double sign_ops_sec;
    double sign_us_op;

    double verify_ops_sec;
    double verify_us_op;

    size_t total_tx_overhead;     /* sig_actual + pubkey (on-chain)   */
} AlgResult;

/* ── Benchmark one algorithm ──────────────────────────────────────────────
 *
 * Runs all three operation types (keygen, sign, verify) for a single
 * algorithm and fills in the AlgResult struct.  Returns 0 on success.
 */
static int benchmark_algorithm(AlgResult *r)
{
    OQS_SIG *sig = OQS_SIG_new(r->alg_id);
    if (!sig) {
        fprintf(stderr, "ERROR: %s not available in this liboqs build.\n",
                r->alg_id);
        return -1;
    }

    r->pubkey_bytes   = sig->length_public_key;
    r->privkey_bytes  = sig->length_secret_key;
    r->sig_max_bytes  = sig->length_signature;

    uint8_t *pk  = malloc(sig->length_public_key);
    uint8_t *sk  = malloc(sig->length_secret_key);
    uint8_t *s   = malloc(sig->length_signature);
    uint8_t *msg = malloc(MSG_LEN);

    if (!pk || !sk || !s || !msg) {
        fprintf(stderr, "ERROR: malloc failed for %s.\n", r->display_name);
        free(pk); free(sk); free(s); free(msg);
        OQS_SIG_free(sig);
        return -1;
    }

    memset(msg, MSG_FILL_BYTE, MSG_LEN);

    volatile OQS_STATUS vrc;
    double t0, t1, total;
    size_t sig_len = 0;

    /* ── Key generation ───────────────────────────────────────────────────
     *
     * Falcon keygen is noticeably slower than ML-DSA because it must
     * sample an NTRU lattice basis and compute its Gram-Schmidt
     * decomposition (the "tree" used for fast-Fourier signing).
     * ML-DSA keygen is a simple matrix-vector multiply.
     */
    printf("  [keygen] warm-up ...");
    fflush(stdout);
    int warmup = KEYGEN_TRIALS / WARMUP_FRAC;
    if (warmup < 5) warmup = 5;
    for (int i = 0; i < warmup; i++) {
        vrc = OQS_SIG_keypair(sig, pk, sk);
    }
    printf(" benchmarking %d trials ...", KEYGEN_TRIALS);
    fflush(stdout);

    t0 = get_time();
    for (int i = 0; i < KEYGEN_TRIALS; i++) {
        vrc = OQS_SIG_keypair(sig, pk, sk);
    }
    t1 = get_time();
    total = t1 - t0;

    r->keygen_ops_sec = (double)KEYGEN_TRIALS / total;
    r->keygen_us_op   = (total / (double)KEYGEN_TRIALS) * 1e6;
    printf(" %.1f ops/sec\n", r->keygen_ops_sec);

    /* ── Signing ──────────────────────────────────────────────────────────
     *
     * Falcon signing uses discrete Gaussian sampling over the NTRU
     * lattice (rejection sampling on a tree), so it has higher variance
     * than ML-DSA's deterministic Fiat-Shamir-with-Aborts.
     */
    printf("  [sign]   warm-up ...");
    fflush(stdout);
    warmup = SIGN_TRIALS / WARMUP_FRAC;
    for (int i = 0; i < warmup; i++) {
        vrc = OQS_SIG_sign(sig, s, &sig_len, msg, MSG_LEN, sk);
    }
    printf(" benchmarking %d trials ...", SIGN_TRIALS);
    fflush(stdout);

    t0 = get_time();
    for (int i = 0; i < SIGN_TRIALS; i++) {
        vrc = OQS_SIG_sign(sig, s, &sig_len, msg, MSG_LEN, sk);
    }
    t1 = get_time();
    total = t1 - t0;

    r->sign_ops_sec = (double)SIGN_TRIALS / total;
    r->sign_us_op   = (total / (double)SIGN_TRIALS) * 1e6;
    r->sig_actual_bytes  = sig_len;
    r->total_tx_overhead = sig_len + sig->length_public_key;
    printf(" %.1f ops/sec\n", r->sign_ops_sec);

    /* ── Verification sanity check ────────────────────────────────────── */
    OQS_STATUS check = OQS_SIG_verify(sig, msg, MSG_LEN, s, sig_len, pk);
    if (check != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: %s sanity-check verification FAILED.\n",
                r->display_name);
        OQS_MEM_insecure_free(pk);
        OQS_MEM_secure_free(sk, sig->length_secret_key);
        OQS_MEM_insecure_free(s);
        OQS_MEM_insecure_free(msg);
        OQS_SIG_free(sig);
        return -1;
    }

    /* ── Verification ─────────────────────────────────────────────────────
     *
     * This is the metric that matters most for blockchain.  Every full
     * node must verify every signature in every block.  At 4,000 tx/block
     * and one block every ~10 s, a validator needs at least 400 verify/sec
     * sustained.  Both algorithms clear this easily, but the margin is
     * what determines block size headroom and hardware cost.
     */
    printf("  [verify] warm-up ...");
    fflush(stdout);
    warmup = VERIFY_TRIALS / WARMUP_FRAC;
    for (int i = 0; i < warmup; i++) {
        vrc = OQS_SIG_verify(sig, msg, MSG_LEN, s, sig_len, pk);
    }
    printf(" benchmarking %d trials ...", VERIFY_TRIALS);
    fflush(stdout);

    t0 = get_time();
    for (int i = 0; i < VERIFY_TRIALS; i++) {
        vrc = OQS_SIG_verify(sig, msg, MSG_LEN, s, sig_len, pk);
    }
    t1 = get_time();
    total = t1 - t0;

    r->verify_ops_sec = (double)VERIFY_TRIALS / total;
    r->verify_us_op   = (total / (double)VERIFY_TRIALS) * 1e6;
    printf(" %.1f ops/sec\n", r->verify_ops_sec);

    (void)vrc;

    OQS_MEM_insecure_free(pk);
    OQS_MEM_secure_free(sk, sig->length_secret_key);
    OQS_MEM_insecure_free(s);
    OQS_MEM_insecure_free(msg);
    OQS_SIG_free(sig);
    return 0;
}

/* ── Helpers for the comparison table ─────────────────────────────────────── */

static const char *faster_label(double a, double b)
{
    if (a > b * 1.05) return "◄ faster";
    if (b > a * 1.05) return "  faster ►";
    return "  ≈ tied";
}

static const char *smaller_label(size_t a, size_t b)
{
    if (a < b) return "◄ smaller";
    if (b < a) return "  smaller ►";
    return "  ≈ equal";
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 vs ML-DSA-44 Comparison  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    OQS_init();

    AlgResult results[NUM_ALGORITHMS] = {
        { .alg_id = OQS_SIG_alg_falcon_512, .display_name = "Falcon-512"  },
        { .alg_id = OQS_SIG_alg_ml_dsa_44,  .display_name = "ML-DSA-44"   },
    };

    for (int a = 0; a < NUM_ALGORITHMS; a++) {
        printf("─── %s ───\n", results[a].display_name);
        if (benchmark_algorithm(&results[a]) != 0) {
            fprintf(stderr, "FATAL: benchmarking %s failed.\n",
                    results[a].display_name);
            OQS_destroy();
            return EXIT_FAILURE;
        }
        printf("\n");
    }

    AlgResult *falcon  = &results[0];
    AlgResult *mldsa   = &results[1];

    /* ── Comparison ratios ────────────────────────────────────────────────
     *
     * Ratios > 1 favour Falcon; ratios < 1 favour ML-DSA.
     * For throughput metrics (ops/sec), higher is better.
     * For size metrics (bytes), lower is better.
     */
    double verify_speedup    = falcon->verify_ops_sec / mldsa->verify_ops_sec;
    double sign_speedup      = falcon->sign_ops_sec   / mldsa->sign_ops_sec;
    double keygen_speedup    = falcon->keygen_ops_sec  / mldsa->keygen_ops_sec;
    double sig_size_ratio    = (double)falcon->sig_actual_bytes
                             / (double)mldsa->sig_actual_bytes;
    double pubkey_ratio      = (double)falcon->pubkey_bytes
                             / (double)mldsa->pubkey_bytes;
    double tx_overhead_ratio = (double)falcon->total_tx_overhead
                             / (double)mldsa->total_tx_overhead;

    /* ── Human-readable comparison table ──────────────────────────────── */

    printf("================================================================\n");
    printf("  HEAD-TO-HEAD COMPARISON\n");
    printf("================================================================\n\n");

    printf("  %-22s  %14s  %14s  %s\n",
           "Metric", "Falcon-512", "ML-DSA-44", "Winner");
    printf("  %-22s  %14s  %14s  %s\n",
           "──────────────────────", "──────────────", "──────────────",
           "──────────");

    printf("  %-22s  %11.1f /s  %11.1f /s  %s\n",
           "Keygen throughput",
           falcon->keygen_ops_sec, mldsa->keygen_ops_sec,
           faster_label(falcon->keygen_ops_sec, mldsa->keygen_ops_sec));

    printf("  %-22s  %11.1f /s  %11.1f /s  %s\n",
           "Sign throughput",
           falcon->sign_ops_sec, mldsa->sign_ops_sec,
           faster_label(falcon->sign_ops_sec, mldsa->sign_ops_sec));

    printf("  %-22s  %11.1f /s  %11.1f /s  %s\n",
           "Verify throughput",
           falcon->verify_ops_sec, mldsa->verify_ops_sec,
           faster_label(falcon->verify_ops_sec, mldsa->verify_ops_sec));

    printf("\n");

    printf("  %-22s  %10.1f µs   %10.1f µs   %s\n",
           "Keygen latency",
           falcon->keygen_us_op, mldsa->keygen_us_op,
           faster_label(1.0/falcon->keygen_us_op, 1.0/mldsa->keygen_us_op));

    printf("  %-22s  %10.1f µs   %10.1f µs   %s\n",
           "Sign latency",
           falcon->sign_us_op, mldsa->sign_us_op,
           faster_label(1.0/falcon->sign_us_op, 1.0/mldsa->sign_us_op));

    printf("  %-22s  %10.1f µs   %10.1f µs   %s\n",
           "Verify latency",
           falcon->verify_us_op, mldsa->verify_us_op,
           faster_label(1.0/falcon->verify_us_op, 1.0/mldsa->verify_us_op));

    printf("\n");

    printf("  %-22s  %10zu B     %10zu B     %s\n",
           "Public key size",
           falcon->pubkey_bytes, mldsa->pubkey_bytes,
           smaller_label(falcon->pubkey_bytes, mldsa->pubkey_bytes));

    printf("  %-22s  %10zu B     %10zu B     %s\n",
           "Secret key size",
           falcon->privkey_bytes, mldsa->privkey_bytes,
           smaller_label(falcon->privkey_bytes, mldsa->privkey_bytes));

    printf("  %-22s  %10zu B     %10zu B     %s\n",
           "Signature size",
           falcon->sig_actual_bytes, mldsa->sig_actual_bytes,
           smaller_label(falcon->sig_actual_bytes, mldsa->sig_actual_bytes));

    printf("  %-22s  %10zu B     %10zu B     %s\n",
           "On-chain tx overhead",
           falcon->total_tx_overhead, mldsa->total_tx_overhead,
           smaller_label(falcon->total_tx_overhead, mldsa->total_tx_overhead));

    /* ── Blockchain analysis ──────────────────────────────────────────────
     *
     * Frame the numbers in terms a blockchain audience cares about:
     * block validation time and per-transaction storage cost.
     */
    printf("\n");
    printf("================================================================\n");
    printf("  BLOCKCHAIN IMPACT ANALYSIS\n");
    printf("================================================================\n\n");

    int block_tx = 4000;
    double falcon_block_ms = ((double)block_tx / falcon->verify_ops_sec) * 1e3;
    double mldsa_block_ms  = ((double)block_tx / mldsa->verify_ops_sec) * 1e3;

    printf("  Scenario: %d transactions per block (single-threaded verification)\n\n",
           block_tx);

    printf("  Falcon-512 block verify time : %8.1f ms\n", falcon_block_ms);
    printf("  ML-DSA-44  block verify time : %8.1f ms\n", mldsa_block_ms);
    printf("  Speedup (Falcon / ML-DSA)    : %8.2fx\n\n", verify_speedup);

    long falcon_block_bytes = (long)block_tx * (long)falcon->total_tx_overhead;
    long mldsa_block_bytes  = (long)block_tx * (long)mldsa->total_tx_overhead;

    printf("  Falcon-512 block sig data    : %8.1f KB  (%zu B/tx)\n",
           falcon_block_bytes / 1024.0, falcon->total_tx_overhead);
    printf("  ML-DSA-44  block sig data    : %8.1f KB  (%zu B/tx)\n",
           mldsa_block_bytes / 1024.0, mldsa->total_tx_overhead);
    printf("  Size ratio (Falcon / ML-DSA) : %8.2fx\n\n",
           tx_overhead_ratio);

    int falcon_wins = 0;
    if (falcon->verify_ops_sec > mldsa->verify_ops_sec) falcon_wins++;
    if (falcon->total_tx_overhead < mldsa->total_tx_overhead) falcon_wins++;
    if (falcon->sign_ops_sec > mldsa->sign_ops_sec) falcon_wins++;

    if (falcon_wins >= 2) {
        printf("  ► Recommendation: Falcon-512\n");
        printf("    Faster verification AND smaller on-chain footprint make it\n");
        printf("    the stronger choice for blockchain transaction signing.\n");
        printf("    The slower keygen is irrelevant -- addresses are generated\n");
        printf("    once, while signatures are verified millions of times.\n");
    } else {
        printf("  ► Recommendation: ML-DSA-44\n");
        printf("    Simpler constant-time implementation and faster signing\n");
        printf("    may outweigh the larger signature size depending on the\n");
        printf("    target blockchain's block size limits.\n");
    }

    /* ── JSON output ──────────────────────────────────────────────────────
     *
     * Structured for automated ingestion by analysis scripts.
     * The "comparison" sub-object gives pre-computed ratios so
     * downstream tools don't need to re-derive them.
     */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_vs_mldsa44\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"config\": {\n");
    printf("    \"keygen_trials\": %d,\n", KEYGEN_TRIALS);
    printf("    \"sign_trials\": %d,\n", SIGN_TRIALS);
    printf("    \"verify_trials\": %d,\n", VERIFY_TRIALS);
    printf("    \"message_len\": %d\n", MSG_LEN);
    printf("  },\n");

    printf("  \"algorithms\": {\n");
    for (int a = 0; a < NUM_ALGORITHMS; a++) {
        AlgResult *r = &results[a];
        printf("    \"%s\": {\n", r->display_name);
        printf("      \"keygen_ops_sec\": %.2f,\n", r->keygen_ops_sec);
        printf("      \"keygen_us_op\": %.2f,\n", r->keygen_us_op);
        printf("      \"sign_ops_sec\": %.2f,\n", r->sign_ops_sec);
        printf("      \"sign_us_op\": %.2f,\n", r->sign_us_op);
        printf("      \"verify_ops_sec\": %.2f,\n", r->verify_ops_sec);
        printf("      \"verify_us_op\": %.2f,\n", r->verify_us_op);
        printf("      \"pubkey_bytes\": %zu,\n", r->pubkey_bytes);
        printf("      \"privkey_bytes\": %zu,\n", r->privkey_bytes);
        printf("      \"signature_bytes\": %zu,\n", r->sig_actual_bytes);
        printf("      \"total_tx_overhead\": %zu\n", r->total_tx_overhead);
        printf("    }%s\n", (a < NUM_ALGORITHMS - 1) ? "," : "");
    }
    printf("  },\n");

    printf("  \"comparison\": {\n");
    printf("    \"verify_speedup_falcon\": %.4f,\n", verify_speedup);
    printf("    \"sign_speedup_falcon\": %.4f,\n", sign_speedup);
    printf("    \"keygen_speedup_falcon\": %.4f,\n", keygen_speedup);
    printf("    \"signature_size_ratio\": %.4f,\n", sig_size_ratio);
    printf("    \"pubkey_size_ratio\": %.4f,\n", pubkey_ratio);
    printf("    \"total_tx_overhead_falcon\": %zu,\n", falcon->total_tx_overhead);
    printf("    \"total_tx_overhead_dilithium\": %zu,\n", mldsa->total_tx_overhead);
    printf("    \"tx_overhead_ratio\": %.4f\n", tx_overhead_ratio);
    printf("  }\n");
    printf("}\n");

    OQS_destroy();

    printf("\nComparison benchmark complete.\n");
    return EXIT_SUCCESS;
}
