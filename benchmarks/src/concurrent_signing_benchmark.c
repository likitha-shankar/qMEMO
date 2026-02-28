/*
 * concurrent_signing_benchmark.c — Falcon-512 Concurrent Signature Generation
 *
 * Part of the qMEMO project (IIT Chicago): benchmarks post-quantum signature
 * signing for high-throughput blockchain transaction submission.
 *
 * Scenario
 * ────────
 * A wallet service or load-generator must sign N transactions simultaneously
 * using a single Falcon-512 secret key.  This benchmark compares:
 *
 *   Concurrent: N signing tasks dispatched to a pool of 4 worker threads,
 *               each holding its own OQS_SIG context (mandatory for safety).
 *   Sequential: Same N tasks signed one after another (single-thread baseline).
 *
 * Thread-safety model
 * ───────────────────
 * OQS_SIG_sign() is safe to call concurrently provided:
 *
 *   1. Each thread owns its own OQS_SIG * context.  Falcon's sign path
 *      allocates temporary working memory inside the context; sharing a
 *      single context across threads would cause data races.
 *
 *   2. The secret key buffer is shared read-only.  Falcon does NOT mutate
 *      the key during signing — only the working memory changes.
 *
 *   3. Each task has its own output signature buffer (trivially separate).
 *
 *   4. OQS_randombytes calls the OS RNG (arc4random on macOS, getrandom on
 *      Linux) which is independently thread-safe per call.
 *
 *   5. Each worker calls OQS_thread_stop() before exiting, as required by
 *      liboqs to release per-thread OpenSSL resources.
 *
 * Timing correctness
 * ──────────────────
 * Identical startup-barrier methodology to concurrent_benchmark.c:
 *
 *   create threads → workers init OQS_SIG contexts → barrier_wait (all 5
 *   participants: 4 workers + main) → t_start → work drains → t_end
 *
 * Thread-spawn and OQS_SIG initialisation overhead are excluded from the
 * timed window.  We measure only the signing work itself.
 *
 * Blockchain relevance
 * ────────────────────
 * Validator nodes only run verify — signing never appears on the hot path.
 * Concurrent signing matters for:
 *   - tx_generator.py: must sign >= target TPS before submitting to the node.
 *     Sequential signing at ~7,000 ops/sec covers 500 TPS easily; concurrent
 *     signing is needed when pushing load tests above ~5,000 TPS.
 *   - Wallet services signing batches for multiple users simultaneously.
 *   - Any scenario where signing, not verification, is the rate limiter.
 *
 * Signature size note
 * ───────────────────
 * Falcon-512 produces variable-length signatures (up to 752 bytes in the
 * unpadded variant).  This benchmark records min/max/average actual sig
 * lengths from the concurrent run, making the size distribution visible.
 *
 * Compile (from project root):
 *   make concurrent_sign
 *
 * Run:
 *   ./benchmarks/bin/concurrent_signing_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp, barrier_t — must be first */

#include <oqs/oqs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define NUM_SIGNING_TASKS  100
#define NUM_WORKERS          4
#define MSG_LEN            256

/* ── Signing pool state ───────────────────────────────────────────────────── */

typedef struct {
    const uint8_t  *secret_key;     /* shared read-only across all workers   */
    uint8_t       **messages;       /* pre-filled input messages, read-only  */
    uint8_t       **out_signatures; /* per-task output buffer (pre-allocated) */
    size_t         *out_sig_lens;   /* written by each worker                */
    size_t          sig_max_len;    /* OQS max sig length for buffer sizing  */

    int             next_index;     /* next task to claim (protected by mutex) */
    int             completed;      /* tasks finished    (protected by mutex) */
    pthread_mutex_t mutex;
    pthread_cond_t  done_cond;
    barrier_t       start_barrier;  /* synchronise startup so t_start is accurate */
} sign_pool_t;

/*
 * Worker function.
 *
 * Each worker creates its own OQS_SIG context — this is the key difference
 * from concurrent verification, where a single shared context is safe.
 * For signing, each context owns internal temporary working memory that
 * would race if shared.
 *
 * The secret_key pointer is read-only.  Multiple workers signing with the
 * same key simultaneously is safe because Falcon never writes back to the
 * key buffer during sign.
 */
