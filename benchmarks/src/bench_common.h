/*
 * bench_common.h — Shared utilities for all qMEMO benchmark programs.
 *
 * Part of the qMEMO project (IIT Chicago).
 *
 * Provides:
 *   - get_time()       — nanosecond-precision monotonic timer
 *   - get_timestamp()  — ISO-8601 UTC wall-clock string (thread-safe)
 *   - barrier_t        — portable pthread barrier (macOS lacks one by default)
 *
 * IMPORTANT: Include this header BEFORE all other headers in each benchmark
 * file.  The _POSIX_C_SOURCE feature-test macro must be set before any
 * system header is processed; including bench_common.h first ensures that.
 */

#pragma once

#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <time.h>

/* ── High-resolution monotonic timer ────────────────────────────────────────
 *
 * CLOCK_MONOTONIC is immune to NTP adjustments and wall-clock slew.
 * On macOS (commpage/vDSO) and Linux (vDSO) the syscall overhead is
 * < 25 ns — negligible against the tens-of-microsecond Falcon-512
 * verification cost.
 */
static inline double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── ISO-8601 UTC timestamp ──────────────────────────────────────────────────
 *
 * Uses gmtime_r (POSIX.1-2008, thread-safe) rather than gmtime (not
 * thread-safe — uses a single static buffer shared across threads).
 * Embeds the wall-clock time of the run so result files are
 * self-documenting when collected across machines and dates.
 */
static inline void get_timestamp(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

/* ── Portable pthread barrier ────────────────────────────────────────────────
 *
 * macOS does not export pthread_barrier_t by default (it is an optional
 * POSIX extension).  This implementation works identically on macOS and
 * Linux, using only mutex + condvar which are universally available.
 *
 * The `phase` counter advances each time all participants arrive, which
 * prevents a late-waking thread from a previous round from mistakenly
 * treating the next broadcast as its own release signal.
 */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned        count;   /* participants arrived so far this round */
    unsigned        total;   /* number of participants per round       */
    unsigned        phase;   /* incremented each time the barrier fires */
} barrier_t;

static inline void barrier_init(barrier_t *b, unsigned total)
{
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count = 0;
    b->total = total;
    b->phase = 0;
}

static inline void barrier_wait(barrier_t *b)
{
    pthread_mutex_lock(&b->mutex);
    b->count++;
    if (b->count >= b->total) {
        /* Last participant: advance phase and wake everyone. */
        b->phase++;
        b->count = 0;
        pthread_cond_broadcast(&b->cond);
    } else {
        /* Not the last: wait until phase advances. */
        unsigned phase = b->phase;
        while (phase == b->phase)
            pthread_cond_wait(&b->cond, &b->mutex);
    }
    pthread_mutex_unlock(&b->mutex);
}

static inline void barrier_destroy(barrier_t *b)
{
    pthread_cond_destroy(&b->cond);
    pthread_mutex_destroy(&b->mutex);
}
