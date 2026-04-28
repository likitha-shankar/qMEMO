#!/usr/bin/env bash
#
# perf_spotcheck.sh
#
# Targeted perf-counter passes to validate the three soft claims from
# the Experiment 1 analysis before committing to Experiment 2.
#
# Run from the qMEMO/benchmarks/ directory on a Chameleon
# compute_cascadelake_r node, after prepare_system.sh has set the
# governor, disabled THP, etc.
#
# Total runtime: ~25-40 minutes including cool-downs.
#
# Outputs land in ./results/perf_spotcheck_<timestamp>/
#

set -euo pipefail

# ---------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------

BENCH_BIN="${BENCH_BIN:-./bin/bench_sign}"
ITERATIONS="${ITERATIONS:-100000}"
ITERATIONS_BIMODAL="${ITERATIONS_BIMODAL:-1000000}"
WARMUP="${WARMUP:-1000}"
WARMUP_BIMODAL="${WARMUP_BIMODAL:-10000}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT="./results/perf_spotcheck_${TS}"
mkdir -p "${OUT}"

# Events we capture. Grouped because perf can only multiplex a finite
# number simultaneously; we run two passes per config so each group
# gets full counter coverage.
EVENTS_GROUP_A="cycles,instructions,branches,branch-misses,\
cache-references,cache-misses,\
LLC-loads,LLC-load-misses,\
cycle_activity.stalls_l3_miss,\
cycle_activity.stalls_mem_any,\
cycle_activity.stalls_total"

EVENTS_GROUP_B="cycles,instructions,\
L1-dcache-loads,L1-dcache-load-misses,\
dTLB-loads,dTLB-load-misses,\
mem_inst_retired.all_loads,\
mem_inst_retired.all_stores,\
offcore_requests.all_data_rd"

# topdown-fe-bound etc. are not available on all kernels; use the raw slot
# counters that are universally present on Cascade Lake:
#   slots-retired / total-slots ≈ retiring
#   fetch-bubbles / total-slots ≈ frontend-bound
#   recovery-bubbles / total-slots ≈ bad-speculation
EVENTS_TOPDOWN="topdown-total-slots,topdown-slots-retired,\
topdown-fetch-bubbles,topdown-recovery-bubbles"

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

log() { printf '[%(%H:%M:%S)T] %s\n' -1 "$*" | tee -a "${OUT}/run.log"; }

cool_down() {
  log "  cool-down ${1}s"
  sync
  echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
  sleep "${1}"
}

run_perf() {
  local label="$1"
  local algo="$2"
  local threads="$3"
  local pin_list="$4"
  local events="$5"
  local iters="${6:-$ITERATIONS}"
  local warmup="${7:-$WARMUP}"

  local prefix="${OUT}/${label}"
  local perf_csv="${prefix}_perf.csv"

  log "==> ${label}: algo=${algo} threads=${threads} iters=${iters} cores=${pin_list}"

  # perf stat -x , gives CSV; -o file writes to file; --pre runs before;
  # we don't need to fork the bench separately because perf will run it.
  perf stat \
    -x , \
    -o "${perf_csv}" \
    --append \
    -e "${events}" \
    -- \
    "${BENCH_BIN}" \
      --algo "${algo}" \
      --threads "${threads}" \
      --cores "${pin_list}" \
      --iterations "${iters}" \
      --warmup "${warmup}" \
      --runs 1 \
      --pin-strategy compact \
      --output-prefix "${prefix}" \
      --tag "perf_spotcheck_${label}" \
    >> "${OUT}/run.log" 2>&1 || {
      log "    WARN: perf stat exited non-zero for ${label} (unsupported event?); continuing"
  }

  log "    -> ${perf_csv}"
}

# ---------------------------------------------------------------------
# 0. System sanity
# ---------------------------------------------------------------------

log "=== qMEMO perf spot-check ==="
log "Output dir: ${OUT}"

if [[ ! -x "${BENCH_BIN}" ]]; then
  log "ERROR: ${BENCH_BIN} not found or not executable"
  exit 1
fi

if ! command -v perf >/dev/null 2>&1; then
  log "ERROR: perf not installed. Run: sudo apt-get install linux-tools-\$(uname -r)"
  exit 1
fi