static void *signing_worker(void *arg)
{
    sign_pool_t *pool = (sign_pool_t *)arg;

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (!sig) {
        fprintf(stderr, "ERROR: worker failed to create OQS_SIG context\n");
        /* Still need to join the barrier so the other threads aren't stuck. */
        barrier_wait(&pool->start_barrier);
        return NULL;
    }

    /*
     * Block at startup barrier.  All NUM_WORKERS workers + main must arrive
     * before any worker starts pulling tasks.  This ensures t_start (recorded
     * by main immediately after barrier_wait) is accurate — no worker has
     * begun signing before the clock starts.
     */
    barrier_wait(&pool->start_barrier);

    while (1) {
        int task;
        pthread_mutex_lock(&pool->mutex);
        if (pool->next_index >= NUM_SIGNING_TASKS) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        task = pool->next_index++;
        pthread_mutex_unlock(&pool->mutex);

        pool->out_sig_lens[task] = pool->sig_max_len;
        (void)OQS_SIG_sign(sig,
                           pool->out_signatures[task],
                           &pool->out_sig_lens[task],
                           pool->messages[task],
                           MSG_LEN,
                           pool->secret_key);

        pthread_mutex_lock(&pool->mutex);
        pool->completed++;
        if (pool->completed >= NUM_SIGNING_TASKS)
            pthread_cond_broadcast(&pool->done_cond);
        pthread_mutex_unlock(&pool->mutex);
    }

    /*
     * liboqs requires each thread to call OQS_thread_stop() before exit to
     * release per-thread OpenSSL state (ERR and EVP_MD_CTX objects).
     * Skipping this leaks memory in long-running processes.
     */
    OQS_thread_stop();
    OQS_SIG_free(sig);
    return NULL;
}

/*
 * run_concurrent — dispatch all NUM_SIGNING_TASKS to a pool of NUM_WORKERS.
 *
 * Returns total wall-clock time in seconds (spawn + context init excluded),
 * or -1.0 on error.
 */
