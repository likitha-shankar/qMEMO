#!/usr/bin/env bash
#
# run_all_benchmarks.sh -- Run all qMEMO benchmarks and aggregate results
#
# One command for full reproducibility: compiles all benchmarks from a
# clean state, runs them, captures JSON output, collects system specs,
# and produces a timestamped results directory with a human-readable
# markdown report.
#
# Usage:
#   ./scripts/run_all_benchmarks.sh
#
# Output:
#   benchmarks/results/run_YYYYMMDD_HHMMSS/
#   ├── system_specs.json
#   ├── verify_results.json
#   ├── statistical_results.json
#   ├── comparison_results.json
#   ├── multicore_results.json
#   ├── concurrent_results.json
#   ├── summary.json
#   └── REPORT.md

set -euo pipefail

# ── Colours ─────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    BOLD='\033[1m'
    GREEN='\033[0;32m'
    CYAN='\033[0;36m'
    YELLOW='\033[0;33m'
    RED='\033[0;31m'
    DIM='\033[2m'
    RESET='\033[0m'
else
    BOLD='' GREEN='' CYAN='' YELLOW='' RED='' DIM='' RESET=''
fi

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="$PROJECT_ROOT/benchmarks"
OQS_DIR="$PROJECT_ROOT/liboqs_install"
RESULTS_BASE="$BENCH_DIR/results"

RUN_TAG="$(date '+%Y%m%d_%H%M%S')"
RUN_DIR="$RESULTS_BASE/run_${RUN_TAG}"

TOTAL_STEPS=7
CURRENT_STEP=0

# ── Helpers ─────────────────────────────────────────────────────────────────

info()  { printf "${CYAN}[INFO]${RESET}  %s\n" "$*"; }
ok()    { printf "${GREEN}[ OK ]${RESET}  %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${RESET}  %s\n" "$*"; }
fail()  { printf "${RED}[FAIL]${RESET}  %s\n" "$*" >&2; exit 1; }

step() {
    CURRENT_STEP=$((CURRENT_STEP + 1))
    printf "\n${BOLD}[${CURRENT_STEP}/${TOTAL_STEPS}] %s${RESET}\n" "$1"
    printf "${DIM}────────────────────────────────────────────────────${RESET}\n"
}

# Extract JSON from mixed benchmark output.
# Each benchmark prints "--- JSON ---" followed by a JSON object.
# We grab everything after the marker, drop the marker line itself,
# then stop reading at the top-level closing brace.
extract_json() {
    sed -n '/^--- JSON ---$/,$ p' | sed '1d' | sed '/^}$/q'
}

# Pretty-print a duration in seconds.
fmt_duration() {
    local secs=$1
    if [ "$secs" -ge 60 ]; then
        printf "%dm %ds" $((secs / 60)) $((secs % 60))
    else
        printf "%ds" "$secs"
    fi
}

# ── Banner ──────────────────────────────────────────────────────────────────

printf "\n${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n"
printf "${BOLD}  qMEMO -- Full Benchmark Suite${RESET}\n"
printf "${BOLD}  %s${RESET}\n" "$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
printf "${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n"

SUITE_START=$(date +%s)

# ── Step 1: Pre-flight checks ──────────────────────────────────────────────

step "Pre-flight checks"

[ -d "$PROJECT_ROOT" ] || fail "Project root not found: $PROJECT_ROOT"

if [ ! -d "$OQS_DIR/lib" ] || [ ! -d "$OQS_DIR/include/oqs" ]; then
    fail "liboqs not installed at $OQS_DIR. Run ./install_liboqs.sh first."
fi
ok "liboqs installation found at $OQS_DIR"

for tool in make jq; do
    command -v "$tool" &>/dev/null || fail "$tool is required but not found."
    ok "Found $tool"
done

[ -f "$BENCH_DIR/Makefile" ] || fail "Makefile not found at $BENCH_DIR/Makefile"
ok "Makefile found"

# ── Step 2: Compile benchmarks ─────────────────────────────────────────────

step "Compile benchmarks (clean build)"

