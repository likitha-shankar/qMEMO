#!/usr/bin/env bash
# run_logged.sh — Run the full qMEMO benchmark suite with per-run log files.
#
# Creates: benchmarks/results/run_YYYYMMDD_HHMMSS/
#   system_info.log          — hardware, OS, compiler, library versions
#   key_inspection.log       — full hex dump of keys and signatures for all 7 algs
#   verify_benchmark.log
#   statistical_benchmark.log
#   comparison_benchmark.log
#   multicore_benchmark.log
#   concurrent_benchmark.log
#   concurrent_signing_benchmark.log
#   sign_benchmark.log
#   signature_size_analysis.log
#   classical_benchmark.log
#   comprehensive_comparison.log
#   SUMMARY.md               — extracted tables + JSON from every run
#
# Usage:
#   cd /path/to/qMEMO
#   bash scripts/run_logged.sh

set -euo pipefail

# ── Paths ─────────────────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="$REPO_ROOT/benchmarks"
BIN_DIR="$BENCH_DIR/bin"
TIMESTAMP="$(date -u +%Y%m%d_%H%M%S)"
RUN_DIR="$BENCH_DIR/results/run_$TIMESTAMP"

mkdir -p "$RUN_DIR"
echo "Run directory: $RUN_DIR"
echo ""

# ── System information ─────────────────────────────────────────────────────
SYS_LOG="$RUN_DIR/system_info.log"
{
    echo "================================================================"
    echo "  qMEMO System Information"
    echo "  Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "================================================================"
    echo ""
    echo "--- OS / Kernel ---"
    uname -a

    echo ""
    echo "--- CPU ---"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        sysctl -n machdep.cpu.brand_string    2>/dev/null || true
        sysctl -n hw.physicalcpu              2>/dev/null | xargs -I{} echo "Physical cores: {}"
        sysctl -n hw.logicalcpu               2>/dev/null | xargs -I{} echo "Logical cores:  {}"
        sysctl -n hw.memsize                  2>/dev/null | \
            awk '{printf "RAM: %.1f GB\n", $1/1073741824}'
    else
        grep "model name" /proc/cpuinfo | head -1 || true
        nproc --all | xargs -I{} echo "Logical cores: {}"
        grep MemTotal /proc/meminfo            || true
    fi

    echo ""
    echo "--- Compiler ---"
    cc --version 2>&1 | head -2

    echo ""
    echo "--- liboqs ---"
    grep "OQS_VERSION_TEXT" "$REPO_ROOT/liboqs_install/include/oqs/oqsconfig.h" \
        2>/dev/null | head -1 || echo "(oqsconfig.h not found)"

    echo ""
    echo "--- OpenSSL ---"
    if [[ -x /opt/homebrew/opt/openssl/bin/openssl ]]; then
        /opt/homebrew/opt/openssl/bin/openssl version
    else
        openssl version 2>/dev/null || echo "(openssl not found in PATH)"
    fi

    echo ""
    echo "--- Build flags ---"
    head -60 "$BENCH_DIR/Makefile" | grep -E "^CFLAGS|^LDFLAGS|^LDLIBS|^OPENSSL_ROOT|^OQS_ROOT" || true

} | tee "$SYS_LOG"
echo ""

# ── Build ──────────────────────────────────────────────────────────────────
echo "================================================================"
echo "  Building all benchmarks …"
echo "================================================================"
(cd "$BENCH_DIR" && make clean all 2>&1) | tee "$RUN_DIR/build.log"
echo ""

# ── Benchmark list ─────────────────────────────────────────────────────────
# Format: "binary_name|Log label"
# key_inspection runs first (correctness audit before throughput tests)
BENCHMARKS=(
    "key_inspection|Key Material Inspection (correctness audit)"
    "verify_benchmark|Verify Benchmark (single-pass, 10K iterations)"
    "statistical_benchmark|Statistical Benchmark (1000 trials x 100 ops)"
    "comparison_benchmark|Comparison Benchmark (Falcon-512 vs ML-DSA-44)"
    "multicore_benchmark|Multicore Verification (1/2/4/6/8/10 cores)"
    "concurrent_benchmark|Concurrent Verification (thread pool)"
    "concurrent_signing_benchmark|Concurrent Signing (thread pool)"
    "sign_benchmark|Multicore Signing (1/2/4/6/8/10 cores)"
    "signature_size_analysis|Signature Size Distribution (10K sigs each)"
    "classical_benchmark|Classical Baselines (ECDSA secp256k1 + Ed25519)"
    "comprehensive_comparison|Comprehensive Comparison (all 7 algorithms)"
)

