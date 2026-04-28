#!/usr/bin/env bash
# prepare_system.sh — Run once with sudo before a benchmark session.
# Idempotent: safe to re-run.
set -euo pipefail

die() { echo "ERROR: $*" >&2; exit 1; }
warn() { echo "WARN:  $*" >&2; }
info() { echo "INFO:  $*"; }

[[ $EUID -eq 0 ]] || die "Must run as root (sudo $0)"

# 1 — CPU governor: performance on all cores
info "Setting CPU governor to performance..."
if command -v cpupower &>/dev/null; then
    cpupower frequency-set -g performance
elif ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor &>/dev/null 2>&1; then
    for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo performance > "$g"
    done
else
    warn "Cannot set governor (cpupower not found, cpufreq not available)"
fi

# 2 — Disable turbo boost (Intel pstate)
if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
    info "Disabling turbo boost..."
    echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
else
    warn "/sys/devices/system/cpu/intel_pstate/no_turbo not found (non-Intel or non-pstate driver)"
fi

# 3 — Transparent hugepages: never
if [[ -f /sys/kernel/mm/transparent_hugepage/enabled ]]; then
    info "Setting THP to never..."
    echo never > /sys/kernel/mm/transparent_hugepage/enabled
fi

# 4 — Swap off
info "Disabling swap..."
swapoff -a 2>/dev/null || warn "swapoff failed (no swap may be fine)"

# 5 — NMI watchdog off
if [[ -f /proc/sys/kernel/nmi_watchdog ]]; then
    info "Disabling NMI watchdog..."
    echo 0 > /proc/sys/kernel/nmi_watchdog
fi

# 6 — Stop common background services (best-effort)
for svc in cron snapd systemd-timesyncd tuned irqbalance; do
    if systemctl is-active --quiet "$svc" 2>/dev/null; then
        info "Stopping $svc..."
        systemctl stop "$svc" || warn "Could not stop $svc"
    fi
done

# 7 — Drop filesystem caches
info "Dropping FS caches..."
sync
echo 3 > /proc/sys/vm/drop_caches

# 8 — Report state
echo ""
echo "System state after prepare_system.sh:"
echo "  governor (cpu0):  $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
echo "  no_turbo:         $(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo n/a)"
echo "  THP:              $(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
echo "  nmi_watchdog:     $(cat /proc/sys/kernel/nmi_watchdog 2>/dev/null || echo n/a)"
echo "  logical CPUs:     $(nproc)"
echo "  free mem (MB):    $(free -m | awk 'NR==2{print $7}')"
echo ""
echo "prepare_system.sh done. Now run verify_system.sh to confirm."
