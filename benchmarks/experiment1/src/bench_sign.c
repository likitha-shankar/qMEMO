/* bench_sign.c — Experiment 1: Signature Creation Micro-Benchmark
 *
 * Ed25519 (OpenSSL EVP) | Falcon-512 (liboqs) | ML-DSA-44/Dilithium2 (liboqs)
 *
 * Build:  see ../Makefile
 * Usage:  bench_sign --algo <ed25519|falcon512|dilithium2> \
 *                    --threads N --output-prefix PATH [options]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include <oqs/oqs.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define ALGO_ED25519    0
#define ALGO_FALCON512  1
#define ALGO_DILITHIUM2 2

#define PIN_COMPACT     0
#define PIN_SPREAD      1
#define PIN_NONE        2

#define MSG_FIXED       0
#define MSG_FRESH       1

#define CTX_FRESH       0   /* EVP_MD_CTX_new/free per iteration — matches qMEMO wallet */
#define CTX_REUSE       1   /* pre-allocated, reset per iteration — lower-bound cost */

#define MAX_THREADS     128
#define MAX_PATH_LEN    512
#define MAX_TAG_LEN     256
#define MAX_CORES_STR   512
#define ED25519_SIGLEN  64

/* ── Config struct (filled by parse_args, read-only by workers) ─────────── */

typedef struct {
    int    algo;
    const char *algo_str;
    int    n_threads;
    int    cpu_ids[MAX_THREADS];  /* -1 = no pinning for that slot */
    int    n_explicit_cpus;       /* len of cores given via --cores, 0 = derived */
    long   iterations;
    long   warmup;
    int    runs;
    int    message_size;
    int    message_mode;
    int    pin_strategy;
    int    numa_node;
    char   output_prefix[MAX_PATH_LEN];
    char   tag[MAX_TAG_LEN];
    int    verify_after_sign;
    int    ctx_mode;
    char   cores_str[MAX_CORES_STR];
    char   pin_strategy_str[16];
} config_t;

/* ── Per-thread in/out ──────────────────────────────────────────────────── */

typedef struct {
    config_t  *cfg;
    int        thread_id;
    int        run_id;
    uint64_t  *latency_ns;   /* pre-allocated [cfg->iterations] by main */
    volatile uint8_t sink;
    int        rc;           /* 0 = ok, 1 = setup error, 2 = verify fail */
    long       n_signed;
} worker_args_t;

/* ── Sampler state ──────────────────────────────────────────────────────── */

typedef struct {
    volatile int running;
    int          cpu;        /* -1 = unpinned */
    char         path[MAX_PATH_LEN];
} sampler_state_t;

/* ── Global barrier (n_threads + 1 participants: workers + main) ────────── */

static pthread_barrier_t g_barrier;

/* ═══════════════════════════════════════════════════════════════════════════
 * Utility helpers
 * ═════════════════════════════════════════════════════════════════════════*/

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* arr must be sorted ascending */
static uint64_t pct_ns(const uint64_t *arr, long n, double p) {
    if (n <= 0) return 0;
    long idx = (long)(p / 100.0 * (double)n);
    if (idx >= n) idx = n - 1;
    return arr[idx];
}

static double mean_f(const uint64_t *arr, long n) {
    double s = 0;
    for (long i = 0; i < n; i++) s += (double)arr[i];
    return s / (double)n;
}

static double stddev_f(const uint64_t *arr, long n, double m) {
    double s = 0;
    for (long i = 0; i < n; i++) { double d = (double)arr[i] - m; s += d * d; }
    return sqrt(s / (double)n);
}

/* Parse "0,2,4,6" or "0-7" or "0-3,8-11" into ids[]. Returns count. */
static int parse_cpu_list(const char *str, int *ids, int cap) {
    int n = 0;
    char buf[MAX_CORES_STR];
    snprintf(buf, sizeof(buf), "%s", str);
    for (char *tok = strtok(buf, ","); tok && n < cap; tok = strtok(NULL, ",")) {
        char *dash = strchr(tok, '-');
        if (dash) {
            int lo = atoi(tok), hi = atoi(dash + 1);
            for (int c = lo; c <= hi && n < cap; c++) ids[n++] = c;
        } else {
            ids[n++] = atoi(tok);
        }
    }
    return n;
}

/* Build cpu_ids[] from n_threads + pin_strategy when --cores not given. */
static void build_cpu_ids(config_t *cfg) {
    int n = cfg->n_threads;
    if (cfg->pin_strategy == PIN_NONE) {
        for (int i = 0; i < n; i++) cfg->cpu_ids[i] = -1;
        snprintf(cfg->cores_str, MAX_CORES_STR, "none");
        return;
    }
    int total = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (total < 1) total = 96;

    if (cfg->pin_strategy == PIN_COMPACT) {
        for (int i = 0; i < n; i++) cfg->cpu_ids[i] = i % total;
    } else { /* SPREAD: maximum physical separation */
        int stride = (total > n) ? total / n : 1;
        for (int i = 0; i < n; i++) cfg->cpu_ids[i] = (i * stride) % total;
    }

    /* Build cores_str */
    int pos = 0;
    for (int i = 0; i < n && pos < MAX_CORES_STR - 8; i++) {
        pos += snprintf(cfg->cores_str + pos, MAX_CORES_STR - pos,
                        i ? ",%d" : "%d", cfg->cpu_ids[i]);
    }
}

