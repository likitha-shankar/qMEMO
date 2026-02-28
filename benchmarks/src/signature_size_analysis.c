/*
 * signature_size_analysis.c — Falcon Signature Size Distribution
 *
 * Part of the qMEMO project (IIT Chicago): analyses the variable-length
 * signature output of all four Falcon variants.
 *
 * Schemes benchmarked (all via liboqs):
 *   Falcon-512           (unpadded, NIST Level 1, max 666 bytes)
 *   Falcon-padded-512    (constant length padded, NIST Level 1)
 *   Falcon-1024          (unpadded, NIST Level 5, max 1280 bytes)
 *   Falcon-padded-1024   (constant length padded, NIST Level 5)
 *
 * For each scheme, 10,000 real signatures are produced (one fresh random
 * message each time, same keypair throughout) and the actual signature
 * length is recorded.  Computed statistics:
 *   - min, max, mean
 *   - standard deviation (via Kahan compensated sum for numerical stability)
 *   - percentiles: p25, p50 (median), p75, p95, p99
 *   - comparison: measured mean vs. NIST spec maximum
 *
 * Unpadded Falcon signatures are variable-length (compressed NTRU lattice
 * vectors).  Padded variants always output the maximum length.  Observing
 * the distribution confirms the compression gain and validates the padding
 * overhead before network-layer analysis.
 *
 * Compile:
 *   make sigsize
 *
 * Run:
 *   ./benchmarks/bin/signature_size_analysis
 */

#include "bench_common.h"   /* get_time, get_timestamp — must be first */

#include <oqs/oqs.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

#define MSG_LEN    256
#define NUM_SIGS   10000

/* ── Comparison helper ───────────────────────────────────────────────────── */

static int cmp_size(const void *a, const void *b)
{
    size_t x = *(const size_t *)a;
    size_t y = *(const size_t *)b;
    return (x > y) - (x < y);
}

/* ── Per-scheme analysis ─────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *alg_id;
    int         nist_level;
    size_t      spec_max;   /* bytes from NIST spec */
} scheme_info_t;

static const scheme_info_t SCHEMES[] = {
    { "Falcon-512",        OQS_SIG_alg_falcon_512,        1,  666  },
    { "Falcon-padded-512", OQS_SIG_alg_falcon_padded_512, 1,  666  },
    { "Falcon-1024",       OQS_SIG_alg_falcon_1024,       5, 1280  },
    { "Falcon-padded-1024",OQS_SIG_alg_falcon_padded_1024,5, 1280  },
};
enum { NUM_SCHEMES = (int)(sizeof(SCHEMES) / sizeof(SCHEMES[0])) };

typedef struct {
    double min_b;
    double max_b;
    double mean;
    double std_dev;
    double p25;
    double p50;
    double p75;
    double p95;
    double p99;
} stats_t;

/*
 * Collect NUM_SIGS signatures for one scheme.  A new random message is used
 * for each signing call; the keypair is reused.  Returns 0 on success, -1
 * on any allocation or crypto failure.
 */
static int analyse_scheme(const scheme_info_t *s, stats_t *out)
{
    OQS_SIG *sig = OQS_SIG_new(s->alg_id);
    if (!sig) {
        fprintf(stderr, "  ERROR: could not instantiate %s\n", s->name);
        return -1;
    }

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);
    uint8_t *message    = malloc(MSG_LEN);
    size_t  *sizes      = malloc(NUM_SIGS * sizeof(size_t));

    if (!public_key || !secret_key || !signature || !message || !sizes) {
        fprintf(stderr, "  ERROR: malloc failed for %s\n", s->name);
        free(public_key); free(secret_key); free(signature);
        free(message); free(sizes);
        OQS_SIG_free(sig);
        return -1;
    }

    if (OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS) {
        fprintf(stderr, "  ERROR: keygen failed for %s\n", s->name);
        goto fail;
    }

    /* Produce NUM_SIGS signatures; fresh random message each time. */
    for (int i = 0; i < NUM_SIGS; i++) {
        /* Fill message bytes with a pseudo-random pattern keyed on i. */
        for (int b = 0; b < MSG_LEN; b++)
            message[b] = (uint8_t)((i ^ (b * 31)) & 0xFF);

        size_t len = 0;
        if (OQS_SIG_sign(sig, signature, &len, message, MSG_LEN, secret_key) != OQS_SUCCESS) {
            fprintf(stderr, "  ERROR: sign[%d] failed for %s\n", i, s->name);
            goto fail;
        }
        sizes[i] = len;
    }

    /* Sort for percentile computation */
    qsort(sizes, NUM_SIGS, sizeof(size_t), cmp_size);

    /* Min / Max */
    out->min_b = (double)sizes[0];
    out->max_b = (double)sizes[NUM_SIGS - 1];

    /* Mean (Kahan compensated sum) */
    double sum = 0.0, comp = 0.0;
    for (int i = 0; i < NUM_SIGS; i++) {
        double y = (double)sizes[i] - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum  = t;
    }
    out->mean = sum / (double)NUM_SIGS;

    /* Variance → std deviation */
    double var_sum = 0.0, var_comp = 0.0;
    for (int i = 0; i < NUM_SIGS; i++) {
        double d = (double)sizes[i] - out->mean;
        double y = d * d - var_comp;
        double t = var_sum + y;
        var_comp = (t - var_sum) - y;
        var_sum  = t;
    }
    out->std_dev = sqrt(var_sum / (double)(NUM_SIGS - 1));

    /* Percentiles (nearest-rank, 1-indexed) */
    out->p25 = (double)sizes[(int)(0.25 * (NUM_SIGS - 1))];
    out->p50 = (double)sizes[(int)(0.50 * (NUM_SIGS - 1))];
    out->p75 = (double)sizes[(int)(0.75 * (NUM_SIGS - 1))];
    out->p95 = (double)sizes[(int)(0.95 * (NUM_SIGS - 1))];
    out->p99 = (double)sizes[(int)(0.99 * (NUM_SIGS - 1))];

    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    free(sizes);
    OQS_SIG_free(sig);
    return 0;

