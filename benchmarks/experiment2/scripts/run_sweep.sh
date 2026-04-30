#!/usr/bin/env bash
# run_sweep.sh — Experiment 2 reduced sweep (spec §8 reduced matrix).
#
# Reduced matrix: 3 algos × 5 thread counts × 1 pin strategy × 3 mixes × 5 runs
# = 225 invocations, ~4 hours on Cascade Lake-R.
#
# Usage:
#   bash run_sweep.sh [--outdir DIR] [--iters N] [--warmup N] [--dry-run]
#   bash run_sweep.sh --reduced      (same as above, alias)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH="${SCRIPT_DIR}/../../bin/bench_verify"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTDIR=""
ITERS=100000
WARMUP=1000
MSGSIZE=32
DRYRUN=0
ALGO_FILTER=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --outdir)      OUTDIR="$2"; shift 2 ;;
        --iters)       ITERS="$2"; shift 2 ;;
        --warmup)      WARMUP="$2"; shift 2 ;;
        --msgsize)     MSGSIZE="$2"; shift 2 ;;
        --dry-run)     DRYRUN=1; shift ;;
        --reduced)     shift ;;  # alias; reduced is the default
        --algo-only)   ALGO_FILTER="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$OUTDIR" ]] && OUTDIR="${SCRIPT_DIR}/../results/exp2_sweep_${TIMESTAMP}"

if [[ ! -x "$BENCH" ]]; then
    echo "ERROR: bench_verify not found at $BENCH" >&2
    echo "  Build it first:  make -C ${SCRIPT_DIR}/.." >&2
    exit 1
fi

mkdir -p "$OUTDIR"
echo "Sweep output: $OUTDIR"
echo "bench_verify: $BENCH"
echo "iters/thread: $ITERS  warmup: $WARMUP"

ALGOS=(ed25519 falcon512 dilithium2)
[[ -n "$ALGO_FILTER" ]] && ALGOS=("$ALGO_FILTER")

# Reduced thread counts (spec §8 reduced)
THREADS=(1 8 24 48 96)

# Reduced signature mixes (skip random:0.10 for first pass)
MIXES=(valid invalid alternating)
INVALID_MODE="flip-bit"   # used when mix != valid

TOTAL=$(( ${#ALGOS[@]} * ${#THREADS[@]} * ${#MIXES[@]} ))
DONE=0
FAIL_COUNT=0
COOLDOWN=30
EST_PER_RUN=30

echo "Total invocations: $TOTAL  (est. $(( (TOTAL * (EST_PER_RUN + COOLDOWN)) / 3600 )) h)"
echo ""

run_one() {
    local algo="$1" threads="$2" mix="$3"
    local prefix="${OUTDIR}/${algo}_t${threads}_compact_${mix}"
    local tag="${algo}_t${threads}_compact_${mix}_${TIMESTAMP}"

    DONE=$(( DONE + 1 ))
    local pct=$(( DONE * 100 / TOTAL ))
    local eta_s=$(( (TOTAL - DONE) * (EST_PER_RUN + COOLDOWN) ))
    printf "[%3d/%3d %3d%%  ETA ~%dm] %s t=%-3d mix=%s\n" \
        "$DONE" "$TOTAL" "$pct" "$(( eta_s / 60 ))" "$algo" "$threads" "$mix"

    local CMD=(
        "$BENCH"
        --algo "$algo"
        --threads "$threads"
        --pin-strategy compact
        --iterations "$ITERS"
        --warmup "$WARMUP"
        --message-size "$MSGSIZE"
        --runs 5
        --signature-mix "$mix"
        --output-prefix "$prefix"
        --tag "$tag"
    )
    # invalid and alternating need --invalid-mode
    if [[ "$mix" != "valid" ]]; then
        CMD+=(--invalid-mode "$INVALID_MODE")
    fi

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
        # rc=2 means correctness audit failed — ABORT the entire sweep
        if [[ $rc -eq 2 ]]; then
            echo "CORRECTNESS AUDIT FAILURE — aborting sweep immediately." | tee -a "${OUTDIR}/sweep.log"
            exit 2
        fi
        return 1
    fi
}

cooldown() {
    [[ $DRYRUN -eq 1 ]] && return
    [[ $DONE -ge $TOTAL ]] && return
    printf "  cool-down %ds..." "$COOLDOWN"
    sleep "$COOLDOWN"
    printf " done\n"
}

for algo in "${ALGOS[@]}"; do
    for t in "${THREADS[@]}"; do
        for mix in "${MIXES[@]}"; do
            run_one "$algo" "$t" "$mix" && cooldown || true
        done
    done
done

echo ""
echo "Sweep complete."
echo "  Successful: $(( DONE - FAIL_COUNT ))/${DONE}"
[[ $FAIL_COUNT -gt 0 ]] && echo "  FAILURES: $FAIL_COUNT — check ${OUTDIR}/sweep.log"
echo "  Results: $OUTDIR"
echo ""
echo "Next: python3 ${SCRIPT_DIR}/../analysis/analyze.py --input $OUTDIR --cross-experiment-input <exp1_dir>"