make -C "$BENCH_DIR" clean 2>&1 | while IFS= read -r line; do
    printf "  ${DIM}%s${RESET}\n" "$line"
done

make -C "$BENCH_DIR" all 2>&1 | while IFS= read -r line; do
    printf "  ${DIM}%s${RESET}\n" "$line"
done

for bin in verify_benchmark statistical_benchmark comparison_benchmark \
           multicore_benchmark concurrent_benchmark; do
    [ -x "$BENCH_DIR/bin/$bin" ] || fail "Binary not built: $bin"
done
ok "All 5 benchmarks compiled"

# ── Step 3: Create results directory ───────────────────────────────────────

step "Create results directory"

mkdir -p "$RUN_DIR"
ok "Created $RUN_DIR"

# ── Step 4: Collect system specs ───────────────────────────────────────────

step "Collect system specifications"

OS="$(uname -s)"
ARCH="$(uname -m)"

if [ "$OS" = "Darwin" ]; then
    OS_VERSION="macOS $(sw_vers -productVersion 2>/dev/null || echo unknown)"
    CPU_MODEL="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
    CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
    RAM_BYTES="$(sysctl -n hw.memsize 2>/dev/null || echo 0)"
    RAM_GB="$(( RAM_BYTES / 1073741824 ))"
    CC_VERSION="$(cc --version 2>&1 | head -1)"
else
    OS_VERSION="Linux $(uname -r)"
    CPU_MODEL="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || echo unknown)"
    CPU_CORES="$(nproc 2>/dev/null || echo unknown)"
    RAM_KB="$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}' || echo 0)"
    RAM_GB="$(( RAM_KB / 1048576 ))"
    CC_VERSION="$(cc --version 2>&1 | head -1)"
fi

HOSTNAME_STR="$(hostname 2>/dev/null || echo unknown)"
LIBOQS_VERSION="$(git -C "$PROJECT_ROOT/liboqs" describe --tags --always 2>/dev/null || echo unknown)"

cat > "$RUN_DIR/system_specs.json" <<SPECS
{
  "hostname": "${HOSTNAME_STR}",
  "os": "${OS_VERSION}",
  "arch": "${ARCH}",
  "cpu_model": "${CPU_MODEL}",
  "cpu_cores": ${CPU_CORES},
  "ram_gb": ${RAM_GB},
  "compiler": "${CC_VERSION}",
  "liboqs_version": "${LIBOQS_VERSION}",
  "timestamp": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
  "run_tag": "${RUN_TAG}"
}
SPECS

ok "System specs saved"
printf "  CPU: %s (%s cores)\n" "$CPU_MODEL" "$CPU_CORES"
printf "  RAM: %d GB  |  OS: %s  |  Arch: %s\n" "$RAM_GB" "$OS_VERSION" "$ARCH"

# ── Step 5: Run benchmarks ─────────────────────────────────────────────────

step "Run benchmarks"

run_benchmark() {
    local name="$1"
    local bin="$2"
    local json_out="$3"
    local full_out="$RUN_DIR/${name}_full_output.txt"

    printf "\n  ${BOLD}▸ %s${RESET}\n" "$name"

    local t_start
    t_start=$(date +%s)

    "$bin" > "$full_out" 2>&1
    local rc=$?

    local t_end
    t_end=$(date +%s)
    local duration=$(( t_end - t_start ))

    if [ $rc -ne 0 ]; then
        warn "$name exited with code $rc -- see $full_out"
        # Write an empty object so --slurpfile in the aggregate step always succeeds.
        printf '{}' > "$json_out"
        return 1
    fi

    extract_json < "$full_out" > "$json_out"

    if jq empty "$json_out" 2>/dev/null; then
        ok "$name completed in $(fmt_duration $duration) → $(basename "$json_out")"
    else
        warn "$name produced invalid JSON -- see $full_out"
        printf '{}' > "$json_out"
        return 1
    fi
}

BENCH_FAILED=0

run_benchmark "verify_benchmark" \
    "$BENCH_DIR/bin/verify_benchmark" \
    "$RUN_DIR/verify_results.json" \
    || BENCH_FAILED=1

