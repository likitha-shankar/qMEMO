/*
 * bench_verify.c — Experiment 2: Signature Verification Micro-Benchmark
 *
 * Mirrors bench_sign.c structure. Key additions:
 *   --signature-mix  {valid|invalid|alternating|random:P}
 *   --invalid-mode   {flip-bit|zero-sig|wrong-key|garbage}
 *   --batch-size N   (default 1; >1 reserved for future batch-verify experiment)
 *
 * Correctness gate: warmup must prove verify(valid)==TRUE and
 * verify(invalid)==FALSE before the timed region starts. Run aborts otherwise.
 * Post-loop audit: count of TRUE/FALSE results must match the configured mix.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <math.h>
#include <getopt.h>

#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/ec.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define MAX_THREADS      128
#define MAX_PATH_LEN     512
#define MAX_TAG_LEN      256
#define MAX_CORES_STR    1024

#define ALGO_ED25519     0
#define ALGO_FALCON512   1
#define ALGO_DILITHIUM2  2

#define PIN_COMPACT      0
#define PIN_SPREAD       1
#define PIN_NONE         2

#define MIX_VALID        0
#define MIX_INVALID      1
#define MIX_ALTERNATING  2
#define MIX_RANDOM       3   /* use random_p field */

#define INV_FLIP_BIT     0
#define INV_ZERO_SIG     1
#define INV_WRONG_KEY    2
#define INV_GARBAGE      3

/* ── Config ─────────────────────────────────────────────────────────────── */

typedef struct {
    int    algo;
    char   algo_str[32];
    int    n_threads;
    int    cpu_ids[MAX_THREADS];
    int    n_explicit_cpus;
    long   iterations;
    long   warmup;
    int    runs;
    int    message_size;
    int    pin_strategy;
    int    numa_node;
    char   output_prefix[MAX_PATH_LEN];
    char   tag[MAX_TAG_LEN];
    char   cores_str[MAX_CORES_STR];
    char   pin_strategy_str[16];
    int    sig_mix;
    double random_p;           /* fraction invalid for MIX_RANDOM */
    int    invalid_mode;
    char   sig_mix_str[32];
    char   invalid_mode_str[32];
    int    batch_size;         /* currently unused >1; reserved */
} config_t;

/* ── Per-thread ─────────────────────────────────────────────────────────── */

typedef struct {
    config_t  *cfg;
    int        thread_id;
    int        run_id;
    uint64_t  *latency_ns;    /* [cfg->iterations] */
    uint8_t   *result_arr;    /* [cfg->iterations]: verify return value */
    uint8_t   *was_valid_arr; /* [cfg->iterations]: 1=valid sig used */
    volatile uint8_t sink;
    int        rc;            /* 0=ok, 1=setup error, 2=audit fail */
    long       n_verified;
    long       n_true;
    long       n_false;
    long       expected_true;
    long       expected_false;
    int        correctness_audit_pass;
} worker_args_t;

/* ── Barrier ─────────────────────────────────────────────────────────────── */

static pthread_barrier_t g_barrier;

/* ── Time ────────────────────────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Stats ───────────────────────────────────────────────────────────────── */

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static double mean_f(const uint64_t *a, long n) {
    double s = 0; for (long i = 0; i < n; i++) s += (double)a[i];
    return s / n;
}

static double stddev_f(const uint64_t *a, long n, double m) {
    double s = 0; for (long i = 0; i < n; i++) { double d = (double)a[i] - m; s += d*d; }
    return sqrt(s / n);
}

static uint64_t pct_ns(const uint64_t *sorted, long n, double p) {
    if (n == 0) return 0;
    long idx = (long)(p / 100.0 * (double)n);
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

/* ── Hash for MIX_RANDOM (non-predictable, defeats branch predictor) ──── */

static uint32_t mix32(uint32_t a, uint32_t b) {
    a ^= b; a *= 0x9e3779b9u; a ^= a >> 16;
    return a;
}

/* Returns 1 if this iteration should use an invalid sig */
static int is_invalid_iter(const config_t *cfg, int thread_id, long iter) {
    switch (cfg->sig_mix) {
    case MIX_VALID:       return 0;
    case MIX_INVALID:     return 1;
    case MIX_ALTERNATING: return (int)(iter & 1);
    case MIX_RANDOM: {
        uint32_t h = mix32((uint32_t)thread_id * 1000003u, (uint32_t)iter);
        return (double)(h % 10000u) < cfg->random_p * 10000.0;
    }
    default: return 0;
    }
}

/* Expected counts given mix and total iterations */
static void expected_counts(const config_t *cfg, long iters,
                             long *exp_true, long *exp_false) {
    switch (cfg->sig_mix) {
    case MIX_VALID:       *exp_true = iters; *exp_false = 0; break;
    case MIX_INVALID:     *exp_true = 0;     *exp_false = iters; break;
    case MIX_ALTERNATING: *exp_false = iters / 2 + (iters & 1);
                          *exp_true  = iters - *exp_false; break;
    case MIX_RANDOM:
        /* Approximate; exact count computed post-loop */
        *exp_true = *exp_false = -1; break;
    default: *exp_true = iters; *exp_false = 0;
    }
}

/* ── Pin thread ──────────────────────────────────────────────────────────── */

static int pin_thread(int cpu) {
    if (cpu < 0) return 0;
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(cpu, &cs);
    return sched_setaffinity(0, sizeof(cs), &cs);
}

/* ── Core allocation helpers (mirrors bench_sign.c logic) ──────────────── */

static void assign_cpus_compact(config_t *cfg) {
    int n_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < cfg->n_threads; i++)
        cfg->cpu_ids[i] = i % n_cpus;
}