/* Map thread_id → cpu_id, wrapping when --cores has fewer entries than threads */
static int thread_cpu(const config_t *cfg, int tid) {
    if (cfg->n_explicit_cpus > 0)
        return cfg->cpu_ids[tid % cfg->n_explicit_cpus];
    return cfg->cpu_ids[tid];
}

static void pin_self(int cpu) {
    if (cpu < 0) return;
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(cpu, &cs);
    if (sched_setaffinity(0, sizeof(cs), &cs) != 0)
        fprintf(stderr, "sched_setaffinity(cpu=%d): %s\n", cpu, strerror(errno));
}

static int verify_pin(int cpu) {
    if (cpu < 0) return 1;
    cpu_set_t cs;
    if (sched_getaffinity(0, sizeof(cs), &cs) != 0) return 0;
    return CPU_ISSET(cpu, &cs);
}

/* Execute cmd, return first line (heap). Caller frees. */
static char *capture(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("unavailable");
    char buf[512] = {0};
    if (fgets(buf, sizeof(buf) - 1, fp)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }
    pclose(fp);
    return strdup(buf[0] ? buf : "unavailable");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * System verification (called before timing; refuses run on failure)
 * ═════════════════════════════════════════════════════════════════════════*/

static int verify_system(void) {
    int ok = 1;

    /* Governor */
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (f) {
        char gov[64] = {0};
        if (fgets(gov, sizeof(gov), f)) {
            gov[strcspn(gov, "\n")] = '\0';
            if (strcmp(gov, "performance") != 0) {
                fprintf(stderr, "VERIFY FAIL: governor='%s', need 'performance'.\n"
                        "  Fix: sudo cpupower frequency-set -g performance\n", gov);
                ok = 0;
            }
        }
        fclose(f);
    } else {
        fprintf(stderr, "VERIFY WARN: cannot read scaling_governor\n");
    }

    /* Turbo (warn only — affects reproducibility but not correctness) */
    f = fopen("/sys/devices/system/cpu/intel_pstate/no_turbo", "r");
    if (f) {
        char v[8] = {0};
        if (fgets(v, sizeof(v), f)) {
            v[strcspn(v, "\n")] = '\0';
            if (strcmp(v, "1") != 0)
                fprintf(stderr, "VERIFY WARN: turbo not disabled (no_turbo=%s). "
                        "Frequency may drift across thread counts.\n", v);
        }
        fclose(f);
    }

    /* THP */
    f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
    if (f) {
        char v[128] = {0};
        if (fgets(v, sizeof(v), f) && !strstr(v, "[never]"))
            fprintf(stderr, "VERIFY WARN: THP not 'never'. "
                    "Fix: echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled\n");
        fclose(f);
    }

    /* Swap */
    f = fopen("/proc/swaps", "r");
    if (f) {
        char hdr[256], line[256];
        if (fgets(hdr, sizeof(hdr), f) && fgets(line, sizeof(line), f))
            fprintf(stderr, "VERIFY WARN: swap is active: %s", line);
        fclose(f);
    }

    /* NMI watchdog */
    f = fopen("/proc/sys/kernel/nmi_watchdog", "r");
    if (f) {
        char v[8] = {0};
        if (fgets(v, sizeof(v), f)) {
            v[strcspn(v, "\n")] = '\0';
            if (strcmp(v, "0") != 0)
                fprintf(stderr, "VERIFY WARN: nmi_watchdog=%s (should be 0)\n", v);
        }
        fclose(f);
    }

    if (!ok) fprintf(stderr, "Run scripts/prepare_system.sh with sudo and retry.\n");
    return ok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sampler thread — 100 ms poll, pinned to a CPU outside the bench set
 * ═════════════════════════════════════════════════════════════════════════*/

static void *sampler_fn(void *arg) {
    sampler_state_t *st = (sampler_state_t *)arg;
    pin_self(st->cpu);

    FILE *out = fopen(st->path, "w");
    if (!out) { fprintf(stderr, "sampler: cannot open %s\n", st->path); return NULL; }
    fprintf(out, "timestamp_ns,cpu0_freq_khz,mem_rss_kb,"
                 "voluntary_ctxt_switches,nonvoluntary_ctxt_switches\n");

    while (st->running) {
        uint64_t ts = now_ns();
        long freq_khz = 0, rss_kb = 0, vol = 0, invol = 0;

        FILE *ff = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
        if (ff) { int _r = fscanf(ff, "%ld", &freq_khz); (void)_r; fclose(ff); }

        FILE *sf = fopen("/proc/self/status", "r");
        if (sf) {
            char ln[256];
            while (fgets(ln, sizeof(ln), sf)) {
                if      (strncmp(ln, "VmRSS:", 6) == 0)                    sscanf(ln + 6,  "%ld", &rss_kb);
                else if (strncmp(ln, "voluntary_ctxt_switches:", 24) == 0)  sscanf(ln + 24, "%ld", &vol);
                else if (strncmp(ln, "nonvoluntary_ctxt_switches:", 27) == 0) sscanf(ln + 27, "%ld", &invol);
            }
            fclose(sf);
        }

        fprintf(out, "%lu,%ld,%ld,%ld,%ld\n",
                (unsigned long)ts, freq_khz, rss_kb, vol, invol);
        fflush(out);
        usleep(100000);
    }
    fclose(out);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Inline sign helpers — called inside the timed loop; no malloc
 * ═════════════════════════════════════════════════════════════════════════*/

static inline int sign_ed25519(EVP_MD_CTX *ctx, EVP_PKEY *pkey,
                               const uint8_t *msg, size_t mlen,
                               uint8_t *sig, size_t *slen) {
    EVP_MD_CTX_reset(ctx);
    return EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) == 1 &&
           EVP_DigestSign(ctx, sig, slen, msg, mlen) == 1;
}

static inline int sign_oqs(OQS_SIG *sig_obj, const uint8_t *msg, size_t mlen,
                            uint8_t *sig, size_t *slen, const uint8_t *sk) {
    return OQS_SIG_sign(sig_obj, sig, slen, msg, mlen, sk) == OQS_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Worker thread
 * ═════════════════════════════════════════════════════════════════════════*/

static void *worker_fn(void *raw) {
    worker_args_t *wa  = (worker_args_t *)raw;
    config_t      *cfg = wa->cfg;
    wa->rc = 0; wa->n_signed = 0; wa->sink = 0;

    /* All heap pointers NULL so free() is safe regardless of where we exit */
    OQS_SIG    *oqs     = NULL;
    uint8_t    *pk      = NULL, *sk  = NULL;
    EVP_PKEY   *evp_key = NULL;
    EVP_MD_CTX *evp_ctx = NULL;
    uint8_t    *msg     = NULL, *sig_buf = NULL;
    size_t      sig_max = 0;
    int         ok      = 1;

    /* ── 1. Pin ── */
    int cpu = thread_cpu(cfg, wa->thread_id);
    pin_self(cpu);
    if (cpu >= 0 && !verify_pin(cpu)) {
        fprintf(stderr, "thread %d: pin verify failed for cpu %d\n", wa->thread_id, cpu);
        ok = 0; wa->rc = 1;
    }

    /* ── 2. Algorithm setup (NOT timed) ── */
    if (ok) {
        if (cfg->algo == ALGO_ED25519) {
            sig_max = ED25519_SIGLEN;
            EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
            if (!kctx || EVP_PKEY_keygen_init(kctx) <= 0 ||
                EVP_PKEY_keygen(kctx, &evp_key) <= 0) {
                fprintf(stderr, "thread %d: Ed25519 keygen failed\n", wa->thread_id);
                ok = 0; wa->rc = 1;
            }
            if (kctx) EVP_PKEY_CTX_free(kctx);
            if (ok && cfg->ctx_mode == CTX_REUSE) {
                evp_ctx = EVP_MD_CTX_new();
                if (!evp_ctx) { ok = 0; wa->rc = 1; }
            }
        } else {
            const char *alg = (cfg->algo == ALGO_FALCON512)
                              ? OQS_SIG_alg_falcon_512
                              : OQS_SIG_alg_ml_dsa_44;
            oqs = OQS_SIG_new(alg);
            if (!oqs) {
                fprintf(stderr, "thread %d: OQS_SIG_new(%s) failed\n", wa->thread_id, alg);
                ok = 0; wa->rc = 1;
            } else {
                sig_max = oqs->length_signature;
                pk = malloc(oqs->length_public_key);
                sk = malloc(oqs->length_secret_key);
                if (!pk || !sk || OQS_SIG_keypair(oqs, pk, sk) != OQS_SUCCESS) {
                    fprintf(stderr, "thread %d: keypair generation failed\n", wa->thread_id);
                    ok = 0; wa->rc = 1;
                }
            }
        }
    }

    /* ── 3. Pre-allocate message and signature buffers (NOT timed) ── */
    if (ok) {
        msg     = calloc((size_t)cfg->message_size, 1);
        sig_buf = malloc(sig_max > 0 ? sig_max : 1);
        if (!msg || !sig_buf) { ok = 0; wa->rc = 1; }
    }

    /* ── 4. Warmup (NOT timed) ── */
    if (ok) {
        for (long w = 0; w < cfg->warmup; w++) {
            size_t slen = sig_max;
            if (cfg->algo == ALGO_ED25519) {
                if (cfg->ctx_mode == CTX_FRESH) {
                    EVP_MD_CTX *wctx = EVP_MD_CTX_new();
                    EVP_DigestSignInit(wctx, NULL, NULL, NULL, evp_key);
                    EVP_DigestSign(wctx, sig_buf, &slen, msg, (size_t)cfg->message_size);
                    EVP_MD_CTX_free(wctx);
                } else {
                    sign_ed25519(evp_ctx, evp_key, msg, (size_t)cfg->message_size, sig_buf, &slen);
                }
            } else {
                sign_oqs(oqs, msg, (size_t)cfg->message_size, sig_buf, &slen, sk);
            }
        }

        /* Sanity verify on one warmup signature */
        if (cfg->verify_after_sign) {
            size_t slen = sig_max; int vok = 0;
            if (cfg->algo == ALGO_ED25519) {
                if (cfg->ctx_mode == CTX_FRESH) {
                    EVP_MD_CTX *vwctx = EVP_MD_CTX_new();
                    EVP_DigestSignInit(vwctx, NULL, NULL, NULL, evp_key);
                    EVP_DigestSign(vwctx, sig_buf, &slen, msg, (size_t)cfg->message_size);
                    EVP_MD_CTX_free(vwctx);
                } else {
                    sign_ed25519(evp_ctx, evp_key, msg, (size_t)cfg->message_size, sig_buf, &slen);
                }
                EVP_MD_CTX *vctx = EVP_MD_CTX_new();
                vok = vctx &&
                      EVP_DigestVerifyInit(vctx, NULL, NULL, NULL, evp_key) == 1 &&
                      EVP_DigestVerify(vctx, sig_buf, slen, msg, (size_t)cfg->message_size) == 1;
                if (vctx) EVP_MD_CTX_free(vctx);
            } else {
                sign_oqs(oqs, msg, (size_t)cfg->message_size, sig_buf, &slen, sk);
                vok = OQS_SIG_verify(oqs, msg, (size_t)cfg->message_size, sig_buf, slen, pk) == OQS_SUCCESS;
            }
            if (!vok) {
                fprintf(stderr, "thread %d: SANITY VERIFY FAILED — algo=%s\n",
                        wa->thread_id, cfg->algo_str);
                ok = 0; wa->rc = 2;
            }
        }
    }

    /* ── 5. Barrier: all threads synchronize before timed region ── */
    pthread_barrier_wait(&g_barrier);

    /* ── 6. Timed region ── */
    if (ok && wa->rc == 0) {
        struct timespec ts0, ts1;
        volatile uint8_t vsink = 0;

        for (long i = 0; i < cfg->iterations; i++) {
            if (cfg->message_mode == MSG_FRESH) {
                uint64_t ctr = (uint64_t)i;
                memcpy(msg, &ctr, sizeof(ctr));
            }
            size_t slen = sig_max;

            asm volatile("" ::: "memory");
            clock_gettime(CLOCK_MONOTONIC, &ts0);

            if (cfg->algo == ALGO_ED25519) {
                if (cfg->ctx_mode == CTX_FRESH) {
                    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
                    EVP_DigestSignInit(ctx, NULL, NULL, NULL, evp_key);
                    EVP_DigestSign(ctx, sig_buf, &slen, msg, (size_t)cfg->message_size);
                    EVP_MD_CTX_free(ctx);
                } else {
                    sign_ed25519(evp_ctx, evp_key, msg, (size_t)cfg->message_size,
                                 sig_buf, &slen);
                }
            } else {
                sign_oqs(oqs, msg, (size_t)cfg->message_size, sig_buf, &slen, sk);
            }

            clock_gettime(CLOCK_MONOTONIC, &ts1);
            asm volatile("" ::: "memory");

            int64_t diff = ((int64_t)ts1.tv_sec  - (int64_t)ts0.tv_sec)  * 1000000000LL
                         + ((int64_t)ts1.tv_nsec - (int64_t)ts0.tv_nsec);
            wa->latency_ns[i] = (diff > 0) ? (uint64_t)diff : 0ULL;
            vsink ^= sig_buf[0];
        }
        wa->n_signed = cfg->iterations;
        wa->sink     = vsink;
    }

    /* ── 7. Post-timed RNG cleanup for Falcon ── */
    if (cfg->algo == ALGO_FALCON512) OQS_thread_stop();

    /* ── 8. Cleanup ── */
    free(msg); free(sig_buf); free(pk); free(sk);
    if (oqs)     OQS_SIG_free(oqs);
    if (evp_ctx) EVP_MD_CTX_free(evp_ctx);
    if (evp_key) EVP_PKEY_free(evp_key);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Output writers
 * ═════════════════════════════════════════════════════════════════════════*/

static void write_latencies(const char *prefix, const config_t *cfg,
                            worker_args_t *wa, int run_id) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_latencies.csv", prefix);

    /* Append if file already exists (multiple runs) */
    FILE *f = fopen(path, run_id == 0 ? "w" : "a");
    if (!f) { fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno)); return; }

    /* 8 MB write buffer */
    char *wbuf = malloc(8 * 1024 * 1024);
    if (wbuf) setvbuf(f, wbuf, _IOFBF, 8 * 1024 * 1024);

    if (run_id == 0)
        fprintf(f, "algo,threads,cores_str,run_id,thread_id,iteration,latency_ns\n");

    for (int t = 0; t < cfg->n_threads; t++) {
        if (wa[t].rc != 0) continue;
        for (long i = 0; i < wa[t].n_signed; i++) {
            fprintf(f, "%s,%d,\"%s\",%d,%d,%ld,%lu\n",
                    cfg->algo_str, cfg->n_threads, cfg->cores_str,
                    run_id, t, i, (unsigned long)wa[t].latency_ns[i]);
        }
    }
    fclose(f);
    free(wbuf);
}

static void write_summary_row(const char *prefix, const config_t *cfg,
                              worker_args_t *wa, int run_id,
                              uint64_t wall_ns) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_summary.csv", prefix);

    FILE *f = fopen(path, run_id == 0 ? "w" : "a");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }

    /* Collect all latencies from all successful threads */
    long total = 0;
    for (int t = 0; t < cfg->n_threads; t++)
        if (wa[t].rc == 0) total += wa[t].n_signed;

    uint64_t *all = malloc(sizeof(uint64_t) * (total > 0 ? total : 1));
    long pos = 0;
    for (int t = 0; t < cfg->n_threads; t++) {
        if (wa[t].rc != 0) continue;
        memcpy(all + pos, wa[t].latency_ns, sizeof(uint64_t) * wa[t].n_signed);
        pos += wa[t].n_signed;
    }
    qsort(all, total, sizeof(uint64_t), cmp_u64);

    double m   = total > 0 ? mean_f(all, total) : 0;
    double sd  = total > 0 ? stddev_f(all, total, m) : 0;
    double ops = wall_ns > 0 ? (double)total / ((double)wall_ns / 1e9) : 0;

    const char *ctx_mode_str = (cfg->ctx_mode == 1) ? "reuse" : "fresh";  /* CTX_REUSE=1 */

    if (run_id == 0)
        fprintf(f, "algo,threads,cores_str,pin_strategy,ctx_mode,run_id,"
                   "total_signatures,wall_clock_ns,"
                   "ops_per_sec_total,ops_per_sec_per_thread,"
                   "mean_ns,median_ns,p95_ns,p99_ns,p99_9_ns,max_ns,stddev_ns,"
                   "speedup_vs_1thread,parallel_efficiency_pct\n");

    fprintf(f, "%s,%d,\"%s\",%s,%s,%d,"
               "%ld,%lu,"
               "%.2f,%.2f,"
               "%.1f,%lu,%lu,%lu,%lu,%lu,%.1f,"
               "-1,-1\n",
            cfg->algo_str, cfg->n_threads, cfg->cores_str, cfg->pin_strategy_str,
            ctx_mode_str, run_id,
            total, (unsigned long)wall_ns,
            ops, ops / cfg->n_threads,
            m,
            pct_ns(all, total, 50.0),
            pct_ns(all, total, 95.0),
            pct_ns(all, total, 99.0),
            pct_ns(all, total, 99.9),
            total > 0 ? all[total - 1] : (uint64_t)0,
            sd);

    fclose(f);
    free(all);
}

