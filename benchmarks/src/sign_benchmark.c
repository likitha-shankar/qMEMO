/*
 * sign_benchmark.c -- Falcon-512 Multicore Signing Throughput
 *
 * Part of the qMEMO project (IIT Chicago): benchmarks post-quantum
 * cryptographic signing performance across multiple cores.
 *
 * Measures total signing throughput when running N threads in parallel
 * (N = 1, 2, 4, 6, 8, 10).  Unlike verification, each thread MUST own
 * its own OQS_SIG context and secret key -- the Falcon signing path is
 * stateful (uses a PRNG seeded from the secret key) and is NOT safe to
 * share across threads.
 *
 * Methodology (mirrors multicore_benchmark.c exactly):
 *   - Main thread generates one keypair; secret key is copied to each
 *     thread's private buffer so every thread signs with the same identity
 *     but with its own independent OQS_SIG state.
 *   - Per-thread warm-up (50 signs) before the timed section.
 *   - All threads wait at a barrier after warm-up; main joins last and
 *     records t_start immediately after the barrier releases.  Excludes
 *     warm-up and thread-spawn overhead from the measurement.
 *   - Each thread performs SIGN_PER_THREAD signs in the timed section.
 *   - Total throughput = (N x SIGN_PER_THREAD) / (t_end − t_start).
 *   - Each thread calls OQS_thread_stop() before exit to release
 *     per-thread liboqs RNG state.
 *
 * Metrics: total ops/sec, per-thread ops/sec, efficiency %, speedup vs 1-thread.
 *
 * Compile (from project root, after install_liboqs.sh):
 *   make sign
 *
 * Run:
 *   ./benchmarks/bin/sign_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp, barrier_t -- must be first */

#include <oqs/oqs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN            256
#define MSG_FILL_BYTE      0x42
#define WARMUP_PER_THREAD  50
#define SIGN_PER_THREAD    500

/*
 * NUM_CORE_CONFIGS is an enum constant so it can be used as a C array size
 * without triggering variable-length-array warnings.
 */
enum { NUM_CORE_CONFIGS = 6 };
static const int CORE_COUNTS[NUM_CORE_CONFIGS] = { 1, 2, 4, 6, 8, 10 };

/* ── Per-thread argument ──────────────────────────────────────────────────── */

typedef struct {
    int        id;
    uint8_t   *secret_key;    /* private copy of the secret key */
    size_t     sk_len;
    uint8_t   *message;       /* private copy of the message    */
    barrier_t *barrier;
} thread_arg_t;

/*
 * Worker: create a private OQS_SIG context (required for thread safety),
 * sign during warm-up (not timed), wait at the barrier, then sign
 * SIGN_PER_THREAD times in the timed section.
 *
 * OQS_thread_stop() is called before return to release the per-thread
 * PRNG state allocated by liboqs.
 */
static void *worker(void *arg)
{
    thread_arg_t *a = (thread_arg_t *)arg;

    /* Each thread creates its own OQS_SIG context (mandatory for signing). */
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: thread %d -- OQS_SIG_new failed.\n", a->id);
        OQS_thread_stop();
        return NULL;
    }

    uint8_t *signature = malloc(sig->length_signature);
    size_t   sig_len   = 0;

    if (!signature) {
        fprintf(stderr, "ERROR: thread %d -- malloc for signature failed.\n", a->id);
        OQS_SIG_free(sig);
        OQS_thread_stop();
        return NULL;
    }

    /* Warm-up (not timed) */
    for (int i = 0; i < WARMUP_PER_THREAD; i++) {
        sig_len = 0;
        OQS_SIG_sign(sig, signature, &sig_len, a->message, MSG_LEN, a->secret_key);
    }

    /* All threads synchronise here; main joins last and records t_start. */
    barrier_wait(a->barrier);

    /* Timed section: exactly SIGN_PER_THREAD signings */
    for (int i = 0; i < SIGN_PER_THREAD; i++) {
        sig_len = 0;
        OQS_SIG_sign(sig, signature, &sig_len, a->message, MSG_LEN, a->secret_key);
    }

    OQS_MEM_insecure_free(signature);
    OQS_SIG_free(sig);
    OQS_thread_stop();
    return NULL;
}

/*
 * Run benchmark for one core count.
 *
 * Returns total ops/sec (all threads combined), or -1.0 on error.
 *
 * Timing sequence:
 *   spawn threads → threads do warm-up → barrier_wait (main joins last)
 *   → t_start recorded → threads do timed signs → join all → t_end
 */