static void assign_cpus_spread(config_t *cfg) {
    int n_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int stride = (cfg->n_threads > 0) ? (n_cpus / cfg->n_threads) : 1;
    if (stride < 1) stride = 1;
    for (int i = 0; i < cfg->n_threads; i++)
        cfg->cpu_ids[i] = (i * stride) % n_cpus;
}

/* ── OQS verify wrapper ─────────────────────────────────────────────────── */

static int oqs_verify(OQS_SIG *sig,
                       const uint8_t *msg, size_t msg_len,
                       const uint8_t *sigbuf, size_t sig_len,
                       const uint8_t *pk) {
    return OQS_SIG_verify(sig, msg, msg_len, sigbuf, sig_len, pk) == OQS_SUCCESS;
}

/* ── Ed25519 verify via OpenSSL ─────────────────────────────────────────── */

static int ed25519_verify(const uint8_t *msg, size_t msg_len,
                           const uint8_t *sigbuf, size_t sig_len,
                           EVP_PKEY *pkey) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    int ok = (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1 &&
              EVP_DigestVerify(ctx, sigbuf, sig_len, msg, msg_len) == 1);
    EVP_MD_CTX_free(ctx);
    return ok;
}

/* ── Worker ─────────────────────────────────────────────────────────────── */

static void *worker(void *arg) {
    worker_args_t *wa  = (worker_args_t *)arg;
    config_t      *cfg = wa->cfg;
    int tid            = wa->thread_id;
    int run            = wa->run_id;

    wa->rc  = 0;
    wa->n_verified = 0;
    wa->n_true = 0;
    wa->n_false = 0;
    wa->correctness_audit_pass = 1;

    /* Pin thread */
    if (cfg->cpu_ids[tid] >= 0 && pin_thread(cfg->cpu_ids[tid]) != 0) {
        fprintf(stderr, "[t%d] pin to cpu %d failed\n", tid, cfg->cpu_ids[tid]);
        /* non-fatal: proceed unpinned */
    }

    /* -- Keygen (not timed) -- */
    uint8_t *pk = NULL, *sk = NULL;
    OQS_SIG *sig_ctx = NULL;
    EVP_PKEY *evp_pkey = NULL;      /* Ed25519 only */
    EVP_PKEY *evp_pkey_wrong = NULL;/* wrong-key variant */
    size_t pk_len = 0, sk_len = 0, sig_len_expected = 0;

    if (cfg->algo == ALGO_ED25519) {
        EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
        if (!kctx || EVP_PKEY_keygen_init(kctx) <= 0 ||
            EVP_PKEY_keygen(kctx, &evp_pkey) <= 0) {
            EVP_PKEY_CTX_free(kctx); wa->rc = 1; goto done;
        }
        EVP_PKEY_CTX_free(kctx);
        /* Wrong-key: a second independent keypair for INV_WRONG_KEY mode */
        EVP_PKEY_CTX *kctx2 = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
        if (!kctx2 || EVP_PKEY_keygen_init(kctx2) <= 0 ||
            EVP_PKEY_keygen(kctx2, &evp_pkey_wrong) <= 0) {
            EVP_PKEY_CTX_free(kctx2); wa->rc = 1; goto done;
        }
        EVP_PKEY_CTX_free(kctx2);
        sig_len_expected = 64;
        pk_len = 32;
    } else {
        const char *oqs_name = (cfg->algo == ALGO_FALCON512)
                                ? OQS_SIG_alg_falcon_512
                                : OQS_SIG_alg_ml_dsa_44;
        sig_ctx = OQS_SIG_new(oqs_name);
        if (!sig_ctx) { wa->rc = 1; goto done; }
        pk_len = sig_ctx->length_public_key;
        sk_len = sig_ctx->length_secret_key;
        sig_len_expected = sig_ctx->length_signature;
        pk = malloc(pk_len); sk = malloc(sk_len);
        if (!pk || !sk) { wa->rc = 1; goto done; }
        if (OQS_SIG_keypair(sig_ctx, pk, sk) != OQS_SUCCESS) {
            wa->rc = 1; goto done;
        }
    }

    /* -- Build message -- */
    size_t msg_len = (size_t)cfg->message_size;
    uint8_t *msg       = NULL;
    uint8_t *valid_sig = NULL;
    uint8_t *invalid_sig = NULL;
    uint8_t *invalid_pk  = NULL;

    msg = malloc(msg_len);
    if (!msg) { wa->rc = 1; goto done; }
    for (size_t i = 0; i < msg_len; i++) msg[i] = (uint8_t)((tid * 7 + i) & 0xff);

    /* -- Sign one valid signature (not timed) -- */
    valid_sig = malloc(sig_len_expected + 16);
    if (!valid_sig) { wa->rc = 1; goto done; }
    size_t actual_sig_len = sig_len_expected;

    if (cfg->algo == ALGO_ED25519) {
        EVP_MD_CTX *sctx = EVP_MD_CTX_new();
        if (!sctx) { wa->rc = 1; goto done; }
        size_t slen = sig_len_expected;
        if (EVP_DigestSignInit(sctx, NULL, NULL, NULL, evp_pkey) != 1 ||
            EVP_DigestSign(sctx, valid_sig, &slen, msg, msg_len) != 1) {
            EVP_MD_CTX_free(sctx); wa->rc = 1; goto done;
        }
        actual_sig_len = slen;
        EVP_MD_CTX_free(sctx);
    } else {
        if (OQS_SIG_sign(sig_ctx, valid_sig, &actual_sig_len, msg, msg_len, sk)
                != OQS_SUCCESS) {
            wa->rc = 1; goto done;
        }
    }

    /* -- Build invalid signature variants -- */
    invalid_sig = malloc(actual_sig_len + 16);
    if (!invalid_sig) { wa->rc = 1; goto done; }
    memcpy(invalid_sig, valid_sig, actual_sig_len);

    switch (cfg->invalid_mode) {
    case INV_FLIP_BIT:
        invalid_sig[actual_sig_len / 2] ^= 0x01;
        break;
    case INV_ZERO_SIG:
        memset(invalid_sig, 0, actual_sig_len);
        break;
    case INV_WRONG_KEY:
        /* We'll verify with a wrong public key; sig stays valid */
        if (cfg->algo != ALGO_ED25519) {
            /* Generate a different keypair for the wrong pk */
            invalid_pk = malloc(pk_len);
            uint8_t *tmp_sk = malloc(sk_len);
            if (!invalid_pk || !tmp_sk ||
                OQS_SIG_keypair(sig_ctx, invalid_pk, tmp_sk) != OQS_SUCCESS) {
                free(tmp_sk); wa->rc = 1; goto done;
            }
            free(tmp_sk);
        }
        break;
    case INV_GARBAGE:
        for (size_t i = 0; i < actual_sig_len; i++)
            invalid_sig[i] = (uint8_t)(mix32((uint32_t)tid, (uint32_t)i) & 0xff);
        break;
    }

    /* -- Correctness gate (must pass before timed region) -- */
    int gate_valid_result, gate_invalid_result;

    if (cfg->algo == ALGO_ED25519) {
        gate_valid_result = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len, evp_pkey);
        if (cfg->invalid_mode == INV_WRONG_KEY) {
            gate_invalid_result = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len,
                                                  evp_pkey_wrong);
        } else {
            gate_invalid_result = ed25519_verify(msg, msg_len, invalid_sig, actual_sig_len,
                                                  evp_pkey);
        }
    } else {
        gate_valid_result = oqs_verify(sig_ctx, msg, msg_len,
                                        valid_sig, actual_sig_len, pk);
        uint8_t *check_pk  = (cfg->invalid_mode == INV_WRONG_KEY && invalid_pk) ? invalid_pk : pk;
        uint8_t *check_sig = (cfg->invalid_mode == INV_WRONG_KEY) ? valid_sig : invalid_sig;
        gate_invalid_result = oqs_verify(sig_ctx, msg, msg_len,
                                          check_sig, actual_sig_len, check_pk);
    }

    if (!gate_valid_result) {
        fprintf(stderr, "[t%d run%d] ABORT: verify(valid_sig) returned FALSE — "
                "broken verifier or keygen mismatch\n", tid, run);
        wa->rc = 2; goto done;
    }
    if (gate_invalid_result) {
        fprintf(stderr, "[t%d run%d] ABORT: verify(invalid_sig) returned TRUE — "
                "verifier accepts garbage (the Harsha bug)\n", tid, run);
        wa->rc = 2; goto done;
    }

    /* -- Warmup (not timed, same mix as main loop) -- */
    for (long w = 0; w < cfg->warmup; w++) {
        int inv = is_invalid_iter(cfg, tid, w);
        int res;
        if (cfg->algo == ALGO_ED25519) {
            if (inv && cfg->invalid_mode == INV_WRONG_KEY)
                res = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len, evp_pkey_wrong);
            else if (inv)
                res = ed25519_verify(msg, msg_len, invalid_sig, actual_sig_len, evp_pkey);
            else
                res = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len, evp_pkey);
        } else {
            uint8_t *use_sig = inv ? invalid_sig : valid_sig;
            uint8_t *use_pk  = (inv && cfg->invalid_mode == INV_WRONG_KEY && invalid_pk)
                                ? invalid_pk : pk;
            if (inv && cfg->invalid_mode == INV_WRONG_KEY) use_sig = valid_sig;
            res = oqs_verify(sig_ctx, msg, msg_len, use_sig, actual_sig_len, use_pk);
        }
        wa->sink = (uint8_t)res;
    }

    /* -- Barrier: all threads ready -- */
    pthread_barrier_wait(&g_barrier);

    /* -- Timed loop -- */
    long expected_t, expected_f;
    expected_counts(cfg, cfg->iterations, &expected_t, &expected_f);

    for (long i = 0; i < cfg->iterations; i++) {
        int inv = is_invalid_iter(cfg, tid, i);
        wa->was_valid_arr[i] = (uint8_t)(!inv);

        uint64_t t0, t1;
        int res;

        asm volatile("" ::: "memory");
        t0 = now_ns();

        if (cfg->algo == ALGO_ED25519) {
            if (inv && cfg->invalid_mode == INV_WRONG_KEY)
                res = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len, evp_pkey_wrong);
            else if (inv)
                res = ed25519_verify(msg, msg_len, invalid_sig, actual_sig_len, evp_pkey);
            else
                res = ed25519_verify(msg, msg_len, valid_sig, actual_sig_len, evp_pkey);
        } else {
            uint8_t *use_sig = inv ? invalid_sig : valid_sig;
            uint8_t *use_pk  = pk;
            if (inv && cfg->invalid_mode == INV_WRONG_KEY) {
                use_sig = valid_sig;
                use_pk  = invalid_pk ? invalid_pk : pk;
            }
            res = oqs_verify(sig_ctx, msg, msg_len, use_sig, actual_sig_len, use_pk);
        }

        t1 = now_ns();
        asm volatile("" ::: "memory");

        wa->latency_ns[i] = t1 - t0;
        wa->result_arr[i] = (uint8_t)res;
        if (res) wa->n_true++; else wa->n_false++;
    }

    wa->n_verified = cfg->iterations;

    /* -- Post-loop correctness audit -- */
    if (cfg->sig_mix != MIX_RANDOM) {
        if (wa->n_true != expected_t || wa->n_false != expected_f) {
            fprintf(stderr, "[t%d run%d] AUDIT FAIL: expected true=%ld false=%ld "
                    "got true=%ld false=%ld\n",
                    tid, run, expected_t, expected_f, wa->n_true, wa->n_false);
            wa->correctness_audit_pass = 0;
        }
    } else {
        /* Random mode: just check all results are plausible (no all-TRUE with invalid sigs) */
        long inv_count = 0;
        for (long i = 0; i < cfg->iterations; i++)
            if (!wa->was_valid_arr[i]) inv_count++;
        if (inv_count > 0 && wa->n_false == 0) {
            fprintf(stderr, "[t%d run%d] AUDIT FAIL: sent %ld invalid sigs but got 0 FALSE\n",
                    tid, run, inv_count);
            wa->correctness_audit_pass = 0;
        }
    }

