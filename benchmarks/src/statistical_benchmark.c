/*
 * statistical_benchmark.c — Falcon-512 Verification with Statistical Analysis
 *
 * Part of the qMEMO project: benchmarking post-quantum digital signatures
 * for blockchain transaction verification (IIT Chicago).
 *
 * Why a two-level design?
 * ──────────────────────
 * Instead of timing one giant loop (like verify_benchmark.c), we run
 * 1,000 independent *trials*, each of which times a batch of 100
 * verifications.  This gives us 1,000 independent samples of ops/sec
 * from which we can compute distribution statistics.
 *
 *   ┌────────────────────────────────────────────────────────┐
 *   │  Warm-up (200 verifications, untimed)                  │
 *   ├────────────────────────────────────────────────────────┤
 *   │  Trial 0:   clock → 100 verifications → clock → Δt₀   │
 *   │  Trial 1:   clock → 100 verifications → clock → Δt₁   │
 *   │  …                                                     │
 *   │  Trial 999: clock → 100 verifications → clock → Δt₉₉₉ │
 *   └────────────────────────────────────────────────────────┘
 *
 * Batching 100 operations per trial amortises the clock_gettime overhead
 * (~25 ns) against the verify cost (~23 µs × 100 ≈ 2.3 ms per trial),
 * keeping timing noise below 0.002%.  By the Central Limit Theorem the
 * per-trial batch mean trends Gaussian even if individual verifications
 * have a skewed distribution — this is what makes parametric analysis
 * applicable.
 *
 * Statistical outputs
 * ───────────────────
 * - Mean, standard deviation (Bessel-corrected, n−1 denominator)
 * - Coefficient of Variation (CV = σ/μ): < 2% is good, > 5% is noisy
 * - Percentiles via linear interpolation (matches NumPy default)
 * - Skewness & kurtosis (3rd/4th standardised moments)
 * - Jarque–Bera normality test at α = 0.05
 *     → If JB passes: report mean ± SD, use t-test / ANOVA
 *     → If JB fails:  report median / IQR, use Mann–Whitney U
 * - Outlier count (> 3σ from mean)
 * - Full raw data array in JSON for offline re-analysis
 *
 * Compile (from the project root, after install_liboqs.sh):
 *
 *   cc -O3 -I./liboqs_install/include -L./liboqs_install/lib \
 *      benchmarks/src/statistical_benchmark.c -loqs -lm \
 *      -o benchmarks/bin/statistical_benchmark
 */

#include "bench_common.h"   /* get_time, get_timestamp — must be first */

#include <oqs/oqs.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define NUM_TRIALS           1000
#define ITERS_PER_TRIAL      100
#define WARMUP_ITERATIONS    200
#define MSG_LEN              256
#define MSG_FILL_BYTE        0x42

/*
 * Jarque–Bera critical value for α = 0.05 with 2 degrees of freedom.
 * JB ~ χ²(2); the 95th percentile of χ²(2) is 5.991.
 * If JB > 5.991 we reject the null hypothesis of normality.
 */
#define JB_CRITICAL_005  5.991

/* ══════════════════════════════════════════════════════════════════════════
 *  Statistics library — pure functions, no global state
 *
 *  Every function takes a (const double *, int) pair and returns a scalar.
 *  The percentile function requires a pre-sorted array.
 * ══════════════════════════════════════════════════════════════════════════ */

static double stat_mean(const double *data, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += data[i];
    return sum / n;
}

/*
 * Bessel-corrected sample standard deviation (n−1 denominator).
 *
 * We divide by (n−1) rather than n because our 1,000 trials are a *sample*
 * from the infinite population of all possible runs.  Bessel's correction
 * makes this an unbiased estimator of the population variance, which is
 * the standard expected by reviewers for publication-quality results.
 */
static double stat_stddev(const double *data, int n, double mean)
{
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        double d = data[i] - mean;
        sum_sq += d * d;
    }
    return sqrt(sum_sq / (n - 1));
}

/*
 * Percentile via linear interpolation (method matches NumPy's default).
 *
 * Given a sorted array of n values and a percentile p ∈ [0, 100], compute
 * the linearly interpolated value at rank (p/100) × (n−1).  This is the
 * same as numpy.percentile(data, p, interpolation='linear').
 *
 * PRECONDITION: `sorted` must be sorted in ascending order.
 */
