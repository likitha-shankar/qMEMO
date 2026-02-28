/*
 * multicore_benchmark.c — Falcon-512 Verification Across Multiple Cores
 *
 * Part of the qMEMO project (IIT Chicago): benchmarks post-quantum
 * signature verification for MEMO blockchain.
 *
 * Measures total verification throughput when running N threads in
 * parallel (N = 1, 2, 4, 6, 8, 10). Each thread has its own copy of
 * the public key, message, and signature to avoid cache-line contention.
 * Wall-clock time is used so throughput = (N × verifications_per_thread) / duration.
 *
 * Methodology (aligned with verify_benchmark.c):
 *   - One keypair and one signature generated in the main thread.
 *   - Per-thread warm-up (100 verifications) before the timed section.
 *   - All threads wait at a barrier after warm-up; main joins last and
 *     records t_start immediately after the barrier releases.  This
 *     excludes warm-up time from the measurement and ensures all threads
 *     enter the timed loop simultaneously.
 *   - Each thread performs VERIF_PER_THREAD verifications in the timed section.
 *   - Total throughput = (N × VERIF_PER_THREAD) / (t_end − t_start).
 *
 * Compile (from project root, after install_liboqs.sh):
 *   make multicore
 *
 * Run:
 *   ./benchmarks/bin/multicore_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp, barrier_t — must be first */

#include <oqs/oqs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN           256
#define MSG_FILL_BYTE     0x42
#define WARMUP_PER_THREAD 100
#define VERIF_PER_THREAD  1000

/*
 * NUM_CORE_CONFIGS is an enum constant so it can be used as a C array size
 * without triggering variable-length-array warnings.  Keep it in sync with
 * the CORE_COUNTS initialiser below.
 */
enum { NUM_CORE_CONFIGS = 6 };
static const int CORE_COUNTS[NUM_CORE_CONFIGS] = { 1, 2, 4, 6, 8, 10 };

/* ── Per-thread argument ──────────────────────────────────────────────────── */

typedef struct {
    int        id;
    OQS_SIG   *sig;
    uint8_t   *public_key;
    uint8_t   *message;
    uint8_t   *signature;
    size_t     sig_len;
    barrier_t *barrier;
} thread_arg_t;

/*
 * Worker: warm up, then wait at the barrier.  Once the main thread also
 * arrives at the barrier, all participants wake simultaneously and each
 * thread performs exactly VERIF_PER_THREAD timed verifications.
 *
 * The timed section is not bounded by another barrier: we measure from
 * t_start (recorded by main immediately after the barrier returns) to
 * t_end (recorded after joining all threads), so the clock captures the
 * total wall-clock duration for the slowest thread to finish.
 */
static void *worker(void *arg)
{
    thread_arg_t *a = (thread_arg_t *)arg;
    volatile OQS_STATUS rc;

    /* Warm-up (not timed) */
    for (int i = 0; i < WARMUP_PER_THREAD; i++) {
        rc = OQS_SIG_verify(a->sig, a->message, MSG_LEN,
                            a->signature, a->sig_len, a->public_key);
    }
    (void)rc;

    /* All threads synchronise here; main joins last and records t_start. */
    barrier_wait(a->barrier);

    /* Timed section: exactly VERIF_PER_THREAD verifications */
    for (int i = 0; i < VERIF_PER_THREAD; i++) {
        rc = OQS_SIG_verify(a->sig, a->message, MSG_LEN,
                            a->signature, a->sig_len, a->public_key);
    }

    return NULL;
}

/*
 * Run benchmark for one core count.
 *
 * Returns total ops/sec (all threads combined), or -1.0 on error.
 *
 * Timing sequence:
 *   spawn threads → threads do warm-up → barrier_wait (main joins last)
 *   → t_start recorded → threads do timed verifications → join all → t_end
 */