done:
    if (sig_ctx)        OQS_SIG_free(sig_ctx);
    if (evp_pkey)       EVP_PKEY_free(evp_pkey);
    if (evp_pkey_wrong) EVP_PKEY_free(evp_pkey_wrong);
    free(pk); free(sk); free(msg); free(valid_sig); free(invalid_sig); free(invalid_pk);
    return NULL;
}

/* ── Output writers ────────────────────────────────────────────────────── */

static char *capture(const char *cmd) {
    FILE *p = popen(cmd, "r");
    if (!p) return strdup("unknown");
    char buf[1024]; buf[0] = 0;
    if (fgets(buf, sizeof(buf), p)) {
        size_t l = strlen(buf);
        if (l > 0 && buf[l-1] == '\n') buf[l-1] = 0;
    }
    pclose(p);
    return strdup(buf);
}

static void write_latencies(const char *prefix, const config_t *cfg,
                             worker_args_t *wa, int run_id) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_latencies.csv", prefix);
    FILE *f = fopen(path, run_id == 0 ? "w" : "a");
    if (!f) return;
    if (run_id == 0)
        fprintf(f, "algo,threads,cores_str,run_id,thread_id,iteration,"
                   "signature_mix,was_valid,result,latency_ns\n");
    for (int t = 0; t < cfg->n_threads; t++) {
        if (wa[t].rc != 0) continue;
        /* Sample 1-in-10 to keep file sizes manageable */
        for (long i = 0; i < wa[t].n_verified; i += 10) {
            fprintf(f, "%s,%d,\"%s\",%d,%d,%ld,%s,%d,%d,%"PRIu64"\n",
                    cfg->algo_str, cfg->n_threads, cfg->cores_str, run_id, t, i,
                    cfg->sig_mix_str,
                    (int)wa[t].was_valid_arr[i],
                    (int)wa[t].result_arr[i],
                    wa[t].latency_ns[i]);
        }
    }
    fclose(f);
}