static double run_for_cores(const uint8_t *secret_key, size_t sk_len, int ncores)
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

    for (int i = 0; i < ncores; i++) {
        args[i].id         = i;
        args[i].secret_key = malloc(sk_len);
        args[i].sk_len     = sk_len;
        args[i].message    = malloc(MSG_LEN);
        args[i].barrier    = &barrier;

        if (!args[i].secret_key || !args[i].message) {
            fprintf(stderr, "ERROR: malloc failed for thread %d buffers\n", i);
            for (int j = 0; j <= i; j++) {
                OQS_MEM_secure_free(args[j].secret_key, sk_len);
                free(args[j].message);
            }
            free(threads);
            free(args);
            barrier_destroy(&barrier);
            return -1.0;
        }
        memcpy(args[i].secret_key, secret_key, sk_len);
        memset(args[i].message,    MSG_FILL_BYTE, MSG_LEN);
    }

    for (int i = 0; i < ncores; i++) {
        if (pthread_create(&threads[i], NULL, worker, &args[i]) != 0) {
            fprintf(stderr, "FATAL: pthread_create failed for thread %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Main thread joins the barrier last.  When it returns, all workers have
     * finished warm-up and are simultaneously entering the timed loops.
     */
    barrier_wait(&barrier);
    double t_start = get_time();

    for (int i = 0; i < ncores; i++)
        pthread_join(threads[i], NULL);

    double t_end = get_time();

    for (int i = 0; i < ncores; i++) {
        OQS_MEM_secure_free(args[i].secret_key, sk_len);
        free(args[i].message);
    }
    free(threads);
    free(args);
    barrier_destroy(&barrier);

    double total_signs = (double)ncores * (double)SIGN_PER_THREAD;
    return total_signs / (t_end - t_start);
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    OQS_STATUS rc;
    char timestamp[64];

    double ops_per_sec[NUM_CORE_CONFIGS];
    double speedup[NUM_CORE_CONFIGS];
    double efficiency[NUM_CORE_CONFIGS];

    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Multicore Signing Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    OQS_init();

    /* Use a temporary OQS_SIG context just for keygen in main. */
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: Falcon-512 is not enabled in this liboqs build.\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);

    if (!public_key || !secret_key) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        free(public_key);
        OQS_MEM_secure_free(secret_key, sig->length_secret_key);
        OQS_SIG_free(sig);
        OQS_destroy();
        return EXIT_FAILURE;
    }

    printf("Generating keypair ...\n");
    rc = OQS_SIG_keypair(sig, public_key, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Key generation failed.\n");
        goto cleanup;
    }
    printf("OK. Public key: %zu bytes, Secret key: %zu bytes.\n\n",
           sig->length_public_key, sig->length_secret_key);

    printf("Config:  %d warm-up signs, %d timed signs per thread\n\n",
           WARMUP_PER_THREAD, SIGN_PER_THREAD);
    printf("Cores  |  Throughput (ops/sec)  |  Per-thread (ops/sec)  |  Speedup  |  Efficiency\n");
    printf("-------|------------------------|------------------------|-----------|------------\n");

    for (int c = 0; c < NUM_CORE_CONFIGS; c++) {
        int n = CORE_COUNTS[c];
        double ops = run_for_cores(secret_key, sig->length_secret_key, n);
        if (ops < 0.0) {
            fprintf(stderr, "ERROR: Benchmark failed for %d cores.\n", n);
            goto cleanup;
        }
        ops_per_sec[c] = ops;
        speedup[c]     = (c == 0) ? 1.0 : (ops / ops_per_sec[0]);
        efficiency[c]  = (speedup[c] / (double)n) * 100.0;

        printf("  %2d   |  %18.0f     |  %18.0f     |  %6.2f   |  %5.1f%%\n",
               n, ops_per_sec[c], ops_per_sec[c] / (double)n,
               speedup[c], efficiency[c]);
    }

    /* ── JSON output ────────────────────────────────────────────────────── */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_multicore_sign\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"sign_per_thread\": %d,\n", SIGN_PER_THREAD);
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
    OQS_SIG_free(sig);
    OQS_destroy();

    printf("\nMulticore signing benchmark complete.\n");
    return EXIT_SUCCESS;

cleanup:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_FAILURE;
}