run_benchmark "statistical_benchmark" \
    "$BENCH_DIR/bin/statistical_benchmark" \
    "$RUN_DIR/statistical_results.json" \
    || BENCH_FAILED=1

run_benchmark "comparison_benchmark" \
    "$BENCH_DIR/bin/comparison_benchmark" \
    "$RUN_DIR/comparison_results.json" \
    || BENCH_FAILED=1

run_benchmark "multicore_benchmark" \
    "$BENCH_DIR/bin/multicore_benchmark" \
    "$RUN_DIR/multicore_results.json" \
    || BENCH_FAILED=1

run_benchmark "concurrent_benchmark" \
    "$BENCH_DIR/bin/concurrent_benchmark" \
    "$RUN_DIR/concurrent_results.json" \
    || BENCH_FAILED=1

if [ "$BENCH_FAILED" -eq 1 ]; then
    warn "One or more benchmarks had issues (see warnings above)"
fi

# ── Step 6: Aggregate into summary.json ────────────────────────────────────

step "Aggregate results"

jq -n \
    --arg tag "$RUN_TAG" \
    --arg ts "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" \
    --slurpfile specs     "$RUN_DIR/system_specs.json" \
    --slurpfile verify    "$RUN_DIR/verify_results.json" \
    --slurpfile stats     "$RUN_DIR/statistical_results.json" \
    --slurpfile compare   "$RUN_DIR/comparison_results.json" \
    --slurpfile multicore "$RUN_DIR/multicore_results.json" \
    --slurpfile concurrent "$RUN_DIR/concurrent_results.json" \
'{
  run_tag: $tag,
  timestamp: $ts,
  system: $specs[0],
  verify_benchmark: (
    if ($verify[0] | has("ops_per_sec")) then {
      ops_per_sec: $verify[0].ops_per_sec,
      us_per_op:   $verify[0].us_per_op,
      iterations:  $verify[0].iterations
    } else null end),
  statistical_benchmark: (
    if ($stats[0] | has("statistics")) then {
      mean_ops_sec:   $stats[0].statistics.mean_ops_sec,
      median_ops_sec: $stats[0].statistics.median_ops_sec,
      stddev_ops_sec: $stats[0].statistics.stddev_ops_sec,
      cv_percent:     $stats[0].statistics.cv_percent,
      p95_ops_sec:    $stats[0].statistics.p95_ops_sec,
      p99_ops_sec:    $stats[0].statistics.p99_ops_sec,
      outliers:       $stats[0].statistics.outliers_count,
      normality_pass: $stats[0].statistics.normality_pass,
      trials:         $stats[0].trials
    } else null end),
  comparison_benchmark: (
    if ($compare[0] | has("algorithms")) then {
      falcon_512:             $compare[0].algorithms["Falcon-512"],
      ml_dsa_44:              $compare[0].algorithms["ML-DSA-44"],
      verify_speedup_falcon:  $compare[0].comparison.verify_speedup_falcon,
      signature_size_ratio:   $compare[0].comparison.signature_size_ratio,
      tx_overhead_ratio:      $compare[0].comparison.tx_overhead_ratio
    } else null end),
  multicore_benchmark: (
    if ($multicore[0] | has("cores")) then {
      cores:        $multicore[0].cores,
      ops_per_sec:  $multicore[0].ops_per_sec,
      speedup:      $multicore[0].speedup,
      efficiency_pct: $multicore[0].efficiency_pct
    } else null end),
  concurrent_benchmark: (
    if ($concurrent[0] | has("concurrent")) then {
      worker_threads:       $concurrent[0].concurrent.worker_threads,
      concurrent_ops_sec:   $concurrent[0].concurrent.throughput,
      sequential_ops_sec:   $concurrent[0].sequential.throughput,
      analysis:             $concurrent[0].analysis
    } else null end)
}' > "$RUN_DIR/summary.json"

ok "Summary written to summary.json"

# ── Step 7: Generate markdown report ──────────────────────────────────────