# ── Run each benchmark ─────────────────────────────────────────────────────
FAILED=()

for entry in "${BENCHMARKS[@]}"; do
    BIN="${entry%%|*}"
    LABEL="${entry##*|}"
    LOG="$RUN_DIR/${BIN}.log"

    echo "================================================================"
    echo "  $LABEL"
    echo "================================================================"

    if [[ ! -x "$BIN_DIR/$BIN" ]]; then
        echo "  SKIP: $BIN_DIR/$BIN not found"
        FAILED+=("$BIN")
        continue
    fi

    # Add timestamp header to each log file
    {
        echo "================================================================"
        echo "  $LABEL"
        echo "  Run: $TIMESTAMP"
        echo "================================================================"
        echo ""
    } > "$LOG"

    # Run with time measurement, tee to log
    START_TS=$(date +%s%N 2>/dev/null || date +%s)
    "$BIN_DIR/$BIN" 2>&1 | tee -a "$LOG"
    EXIT_CODE="${PIPESTATUS[0]}"
    END_TS=$(date +%s%N 2>/dev/null || date +%s)

    # Elapsed (nanosecond precision on Linux, seconds on macOS fallback)
    if [[ ${#START_TS} -gt 10 ]]; then
        ELAPSED_MS=$(( (END_TS - START_TS) / 1000000 ))
        echo "" | tee -a "$LOG"
        echo "  [Elapsed: ${ELAPSED_MS} ms]" | tee -a "$LOG"
    fi

    if [[ $EXIT_CODE -ne 0 ]]; then
        echo "  *** BENCHMARK EXITED WITH CODE $EXIT_CODE ***" | tee -a "$LOG"
        FAILED+=("$BIN")
    fi

    echo ""
done

# ── Generate SUMMARY.md ───────────────────────────────────────────────────
SUMMARY="$RUN_DIR/SUMMARY.md"
{
    echo "# qMEMO Benchmark Run — $TIMESTAMP"
    echo ""
    echo "**Platform:** $(uname -m) / $(uname -s)"
    echo "**Date:** $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "**liboqs:** $(grep OQS_VERSION_TEXT "$REPO_ROOT/liboqs_install/include/oqs/oqsconfig.h" 2>/dev/null | awk -F'"' '{print $2}')"
    echo ""
    echo "---"
    echo ""

    for entry in "${BENCHMARKS[@]}"; do
        BIN="${entry%%|*}"
        LABEL="${entry##*|}"
        LOG="$RUN_DIR/${BIN}.log"

        echo "## $LABEL"
        echo ""
        echo '```'

        if [[ -f "$LOG" ]]; then
            # Extract the human-readable table section (before "--- JSON ---")
            awk '/--- JSON ---/{exit} NR>4{print}' "$LOG" | \
                grep -v "^$" | tail -40
        else
            echo "(log not found)"
        fi

        echo '```'
        echo ""

        # Extract and pretty-print JSON block if present
        if [[ -f "$LOG" ]] && grep -q "^--- JSON ---" "$LOG"; then
            echo "<details><summary>JSON output</summary>"
            echo ""
            echo '```json'
            awk '/^--- JSON ---/{found=1; next} found{print}' "$LOG"
            echo '```'
            echo ""
            echo "</details>"
            echo ""
        fi

        echo "---"
        echo ""
    done

    # Failed benchmarks
    if [[ ${#FAILED[@]} -gt 0 ]]; then
        echo "## Failed Benchmarks"
        echo ""
        for f in "${FAILED[@]}"; do
            echo "- \`$f\`"
        done
        echo ""
    fi
} > "$SUMMARY"

# ── Final report ───────────────────────────────────────────────────────────
echo "================================================================"
echo "  Run complete"
echo "================================================================"
echo ""
echo "  Log directory:  $RUN_DIR"
echo "  Summary:        $SUMMARY"
echo "  System info:    $SYS_LOG"
echo ""
echo "  Per-benchmark log files:"
for entry in "${BENCHMARKS[@]}"; do
    BIN="${entry%%|*}"
    LOG="$RUN_DIR/${BIN}.log"
    if [[ -f "$LOG" ]]; then
        SIZE=$(wc -c < "$LOG")
        printf "    %-42s  %6d bytes\n" "${BIN}.log" "$SIZE"
    fi
done
echo ""

if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo "  FAILED: ${FAILED[*]}"
    exit 1
else
    echo "  All benchmarks passed."
fi
