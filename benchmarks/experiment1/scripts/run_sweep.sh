#!/usr/bin/env bash
# run_sweep.sh — Full Experiment 1 sweep (spec §8).
#
# Runs bench_sign across 3 algos × 8 thread counts × 2 pin strategies × 5 runs.
# Total: ~195 invocations, ~5 hours on Cascade Lake.
#
# Usage:
#   bash run_sweep.sh [--outdir DIR] [--iters N] [--warmup N] [--dry-run]
#
# Options:
#   --outdir DIR   output directory   [default: ../results/sweep_<timestamp>]
#   --iters  N     iterations/thread  [default: 100000]
#   --warmup N     warmup/thread      [default: 1000]
#   --msgsize B    message size bytes [default: 32]
#   --dry-run      print commands without executing
#   --algo-only A  run only this algo (ed25519|falcon512|dilithium2)
#   --threads-only N  run only this thread count

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH="${SCRIPT_DIR}/../../bin/bench_sign"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTDIR=""
ITERS=100000
WARMUP=1000
MSGSIZE=32
DRYRUN=0
ALGO_FILTER=""
THREADS_FILTER=""

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --outdir)      OUTDIR="$2"; shift 2 ;;
        --iters)       ITERS="$2"; shift 2 ;;
        --warmup)      WARMUP="$2"; shift 2 ;;
        --msgsize)     MSGSIZE="$2"; shift 2 ;;
        --dry-run)     DRYRUN=1; shift ;;
        --algo-only)   ALGO_FILTER="$2"; shift 2 ;;
        --threads-only) THREADS_FILTER="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$OUTDIR" ]] && OUTDIR="${SCRIPT_DIR}/../results/sweep_${TIMESTAMP}"

# ── Pre-flight ───────────────────────────────────────────────────────────────
if [[ ! -x "$BENCH" ]]; then
    echo "ERROR: bench_sign not found at $BENCH" >&2
    echo "  Build it first:  make -C ${SCRIPT_DIR}/.." >&2
    exit 1
fi

mkdir -p "$OUTDIR"
echo "Sweep output: $OUTDIR"
echo "bench_sign:   $BENCH"
echo "iters/thread: $ITERS  warmup: $WARMUP  msgsize: $MSGSIZE"

# ── Sweep matrix ─────────────────────────────────────────────────────────────
# Algos
ALGOS=(ed25519 falcon512 dilithium2)
[[ -n "$ALGO_FILTER" ]] && ALGOS=("$ALGO_FILTER")

# Thread counts (spec §8)
THREADS_COMPACT=(1 2 4 8 12 24 48 96)
THREADS_SPREAD=(2 4 8 24 48)
[[ -n "$THREADS_FILTER" ]] && THREADS_COMPACT=("$THREADS_FILTER") && THREADS_SPREAD=("$THREADS_FILTER")