# Check perf paranoid level (need <=1 for most counters, <=2 for some)
PARANOID="$(cat /proc/sys/kernel/perf_event_paranoid)"
log "perf_event_paranoid = ${PARANOID}"
if [[ "${PARANOID}" -gt 1 ]]; then
  log "WARN: perf_event_paranoid=${PARANOID} may block some counters."
  log "      Consider: echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid"
fi

# Capture full env snapshot
{
  echo "=== lscpu ==="
  lscpu
  echo
  echo "=== numactl ==="
  numactl --hardware
  echo
  echo "=== governor ==="
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
  echo
  echo "=== turbo ==="
  cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo "no_turbo file missing"
  echo
  echo "=== THP ==="
  cat /sys/kernel/mm/transparent_hugepage/enabled
  echo
  echo "=== meminfo ==="
  head -5 /proc/meminfo
} > "${OUT}/env.txt"

log "Env snapshot saved to ${OUT}/env.txt"
echo

# ---------------------------------------------------------------------
# 1. Ed25519 scaling cliff: is it really memory-bandwidth-bound?
#
# If the memory-bandwidth hypothesis is right, stalls_l3_miss and
# offcore_requests.all_data_rd should rise sharply from threads=1 to
# threads=96. If they stay flat but IPC drops, the bottleneck is
# something else (allocator contention, syscalls, lock contention).
# ---------------------------------------------------------------------

log "--- Phase 1: Ed25519 scaling diagnosis ---"

for tc in 1 24 48 96; do
  case "${tc}" in
    1)  cores="0" ;;
    24) cores="0-23" ;;
    48) cores="0-47" ;;
    96) cores="0-95" ;;
  esac

  run_perf "ed25519_t${tc}_groupA" "ed25519" "${tc}" "${cores}" "${EVENTS_GROUP_A}"
  cool_down 15

  run_perf "ed25519_t${tc}_groupB" "ed25519" "${tc}" "${cores}" "${EVENTS_GROUP_B}"
  cool_down 15

  run_perf "ed25519_t${tc}_topdown" "ed25519" "${tc}" "${cores}" "${EVENTS_TOPDOWN}"
  cool_down 15
done

# ---------------------------------------------------------------------
# 2. ML-DSA-44 bimodality: real or noise?
#
# Re-collect the latency CDF at threads=1 with 10x more iterations
# and 10x longer warmup. If the bimodality persists, it is real.
# If it disappears, it was warmup or sampler noise from the original
# run.
#
# We run this WITHOUT perf wrapping so the perf overhead does not
# itself create a bimodal distribution. The output we care about is
# the latencies CSV from bench_sign, not perf counters.
# ---------------------------------------------------------------------

log "--- Phase 2: ML-DSA-44 bimodality check (long run, no perf) ---"

cool_down 30  # extra cool down before this clean run

"${BENCH_BIN}" \
  --algo dilithium2 \
  --threads 1 \
  --cores "0" \
  --iterations "${ITERATIONS_BIMODAL}" \
  --warmup "${WARMUP_BIMODAL}" \
  --runs 1 \
  --pin-strategy compact \
  --output-prefix "${OUT}/dilithium2_bimodal_check" \
  --tag "bimodal_check_long" \
  >> "${OUT}/run.log" 2>&1

log "    -> ${OUT}/dilithium2_bimodal_check_latencies.csv"
log "    Inspect with analyze.py --plot-cdf or by hand: if two clusters"
log "    persist at 1M iterations / 10k warmup, bimodality is real."

# Also compare against Falcon at the same length, since Falcon is the
# algorithm that SHOULD show a tail (rejection sampling).
cool_down 15

"${BENCH_BIN}" \
  --algo falcon512 \
  --threads 1 \
  --cores "0" \
  --iterations "${ITERATIONS_BIMODAL}" \
  --warmup "${WARMUP_BIMODAL}" \
  --runs 1 \
  --pin-strategy compact \
  --output-prefix "${OUT}/falcon_tail_check" \
  --tag "tail_check_long" \
  >> "${OUT}/run.log" 2>&1

log "    -> ${OUT}/falcon_tail_check_latencies.csv"
log "    Falcon SHOULD show a tail. If Falcon is tight and ML-DSA is"
log "    bimodal, something is genuinely odd and we re-examine."