static double stat_percentile(const double *sorted, int n, double p)
{
    double rank = (p / 100.0) * (n - 1);
    int lo = (int)rank;
    int hi = lo + 1;
    if (hi >= n) return sorted[n - 1];
    double frac = rank - lo;
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

/*
 * Skewness — the third standardised moment.
 *
 * Measures asymmetry of the distribution:
 *   > 0 → right-skewed (long tail towards high values; typical for
 *          latency data where OS interrupts cause occasional slow trials)
 *   < 0 → left-skewed
 *   ≈ 0 → symmetric (Gaussian-like)
 *
 * This is the "adjusted Fisher–Pearson" formula used by Excel, SAS, and
 * scipy.stats.skew with bias=False:
 *   G₁ = [n / ((n−1)(n−2))] × Σ[(xᵢ − x̄)/s]³
 */
static double stat_skewness(const double *data, int n, double mean, double sd)
{
    if (sd == 0.0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double z = (data[i] - mean) / sd;
        sum += z * z * z;
    }
    double adj = (double)n / ((double)(n - 1) * (double)(n - 2));
    return adj * sum;
}

/*
 * Excess kurtosis — the fourth standardised moment minus 3.
 *
 * A Gaussian distribution has excess kurtosis = 0.
 *   > 0 → leptokurtic (heavy tails, more outliers than Gaussian)
 *   < 0 → platykurtic (light tails, fewer outliers than Gaussian)
 *
 * Uses the bias-corrected formula:
 *   G₂ = [(n(n+1)) / ((n−1)(n−2)(n−3))] × Σ[(xᵢ − x̄)/s]⁴
 *        − [3(n−1)² / ((n−2)(n−3))]
 */
static double stat_kurtosis(const double *data, int n, double mean, double sd)
{
    if (sd == 0.0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double z = (data[i] - mean) / sd;
        double z2 = z * z;
        sum += z2 * z2;
    }
    double n_d  = (double)n;
    double nm1  = n_d - 1.0;
    double nm2  = n_d - 2.0;
    double nm3  = n_d - 3.0;
    double term1 = (n_d * (n_d + 1.0)) / (nm1 * nm2 * nm3) * sum;
    double term2 = (3.0 * nm1 * nm1) / (nm2 * nm3);
    return term1 - term2;
}

/*
 * Jarque–Bera test for normality.
 *
 * JB = (n/6) × [ S² + (K²/4) ]
 *
 * where S = skewness, K = excess kurtosis (both from the raw-moment
 * formulas, not the bias-corrected ones).  Under H₀ (normality),
 * JB ~ χ²(2).  We reject normality if JB > 5.991 (α = 0.05).
 *
 * NOTE: we use the *sample* (bias-corrected) skewness and kurtosis as
 * inputs.  For n = 1,000 the difference from the raw-moment versions is
 * negligible — the correction factors are ~ 1.001.  This avoids computing
 * a second set of moments.
 */
static double stat_jarque_bera(int n, double skew, double kurt)
{
    return ((double)n / 6.0) * (skew * skew + (kurt * kurt) / 4.0);
}

/*
 * Count values more than 3 standard deviations from the mean.
 *
 * For a Gaussian distribution, P(|X − μ| > 3σ) ≈ 0.27%, so we expect
 * about 2–3 outliers in 1,000 trials.  Significantly more suggests
 * non-Gaussian tails (OS scheduling jitter, thermal throttling, etc.)
 * and should be noted in the paper.
 */
static int count_outliers(const double *data, int n, double mean, double sd)
{
    int count = 0;
    double lo = mean - 3.0 * sd;
    double hi = mean + 3.0 * sd;
    for (int i = 0; i < n; i++) {
        if (data[i] < lo || data[i] > hi)
            count++;
    }
    return count;
}

/* qsort comparator for ascending doubles. */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    OQS_STATUS rc;
    char timestamp[64];
    double *sorted = NULL;

    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon-512 Statistical Benchmark  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n\n");

    /* ── Initialise liboqs ────────────────────────────────────────────────── */

    OQS_init();

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_falcon_512);
    if (sig == NULL) {
        fprintf(stderr, "ERROR: Falcon-512 not enabled in this liboqs build.\n");
        OQS_destroy();
        return EXIT_FAILURE;
    }

    printf("Algorithm           : %s\n", sig->method_name);
    printf("Public key size     : %zu bytes\n", sig->length_public_key);
    printf("Secret key size     : %zu bytes\n", sig->length_secret_key);
    printf("Max signature size  : %zu bytes\n", sig->length_signature);
    printf("Trials              : %d\n", NUM_TRIALS);
    printf("Iterations per trial: %d\n", ITERS_PER_TRIAL);
    printf("Warm-up iterations  : %d\n", WARMUP_ITERATIONS);
    printf("Message length      : %d bytes (0x%02X fill)\n", MSG_LEN, MSG_FILL_BYTE);
    printf("\n");

    /* ── Allocate buffers ─────────────────────────────────────────────────── */

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);
    uint8_t *message    = malloc(MSG_LEN);

    /*
     * Raw data array: one ops/sec measurement per trial.
     * Heap-allocated because 1,000 × 8 bytes = 8 KB which is fine,
     * but we keep it off the stack to be safe on embedded/CI systems.
     */
    double *ops_data = malloc(NUM_TRIALS * sizeof(double));

    if (!public_key || !secret_key || !signature || !message || !ops_data) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        free(public_key); free(secret_key); free(signature);
        free(message); free(ops_data);
        OQS_SIG_free(sig);
        OQS_destroy();
        return EXIT_FAILURE;
    }

    /* ── Prepare test data ────────────────────────────────────────────────── */

    memset(message, MSG_FILL_BYTE, MSG_LEN);

    printf("[1/7] Generating Falcon-512 keypair …\n");
    rc = OQS_SIG_keypair(sig, public_key, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Key generation failed (OQS_STATUS = %d).\n", rc);
        goto cleanup;
    }
    printf("       Done.\n");

    size_t sig_len = 0;
    printf("[2/7] Signing test message …\n");
    rc = OQS_SIG_sign(sig, signature, &sig_len, message, MSG_LEN, secret_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Signing failed (OQS_STATUS = %d).\n", rc);
        goto cleanup;
    }
    printf("       Signature: %zu bytes.\n", sig_len);

    printf("[3/7] Sanity check …\n");
    rc = OQS_SIG_verify(sig, message, MSG_LEN, signature, sig_len, public_key);
    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "ERROR: Verification FAILED (OQS_STATUS = %d).\n", rc);
        goto cleanup;
    }
    printf("       Passed.\n");

    /* ── Warm-up ──────────────────────────────────────────────────────────
     *
     * 200 iterations (2× the per-trial batch) to stabilise caches and
     * let the CPU governor ramp to sustained boost frequency.
     */
    printf("[4/7] Warm-up: %d verifications …\n", WARMUP_ITERATIONS);

    volatile OQS_STATUS warmup_rc;
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        warmup_rc = OQS_SIG_verify(sig, message, MSG_LEN,
                                   signature, sig_len, public_key);
    }
    (void)warmup_rc;
    printf("       Complete.\n");

    /* ── Data collection ──────────────────────────────────────────────────
     *
     * Each trial wraps its own clock pair around exactly ITERS_PER_TRIAL
     * verifications.  Nothing else executes between the clocks — no
     * printf, no array stores beyond the single result write after t_end.
     *
     * `volatile` on bench_rc prevents dead-code elimination of the verify
     * calls under -O3.
     */
    printf("[5/7] Running %d trials × %d iterations …\n",
           NUM_TRIALS, ITERS_PER_TRIAL);

    volatile OQS_STATUS bench_rc;

    for (int t = 0; t < NUM_TRIALS; t++) {
        double t_start = get_time();

        for (int i = 0; i < ITERS_PER_TRIAL; i++) {
            bench_rc = OQS_SIG_verify(sig, message, MSG_LEN,
                                      signature, sig_len, public_key);
        }

        double t_end = get_time();

        ops_data[t] = (double)ITERS_PER_TRIAL / (t_end - t_start);

        if ((t + 1) % 200 == 0)
            printf("       … %d / %d trials\n", t + 1, NUM_TRIALS);
    }
    (void)bench_rc;
    printf("       Data collection complete.\n");

    /* ── Statistical analysis ─────────────────────────────────────────────
     *
     * We work on the ops_data array (ops/sec per trial) and also create a
     * sorted copy for percentile calculations.
     */
    printf("[6/7] Analysing …\n\n");

    sorted = malloc(NUM_TRIALS * sizeof(double));
    if (!sorted) {
        fprintf(stderr, "ERROR: malloc for sorted array failed.\n");
        goto cleanup;
    }
    memcpy(sorted, ops_data, NUM_TRIALS * sizeof(double));
    qsort(sorted, NUM_TRIALS, sizeof(double), cmp_double);

    double mean   = stat_mean(ops_data, NUM_TRIALS);
    double sd     = stat_stddev(ops_data, NUM_TRIALS, mean);
    double cv     = (mean > 0.0) ? (sd / mean) * 100.0 : 0.0;
    double min_v  = sorted[0];
    double max_v  = sorted[NUM_TRIALS - 1];
    double median = stat_percentile(sorted, NUM_TRIALS, 50.0);
    double p5     = stat_percentile(sorted, NUM_TRIALS,  5.0);
    double p95    = stat_percentile(sorted, NUM_TRIALS, 95.0);
    double p99    = stat_percentile(sorted, NUM_TRIALS, 99.0);
    double iqr    = stat_percentile(sorted, NUM_TRIALS, 75.0)
                  - stat_percentile(sorted, NUM_TRIALS, 25.0);
    double skew   = stat_skewness(ops_data, NUM_TRIALS, mean, sd);
    double kurt   = stat_kurtosis(ops_data, NUM_TRIALS, mean, sd);
    double jb     = stat_jarque_bera(NUM_TRIALS, skew, kurt);
    int    normal   = (jb <= JB_CRITICAL_005);
    int    outliers = count_outliers(ops_data, NUM_TRIALS, mean, sd);

    /* ── Human-readable output ────────────────────────────────────────────── */

    printf("  ┌───────────────────────────────────────────────────────────┐\n");
    printf("  │  Falcon-512 Verification — Statistical Analysis          │\n");
    printf("  ├───────────────────────────────────────────────────────────┤\n");
    printf("  │  Trials              : %6d                              │\n", NUM_TRIALS);
    printf("  │  Iterations / trial  : %6d                              │\n", ITERS_PER_TRIAL);
    printf("  │  Total verifications : %6d                              │\n",
           NUM_TRIALS * ITERS_PER_TRIAL);
    printf("  ├───────────────────────────────────────────────────────────┤\n");
    printf("  │  Mean   (ops/sec)    : %12.2f                        │\n", mean);
    printf("  │  Std Dev             : %12.2f                        │\n", sd);
    printf("  │  CV                  : %11.2f%%                       │\n", cv);
    printf("  ├───────────────────────────────────────────────────────────┤\n");
    printf("  │  Min    (ops/sec)    : %12.2f                        │\n", min_v);
    printf("  │  P5                  : %12.2f                        │\n", p5);
    printf("  │  Median (P50)        : %12.2f                        │\n", median);
    printf("  │  P95                 : %12.2f                        │\n", p95);
    printf("  │  P99                 : %12.2f                        │\n", p99);
    printf("  │  Max    (ops/sec)    : %12.2f                        │\n", max_v);
    printf("  │  IQR                 : %12.2f                        │\n", iqr);
    printf("  ├───────────────────────────────────────────────────────────┤\n");
    printf("  │  Skewness            : %12.4f  ", skew);
    if (skew > 0.1)       printf("(right-skewed)      │\n");
    else if (skew < -0.1) printf("(left-skewed)       │\n");
    else                  printf("(symmetric)         │\n");
    printf("  │  Excess kurtosis     : %12.4f  ", kurt);
    if (kurt > 0.5)       printf("(heavy tails)       │\n");
    else if (kurt < -0.5) printf("(light tails)       │\n");
    else                  printf("(near-Gaussian)     │\n");
    printf("  │  Jarque–Bera stat    : %12.4f                      │\n", jb);
    printf("  │  Normality (α=0.05)  : %s                      │\n",
           normal ? "PASS (Gaussian) " : "FAIL (non-Gauss.)");
    printf("  │  Outliers (> 3σ)     : %6d / %d                        │\n",
           outliers, NUM_TRIALS);
    printf("  └───────────────────────────────────────────────────────────┘\n");

    if (normal) {
        printf("\n  → Distribution is consistent with Gaussian.\n");
        printf("    Report: mean ± SD.  Use parametric tests (t-test, ANOVA).\n");
    } else {
        printf("\n  → Distribution departs from Gaussian (JB = %.2f > %.3f).\n",
               jb, JB_CRITICAL_005);
        printf("    Report: median and IQR.  Use non-parametric tests ");
        printf("(Mann–Whitney U).\n");
    }

    if (cv < 2.0)
        printf("  → CV = %.2f%% — excellent measurement stability.\n\n", cv);
    else if (cv < 5.0)
        printf("  → CV = %.2f%% — acceptable; consider closing background apps.\n\n", cv);
    else
        printf("  → CV = %.2f%% — noisy; isolate CPUs or use a dedicated bench machine.\n\n", cv);

    /* ── JSON output ──────────────────────────────────────────────────────
     *
     * All 1,000 raw samples are included so results can be re-analysed
     * offline in Python / R (histograms, Q-Q plots, bootstrap CIs, etc.)
     * without re-running the benchmark.
     */
    printf("[7/7] JSON output:\n\n");
    printf("--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon512_verify_statistical\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"algorithm\": \"Falcon-512\",\n");
    printf("  \"trials\": %d,\n", NUM_TRIALS);
    printf("  \"iterations_per_trial\": %d,\n", ITERS_PER_TRIAL);
    printf("  \"total_verifications\": %d,\n", NUM_TRIALS * ITERS_PER_TRIAL);
    printf("  \"signature_bytes\": %zu,\n", sig_len);
    printf("  \"pubkey_bytes\": %zu,\n", sig->length_public_key);
    printf("  \"seckey_bytes\": %zu,\n", sig->length_secret_key);
    printf("  \"statistics\": {\n");
    printf("    \"mean_ops_sec\": %.2f,\n", mean);
    printf("    \"stddev_ops_sec\": %.2f,\n", sd);
    printf("    \"cv_percent\": %.2f,\n", cv);
    printf("    \"min_ops_sec\": %.2f,\n", min_v);
    printf("    \"p5_ops_sec\": %.2f,\n", p5);
    printf("    \"median_ops_sec\": %.2f,\n", median);
    printf("    \"p95_ops_sec\": %.2f,\n", p95);
    printf("    \"p99_ops_sec\": %.2f,\n", p99);
    printf("    \"max_ops_sec\": %.2f,\n", max_v);
    printf("    \"iqr_ops_sec\": %.2f,\n", iqr);
    printf("    \"skewness\": %.6f,\n", skew);
    printf("    \"excess_kurtosis\": %.6f,\n", kurt);
    printf("    \"jarque_bera\": %.6f,\n", jb);
    printf("    \"normality_pass\": %s,\n", normal ? "true" : "false");
    printf("    \"outliers_count\": %d\n", outliers);
    printf("  },\n");

    printf("  \"raw_data\": [\n");
    for (int i = 0; i < NUM_TRIALS; i++) {
        if (i < NUM_TRIALS - 1)
            printf("    %.2f,\n", ops_data[i]);
        else
            printf("    %.2f\n", ops_data[i]);
    }
    printf("  ]\n");
    printf("}\n");

    /* ── Cleanup ──────────────────────────────────────────────────────────── */

    free(sorted);
    free(ops_data);
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();

    printf("\nStatistical benchmark complete.\n");
    return EXIT_SUCCESS;

cleanup:
    free(sorted);
    free(ops_data);
    OQS_MEM_secure_free(secret_key, sig ? sig->length_secret_key : 0);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    OQS_SIG_free(sig);
    OQS_destroy();
    return EXIT_FAILURE;
}