# Count total invocations for ETA
N_COMPACT=${#THREADS_COMPACT[@]}
N_SPREAD=${#THREADS_SPREAD[@]}
N_ALGOS=${#ALGOS[@]}
TOTAL=$(( N_ALGOS * (N_COMPACT + N_SPREAD) ))
DONE=0
FAIL_COUNT=0

COOLDOWN=30       # seconds between invocations
EST_PER_RUN=30    # seconds per bench_sign invocation (100k iters × ~30s)

echo "Total invocations: $TOTAL  (est. $(( (TOTAL * (EST_PER_RUN + COOLDOWN)) / 3600 )) h)"
echo ""

run_one() {
    local algo="$1" threads="$2" pin="$3"
    local prefix="${OUTDIR}/${algo}_t${threads}_${pin}"
    local tag="${algo}_t${threads}_${pin}_${TIMESTAMP}"

    DONE=$(( DONE + 1 ))
    local pct=$(( DONE * 100 / TOTAL ))
    local eta_s=$(( (TOTAL - DONE) * (EST_PER_RUN + COOLDOWN) ))
    printf "[%3d/%3d %3d%%  ETA ~%dm] %s threads=%-3d pin=%s\n" \
        "$DONE" "$TOTAL" "$pct" "$(( eta_s / 60 ))" "$algo" "$threads" "$pin"

    local CMD=(
        "$BENCH"
        --algo "$algo"
        --threads "$threads"
        --pin-strategy "$pin"
        --iterations "$ITERS"
        --warmup "$WARMUP"
        --message-size "$MSGSIZE"
        --message-mode fresh
        --runs 5
        --verify-after-sign 1
        --output-prefix "$prefix"
        --tag "$tag"
    )

    if [[ $DRYRUN -eq 1 ]]; then
        echo "  DRY: ${CMD[*]}"
        return 0
    fi

    if "${CMD[@]}" >> "${OUTDIR}/sweep.log" 2>&1; then
        return 0
    else
        local rc=$?
        echo "  FAILED (rc=$rc) — see ${OUTDIR}/sweep.log"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
        return 1
    fi
}

cooldown() {
    [[ $DRYRUN -eq 1 ]] && return
    [[ $DONE -ge $TOTAL ]] && return
    printf "  cooling down %ds..." "$COOLDOWN"
    sleep "$COOLDOWN"
    printf " done\n"
}

# ── Control run: pin=none at 1 thread per algo ───────────────────────────────
echo "=== Control: no-pinning baseline ==="
for algo in "${ALGOS[@]}"; do
    run_one "$algo" 1 none && cooldown || true
done

# ── Compact pinning (all thread counts) ──────────────────────────────────────
echo ""
echo "=== Compact pinning ==="
for algo in "${ALGOS[@]}"; do
    for t in "${THREADS_COMPACT[@]}"; do
        run_one "$algo" "$t" compact && cooldown || true
    done
done

# ── Spread pinning (subset of thread counts) ─────────────────────────────────
echo ""
echo "=== Spread pinning ==="
for algo in "${ALGOS[@]}"; do
    for t in "${THREADS_SPREAD[@]}"; do
        run_one "$algo" "$t" spread && cooldown || true
    done
done

# ── Ed25519 ctx-mode reuse (compact only) ────────────────────────────────────
# Runs ed25519 with --ctx-mode reuse so fig9 can compare fresh vs reuse.
# Only compact pinning, all thread counts. ML-DSA and Falcon do not use
# EVP_MD_CTX so ctx-mode variation is not needed for them.
echo ""
echo "=== Ed25519 ctx-mode reuse (compact) ==="
if [[ -z "$ALGO_FILTER" || "$ALGO_FILTER" == "ed25519" ]]; then
    for t in "${THREADS_COMPACT[@]}"; do
        local_prefix="${OUTDIR}/ed25519_ctx_reuse_t${t}_compact"
        local_tag="ed25519_ctx_reuse_t${t}_compact_${TIMESTAMP}"
        DONE=$(( DONE + 1 ))
        local_pct=$(( DONE * 100 / TOTAL ))
        printf "[%3d/%3d extra] ed25519 ctx=reuse threads=%-3d pin=compact\n" \
            "$DONE" "$TOTAL" "$t"
        if [[ $DRYRUN -eq 0 ]]; then
            "$BENCH" \
                --algo ed25519 \
                --threads "$t" \
                --pin-strategy compact \
                --ctx-mode reuse \
                --iterations "$ITERS" \
                --warmup "$WARMUP" \
                --message-size "$MSGSIZE" \
                --message-mode fresh \
                --runs 5 \
                --verify-after-sign 1 \
                --output-prefix "$local_prefix" \
                --tag "$local_tag" \
                >> "${OUTDIR}/sweep.log" 2>&1 || true
            cooldown
        else
            echo "  DRY: $BENCH --algo ed25519 --threads $t --pin-strategy compact --ctx-mode reuse ..."
        fi
    done
fi

# ── Message-mode control: fixed vs fresh at threads=1 ────────────────────────
echo ""
echo "=== Control: fixed vs fresh message mode ==="
for algo in "${ALGOS[@]}"; do
    if [[ $DRYRUN -eq 0 ]]; then
        "$BENCH" \
            --algo "$algo" --threads 1 --pin-strategy compact \
            --iterations "$ITERS" --warmup "$WARMUP" \
            --message-mode fixed --runs 5 \
            --output-prefix "${OUTDIR}/${algo}_t1_compact_fixed" \
            --tag "msgmode_fixed_${TIMESTAMP}" \
            >> "${OUTDIR}/sweep.log" 2>&1 || true
        cooldown
    fi
done

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "Sweep complete."
echo "  Successful invocations: $(( DONE - FAIL_COUNT ))/${DONE}"
[[ $FAIL_COUNT -gt 0 ]] && echo "  FAILURES: $FAIL_COUNT — check ${OUTDIR}/sweep.log"
echo "  Results: $OUTDIR"
echo ""
echo "Next step:  python3 ${SCRIPT_DIR}/../analysis/analyze.py --results-dir $OUTDIR"