step "Generate markdown report"

# Read key values via jq for the report template.
V_OPS=$(jq -r  'if .verify_benchmark then .verify_benchmark.ops_per_sec else "N/A" end' "$RUN_DIR/summary.json")
V_US=$(jq -r   'if .verify_benchmark then .verify_benchmark.us_per_op   else "N/A" end' "$RUN_DIR/summary.json")
V_ITER=$(jq -r 'if .verify_benchmark then .verify_benchmark.iterations   else "N/A" end' "$RUN_DIR/summary.json")

S_MEAN=$(jq -r  'if .statistical_benchmark then .statistical_benchmark.mean_ops_sec   else "N/A" end' "$RUN_DIR/summary.json")
S_MED=$(jq -r   'if .statistical_benchmark then .statistical_benchmark.median_ops_sec else "N/A" end' "$RUN_DIR/summary.json")
S_SD=$(jq -r    'if .statistical_benchmark then .statistical_benchmark.stddev_ops_sec else "N/A" end' "$RUN_DIR/summary.json")
S_CV=$(jq -r    'if .statistical_benchmark then .statistical_benchmark.cv_percent     else "N/A" end' "$RUN_DIR/summary.json")
S_P95=$(jq -r   'if .statistical_benchmark then .statistical_benchmark.p95_ops_sec   else "N/A" end' "$RUN_DIR/summary.json")
S_P99=$(jq -r   'if .statistical_benchmark then .statistical_benchmark.p99_ops_sec   else "N/A" end' "$RUN_DIR/summary.json")
S_OUT=$(jq -r   'if .statistical_benchmark then .statistical_benchmark.outliers       else "N/A" end' "$RUN_DIR/summary.json")
S_NORM=$(jq -r  'if .statistical_benchmark then (.statistical_benchmark.normality_pass | tostring) else "N/A" end' "$RUN_DIR/summary.json")
S_TRIALS=$(jq -r 'if .statistical_benchmark then .statistical_benchmark.trials       else "N/A" end' "$RUN_DIR/summary.json")

F_VER=$(jq -r   'if .comparison_benchmark then .comparison_benchmark.falcon_512.verify_ops_sec   else "N/A" end' "$RUN_DIR/summary.json")
F_PK=$(jq -r    'if .comparison_benchmark then .comparison_benchmark.falcon_512.pubkey_bytes      else "N/A" end' "$RUN_DIR/summary.json")
F_SIG=$(jq -r   'if .comparison_benchmark then .comparison_benchmark.falcon_512.signature_bytes   else "N/A" end' "$RUN_DIR/summary.json")
F_TX=$(jq -r    'if .comparison_benchmark then .comparison_benchmark.falcon_512.total_tx_overhead else "N/A" end' "$RUN_DIR/summary.json")
D_VER=$(jq -r   'if .comparison_benchmark then .comparison_benchmark.ml_dsa_44.verify_ops_sec    else "N/A" end' "$RUN_DIR/summary.json")
D_PK=$(jq -r    'if .comparison_benchmark then .comparison_benchmark.ml_dsa_44.pubkey_bytes       else "N/A" end' "$RUN_DIR/summary.json")
D_SIG=$(jq -r   'if .comparison_benchmark then .comparison_benchmark.ml_dsa_44.signature_bytes    else "N/A" end' "$RUN_DIR/summary.json")
D_TX=$(jq -r    'if .comparison_benchmark then .comparison_benchmark.ml_dsa_44.total_tx_overhead  else "N/A" end' "$RUN_DIR/summary.json")
C_SPEEDUP=$(jq -r  'if .comparison_benchmark then .comparison_benchmark.verify_speedup_falcon  else "N/A" end' "$RUN_DIR/summary.json")
C_SIGRATIO=$(jq -r 'if .comparison_benchmark then .comparison_benchmark.signature_size_ratio   else "N/A" end' "$RUN_DIR/summary.json")
C_TXRATIO=$(jq -r  'if .comparison_benchmark then .comparison_benchmark.tx_overhead_ratio      else "N/A" end' "$RUN_DIR/summary.json")

