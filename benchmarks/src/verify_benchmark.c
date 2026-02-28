/*
 * verify_benchmark.c -- Falcon-512 Signature Verification Benchmark
 *
 * Part of the qMEMO project: benchmarking post-quantum digital signatures
 * for blockchain transaction verification.
 *
 * Research context (IIT Chicago):
 *   Blockchain nodes spend most of their signature-related CPU time on
 *   *verification*, not signing.  Every full node must verify every
 *   transaction in every block.  This benchmark isolates that hot path
 *   to measure the per-verification cost of Falcon-512 under controlled
 *   conditions.
 *
 * Methodology:
 *   1. Generate one Falcon-512 keypair and sign a fixed 256-byte message.
 *   2. Warm up the CPU pipeline with 100 untimed verifications.
 *   3. Time 10,000 consecutive verifications with nanosecond-precision
 *      monotonic clocks.
 *   4. Report ops/sec, latency, and estimated cycle cost.
 *
 * The 256-byte payload models a blockchain transaction body (roughly the
 * size of a two-input, two-output Bitcoin transaction without witness data).
 * Using a fixed message eliminates RNG overhead and payload-dependent
 * branching from the timed section, isolating pure verification cost.
 *
 * Compile (after running install_liboqs.sh from the project root):
 *
 *   cc -O3 -I../../liboqs_install/include -L../../liboqs_install/lib \
 *      verify_benchmark.c -loqs -o ../bin/verify_benchmark
 *
 * Run:
 *   ../bin/verify_benchmark
 *   (rpath is embedded at build time -- no LD_LIBRARY_PATH needed)
 *
 * For true cycle counts, use `perf stat` (Linux) or Instruments (macOS)
 * rather than the wall-clock estimate below.
 */

#include "bench_common.h"   /* get_time, get_timestamp -- must be first */

#include <oqs/oqs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define WARMUP_ITERATIONS   100
#define BENCH_ITERATIONS    10000
#define MSG_LEN             256
#define MSG_FILL_BYTE       0x42

/*
 * Approximate clock frequency for cycle-cost estimation.
 * Apple M2 Pro performance cores boost to ~3.49 GHz.  This is NOT a
 * substitute for hardware cycle counters -- it's a convenience for
 * quick back-of-envelope comparisons.  Publication-quality cycle counts
 * should come from perf or PMU reads.
 */