static void write_meta(const char *prefix, const config_t *cfg,
                       uint64_t t_start_ns, uint64_t t_end_ns) {
    char path[MAX_PATH_LEN + 32];
    snprintf(path, sizeof(path), "%s_meta.json", prefix);

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }

    char *cpu_model = capture("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs");
    char *kernel    = capture("uname -r");
    char *lscpu_s   = capture("lscpu | tr '\\n' '|'");
    char *compiler  = capture("cc --version | head -1");
    char *numactl   = capture("numactl --hardware 2>/dev/null | tr '\\n' '|'");
    char *microcode = capture("grep -m1 'microcode' /proc/cpuinfo | cut -d: -f2 | xargs");
    char *oqs_ver   = capture("pkg-config --modversion liboqs 2>/dev/null || echo unknown");
    char *gov0      = capture("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown");
    char *no_turbo  = capture("cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo unknown");
    char *thp       = capture("cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo unknown");
    char *free_mem  = capture("free -m | awk 'NR==2{print $7}'");
    char *temps     = capture("paste /sys/class/thermal/thermal_zone*/temp 2>/dev/null | tr '\\t' ','");

    /* Escape strings for JSON — minimal: replace " with ' */
    /* (Full JSON escaping omitted for brevity; fields are system strings) */

    fprintf(f, "{\n"
               "  \"cpu_model\": \"%s\",\n"
               "  \"kernel\": \"%s\",\n"
               "  \"microcode\": \"%s\",\n"
               "  \"compiler\": \"%s\",\n"
               "  \"openssl_version\": \"%s\",\n"
               "  \"oqs_version\": \"%s\",\n"
               "  \"n_logical_cpus\": %ld,\n"
               "  \"governor_cpu0\": \"%s\",\n"
               "  \"no_turbo\": \"%s\",\n"
               "  \"thp\": \"%s\",\n"
               "  \"free_mem_mb\": \"%s\",\n"
               "  \"temperatures_mc\": \"%s\",\n"
               "  \"lscpu\": \"%s\",\n"
               "  \"numactl\": \"%s\",\n"
               "  \"cli\": {\n"
               "    \"algo\": \"%s\",\n"
               "    \"threads\": %d,\n"
               "    \"cores_str\": \"%s\",\n"
               "    \"pin_strategy\": \"%s\",\n"
               "    \"iterations\": %ld,\n"
               "    \"warmup\": %ld,\n"
               "    \"runs\": %d,\n"
               "    \"message_size\": %d,\n"
               "    \"message_mode\": \"%s\",\n"
               "    \"ctx_mode\": \"%s\",\n"
               "    \"verify_after_sign\": %d,\n"
               "    \"numa_node\": %d,\n"
               "    \"tag\": \"%s\"\n"
               "  },\n"
               "  \"t_start_ns\": %lu,\n"
               "  \"t_end_ns\": %lu\n"
               "}\n",
            cpu_model, kernel, microcode, compiler,
            OPENSSL_VERSION_TEXT, oqs_ver,
            sysconf(_SC_NPROCESSORS_ONLN),
            gov0, no_turbo, thp, free_mem, temps, lscpu_s, numactl,
            cfg->algo_str, cfg->n_threads, cfg->cores_str,
            cfg->pin_strategy_str, cfg->iterations, cfg->warmup,
            cfg->runs, cfg->message_size,
            cfg->message_mode == MSG_FRESH ? "fresh" : "fixed",
            cfg->ctx_mode == CTX_FRESH ? "fresh" : "reuse",
            cfg->verify_after_sign, cfg->numa_node, cfg->tag,
            (unsigned long)t_start_ns, (unsigned long)t_end_ns);

    fclose(f);
    free(cpu_model); free(kernel);  free(lscpu_s);  free(compiler);
    free(numactl);   free(microcode); free(oqs_ver); free(gov0);
    free(no_turbo);  free(thp);     free(free_mem); free(temps);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Argument parsing
 * ═════════════════════════════════════════════════════════════════════════*/

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --algo <ed25519|falcon512|dilithium2> --threads N "
        "--output-prefix PATH [options]\n\n"
        "Options:\n"
        "  --algo            ed25519 | falcon512 | dilithium2\n"
        "  --threads N       number of software threads\n"
        "  --cores LIST      CPU affinity list: '0,2,4' or '0-7' (overrides pin-strategy)\n"
        "  --iterations N    sign operations per thread per run    [default 100000]\n"
        "  --warmup N        warmup iterations per thread          [default 1000]\n"
        "  --runs N          full repetitions                      [default 5]\n"
        "  --message-size B  message size in bytes                 [default 32]\n"
        "  --message-mode    fixed | fresh                         [default fresh]\n"
        "  --ctx-mode        fresh | reuse  (Ed25519 only)         [default fresh]\n"
        "  --pin-strategy    compact | spread | none               [default compact]\n"
        "  --numa-node N     restrict to NUMA node                 [default -1]\n"
        "  --output-prefix   required output path prefix\n"
        "  --tag             free-text label\n"
        "  --verify-after-sign 0|1  sanity check at warmup         [default 1]\n",
        prog);
}

