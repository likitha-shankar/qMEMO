/*
 * concurrent_benchmark.c — Falcon-512 Concurrent Signature Verification
 *
 * Part of the qMEMO project (IIT Chicago): benchmarks post-quantum
 * signature verification for blockchain nodes.
 *
 * Scenario: A node receives 100 transactions simultaneously and must
 * verify all signatures. This benchmark compares:
 *   - Concurrent: 100 verifications dispatched to a pool of 4 worker threads.
 *   - Sequential: Same 100 verifications run one after another (baseline).
 *
 * Timing correctness
 * ──────────────────
 * A common mistake is recording t_start before pthread_create — this
 * includes thread-spawn overhead (~50–200 µs each) in the "concurrent"
 * timing and makes it look slower than sequential.  We fix this with a
 * startup barrier: all NUM_WORKERS threads block at the barrier after
 * being spawned; the main thread joins the barrier, records t_start, and
 * workers then pull tasks simultaneously.  Thread-spawn cost is excluded
 * from the measurement entirely.
 *
 * Methodology:
 *   - Generate 100 distinct keypairs and sign 100 distinct messages.
 *   - Concurrent run: all workers ready → t_start → drain task queue → t_end
 *   - Sequential run: single thread verifies all 100 in order.
 *   - Report total_time_ms, avg_latency_ms, throughput (ops/sec).
 *
 * Compile (from project root, after install_liboqs.sh):
 *   make concurrent
 *
 * Run:
 *   ./benchmarks/bin/concurrent_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp, barrier_t — must be first */

#include <oqs/oqs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define NUM_SIGNATURES  100
#define NUM_WORKERS       4
#define MSG_LEN          256

/* ── Thread pool state ────────────────────────────────────────────────────── */

typedef struct {
    OQS_SIG        *sig;
    uint8_t       **public_keys;
    uint8_t       **messages;
    uint8_t       **signatures;
    size_t         *sig_lens;
    int             next_index;
    int             completed;
    pthread_mutex_t mutex;
    pthread_cond_t  done_cond;
    barrier_t       start_barrier;  /* synchronise startup so t_start is accurate */
} pool_t;

/*
 * Worker function.
 *
 * Each worker waits at start_barrier until all NUM_WORKERS threads have been
 * spawned and the main thread has also arrived.  This ensures no worker starts
 * pulling tasks before t_start is recorded in run_concurrent(), which prevents
 * thread-spawn latency from inflating the concurrent timing measurement.
 */
static void *worker(void *arg)
{
    pool_t *pool = (pool_t *)arg;

    /* Block until all workers and main are ready — then start simultaneously. */
    barrier_wait(&pool->start_barrier);

    while (1) {
        int task;
        pthread_mutex_lock(&pool->mutex);
        if (pool->next_index >= NUM_SIGNATURES) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        task = pool->next_index++;
        pthread_mutex_unlock(&pool->mutex);

        (void)OQS_SIG_verify(pool->sig,
                             pool->messages[task],
                             MSG_LEN,
                             pool->signatures[task],
                             pool->sig_lens[task],
                             pool->public_keys[task]);

        pthread_mutex_lock(&pool->mutex);
        pool->completed++;
        if (pool->completed >= NUM_SIGNATURES)
            pthread_cond_broadcast(&pool->done_cond);
        pthread_mutex_unlock(&pool->mutex);
    }
    return NULL;
}

/*
 * Run all NUM_SIGNATURES verifications using NUM_WORKERS threads.
 *
 * Returns total wall-clock time in seconds (thread-spawn excluded), or -1.0
 * on error.
 *
 * Timing sequence:
 *   create threads → barrier_wait (main joins) → t_start → wait for all
 *   work to complete → t_end → join threads
 */
static double run_concurrent(pool_t *pool)
{
    pthread_t threads[NUM_WORKERS];
    pool->next_index = 0;
    pool->completed  = 0;

    /* Barrier has NUM_WORKERS + 1 participants: all workers + main. */
    barrier_init(&pool->start_barrier, NUM_WORKERS + 1);

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, worker, pool) != 0) {
            fprintf(stderr, "FATAL: pthread_create failed for thread %d.\n", i);
            /* Workers 0..i-1 are stuck at the barrier — unrecoverable. */
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Join the barrier: this releases all workers simultaneously.
     * t_start is recorded immediately after — within nanoseconds of
     * the workers beginning to pull tasks.
     */
    barrier_wait(&pool->start_barrier);
    double t_start = get_time();

    pthread_mutex_lock(&pool->mutex);
    while (pool->completed < NUM_SIGNATURES)
        pthread_cond_wait(&pool->done_cond, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);

    double t_end = get_time();

    for (int i = 0; i < NUM_WORKERS; i++)
        (void)pthread_join(threads[i], NULL);

    barrier_destroy(&pool->start_barrier);
    return t_end - t_start;
}