#define ASSUMED_GHZ  3.5

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    OQS_STATUS rc;
    char timestamp[64];

    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Verification Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    /* ── Initialise liboqs ────────────────────────────────────────────────
     *
     * Must be called before any other OQS function.  On failure the library
     * is in an undefined state so there is no point continuing.
     */
    OQS_init();

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: Falcon-512 is not enabled in this liboqs build.\n");
        fprintf(stderr, "       Rebuild with -DOQS_ENABLE_SIG_FALCON_512=ON\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }

    /* ── Print algorithm parameters ───────────────────────────────────────
     *
     * These come straight from the OQS_SIG struct so they'll track any
     * upstream changes across liboqs versions automatically.
     */
    printf("Algorithm        : %s\n", sig->method_name);
    printf("Public key size  : %zu bytes\n", sig->length_public_key);
    printf("Secret key size  : %zu bytes\n", sig->length_secret_key);
    printf("Max signature    : %zu bytes\n", sig->length_signature);
    printf("Warmup iterations: %d\n", WARMUP_ITERATIONS);
    printf("Bench iterations : %d\n", BENCH_ITERATIONS);
    printf("Message length   : %d bytes (0x%02X fill)\n", MSG_LEN, MSG_FILL_BYTE);
    printf("\n");

    /* ── Allocate buffers ─────────────────────────────────────────────────
     *
     * All allocations happen up-front so the timed loop has zero dynamic
     * memory overhead.  We check every allocation because on constrained
     * systems (or under ASAN) any of these could fail.
     */
    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);
    uint8_t *message    = malloc(MSG_LEN);

    if (!public_key || !secret_key || !signature || !message) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        free(public_key);
        free(secret_key);
        free(signature);
        free(message);
        OQS_SIG_free(sig);
        OQS_destroy();
        return EXIT_FAILURE;
    }

    /* ── Prepare test data ────────────────────────────────────────────────
     *
     * Deterministic 256-byte message filled with 0x42.  Using a fixed
     * payload means every verification traverses the same code paths,
     * giving us the *deterministic best-case* latency.  This is the
     * right metric for blockchain nodes, which verify known-format
     * transactions in a tight loop.
     */
    memset(message, MSG_FILL_BYTE, MSG_LEN);

    /* ── Key generation (untimed) ─────────────────────────────────────────
     *
     * We generate exactly one keypair.  Key generation cost is irrelevant
     * to this benchmark; validators verify with long-lived public keys.
     */
    printf("[1/6] Generating Falcon-512 keypair ...\n");
    rc = OQS_SIG_keypair(sig, public_key, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Key generation failed (OQS_STATUS = %d).\n", rc);
        goto cleanup;
    }
    printf("       Key pair generated.\n");

    /* ── Sign the message once (untimed) ──────────────────────────────────
     *
     * We need exactly one valid signature to feed the verification loop.
     * sig_len is set by OQS_SIG_sign to the actual (possibly shorter than
     * max) signature length for this particular message.
     */
    size_t sig_len = 0;

    printf("[2/6] Signing test message ...\n");
    rc = OQS_SIG_sign(sig, signature, &sig_len, message, MSG_LEN, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Signing failed (OQS_STATUS = %d).\n", rc);
        goto cleanup;
    }
    printf("       Signature produced: %zu bytes (max %zu).\n",
           sig_len, sig->length_signature);

    /* ── Sanity check (untimed) ───────────────────────────────────────────
     *
     * If this single verification fails, there's no point running 10,000
     * of them.  Catches build misconfigurations or memory corruption early.
     */
    printf("[3/6] Sanity check -- verifying signature ...\n");
    rc = OQS_SIG_verify(sig, message, MSG_LEN, signature, sig_len, public_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Sanity-check verification FAILED (OQS_STATUS = %d).\n", rc);
        fprintf(stderr, "       The signature does not verify against the public key.\n");
        fprintf(stderr, "       This indicates a liboqs build problem or memory corruption.\n");
        goto cleanup;
    }
    printf("       Verification passed.\n");

    /* ── Warm-up phase (untimed) ──────────────────────────────────────────
     *
     * 100 verifications to:
     *   - Fill the L1 instruction cache with Falcon-512 verify code paths
     *   - Populate the L1/L2 data cache with the public key and signature
     *   - Train the branch predictor on the verify control flow
     *   - Let the CPU governor ramp up to sustained boost frequency
     *
     * Without warm-up, the first few hundred iterations would show 2-5x
     * higher latency from cold caches, polluting the mean.
     */
    printf("[4/6] Warm-up: %d verifications ...\n", WARMUP_ITERATIONS);

    /*
     * `volatile` prevents the compiler from optimising away the verify
     * calls under -O3.  If the return value is unused or provably constant,
     * aggressive dead-code elimination could remove the entire loop body.
     */
    volatile OQS_STATUS warmup_rc;
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        warmup_rc = OQS_SIG_verify(sig, message, MSG_LEN,
                                   signature, sig_len, public_key);
    }
    (void)warmup_rc;
    printf("       Warm-up complete.\n");

    /* ── Timed benchmark ──────────────────────────────────────────────────
     *
     * The critical section.  NOTHING except OQS_SIG_verify executes
     * between the two clock reads -- no printf, no branches, no counters
     * beyond the loop index.
     *
     * The loop variable `i` and `bench_rc` are the only writes.  Both
     * live in registers on any modern ABI, so there is zero store traffic
     * beyond what the verify function itself generates.
     */
    printf("[5/6] Benchmarking: %d verifications ...\n", BENCH_ITERATIONS);

    volatile OQS_STATUS bench_rc;
    double t_start = get_time();

    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        bench_rc = OQS_SIG_verify(sig, message, MSG_LEN,
                                  signature, sig_len, public_key);
    }

    double t_end = get_time();
    (void)bench_rc;

    double total_sec     = t_end - t_start;
    double ops_per_sec   = (double)BENCH_ITERATIONS / total_sec;
    double ms_per_op     = (total_sec / (double)BENCH_ITERATIONS) * 1e3;
    double us_per_op     = (total_sec / (double)BENCH_ITERATIONS) * 1e6;
    double cycles_per_op = (total_sec / (double)BENCH_ITERATIONS) * ASSUMED_GHZ * 1e9;

    /* ── Human-readable results ───────────────────────────────────────────
     *
     * Printed to stdout so it's visible in interactive runs.
     * The JSON block below is machine-parseable for automated collection.
     */
    printf("[6/6] Results:\n\n");
    printf("  ┌─────────────────────────────────────────────┐\n");
    printf("  │  Falcon-512 Verification Benchmark Results  │\n");
    printf("  ├─────────────────────────────────────────────┤\n");
    printf("  │  Iterations    : %'10d                  │\n", BENCH_ITERATIONS);
    printf("  │  Total time    : %13.6f sec            │\n", total_sec);
    printf("  │  Ops/sec       : %13.2f                │\n", ops_per_sec);
    printf("  │  Per operation : %10.3f ms              │\n", ms_per_op);
    printf("  │                  %10.2f µs              │\n", us_per_op);
    printf("  │  Est. cycles   : %10.0f  (@ %.1f GHz)  │\n", cycles_per_op, ASSUMED_GHZ);
    printf("  │  Signature     : %5zu bytes               │\n", sig_len);
    printf("  │  Public key    : %5zu bytes               │\n", sig->length_public_key);
    printf("  │  Secret key    : %5zu bytes               │\n", sig->length_secret_key);
    printf("  └─────────────────────────────────────────────┘\n");

    /* ── JSON output ──────────────────────────────────────────────────────
     *
     * Machine-parseable block extracted by run_all_benchmarks.sh via
     * the '--- JSON ---' sentinel.
     */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_verify\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"iterations\": %d,\n", BENCH_ITERATIONS);
    printf("  \"warmup\": %d,\n", WARMUP_ITERATIONS);
    printf("  \"total_time_sec\": %.6f,\n", total_sec);
    printf("  \"ops_per_sec\": %.2f,\n", ops_per_sec);
    printf("  \"ms_per_op\": %.3f,\n", ms_per_op);
    printf("  \"us_per_op\": %.2f,\n", us_per_op);
    printf("  \"cycles_per_op\": %.2f,\n", cycles_per_op);
    printf("  \"signature_bytes\": %zu,\n", sig_len);
    printf("  \"pubkey_bytes\": %zu,\n", sig->length_public_key);
    printf("  \"seckey_bytes\": %zu\n", sig->length_secret_key);
    printf("}\n");

    /* ── Cleanup ──────────────────────────────────────────────────────────
     *
     * OQS_MEM_secure_free overwrites the buffer before freeing -- critical
     * for the secret key to avoid leaving key material in freed pages.
     * Public data uses OQS_MEM_insecure_free (plain free + NULL).
     */
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();

    printf("\nBenchmark complete.\n");
    return EXIT_SUCCESS;

cleanup:
    /* Reached only on fatal errors during setup phases. */
    OQS_MEM_secure_free(secret_key, sig ? sig->length_secret_key : 0);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_FAILURE;
}