static int parse_args(int argc, char **argv, config_t *cfg) {
    /* Defaults */
    cfg->algo            = -1;
    cfg->algo_str        = NULL;
    cfg->n_threads       = -1;
    cfg->n_explicit_cpus = 0;
    cfg->iterations      = 100000;
    cfg->warmup          = 1000;
    cfg->runs            = 5;
    cfg->message_size    = 32;
    cfg->message_mode    = MSG_FRESH;
    cfg->ctx_mode        = CTX_FRESH;
    cfg->pin_strategy    = PIN_COMPACT;
    cfg->numa_node       = -1;
    cfg->output_prefix[0] = '\0';
    cfg->tag[0]          = '\0';
    cfg->verify_after_sign = 1;
    cfg->cores_str[0]    = '\0';
    snprintf(cfg->pin_strategy_str, sizeof(cfg->pin_strategy_str), "compact");

    static struct option opts[] = {
        {"algo",               required_argument, 0, 'A'},
        {"threads",            required_argument, 0, 'T'},
        {"cores",              required_argument, 0, 'C'},
        {"iterations",         required_argument, 0, 'I'},
        {"warmup",             required_argument, 0, 'W'},
        {"runs",               required_argument, 0, 'R'},
        {"message-size",       required_argument, 0, 'S'},
        {"message-mode",       required_argument, 0, 'M'},
        {"pin-strategy",       required_argument, 0, 'P'},
        {"numa-node",          required_argument, 0, 'N'},
        {"output-prefix",      required_argument, 0, 'O'},
        {"tag",                required_argument, 0, 'G'},
        {"verify-after-sign",  required_argument, 0, 'V'},
        {"ctx-mode",           required_argument, 0, 'X'},
        {0, 0, 0, 0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (c) {
        case 'A':
            if      (strcmp(optarg, "ed25519")    == 0) { cfg->algo = ALGO_ED25519;    cfg->algo_str = "ed25519"; }
            else if (strcmp(optarg, "falcon512")  == 0) { cfg->algo = ALGO_FALCON512;  cfg->algo_str = "falcon512"; }
            else if (strcmp(optarg, "dilithium2") == 0) { cfg->algo = ALGO_DILITHIUM2; cfg->algo_str = "dilithium2"; }
            else { fprintf(stderr, "Unknown algo: %s\n", optarg); return 0; }
            break;
        case 'T':
            cfg->n_threads = atoi(optarg);
            if (cfg->n_threads < 1 || cfg->n_threads > MAX_THREADS) {
                fprintf(stderr, "--threads must be 1..%d\n", MAX_THREADS); return 0;
            }
            break;
        case 'C': {
            int ids[MAX_THREADS];
            int nc = parse_cpu_list(optarg, ids, MAX_THREADS);
            if (nc == 0) { fprintf(stderr, "--cores parse failed: %s\n", optarg); return 0; }
            cfg->n_explicit_cpus = nc;
            memcpy(cfg->cpu_ids, ids, sizeof(int) * nc);
            snprintf(cfg->cores_str, MAX_CORES_STR, "%s", optarg);
            break;
        }
        case 'I': cfg->iterations   = atol(optarg); break;
        case 'W': cfg->warmup       = atol(optarg); break;
        case 'R': cfg->runs         = atoi(optarg); break;
        case 'S': cfg->message_size = atoi(optarg); break;
        case 'M':
            if      (strcmp(optarg, "fresh") == 0) cfg->message_mode = MSG_FRESH;
            else if (strcmp(optarg, "fixed") == 0) cfg->message_mode = MSG_FIXED;
            else { fprintf(stderr, "Unknown message-mode: %s\n", optarg); return 0; }
            break;
        case 'P':
            if      (strcmp(optarg, "compact") == 0) { cfg->pin_strategy = PIN_COMPACT; snprintf(cfg->pin_strategy_str, 16, "compact"); }
            else if (strcmp(optarg, "spread")  == 0) { cfg->pin_strategy = PIN_SPREAD;  snprintf(cfg->pin_strategy_str, 16, "spread"); }
            else if (strcmp(optarg, "none")    == 0) { cfg->pin_strategy = PIN_NONE;    snprintf(cfg->pin_strategy_str, 16, "none"); }
            else { fprintf(stderr, "Unknown pin-strategy: %s\n", optarg); return 0; }
            break;
        case 'N': cfg->numa_node = atoi(optarg); break;
        case 'O': snprintf(cfg->output_prefix, MAX_PATH_LEN, "%s", optarg); break;
        case 'G': snprintf(cfg->tag, MAX_TAG_LEN, "%s", optarg); break;
        case 'V': cfg->verify_after_sign = atoi(optarg); break;
        case 'X':
            if      (strcmp(optarg, "fresh") == 0) cfg->ctx_mode = CTX_FRESH;
            else if (strcmp(optarg, "reuse") == 0) cfg->ctx_mode = CTX_REUSE;
            else { fprintf(stderr, "Unknown ctx-mode: %s\n", optarg); return 0; }
            break;
        default:  usage(argv[0]); return 0;
        }
    }

    /* Validate required */
    if (cfg->algo == -1)      { fprintf(stderr, "--algo is required\n"); return 0; }
    if (cfg->n_threads == -1) { fprintf(stderr, "--threads is required\n"); return 0; }
    if (cfg->output_prefix[0] == '\0') { fprintf(stderr, "--output-prefix is required\n"); return 0; }

    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═════════════════════════════════════════════════════════════════════════*/

int main(int argc, char **argv) {
    config_t cfg;
    if (!parse_args(argc, argv, &cfg)) return 1;

    /* Build cpu_ids if not given via --cores */
    if (cfg.n_explicit_cpus == 0) {
        build_cpu_ids(&cfg);
    } else {
        /* --cores given: wrap threads across the explicit CPU list */
        /* cores_str already set; fill full cpu_ids[] for the worker indexing */
        for (int t = 0; t < cfg.n_threads; t++)
            cfg.cpu_ids[t] = cfg.cpu_ids[t % cfg.n_explicit_cpus];
        /* pin_strategy_str: note explicit override */
        snprintf(cfg.pin_strategy_str, sizeof(cfg.pin_strategy_str), "explicit");
    }

    /* System verification */
    if (!verify_system()) {
        fprintf(stderr, "System verification failed — aborting.\n");
        return 1;
    }

    /* Find a sampler CPU outside the benchmark's pinned set */
    int sampler_cpu = -1;
    {
        int used[MAX_THREADS]; int nu = 0;
        for (int t = 0; t < cfg.n_threads; t++) {
            int c = cfg.cpu_ids[t];
            if (c >= 0) used[nu++] = c;
        }
        int total_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
        for (int c = 0; c < total_cpus && sampler_cpu < 0; c++) {
            int taken = 0;
            for (int j = 0; j < nu; j++) if (used[j] == c) { taken = 1; break; }
            if (!taken) sampler_cpu = c;
        }
    }

    printf("bench_sign: algo=%s threads=%d pin=%s runs=%d iters=%ld warmup=%ld\n",
           cfg.algo_str, cfg.n_threads, cfg.pin_strategy_str,
           cfg.runs, cfg.iterations, cfg.warmup);
    printf("  output prefix: %s\n", cfg.output_prefix);
    if (sampler_cpu >= 0)
        printf("  sampler CPU:   %d\n", sampler_cpu);
    else
        printf("  sampler CPU:   none (all CPUs used; frequency sampling disabled)\n");
    fflush(stdout);

    /* Allocate per-thread latency arrays for one run at a time */
    uint64_t **lat = malloc(sizeof(uint64_t *) * cfg.n_threads);
    for (int t = 0; t < cfg.n_threads; t++) {
        lat[t] = malloc(sizeof(uint64_t) * (size_t)cfg.iterations);
        if (!lat[t]) { fprintf(stderr, "OOM allocating latency array\n"); return 1; }
    }

    worker_args_t *wa     = calloc(sizeof(worker_args_t), (size_t)cfg.n_threads);
    pthread_t     *threads = malloc(sizeof(pthread_t) * (size_t)cfg.n_threads);
    if (!wa || !threads) { fprintf(stderr, "OOM\n"); return 1; }

    /* Barrier: n_threads workers + 1 main */
    pthread_barrier_init(&g_barrier, NULL, (unsigned)(cfg.n_threads + 1));

    uint64_t t_first_run_start = 0, t_last_run_end = 0;

    for (int run = 0; run < cfg.runs; run++) {
        printf("  run %d/%d ...", run + 1, cfg.runs); fflush(stdout);

        /* Set up worker args */
        for (int t = 0; t < cfg.n_threads; t++) {
            wa[t].cfg        = &cfg;
            wa[t].thread_id  = t;
            wa[t].run_id     = run;
            wa[t].latency_ns = lat[t];
            wa[t].rc         = 0;
            wa[t].n_signed   = 0;
            wa[t].sink       = 0;
        }

        /* Start sampler (only if a free CPU is available) */
        sampler_state_t sst;
        pthread_t sampler_tid;
        int sampler_active = (sampler_cpu >= 0);
        if (sampler_active) {
            sst.running = 1;
            sst.cpu     = sampler_cpu;
            snprintf(sst.path, MAX_PATH_LEN, "%s_metrics.csv", cfg.output_prefix);
            pthread_create(&sampler_tid, NULL, sampler_fn, &sst);
        }

        /* Launch workers */
        for (int t = 0; t < cfg.n_threads; t++)
            pthread_create(&threads[t], NULL, worker_fn, &wa[t]);

        /* Main joins barrier → all threads start timed region simultaneously */
        pthread_barrier_wait(&g_barrier);
        uint64_t t_start = now_ns();
        if (run == 0) t_first_run_start = t_start;

        for (int t = 0; t < cfg.n_threads; t++)
            pthread_join(threads[t], NULL);

        uint64_t t_end = now_ns();
        t_last_run_end = t_end;
        uint64_t wall_ns = t_end - t_start;

        /* Stop sampler */
        if (sampler_active) {
            sst.running = 0;
            pthread_join(sampler_tid, NULL);
        }

        /* Check for failures */
        int nfail = 0;
        for (int t = 0; t < cfg.n_threads; t++) if (wa[t].rc != 0) nfail++;
        if (nfail > 0) {
            fprintf(stderr, "\n  run %d: %d thread(s) failed\n", run, nfail);
            continue;
        }

        long total_sigs = 0;
        for (int t = 0; t < cfg.n_threads; t++) total_sigs += wa[t].n_signed;
        double ops = (double)total_sigs / ((double)wall_ns / 1e9);
        printf(" %.0f ops/s  (wall=%.3fs)\n", ops, (double)wall_ns / 1e9);

        write_latencies(cfg.output_prefix, &cfg, wa, run);
        write_summary_row(cfg.output_prefix, &cfg, wa, run, wall_ns);
    }

    write_meta(cfg.output_prefix, &cfg, t_first_run_start, t_last_run_end);

    pthread_barrier_destroy(&g_barrier);
    for (int t = 0; t < cfg.n_threads; t++) free(lat[t]);
    free(lat); free(wa); free(threads);
    printf("Done. Output: %s_{latencies,summary,metrics,meta}.*\n", cfg.output_prefix);
    return 0;
}