static void write_summary(const char *prefix, const config_t *cfg,
                           worker_args_t *wa, int run_id, uint64_t wall_ns) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_summary.csv", prefix);
    FILE *f = fopen(path, run_id == 0 ? "w" : "a");
    if (!f) return;

    /* Collect all latencies, split by valid/invalid */
    long total = 0, n_valid_lats = 0, n_invalid_lats = 0;
    for (int t = 0; t < cfg->n_threads; t++)
        if (wa[t].rc == 0) total += wa[t].n_verified;

    uint64_t *all     = malloc(sizeof(uint64_t) * (total > 0 ? total : 1));
    uint64_t *valid_l = malloc(sizeof(uint64_t) * (total > 0 ? total : 1));
    uint64_t *inv_l   = malloc(sizeof(uint64_t) * (total > 0 ? total : 1));
    long total_true = 0, total_false = 0;
    int audit_pass = 1;
    long pos = 0;

    for (int t = 0; t < cfg->n_threads; t++) {
        if (wa[t].rc != 0) continue;
        total_true  += wa[t].n_true;
        total_false += wa[t].n_false;
        if (!wa[t].correctness_audit_pass) audit_pass = 0;
        for (long i = 0; i < wa[t].n_verified; i++) {
            all[pos++] = wa[t].latency_ns[i];
            if (wa[t].was_valid_arr[i]) valid_l[n_valid_lats++] = wa[t].latency_ns[i];
            else                        inv_l[n_invalid_lats++]  = wa[t].latency_ns[i];
        }
    }
    qsort(all,     total,        sizeof(uint64_t), cmp_u64);
    qsort(valid_l, n_valid_lats, sizeof(uint64_t), cmp_u64);
    qsort(inv_l,   n_invalid_lats, sizeof(uint64_t), cmp_u64);

    double ops  = wall_ns > 0 ? (double)total / ((double)wall_ns / 1e9) : 0;
    double mv   = n_valid_lats   > 0 ? mean_f(valid_l, n_valid_lats)   : 0;
    double mi   = n_invalid_lats > 0 ? mean_f(inv_l,   n_invalid_lats) : 0;

    if (run_id == 0)
        fprintf(f, "algo,threads,cores_str,pin_strategy,signature_mix,run_id,"
                   "total_verifies,valid_verifies,invalid_verifies,wall_clock_ns,"
                   "ops_per_sec_total,ops_per_sec_per_thread,"
                   "mean_ns_valid,mean_ns_invalid,"
                   "median_ns_valid,median_ns_invalid,"
                   "p95_ns_valid,p99_ns_valid,p99_9_ns_valid,max_ns_valid,"
                   "p95_ns_invalid,p99_ns_invalid,p99_9_ns_invalid,max_ns_invalid,"
                   "stddev_ns_total,"
                   "speedup_vs_1thread,parallel_efficiency_pct,"
                   "correctness_audit_pass\n");

    double sd = total > 0 ? stddev_f(all, total, mean_f(all, total)) : 0;

    fprintf(f, "%s,%d,\"%s\",%s,%s,%d,"
               "%ld,%ld,%ld,%"PRIu64","
               "%.2f,%.2f,"
               "%.1f,%.1f,"
               "%"PRIu64",%"PRIu64","
               "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
               "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
               "%.1f,"
               "-1,-1,"
               "%d\n",
            cfg->algo_str, cfg->n_threads, cfg->cores_str, cfg->pin_strategy_str,
            cfg->sig_mix_str, run_id,
            total, total_true, total_false, (uint64_t)wall_ns,
            ops, ops / cfg->n_threads,
            mv, mi,
            pct_ns(valid_l, n_valid_lats, 50.0),
            pct_ns(inv_l,   n_invalid_lats, 50.0),
            pct_ns(valid_l, n_valid_lats, 95.0),
            pct_ns(valid_l, n_valid_lats, 99.0),
            pct_ns(valid_l, n_valid_lats, 99.9),
            n_valid_lats > 0 ? valid_l[n_valid_lats-1] : (uint64_t)0,
            pct_ns(inv_l, n_invalid_lats, 95.0),
            pct_ns(inv_l, n_invalid_lats, 99.0),
            pct_ns(inv_l, n_invalid_lats, 99.9),
            n_invalid_lats > 0 ? inv_l[n_invalid_lats-1] : (uint64_t)0,
            sd, audit_pass);

    fclose(f); free(all); free(valid_l); free(inv_l);
}