# ---------------------------------------------------------------------
# 3. Where does Ed25519 actually spend its time?
#
# perf record + report at threads=1 to see top symbols. If
# EVP_MD_CTX_new / EVP_MD_CTX_free / malloc / free dominate, the
# allocator hypothesis is confirmed and we add a context-reuse
# variant to the experiment.
# ---------------------------------------------------------------------

log "--- Phase 3: Ed25519 hot-symbol profile ---"

cool_down 15

# perf record needs to be told the duration; we run a short bench and
# attach. Easier: use perf record -- to fork the process directly.
perf record \
  -F 999 \
  --call-graph dwarf \
  -o "${OUT}/ed25519_t1_record.data" \
  -- \
  "${BENCH_BIN}" \
    --algo ed25519 \
    --threads 1 \
    --cores "0" \
    --iterations 200000 \
    --warmup 1000 \
    --runs 1 \
    --pin-strategy compact \
    --output-prefix "${OUT}/ed25519_t1_record" \
    --tag "hot_symbol_profile" \
  >> "${OUT}/run.log" 2>&1

# Generate the symbol report
perf report \
  --no-children \
  --stdio \
  -i "${OUT}/ed25519_t1_record.data" \
  --header \
  > "${OUT}/ed25519_t1_symbols.txt" 2>&1 || true

log "    -> ${OUT}/ed25519_t1_symbols.txt"
log "    Look for: EVP_MD_CTX_new, EVP_MD_CTX_free, malloc, free near top."
log "    If they dominate vs Ed25519_sign / sha512_block, the allocator"
log "    is the bottleneck and we need a context-reuse variant."

# Also do the same for ML-DSA at threads=1 for comparison
cool_down 15

perf record \
  -F 999 \
  --call-graph dwarf \
  -o "${OUT}/dilithium2_t1_record.data" \
  -- \
  "${BENCH_BIN}" \
    --algo dilithium2 \
    --threads 1 \
    --cores "0" \
    --iterations 100000 \
    --warmup 1000 \
    --runs 1 \
    --pin-strategy compact \
    --output-prefix "${OUT}/dilithium2_t1_record" \
    --tag "hot_symbol_profile" \
  >> "${OUT}/run.log" 2>&1

perf report \
  --no-children \
  --stdio \
  -i "${OUT}/dilithium2_t1_record.data" \
  --header \
  > "${OUT}/dilithium2_t1_symbols.txt" 2>&1 || true

log "    -> ${OUT}/dilithium2_t1_symbols.txt"

# ---------------------------------------------------------------------
# 4. Quick scaling-cliff diagnosis for Falcon and ML-DSA at threads=96
#
# We expect Falcon and ML-DSA to also degrade at 96 threads (your data
# already shows that), but the question is whether it's the same
# bottleneck as Ed25519 or a different one. Compact-vs-spread didn't
# fully decide it for them. perf at threads=96 will.
# ---------------------------------------------------------------------

log "--- Phase 4: Falcon / ML-DSA at threads=96 ---"

for algo in falcon512 dilithium2; do
  cool_down 15
  run_perf "${algo}_t96_groupA" "${algo}" 96 "0-95" "${EVENTS_GROUP_A}"
  cool_down 15
  run_perf "${algo}_t96_topdown" "${algo}" 96 "0-95" "${EVENTS_TOPDOWN}"
done

# ---------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------

log ""
log "=== Done ==="
log "Outputs in: ${OUT}"
log ""
log "Recommended analysis order:"
log "  1. Open ${OUT}/dilithium2_bimodal_check_latencies.csv"
log "     and ${OUT}/falcon_tail_check_latencies.csv;"
log "     plot CDFs side by side. Confirms or refutes the bimodality claim."
log ""
log "  2. Open ${OUT}/ed25519_t1_symbols.txt;"
log "     check whether EVP_MD_CTX allocator calls dominate."
log ""
log "  3. Open the *_perf.csv files for ed25519 at thread counts 1, 24, 48, 96;"
log "     plot stalls_l3_miss / cycles vs thread count."
log "     If it rises sharply, memory-bandwidth claim holds. If flat, look elsewhere."
log ""
log "  4. Compare Phase 4 perf data with Phase 1 perf data."
log "     If Falcon/ML-DSA show low stalls_l3_miss at 96 threads but Ed25519"
log "     shows high, the memory-bandwidth claim is differentially supported."

ls -la "${OUT}/"