/* Run all NUM_SIGNATURES verifications sequentially. Returns total time in seconds. */
static double run_sequential(pool_t *pool)
{
    double t_start = get_time();
    for (int i = 0; i < NUM_SIGNATURES; i++) {
        (void)OQS_SIG_verify(pool->sig,
                             pool->messages[i],
                             MSG_LEN,
                             pool->signatures[i],
                             pool->sig_lens[i],
                             pool->public_keys[i]);
    }
    double t_end = get_time();
    return t_end - t_start;
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Concurrent Verification Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    OQS_init();

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: Falcon-512 is not enabled in this liboqs build.\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }

    /* Allocate per-signature pointer arrays */
    uint8_t **public_keys = malloc(NUM_SIGNATURES * sizeof(uint8_t *));
    uint8_t **messages    = malloc(NUM_SIGNATURES * sizeof(uint8_t *));
    uint8_t **signatures  = malloc(NUM_SIGNATURES * sizeof(uint8_t *));
    size_t   *sig_lens    = malloc(NUM_SIGNATURES * sizeof(size_t));

    if (!public_keys || !messages || !signatures || !sig_lens) {
        fprintf(stderr, "ERROR: malloc failed\n");
        goto fail_alloc;
    }

    for (int i = 0; i < NUM_SIGNATURES; i++) {
        public_keys[i] = malloc(sig->length_public_key);
        messages[i]    = malloc(MSG_LEN);
        signatures[i]  = malloc(sig->length_signature);
        if (!public_keys[i] || !messages[i] || !signatures[i]) {
            fprintf(stderr, "ERROR: malloc failed for signature %d\n", i);
            goto fail_per_sig;
        }
    }

    /* Generate 100 keypairs and 100 signatures */
    printf("Generating %d keypairs and signatures …\n", NUM_SIGNATURES);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    if (!secret_key) {
        fprintf(stderr, "ERROR: malloc secret_key failed\n");
        goto fail_per_sig;
    }

    for (int i = 0; i < NUM_SIGNATURES; i++) {
        memset(messages[i], (i & 0xff), MSG_LEN);
        OQS_STATUS rc = OQS_SIG_keypair(sig, public_keys[i], secret_key);
        if (rc != OQS_SUCCESS) {
            fprintf(stderr, "ERROR: keypair %d failed\n", i);
            goto fail_keygen;
        }
        sig_lens[i] = sig->length_signature;
        rc = OQS_SIG_sign(sig, signatures[i], &sig_lens[i],
                          messages[i], MSG_LEN, secret_key);
        if (rc != OQS_SUCCESS) {
            fprintf(stderr, "ERROR: sign %d failed\n", i);
            goto fail_keygen;
        }
    }
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    printf("OK.\n");

    pool_t pool = {
        .sig         = sig,
        .public_keys = public_keys,
        .messages    = messages,
        .signatures  = signatures,
        .sig_lens    = sig_lens,
        .next_index  = 0,
        .completed   = 0,
    };
    pthread_mutex_init(&pool.mutex, NULL);
    pthread_cond_init(&pool.done_cond, NULL);

    /* Timed runs */
    double t_concurrent = run_concurrent(&pool);
    double t_sequential = run_sequential(&pool);

    pthread_cond_destroy(&pool.done_cond);
    pthread_mutex_destroy(&pool.mutex);

    /* Metrics */
    double total_concurrent_ms   = t_concurrent * 1e3;
    double total_sequential_ms   = t_sequential * 1e3;
    double avg_concurrent_ms     = total_concurrent_ms / NUM_SIGNATURES;
    double avg_sequential_ms     = total_sequential_ms / NUM_SIGNATURES;
    double throughput_concurrent = (double)NUM_SIGNATURES / t_concurrent;
    double throughput_sequential = (double)NUM_SIGNATURES / t_sequential;

    double overhead_pct = (t_concurrent > t_sequential)
        ? ((t_concurrent - t_sequential) / t_sequential) * 100.0
        : -((t_sequential - t_concurrent) / t_sequential) * 100.0;

    char analysis[256];
    if (overhead_pct > 0.0)
        snprintf(analysis, sizeof(analysis),
                 "Concurrent adds %.1f%% overhead due to thread coordination",
                 overhead_pct);
    else
        snprintf(analysis, sizeof(analysis),
                 "Concurrent yields %.1f%% lower latency (better parallelism)",
                 -overhead_pct);

    printf("\nConcurrent (%d workers): %.3f ms total, %.4f ms avg, %.0f ops/sec\n",
           NUM_WORKERS, total_concurrent_ms, avg_concurrent_ms, throughput_concurrent);
    printf("Sequential (baseline):   %.3f ms total, %.4f ms avg, %.0f ops/sec\n",
           total_sequential_ms, avg_sequential_ms, throughput_sequential);
    printf("\n%s\n", analysis);

    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_concurrent_verify\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"concurrent\": {\n");
    printf("    \"signatures\": %d,\n", NUM_SIGNATURES);
    printf("    \"worker_threads\": %d,\n", NUM_WORKERS);
    printf("    \"total_time_ms\": %.4f,\n", total_concurrent_ms);
    printf("    \"avg_latency_ms\": %.4f,\n", avg_concurrent_ms);
    printf("    \"throughput\": %.0f\n", throughput_concurrent);
    printf("  },\n");
    printf("  \"sequential\": {\n");
    printf("    \"total_time_ms\": %.4f,\n", total_sequential_ms);
    printf("    \"avg_latency_ms\": %.4f,\n", avg_sequential_ms);
    printf("    \"throughput\": %.0f\n", throughput_sequential);
    printf("  },\n");
    printf("  \"analysis\": \"%s\"\n", analysis);
    printf("}\n");
    printf("\nConcurrent benchmark complete.\n");

    for (int i = 0; i < NUM_SIGNATURES; i++) {
        OQS_MEM_insecure_free(public_keys[i]);
        OQS_MEM_insecure_free(messages[i]);
        OQS_MEM_insecure_free(signatures[i]);
    }
    free(public_keys);
    free(messages);
    free(signatures);
    free(sig_lens);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_SUCCESS;

fail_keygen:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
fail_per_sig:
    for (int i = 0; i < NUM_SIGNATURES; i++) {
        free(public_keys[i]);
        free(messages[i]);
        free(signatures[i]);
    }
fail_alloc:
    free(public_keys);
    free(messages);
    free(signatures);
    free(sig_lens);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_FAILURE;
}