static double run_for_cores(OQS_SIG *sig,
                            const uint8_t *public_key,
                            const uint8_t *message,
                            const uint8_t *signature,
                            size_t sig_len,
                            int ncores)
{
    pthread_t    *threads = malloc((size_t)ncores * sizeof(pthread_t));
    thread_arg_t *args    = malloc((size_t)ncores * sizeof(thread_arg_t));
    barrier_t     barrier;

    if (!threads || !args) {
        fprintf(stderr, "ERROR: malloc failed for %d threads\n", ncores);
        free(threads);
        free(args);
        return -1.0;
    }

    /* Barrier has ncores workers + 1 main thread. */
    barrier_init(&barrier, (unsigned)(ncores + 1));

    /* Per-thread private buffers to avoid false sharing */
    for (int i = 0; i < ncores; i++) {
        args[i].id         = i;
        args[i].sig        = sig;
        args[i].public_key = malloc(sig->length_public_key);
        args[i].message    = malloc(MSG_LEN);
        args[i].signature  = malloc(sig->length_signature);
        args[i].sig_len    = sig_len;
        args[i].barrier    = &barrier;

        if (!args[i].public_key || !args[i].message || !args[i].signature) {
            fprintf(stderr, "ERROR: malloc failed for thread %d buffers\n", i);
            for (int j = 0; j <= i; j++) {
                free(args[j].public_key);
                free(args[j].message);
                free(args[j].signature);
            }
            free(threads);
            free(args);
            barrier_destroy(&barrier);
            return -1.0;
        }
        memcpy(args[i].public_key, public_key, sig->length_public_key);
        memcpy(args[i].message,    message,    MSG_LEN);
        memcpy(args[i].signature,  signature,  sig_len);
    }

    for (int i = 0; i < ncores; i++) {
        if (pthread_create(&threads[i], NULL, worker, &args[i]) != 0) {
            fprintf(stderr, "FATAL: pthread_create failed for thread %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Main thread joins the barrier last.  When it returns, all workers have
     * finished warm-up and are simultaneously beginning their timed loops.
     * Recording t_start here excludes warm-up from the measurement.
     */
    barrier_wait(&barrier);
    double t_start = get_time();

    for (int i = 0; i < ncores; i++)
        pthread_join(threads[i], NULL);

    double t_end = get_time();

    for (int i = 0; i < ncores; i++) {
        free(args[i].public_key);
        free(args[i].message);
        free(args[i].signature);
    }
    free(threads);
    free(args);
    barrier_destroy(&barrier);

    double total_verifications = (double)ncores * (double)VERIF_PER_THREAD;
    return total_verifications / (t_end - t_start);
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    OQS_STATUS rc;
    char timestamp[64];

    /* Sized to NUM_CORE_CONFIGS — update the enum above if CORE_COUNTS changes. */
    double ops_per_sec[NUM_CORE_CONFIGS];
    double speedup[NUM_CORE_CONFIGS];
    double efficiency[NUM_CORE_CONFIGS];

    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Multicore Verification Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    OQS_init();

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: Falcon-512 is not enabled in this liboqs build.\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);
    uint8_t *message    = malloc(MSG_LEN);

    if (!public_key || !secret_key || !signature || !message) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        free(public_key); free(secret_key); free(signature); free(message);
        OQS_SIG_free(sig);
        OQS_destroy();
        return EXIT_FAILURE;
    }

    memset(message, MSG_FILL_BYTE, MSG_LEN);

    printf("Generating keypair and signing message …\n");
    rc = OQS_SIG_keypair(sig, public_key, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Key generation failed.\n");
        goto cleanup;
    }

    size_t sig_len = 0;
    rc = OQS_SIG_sign(sig, signature, &sig_len, message, MSG_LEN, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Signing failed.\n");
        goto cleanup;
    }

    rc = OQS_SIG_verify(sig, message, MSG_LEN, signature, sig_len, public_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Sanity-check verification failed.\n");
        goto cleanup;
    }
    printf("OK. Signature length: %zu bytes.\n\n", sig_len);

    printf("Cores  |  Throughput (ops/sec)  |  Speedup  |  Efficiency\n");
    printf("-------|------------------------|-----------|------------\n");

    for (int c = 0; c < NUM_CORE_CONFIGS; c++) {
        int n = CORE_COUNTS[c];
        double ops = run_for_cores(sig, public_key, message, signature, sig_len, n);
        if (ops < 0.0) {
            fprintf(stderr, "ERROR: Benchmark failed for %d cores.\n", n);
            goto cleanup;
        }
        ops_per_sec[c] = ops;
        speedup[c]     = (c == 0) ? 1.0 : (ops / ops_per_sec[0]);
        efficiency[c]  = (speedup[c] / (double)n) * 100.0;

        printf("  %2d   |  %18.0f     |  %6.2f   |  %5.1f%%\n",
               n, ops_per_sec[c], speedup[c], efficiency[c]);
    }

    /* ── JSON output ──────────────────────────────────────────────────────
     *
     * Arrays are printed via loops so the output adapts automatically
     * when CORE_COUNTS is extended — no hardcoded index literals.
     */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_multicore_verify\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"verif_per_thread\": %d,\n", VERIF_PER_THREAD);
    printf("  \"warmup_per_thread\": %d,\n", WARMUP_PER_THREAD);

    printf("  \"cores\": [");
    for (int c = 0; c < NUM_CORE_CONFIGS; c++)
        printf("%d%s", CORE_COUNTS[c], (c < NUM_CORE_CONFIGS - 1) ? ", " : "");
    printf("],\n");

    printf("  \"ops_per_sec\": [");
    for (int c = 0; c < NUM_CORE_CONFIGS; c++)
        printf("%.0f%s", ops_per_sec[c], (c < NUM_CORE_CONFIGS - 1) ? ", " : "");
    printf("],\n");

    printf("  \"speedup\": [");
    for (int c = 0; c < NUM_CORE_CONFIGS; c++)
        printf("%.2f%s", speedup[c], (c < NUM_CORE_CONFIGS - 1) ? ", " : "");
    printf("],\n");

    printf("  \"efficiency_pct\": [");
    for (int c = 0; c < NUM_CORE_CONFIGS; c++)
        printf("%.1f%s", efficiency[c], (c < NUM_CORE_CONFIGS - 1) ? ", " : "");
    printf("]\n");

    printf("}\n");

    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();

    printf("\nMulticore benchmark complete.\n");
    return EXIT_SUCCESS;

cleanup:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_FAILURE;
}