static void write_meta(const char *prefix, const config_t *cfg,
                        uint64_t t_start, uint64_t t_end) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_meta.json", prefix);
    FILE *f = fopen(path, "w"); if (!f) return;

    char *cpu    = capture("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs");
    char *kernel = capture("uname -r");
    char *gov0   = capture("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null");
    char *turbo  = capture("cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null");
    char *thp    = capture("cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null");

    fprintf(f, "{\n"
               "  \"cpu_model\": \"%s\",\n"
               "  \"kernel\": \"%s\",\n"
               "  \"governor_cpu0\": \"%s\",\n"
               "  \"no_turbo\": \"%s\",\n"
               "  \"thp\": \"%s\",\n"
               "  \"cli\": {\n"
               "    \"algo\": \"%s\",\n"
               "    \"threads\": %d,\n"
               "    \"cores_str\": \"%s\",\n"
               "    \"pin_strategy\": \"%s\",\n"
               "    \"iterations\": %ld,\n"
               "    \"warmup\": %ld,\n"
               "    \"message_size\": %d,\n"
               "    \"signature_mix\": \"%s\",\n"
               "    \"invalid_mode\": \"%s\",\n"
               "    \"batch_size\": %d,\n"
               "    \"tag\": \"%s\"\n"
               "  },\n"
               "  \"t_start_ns\": %"PRIu64",\n"
               "  \"t_end_ns\": %"PRIu64"\n"
               "}\n",
            cpu, kernel, gov0, turbo, thp,
            cfg->algo_str, cfg->n_threads, cfg->cores_str, cfg->pin_strategy_str,
            cfg->iterations, cfg->warmup, cfg->message_size,
            cfg->sig_mix_str, cfg->invalid_mode_str, cfg->batch_size,
            cfg->tag, t_start, t_end);

    fclose(f);
    free(cpu); free(kernel); free(gov0); free(turbo); free(thp);
}

