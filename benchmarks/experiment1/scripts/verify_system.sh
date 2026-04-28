#!/usr/bin/env bash
# verify_system.sh — Pre-flight check called by bench_sign before timing begins.
# Exits 0 if all hard requirements pass; exits 1 on any FAIL.
# WARNs are printed but do not fail.
set -euo pipefail

FAIL=0
warn()  { echo "VERIFY WARN:  $*" >&2; }
fail()  { echo "VERIFY FAIL:  $*" >&2; FAIL=1; }
ok()    { echo "VERIFY OK:    $*"; }

# ── 1. CPU governor ──────────────────────────────────────────────────────────
GOV_FILE="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
if [[ -f "$GOV_FILE" ]]; then
    GOV=$(cat "$GOV_FILE")
    if [[ "$GOV" == "performance" ]]; then
        ok "governor = $GOV"
    else
        fail "governor='$GOV' (need 'performance'). Fix: sudo cpupower frequency-set -g performance"
    fi
else
    warn "Cannot read scaling_governor (cpufreq not present — VM or non-pstate driver?)"
fi

# Spot-check all cores have the same governor
if ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor &>/dev/null 2>&1; then
    OTHERS=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u)
    COUNT=$(echo "$OTHERS" | wc -l)
    if [[ $COUNT -gt 1 ]]; then
        fail "Not all cores have the same governor: $OTHERS"
    fi
fi

# ── 2. Turbo boost ───────────────────────────────────────────────────────────
TURBO_FILE="/sys/devices/system/cpu/intel_pstate/no_turbo"
if [[ -f "$TURBO_FILE" ]]; then
    NT=$(cat "$TURBO_FILE")
    if [[ "$NT" == "1" ]]; then
        ok "turbo disabled (no_turbo=1)"
    else
        warn "turbo NOT disabled (no_turbo=$NT). Frequency may vary across thread counts."
    fi
else
    warn "intel_pstate/no_turbo not found (AMD CPU or non-pstate driver)"
fi

# ── 3. Transparent hugepages ─────────────────────────────────────────────────
THP_FILE="/sys/kernel/mm/transparent_hugepage/enabled"
if [[ -f "$THP_FILE" ]]; then
    THP=$(cat "$THP_FILE")
    if [[ "$THP" == *"[never]"* ]]; then
        ok "THP = never"
    else
        warn "THP not 'never': $THP. Fix: echo never | sudo tee $THP_FILE"
    fi
else
    warn "Cannot read THP setting"
fi

# ── 4. Swap ──────────────────────────────────────────────────────────────────
SWAP_LINES=$(awk 'NR>1' /proc/swaps 2>/dev/null | wc -l)
if [[ "$SWAP_LINES" -eq 0 ]]; then
    ok "swap = off"
else
    warn "Swap is active ($SWAP_LINES device(s)). Fix: sudo swapoff -a"
fi

# ── 5. NMI watchdog ──────────────────────────────────────────────────────────
NMI_FILE="/proc/sys/kernel/nmi_watchdog"
if [[ -f "$NMI_FILE" ]]; then
    NMI=$(cat "$NMI_FILE")
    if [[ "$NMI" == "0" ]]; then
        ok "nmi_watchdog = 0"
    else
        warn "nmi_watchdog=$NMI (should be 0). Fix: echo 0 | sudo tee $NMI_FILE"
    fi
fi

# ── 6. Current CPU frequencies ───────────────────────────────────────────────
echo ""
echo "Current CPU frequencies (MHz):"
if ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq &>/dev/null 2>&1; then
    # Print min, max, mean across all cores
    FREQS=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null)
    MIN=$(echo "$FREQS" | sort -n | head -1)
    MAX=$(echo "$FREQS" | sort -n | tail -1)
    COUNT_F=$(echo "$FREQS" | wc -l)
    SUM=$(echo "$FREQS" | awk '{s+=$1}END{print s}')
    MEAN=$((SUM / COUNT_F))
    echo "  cores: $COUNT_F  min: $((MIN/1000)) MHz  mean: $((MEAN/1000)) MHz  max: $((MAX/1000)) MHz"

    # Warn if spread > 5% of mean
    THRESH=$((MEAN * 5 / 100))
    SPREAD=$((MAX - MIN))
    if [[ $SPREAD -gt $THRESH ]]; then
        warn "CPU frequency spread ${SPREAD} kHz exceeds 5% of mean ${MEAN} kHz"
    fi
elif command -v lscpu &>/dev/null; then
    lscpu | grep -i "MHz" || true
else
    warn "Cannot read current CPU frequencies"
fi

# ── 7. Free memory ───────────────────────────────────────────────────────────
echo ""
FREE_MB=$(free -m | awk 'NR==2{print $7}')
ok "free memory: ${FREE_MB} MB"
if [[ "${FREE_MB}" -lt 4096 ]]; then
    warn "Less than 4 GB free. At 96 threads × 100k iters, latency arrays need ~1 GB."
fi

# ── 8. Core temperatures ─────────────────────────────────────────────────────
echo ""
if ls /sys/class/thermal/thermal_zone*/temp &>/dev/null 2>&1; then
    echo "CPU temperatures (°C):"
    paste /sys/class/thermal/thermal_zone*/temp 2>/dev/null | \
        awk '{for(i=1;i<=NF;i++) printf "  zone%d: %.1f°C\n", i-1, $i/1000}' | head -8
    # Warn if any zone > 80°C
    HOT=$(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | awk '$1>80000{found=1}END{print found+0}')
    if [[ "$HOT" -gt 0 ]]; then
        warn "At least one thermal zone > 80°C — wait for cooldown before benchmarking."
    fi
fi

# ── 9. liboqs sanity ─────────────────────────────────────────────────────────
echo ""
OQS_SPEED="$(dirname "$0")/../../liboqs_install/bin/test_speed_sig"
if [[ -x "$OQS_SPEED" ]]; then
    ok "liboqs test_speed_sig found at $OQS_SPEED"
else
    warn "liboqs test_speed_sig not found at $OQS_SPEED (reference-impl control will be unavailable)"
fi

# ── 10. bench_sign binary ────────────────────────────────────────────────────
BENCH="$(dirname "$0")/../bin/bench_sign"
if [[ ! -x "$BENCH" ]]; then
    fail "bench_sign binary not found at $BENCH. Run: make -C $(dirname "$0")/.."
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
if [[ $FAIL -eq 0 ]]; then
    echo "verify_system.sh: all hard checks PASSED (see WARNs above if any)"
    exit 0
else
    echo "verify_system.sh: FAILED — fix the issues above and re-run."
    exit 1
fi