fail:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_MEM_insecure_free(message);
    free(sizes);
    OQS_SIG_free(sig);
    return -1;
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("\n");
    printf("================================================================\n");
    printf("  Falcon Signature Size Distribution  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n");
    printf("  Schemes: Falcon-512, Falcon-padded-512,\n");
    printf("           Falcon-1024, Falcon-padded-1024\n");
    printf("  Signatures per scheme: %d\n\n", NUM_SIGS);

    OQS_init();

    stats_t results[NUM_SCHEMES];
    int     ok[NUM_SCHEMES];

    for (int i = 0; i < NUM_SCHEMES; i++) {
        printf("Analysing %-24s …", SCHEMES[i].name);
        fflush(stdout);
        ok[i] = analyse_scheme(&SCHEMES[i], &results[i]);
        if (ok[i] == 0)
            printf(" done.\n");
        else
            printf(" FAILED.\n");
    }

    /* ── Human-readable table ───────────────────────────────────────────── */
    printf("\n");
    printf("Scheme                    NIST  SpecMax  Min    Max    Mean   StdDev  "
           "p25  p50  p75  p95   p99\n");
    printf("------------------------  ----  -------  -----  -----  -----  ------  "
           "---  ---  ---  ----  ----\n");

    for (int i = 0; i < NUM_SCHEMES; i++) {
        if (ok[i] != 0) {
            printf("%-24s  (failed)\n", SCHEMES[i].name);
            continue;
        }
        stats_t *r = &results[i];
        printf("%-24s  L%d    %5zu  %5.0f  %5.0f  %5.1f  %6.1f  "
               "%4.0f %4.0f %4.0f %5.0f %5.0f\n",
               SCHEMES[i].name, SCHEMES[i].nist_level, SCHEMES[i].spec_max,
               r->min_b, r->max_b, r->mean, r->std_dev,
               r->p25, r->p50, r->p75, r->p95, r->p99);
    }

    /* ── JSON output ────────────────────────────────────────────────────── */
    printf("\n--- JSON ---\n");
    printf("{\n");
    printf("  \"test_name\": \"falcon_signature_size_distribution\",\n");
    printf("  \"timestamp\": \"%s\",\n", timestamp);
    printf("  \"num_signatures\": %d,\n", NUM_SIGS);
    printf("  \"message_len\": %d,\n", MSG_LEN);
    printf("  \"schemes\": [\n");

    for (int i = 0; i < NUM_SCHEMES; i++) {
        stats_t *r = &results[i];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", SCHEMES[i].name);
        printf("      \"nist_level\": %d,\n", SCHEMES[i].nist_level);
        printf("      \"spec_max_bytes\": %zu,\n", SCHEMES[i].spec_max);

        if (ok[i] == 0) {
            printf("      \"min\": %.0f,\n",    r->min_b);
            printf("      \"max\": %.0f,\n",    r->max_b);
            printf("      \"mean\": %.2f,\n",   r->mean);
            printf("      \"std_dev\": %.2f,\n",r->std_dev);
            printf("      \"p25\": %.0f,\n",    r->p25);
            printf("      \"p50\": %.0f,\n",    r->p50);
            printf("      \"p75\": %.0f,\n",    r->p75);
            printf("      \"p95\": %.0f,\n",    r->p95);
            printf("      \"p99\": %.0f\n",     r->p99);
        } else {
            printf("      \"error\": true\n");
        }

        printf("    }%s\n", (i < NUM_SCHEMES - 1) ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");

    OQS_destroy();
    printf("\nSignature size analysis complete.\n");
    return EXIT_SUCCESS;
}
