#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BENCH="$BENCH_DIR/bin/bench_sign"
OUT="$BENCH_DIR/results/ctx_sweep_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"

THREADS=(1 4 8 16 24 48 96)
MODES=(fresh reuse)
ITERS=100000
WARMUP=10000
RUNS=3

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/run.log"; }

log "=== CTX_FRESH vs CTX_REUSE sweep ==="
log "bench: $BENCH"
log "output: $OUT"
log "thread counts: ${THREADS[*]}"
log "iterations: $ITERS  warmup: $WARMUP  runs: $RUNS"

for mode in "${MODES[@]}"; do
    log "--- mode: $mode ---"
    for t in "${THREADS[@]}"; do
        label="ed25519_${mode}_t${t}"
        log "==> $label"
        "$BENCH" \
            --algo ed25519 \
            --threads "$t" \
            --iterations "$ITERS" \
            --warmup "$WARMUP" \
            --runs "$RUNS" \
            --ctx-mode "$mode" \
            --output-prefix "$OUT/$label"
        log "    -> $OUT/${label}_summary.csv"
        sleep 5
    done
done

log "DONE. Results in $OUT"