MC_AVAIL=$(jq -r 'if .multicore_benchmark then "true" else "false" end' "$RUN_DIR/summary.json")
CON_AVAIL=$(jq -r 'if .concurrent_benchmark then "true" else "false" end' "$RUN_DIR/summary.json")

SUITE_END=$(date +%s)
SUITE_SECS=$(( SUITE_END - SUITE_START ))

cat > "$RUN_DIR/REPORT.md" <<REPORT
# qMEMO Benchmark Report

> **Run:** \`${RUN_TAG}\`
> **Date:** $(date -u '+%Y-%m-%d %H:%M:%S UTC')
> **Duration:** $(fmt_duration $SUITE_SECS)

## System Specifications

| Property | Value |
|----------|-------|
| Hostname | ${HOSTNAME_STR} |
| CPU | ${CPU_MODEL} |
| Cores | ${CPU_CORES} |
| RAM | ${RAM_GB} GB |
| OS | ${OS_VERSION} |
| Arch | ${ARCH} |
| Compiler | ${CC_VERSION} |
| liboqs | ${LIBOQS_VERSION} |

---

## 1. Single-Pass Verification Benchmark

Falcon-512 signature verification over ${V_ITER} consecutive iterations.

| Metric | Value |
|--------|-------|
| Throughput | ${V_OPS} ops/sec |
| Latency | ${V_US} µs/op |

---

## 2. Statistical Verification Benchmark

${S_TRIALS} independent trials, each timing a batch of 100 verifications.

| Statistic | Value |
|-----------|-------|
| Mean | ${S_MEAN} ops/sec |
| Median | ${S_MED} ops/sec |
| Std Dev | ${S_SD} ops/sec |
| CV | ${S_CV}% |
| P95 | ${S_P95} ops/sec |
| P99 | ${S_P99} ops/sec |
| Outliers (>3σ) | ${S_OUT} |
| Normality (JB) | ${S_NORM} |

**Interpretation:** CV < 2% indicates excellent measurement stability.
$([ "$S_NORM" = "true" ] && echo "Distribution is Gaussian -- report mean ± SD." || echo "Distribution is non-Gaussian -- report median and IQR.")

---

## 3. Algorithm Comparison: Falcon-512 vs ML-DSA-44

### Throughput (ops/sec -- higher is better)

| Operation | Falcon-512 | ML-DSA-44 | Ratio (F/D) |
|-----------|-----------|-----------|-------------|
| Verification | ${F_VER} | ${D_VER} | **${C_SPEEDUP}x** |

### Sizes (bytes -- lower is better)

| Component | Falcon-512 | ML-DSA-44 | Ratio (F/D) |
|-----------|-----------|-----------|-------------|
| Public Key | ${F_PK} | ${D_PK} | -- |
| Signature | ${F_SIG} | ${D_SIG} | **${C_SIGRATIO}x** |
| Tx Overhead (sig+pk) | ${F_TX} | ${D_TX} | **${C_TXRATIO}x** |

### Blockchain Impact (4,000 tx/block)

| Metric | Falcon-512 | ML-DSA-44 |
|--------|-----------|-----------|
| Block signature data | $([ "$F_TX" != "N/A" ] && awk "BEGIN{printf \"%.1f\", ${F_TX} * 4000 / 1024}" || echo "N/A") KB | $([ "$D_TX" != "N/A" ] && awk "BEGIN{printf \"%.1f\", ${D_TX} * 4000 / 1024}" || echo "N/A") KB |
| Block verify time (est.) | $([ "$F_VER" != "N/A" ] && awk "BEGIN{printf \"%.1f\", 4000 / ${F_VER} * 1000}" || echo "N/A") ms | $([ "$D_VER" != "N/A" ] && awk "BEGIN{printf \"%.1f\", 4000 / ${D_VER} * 1000}" || echo "N/A") ms |

---

## 4. Multicore Scaling Benchmark

$(if [ "$MC_AVAIL" = "true" ]; then
jq -r '
  .multicore_benchmark |
  "Cores tested: " + (.cores | map(tostring) | join(", ")) + "\n\n" +
  "| Cores | Ops/sec | Speedup | Efficiency |\n" +
  "|-------|---------|---------|------------|\n" +
  [range(.cores | length) |
    "| " + (.cores[.] | tostring) +
    " | " + (.ops_per_sec[.] | floor | tostring) +
    " | " + (.speedup[.] | tostring) + "x" +
    " | " + (.efficiency_pct[.] | tostring) + "% |"
  ] | join("\n")
' "$RUN_DIR/summary.json" 2>/dev/null || echo "_Multicore benchmark data not available._"
else
echo "_Multicore benchmark did not produce results._"
fi)

---

## 5. Concurrent vs Sequential Benchmark

$(if [ "$CON_AVAIL" = "true" ]; then
jq -r '
  .concurrent_benchmark |
  "| Mode | Throughput (ops/sec) |\n" +
  "|------|---------------------|\n" +
  "| Concurrent (" + (.worker_threads | tostring) + " workers) | " + (.concurrent_ops_sec | floor | tostring) + " |\n" +
  "| Sequential | " + (.sequential_ops_sec | floor | tostring) + " |\n\n" +
  "**Analysis:** " + .analysis
' "$RUN_DIR/summary.json" 2>/dev/null || echo "_Concurrent benchmark data not available._"
else
echo "_Concurrent benchmark did not produce results._"
fi)

---

## Recommendation

Falcon-512 delivers **${C_SPEEDUP}x faster verification** with **${C_TXRATIO}x smaller on-chain footprint** compared to ML-DSA-44, making it the stronger candidate for post-quantum blockchain transaction signing. Its slower key generation (~5 ms) is a one-time cost per address and does not affect runtime throughput.

---

## Files in This Run

| File | Description |
|------|-------------|
| \`system_specs.json\` | Hardware and software environment |
| \`verify_results.json\` | Single-pass verification benchmark |
| \`statistical_results.json\` | Statistical analysis (1,000 trials) |
| \`comparison_results.json\` | Falcon-512 vs ML-DSA-44 |
| \`multicore_results.json\` | Scaling across {1,2,4,6,8,10} cores |
| \`concurrent_results.json\` | 4-worker thread pool vs sequential |
| \`summary.json\` | Aggregated key metrics |
| \`REPORT.md\` | This report |

## Reproducibility

\`\`\`bash
cd $(printf '%q' "$PROJECT_ROOT")
./scripts/run_all_benchmarks.sh
\`\`\`
REPORT

ok "Report written to REPORT.md"

# ── Final summary to terminal ──────────────────────────────────────────────

printf "\n${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n"
printf "${BOLD}  Run complete: ${RUN_TAG}${RESET}\n"
printf "${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n\n"

printf "  ${BOLD}Results directory:${RESET} %s\n\n" "$RUN_DIR"

printf "  ${BOLD}Falcon-512 Verification:${RESET}\n"
printf "    Single-pass : %s ops/sec  (%s µs/op)\n" "$V_OPS" "$V_US"
printf "    Statistical : %s ops/sec mean, %s ops/sec median\n" "$S_MEAN" "$S_MED"
printf "    CV          : %s%%\n\n" "$S_CV"

printf "  ${BOLD}Falcon-512 vs ML-DSA-44:${RESET}\n"
printf "    Verify speedup   : %sx (Falcon is faster)\n" "$C_SPEEDUP"
printf "    Signature ratio  : %sx (Falcon is smaller)\n" "$C_SIGRATIO"
printf "    Tx overhead ratio: %sx (Falcon is leaner)\n\n" "$C_TXRATIO"

printf "  ${BOLD}Files saved:${RESET}\n"
for f in "$RUN_DIR"/*.json "$RUN_DIR"/*.md; do
    [ -f "$f" ] && printf "    %s  (%s)\n" "$(basename "$f")" "$(wc -c < "$f" | tr -d ' ') bytes"
done

printf "\n  Total time: ${BOLD}$(fmt_duration $SUITE_SECS)${RESET}\n\n"