/* ── Argument parsing ──────────────────────────────────────────────────── */

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s --algo <ed25519|falcon512|dilithium2> --threads N "
        "--output-prefix PATH [options]\n\n"
        "Options:\n"
        "  --algo            ed25519 | falcon512 | dilithium2\n"
        "  --threads N       number of software threads\n"
        "  --cores LIST      CPU affinity list: '0,2,4' or '0-7'\n"
        "  --iterations N    verify operations per thread    [default 100000]\n"
        "  --warmup N        warmup iterations per thread    [default 1000]\n"
        "  --runs N          full repetitions                [default 5]\n"
        "  --message-size B  message size in bytes           [default 32]\n"
        "  --pin-strategy    compact | spread | none         [default compact]\n"
        "  --output-prefix   required output path prefix\n"
        "  --tag             free-text label\n"
        "  --signature-mix   valid|invalid|alternating|random:P  [default valid]\n"
        "  --invalid-mode    flip-bit|zero-sig|wrong-key|garbage [default flip-bit]\n"
        "  --batch-size N    reserved; must be 1             [default 1]\n",
        argv0);
}

static int parse_args(int argc, char **argv, config_t *cfg) {
    static struct option opts[] = {
        {"algo",           required_argument, 0, 'a'},
        {"threads",        required_argument, 0, 'T'},
        {"cores",          required_argument, 0, 'c'},
        {"iterations",     required_argument, 0, 'i'},
        {"warmup",         required_argument, 0, 'w'},
        {"runs",           required_argument, 0, 'r'},
        {"message-size",   required_argument, 0, 'm'},
        {"pin-strategy",   required_argument, 0, 'p'},
        {"output-prefix",  required_argument, 0, 'O'},
        {"tag",            required_argument, 0, 't'},
        {"signature-mix",  required_argument, 0, 'S'},
        {"invalid-mode",   required_argument, 0, 'I'},
        {"batch-size",     required_argument, 0, 'B'},
        {0, 0, 0, 0}
    };

    /* Defaults */
    cfg->algo          = -1;
    cfg->n_threads     = 0;
    cfg->n_explicit_cpus = 0;
    cfg->iterations    = 100000;
    cfg->warmup        = 1000;
    cfg->runs          = 5;
    cfg->message_size  = 32;
    cfg->pin_strategy  = PIN_COMPACT;
    cfg->numa_node     = -1;
    cfg->output_prefix[0] = '\0';
    cfg->tag[0]        = '\0';
    cfg->sig_mix       = MIX_VALID;
    cfg->random_p      = 0.0;
    cfg->invalid_mode  = INV_FLIP_BIT;
    cfg->batch_size    = 1;
    strncpy(cfg->pin_strategy_str, "compact", sizeof(cfg->pin_strategy_str)-1);
    strncpy(cfg->sig_mix_str,      "valid",   sizeof(cfg->sig_mix_str)-1);
    strncpy(cfg->invalid_mode_str, "flip-bit",sizeof(cfg->invalid_mode_str)-1);

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (opt) {
        case 'a':
            if      (strcmp(optarg, "ed25519")    == 0) { cfg->algo = ALGO_ED25519;    strncpy(cfg->algo_str, "ed25519",    31); }
            else if (strcmp(optarg, "falcon512")  == 0) { cfg->algo = ALGO_FALCON512;  strncpy(cfg->algo_str, "falcon512",  31); }
            else if (strcmp(optarg, "dilithium2") == 0) { cfg->algo = ALGO_DILITHIUM2; strncpy(cfg->algo_str, "dilithium2", 31); }
            else { fprintf(stderr, "Unknown algo: %s\n", optarg); return 0; }
            break;
        case 'T': cfg->n_threads   = atoi(optarg); break;
        case 'i': cfg->iterations  = atol(optarg); break;
        case 'w': cfg->warmup      = atol(optarg); break;
        case 'r': cfg->runs        = atoi(optarg); break;
        case 'm': cfg->message_size = atoi(optarg); break;
        case 'O': strncpy(cfg->output_prefix, optarg, MAX_PATH_LEN-1); break;
        case 't': strncpy(cfg->tag, optarg, MAX_TAG_LEN-1); break;
        case 'B': cfg->batch_size = atoi(optarg); break;
        case 'p':
            if      (strcmp(optarg, "compact") == 0) { cfg->pin_strategy = PIN_COMPACT; strncpy(cfg->pin_strategy_str, "compact", 15); }
            else if (strcmp(optarg, "spread")  == 0) { cfg->pin_strategy = PIN_SPREAD;  strncpy(cfg->pin_strategy_str, "spread",  15); }
            else if (strcmp(optarg, "none")    == 0) { cfg->pin_strategy = PIN_NONE;    strncpy(cfg->pin_strategy_str, "none",    15); }
            else { fprintf(stderr, "Unknown pin-strategy: %s\n", optarg); return 0; }
            break;
        case 'c': {
            /* parse "0,2,4" or "0-7" into cpu_ids */
            strncpy(cfg->cores_str, optarg, MAX_CORES_STR-1);
            for (char *p = cfg->cores_str; *p; p++) if (*p == ',') *p = ';';
            char *tok, *s = strdup(optarg);
            tok = strtok(s, ",");
            while (tok && cfg->n_explicit_cpus < MAX_THREADS) {
                char *dash = strchr(tok, '-');
                if (dash) {
                    int lo = atoi(tok), hi = atoi(dash+1);
                    for (int c = lo; c <= hi && cfg->n_explicit_cpus < MAX_THREADS; c++)
                        cfg->cpu_ids[cfg->n_explicit_cpus++] = c;
                } else {
                    cfg->cpu_ids[cfg->n_explicit_cpus++] = atoi(tok);
                }
                tok = strtok(NULL, ",");
            }
            free(s);
            break;
        }
        case 'S':
            strncpy(cfg->sig_mix_str, optarg, sizeof(cfg->sig_mix_str)-1);
            if      (strcmp(optarg, "valid")       == 0) cfg->sig_mix = MIX_VALID;
            else if (strcmp(optarg, "invalid")     == 0) cfg->sig_mix = MIX_INVALID;
            else if (strcmp(optarg, "alternating") == 0) cfg->sig_mix = MIX_ALTERNATING;
            else if (strncmp(optarg, "random:", 7) == 0) {
                cfg->sig_mix = MIX_RANDOM;
                cfg->random_p = atof(optarg + 7);
                if (cfg->random_p < 0.0 || cfg->random_p > 1.0) {
                    fprintf(stderr, "random:P requires P in [0.0, 1.0]\n"); return 0;
                }
            } else { fprintf(stderr, "Unknown signature-mix: %s\n", optarg); return 0; }
            break;
        case 'I':
            strncpy(cfg->invalid_mode_str, optarg, sizeof(cfg->invalid_mode_str)-1);
            if      (strcmp(optarg, "flip-bit")  == 0) cfg->invalid_mode = INV_FLIP_BIT;
            else if (strcmp(optarg, "zero-sig")  == 0) cfg->invalid_mode = INV_ZERO_SIG;
            else if (strcmp(optarg, "wrong-key") == 0) cfg->invalid_mode = INV_WRONG_KEY;
            else if (strcmp(optarg, "garbage")   == 0) cfg->invalid_mode = INV_GARBAGE;
            else { fprintf(stderr, "Unknown invalid-mode: %s\n", optarg); return 0; }
            break;
        default:
            usage(argv[0]); return 0;
        }
    }

    if (cfg->algo < 0)         { fprintf(stderr, "--algo is required\n"); return 0; }
    if (cfg->n_threads <= 0)   { fprintf(stderr, "--threads is required and must be >0\n"); return 0; }
    if (cfg->output_prefix[0] == '\0') { fprintf(stderr, "--output-prefix is required\n"); return 0; }
    if (cfg->batch_size != 1)  { fprintf(stderr, "--batch-size must be 1 (batched verify not yet implemented)\n"); return 0; }

    /* Assign CPU IDs */
    if (cfg->n_explicit_cpus >= cfg->n_threads) {
        /* explicit --cores overrides pin-strategy */
        /* already stored in cfg->cpu_ids */
    } else {
        if (cfg->pin_strategy == PIN_COMPACT) assign_cpus_compact(cfg);
        else if (cfg->pin_strategy == PIN_SPREAD) assign_cpus_spread(cfg);
        else for (int i = 0; i < cfg->n_threads; i++) cfg->cpu_ids[i] = -1;
    }

    /* Build cores_str if not set via --cores */
    if (cfg->cores_str[0] == '\0') {
        char *p = cfg->cores_str; int rem = MAX_CORES_STR - 1;
        for (int i = 0; i < cfg->n_threads && rem > 0; i++) {
            int n = snprintf(p, rem, "%s%d", i ? ";" : "", cfg->cpu_ids[i]);
            p += n; rem -= n;
        }
    }

    return 1;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (!parse_args(argc, argv, &cfg)) { usage(argv[0]); return 1; }

    printf("bench_verify: algo=%s threads=%d pin=%s mix=%s iters=%ld warmup=%ld runs=%d\n",
           cfg.algo_str, cfg.n_threads, cfg.pin_strategy_str,
           cfg.sig_mix_str, cfg.iterations, cfg.warmup, cfg.runs);

    worker_args_t wa[MAX_THREADS];
    pthread_t     threads[MAX_THREADS];

    for (int run = 0; run < cfg.runs; run++) {
        printf("  run %d/%d ...", run+1, cfg.runs); fflush(stdout);

        /* Allocate per-thread buffers */
        int alloc_ok = 1;
        for (int t = 0; t < cfg.n_threads; t++) {
            wa[t].cfg          = &cfg;
            wa[t].thread_id    = t;
            wa[t].run_id       = run;
            wa[t].latency_ns   = malloc(sizeof(uint64_t) * (size_t)cfg.iterations);
            wa[t].result_arr   = malloc(sizeof(uint8_t)  * (size_t)cfg.iterations);
            wa[t].was_valid_arr= malloc(sizeof(uint8_t)  * (size_t)cfg.iterations);
            wa[t].sink         = 0;
            if (!wa[t].latency_ns || !wa[t].result_arr || !wa[t].was_valid_arr) {
                alloc_ok = 0; break;
            }
        }
        if (!alloc_ok) { fprintf(stderr, "OOM\n"); return 1; }

        pthread_barrier_init(&g_barrier, NULL, cfg.n_threads);

        for (int t = 0; t < cfg.n_threads; t++)
            pthread_create(&threads[t], NULL, worker, &wa[t]);

        uint64_t t_start = now_ns();
        /* Barrier inside worker; t_start here is a lower bound — main joins after all finish */
        for (int t = 0; t < cfg.n_threads; t++)
            pthread_join(threads[t], NULL);
        uint64_t t_end = now_ns();

        pthread_barrier_destroy(&g_barrier);

        /* Check for aborts */
        int any_fail = 0;
        for (int t = 0; t < cfg.n_threads; t++)
            if (wa[t].rc != 0) { any_fail = 1; break; }

        if (any_fail) {
            fprintf(stderr, "\nRun %d aborted due to correctness failure — stopping.\n", run);
            return 2;
        }

        long total_ops = 0;
        for (int t = 0; t < cfg.n_threads; t++) total_ops += wa[t].n_verified;
        uint64_t wall = t_end - t_start;
        double ops_sec = wall > 0 ? (double)total_ops / ((double)wall / 1e9) : 0;
        printf(" %.0f ops/s  (wall=%.3fs)\n", ops_sec, (double)wall / 1e9);

        write_latencies(cfg.output_prefix, &cfg, wa, run);
        write_summary(cfg.output_prefix, &cfg, wa, run, wall);
        if (run == 0) write_meta(cfg.output_prefix, &cfg, t_start, t_end);

        for (int t = 0; t < cfg.n_threads; t++) {
            free(wa[t].latency_ns);
            free(wa[t].result_arr);
            free(wa[t].was_valid_arr);
        }
    }

    printf("Done. Output: %s_{latencies,summary,meta}.*\n", cfg.output_prefix);
    return 0;
}