static double run_concurrent(sign_pool_t *pool)
{
    pthread_t threads[NUM_WORKERS];
    pool->next_index = 0;
    pool->completed  = 0;

    /* Barrier: NUM_WORKERS workers + 1 main = NUM_WORKERS + 1 participants. */
    barrier_init(&pool->start_barrier, NUM_WORKERS + 1);

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, signing_worker, pool) != 0) {
            fprintf(stderr, "FATAL: pthread_create failed for worker %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Main joins the barrier.  All workers are now guaranteed to have
     * created their OQS_SIG contexts and to be blocked at the barrier.
     * The instant the barrier releases, t_start is recorded — workers
     * and the clock start simultaneously.
     */
    barrier_wait(&pool->start_barrier);
    double t_start = get_time();

    pthread_mutex_lock(&pool->mutex);
    while (pool->completed < NUM_SIGNING_TASKS)
        pthread_cond_wait(&pool->done_cond, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);

    double t_end = get_time();

    for (int i = 0; i < NUM_WORKERS; i++)
        (void)pthread_join(threads[i], NULL);

    barrier_destroy(&pool->start_barrier);
    return t_end - t_start;
}

/*
 * run_sequential — sign all NUM_SIGNING_TASKS with a single OQS_SIG context.
 *
 * Uses fresh output buffers (out_sig_lens reset before timing).
 */
static double run_sequential(sign_pool_t *pool)
{
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (!sig) return -1.0;

    double t_start = get_time();
    for (int i = 0; i < NUM_SIGNING_TASKS; i++) {
        pool->out_sig_lens[i] = pool->sig_max_len;
        (void)OQS_SIG_sign(sig,
                           pool->out_signatures[i],
                           &pool->out_sig_lens[i],
                           pool->messages[i],
                           MSG_LEN,
                           pool->secret_key);
    }
    double t_end = get_time();

    OQS_SIG_free(sig);
    return t_end - t_start;
}

/* ── Signature size statistics (from out_sig_lens after a run) ────────────── */

static void print_sig_size_stats(const size_t *lens, int n)
{
    size_t min_len = lens[0], max_len = lens[0];
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        if (lens[i] < min_len) min_len = lens[i];
        if (lens[i] > max_len) max_len = lens[i];
        sum += (double)lens[i];
    }
    double avg = sum / n;

    double var = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)lens[i] - avg;
        var += d * d;
    }
    double std = sqrt(var / n);

    printf("  Signature sizes (n=%d, max-buf=%zu B):\n", n, lens[0] > 0 ? lens[0] : (size_t)752);
    printf("    min=%-4zu  max=%-4zu  avg=%.1f  std=%.1f bytes\n",
           min_len, max_len, avg, std);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Concurrent Signing Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    OQS_init();

    /* Use a temporary sig object only to read key/sig length constants. */
    OQS_SIG *sig_meta = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (!sig_meta) {
        fprintf(stderr, "ERROR: Falcon-512 is not enabled in this liboqs build.\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }
    size_t pk_len  = sig_meta->length_public_key;
    size_t sk_len  = sig_meta->length_secret_key;
    size_t sig_max = sig_meta->length_signature;
    OQS_SIG_free(sig_meta);

    printf("Algorithm   : Falcon-512\n");
    printf("Tasks       : %d signing operations\n", NUM_SIGNING_TASKS);
    printf("Workers     : %d concurrent threads\n", NUM_WORKERS);
    printf("Message len : %d bytes\n", MSG_LEN);
    printf("Key sizes   : pk=%zu B  sk=%zu B  sig_max=%zu B\n\n",
           pk_len, sk_len, sig_max);

    /* ── Key generation ────────────────────────────────────────────────────
     *
     * One keypair.  All workers will sign with the same secret key —
     * this is the realistic scenario (one wallet, concurrent sign calls).
     */
    uint8_t *public_key  = malloc(pk_len);
    uint8_t *secret_key  = malloc(sk_len);
    if (!public_key || !secret_key) {
        fprintf(stderr, "ERROR: malloc failed for key buffers\n");
        free(public_key); free(secret_key);
        OQS_destroy();
        return EXIT_FAILURE;
    }

    printf("Generating keypair … ");
    fflush(stdout);
    {
        OQS_SIG *tmp = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!tmp || OQS_SIG_keypair(tmp, public_key, secret_key) != OQS_SUCCESS) {
            fprintf(stderr, "ERROR: keypair generation failed\n");
            OQS_SIG_free(tmp);
            goto cleanup_keys;
        }
        OQS_SIG_free(tmp);
    }
    printf("done.\n");

    /* ── Allocate per-task buffers ────────────────────────────────────────── */
    uint8_t **messages       = malloc(NUM_SIGNING_TASKS * sizeof(uint8_t *));
    uint8_t **out_signatures = malloc(NUM_SIGNING_TASKS * sizeof(uint8_t *));
    size_t   *out_sig_lens   = malloc(NUM_SIGNING_TASKS * sizeof(size_t));

    if (!messages || !out_signatures || !out_sig_lens) {
        fprintf(stderr, "ERROR: malloc failed for task arrays\n");
        goto cleanup_keys;
    }

    for (int i = 0; i < NUM_SIGNING_TASKS; i++) {
        messages[i]       = malloc(MSG_LEN);
        out_signatures[i] = malloc(sig_max);
        if (!messages[i] || !out_signatures[i]) {
            fprintf(stderr, "ERROR: malloc failed for task %d buffers\n", i);
            goto cleanup_tasks;
        }
        /* Distinct message per task — different byte fill per index. */
        memset(messages[i], (i & 0xff), MSG_LEN);
    }
    printf("Task buffers allocated.\n\n");

    /* ── Pool setup ───────────────────────────────────────────────────────── */
    sign_pool_t pool = {
        .secret_key     = secret_key,
        .messages       = messages,
        .out_signatures = out_signatures,
        .out_sig_lens   = out_sig_lens,
        .sig_max_len    = sig_max,
        .next_index     = 0,
        .completed      = 0,
    };
    pthread_mutex_init(&pool.mutex, NULL);
    pthread_cond_init(&pool.done_cond, NULL);

    /* ── Concurrent run ───────────────────────────────────────────────────── */
    printf("Running concurrent signing (%d workers) …\n", NUM_WORKERS);
    double t_concurrent = run_concurrent(&pool);
    if (t_concurrent < 0.0) {
        fprintf(stderr, "ERROR: concurrent run failed\n");
        goto cleanup_pool;
    }
    print_sig_size_stats(out_sig_lens, NUM_SIGNING_TASKS);

    /* ── Sequential run ───────────────────────────────────────────────────── */
    printf("\nRunning sequential signing (baseline) …\n");
    double t_sequential = run_sequential(&pool);
    if (t_sequential < 0.0) {
        fprintf(stderr, "ERROR: sequential run failed\n");
        goto cleanup_pool;
    }
    print_sig_size_stats(out_sig_lens, NUM_SIGNING_TASKS);

    /* ── Metrics ──────────────────────────────────────────────────────────── */
    double ms_concurrent   = t_concurrent * 1e3;
    double ms_sequential   = t_sequential * 1e3;
    double avg_ms_conc     = ms_concurrent / NUM_SIGNING_TASKS;
    double avg_ms_seq      = ms_sequential / NUM_SIGNING_TASKS;
    double tput_concurrent = (double)NUM_SIGNING_TASKS / t_concurrent;
    double tput_sequential = (double)NUM_SIGNING_TASKS / t_sequential;
    double speedup         = tput_concurrent / tput_sequential;

    double overhead_pct = (t_concurrent > t_sequential)
        ? ((t_concurrent - t_sequential) / t_sequential) * 100.0
        : -((t_sequential - t_concurrent) / t_sequential) * 100.0;

    char analysis[256];
    if (overhead_pct > 0.0)
        snprintf(analysis, sizeof(analysis),
                 "Concurrent adds %.1f%% overhead (mutex contention or cache thrash)",
                 overhead_pct);
    else
        snprintf(analysis, sizeof(analysis),
                 "Concurrent yields %.1fx speedup (%.1f%% faster than sequential)",
                 speedup, -overhead_pct);

    printf("\n================================================================\n");
    printf("  RESULTS\n");
    printf("================================================================\n\n");
    printf("  Concurrent (%d workers): %7.3f ms total | %7.4f ms/op | %8.0f ops/sec\n",
           NUM_WORKERS, ms_concurrent, avg_ms_conc, tput_concurrent);
    printf("  Sequential (baseline):  %7.3f ms total | %7.4f ms/op | %8.0f ops/sec\n",
           ms_sequential, avg_ms_seq, tput_sequential);
    printf("\n  Speedup:  %.2fx\n", speedup);
    printf("  %s\n", analysis);

    /* ── Context: compare to verify throughput ────────────────────────────── */
    printf("\n  For context:\n");
    printf("    Concurrent verify throughput (from concurrent_benchmark): ~141,643 ops/sec\n");
    printf("    Signing is compute-heavier (FFT Gaussian sampling) so lower\n");
    printf("    parallelism efficiency is expected.\n");
    printf("\n  Blockchain relevance:\n");
    printf("    Sequential signing covers 500 TPS load tests easily.\n");
    printf("    Concurrent signing needed for >5,000 TPS stress tests.\n");

    /* ── JSON output ──────────────────────────────────────────────────────── */

    /* Compute sig size stats for JSON */
    size_t sig_min = out_sig_lens[0], sig_max_obs = out_sig_lens[0];
    double sig_sum = 0.0;
    for (int i = 0; i < NUM_SIGNING_TASKS; i++) {
        if (out_sig_lens[i] < sig_min)     sig_min = out_sig_lens[i];
        if (out_sig_lens[i] > sig_max_obs) sig_max_obs = out_sig_lens[i];
        sig_sum += (double)out_sig_lens[i];
    }
    double sig_avg = sig_sum / NUM_SIGNING_TASKS;

    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_concurrent_sign\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"config\": {\n");
    printf("    \"signing_tasks\": %d,\n", NUM_SIGNING_TASKS);
    printf("    \"worker_threads\": %d,\n", NUM_WORKERS);
    printf("    \"message_len\": %d,\n", MSG_LEN);
    printf("    \"sig_max_bytes\": %zu\n", sig_max);
    printf("  },\n");
    printf("  \"concurrent\": {\n");
    printf("    \"total_time_ms\": %.4f,\n", ms_concurrent);
    printf("    \"avg_latency_ms\": %.4f,\n", avg_ms_conc);
    printf("    \"throughput_ops_sec\": %.0f\n", tput_concurrent);
    printf("  },\n");
    printf("  \"sequential\": {\n");
    printf("    \"total_time_ms\": %.4f,\n", ms_sequential);
    printf("    \"avg_latency_ms\": %.4f,\n", avg_ms_seq);
    printf("    \"throughput_ops_sec\": %.0f\n", tput_sequential);
    printf("  },\n");
    printf("  \"speedup\": %.4f,\n", speedup);
    printf("  \"sig_size_stats\": {\n");
    printf("    \"min_bytes\": %zu,\n", sig_min);
    printf("    \"max_bytes\": %zu,\n", sig_max_obs);
    printf("    \"avg_bytes\": %.1f,\n", sig_avg);
    printf("    \"spec_max_bytes\": %zu\n", sig_max);
    printf("  },\n");
    printf("  \"analysis\": \"%s\"\n", analysis);
    printf("}\n");

    printf("\nConcurrent signing benchmark complete.\n");

    /* ── Cleanup ──────────────────────────────────────────────────────────── */
cleanup_pool:
    pthread_cond_destroy(&pool.done_cond);
    pthread_mutex_destroy(&pool.mutex);

cleanup_tasks:
    for (int i = 0; i < NUM_SIGNING_TASKS; i++) {
        free(messages[i]);
        OQS_MEM_insecure_free(out_signatures[i]);
    }
    free(messages);
    free(out_signatures);
    free(out_sig_lens);

cleanup_keys:
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_secure_free(secret_key, sk_len);
    OQS_destroy();
    return EXIT_SUCCESS;
}
