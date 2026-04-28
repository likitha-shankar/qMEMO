#!/usr/bin/env bash
# perf_wrap.sh — Run bench_sign under perf stat for hardware counter collection.
#
# Captures the 20 events from spec §12.4 and writes <prefix>_perf.csv.
# Use for spot checks (not the full 195-run sweep — perf adds ~10% overhead).
#
# Usage:
#   bash perf_wrap.sh --algo ed25519 --threads 8 --output-prefix /path/prefix [bench_sign opts...]
#
# Requires: linux-tools-$(uname -r) installed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH="${SCRIPT_DIR}/../bin/bench_sign"

# ── Events (spec §12.4) ──────────────────────────────────────────────────────
EVENTS=(
    cycles
    instructions
    branches
    branch-misses
    cache-references
    cache-misses
    L1-dcache-loads
    L1-dcache-load-misses
    LLC-loads
    LLC-load-misses
    dTLB-loads
    dTLB-load-misses
    cycle_activity.stalls_total
    cycle_activity.stalls_l1d_miss
    cycle_activity.stalls_l2_miss
    cycle_activity.stalls_l3_miss
    cycle_activity.stalls_mem_any
    topdown-fe-bound
    topdown-be-bound
    topdown-retiring
)

# Build comma-separated event string
EVENT_STR=$(IFS=,; echo "${EVENTS[*]}")

# ── Arg parsing ──────────────────────────────────────────────────────────────
PREFIX=""
REMAINING=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-prefix) PREFIX="$2"; REMAINING+=("$1" "$2"); shift 2 ;;
        *)                REMAINING+=("$1"); shift ;;
    esac
done

if [[ -z "$PREFIX" ]]; then
    echo "ERROR: --output-prefix is required" >&2
    exit 1
fi

PERF_RAW="${PREFIX}_perf_raw.txt"
PERF_CSV="${PREFIX}_perf.csv"

# ── Check perf is available ───────────────────────────────────────────────────
if ! command -v perf &>/dev/null; then
    echo "ERROR: perf not found. Install: sudo apt install linux-tools-\$(uname -r)" >&2
    exit 1
fi

# Check if perf can access hardware counters (may need paranoid=1 or sudo)
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 3)
if [[ "$PARANOID" -gt 1 ]]; then
    echo "WARN: /proc/sys/kernel/perf_event_paranoid=$PARANOID"
    echo "  Hardware counters may be restricted. Fix: echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid"
fi

echo "Running bench_sign under perf stat..."
echo "  Events: ${EVENT_STR}"
echo "  Output: ${PERF_CSV}"

# ── Run under perf stat ───────────────────────────────────────────────────────
perf stat \
    -e "${EVENT_STR}" \
    --field-separator=',' \
    -o "${PERF_RAW}" \
    -- "$BENCH" "${REMAINING[@]}"

# ── Parse perf output to CSV ──────────────────────────────────────────────────
# perf stat -o writes lines like:
#   <value>,<unit>,<event>,<run%>,<variance>
# We extract event name and value.

ALGO="" THREADS=""
for arg in "${REMAINING[@]}"; do
    case "$prev" in
        --algo)    ALGO="$arg" ;;
        --threads) THREADS="$arg" ;;
    esac
    prev="$arg"
done

{
    echo "algo,threads,event,value,unit"
    grep -v '^#' "${PERF_RAW}" | grep -v '^$' | while IFS=',' read -r val unit event rest; do
        # Strip leading whitespace
        val="${val#"${val%%[![:space:]]*}"}"
        event="${event#"${event%%[![:space:]]*}"}"
        # Skip lines that don't look like data
        [[ "$val" =~ ^[0-9] ]] || continue
        echo "${ALGO},${THREADS},${event},${val},${unit}"
    done
} > "${PERF_CSV}"

echo "Perf CSV: ${PERF_CSV}"
echo ""
echo "Key stall fractions (approximate):"
awk -F',' '
    /stalls_total/       { stalls=$4 }
    /stalls_l1d_miss/    { l1=$4 }
    /stalls_l3_miss/     { l3=$4 }
    /stalls_mem_any/     { mem=$4 }
    /^[^a-z]*cycles/     { cycles=$4 }
    END {
        if (cycles+0 > 0) {
            printf "  stalls/cycle:      %.3f\n", stalls/cycles
            printf "  L1 miss stalls:    %.3f\n", l1/cycles
            printf "  L3 miss stalls:    %.3f\n", l3/cycles
            printf "  mem-any stalls:    %.3f\n", mem/cycles
        }
    }
' "${PERF_CSV}"
