#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# MONITOR - Unified Log Viewer for MEMO Blockchain
# ═══════════════════════════════════════════════════════════════════
# Usage:
#   ./monitor.sh [log_dir]
#   ./monitor.sh ./benchmark_results
# ═══════════════════════════════════════════════════════════════════

LOG_DIR="${1:-./benchmark_results}"

# ANSI colors
C_BC="\033[1;34m";  C_PL="\033[1;36m";  C_MT="\033[1;33m"
C_VL="\033[1;35m";  C_SN="\033[1;32m";  C_ER="\033[1;31m"
C_MN="\033[1;37m";  C_RS="\033[0m"

# Important patterns
PAT="BLOCK_TIMING|ACCEPTED|REJECTED|CONFIRMED|WINNER|DEADLINE|ERROR|WARN|TPS|submitted|CHALLENGE.*Block|pending=|Step [0-9]|FUNDED|FUND_WALLET|Block #"

echo -e "${C_MN}╔══════════════════════════════════════════════════════════╗${C_RS}"
echo -e "${C_MN}║         MEMO Blockchain — Unified Monitor               ║${C_RS}"
echo -e "${C_MN}╠══════════════════════════════════════════════════════════╣${C_RS}"
echo -e "${C_MN}║  ${C_BC}■ BLOCKCHAIN${C_MN}  ${C_PL}■ POOL${C_MN}  ${C_MT}■ METRONOME${C_MN}  ${C_VL}■ VALIDATOR${C_MN}  ║${C_RS}"
echo -e "${C_MN}║  Log dir: ${LOG_DIR}${C_RS}"
echo -e "${C_MN}║  Press Ctrl+C to stop${C_RS}"
echo -e "${C_MN}╚══════════════════════════════════════════════════════════╝${C_RS}"
echo ""

# Wait for at least one log file
for attempt in $(seq 1 30); do
    if ls "$LOG_DIR"/*.log >/dev/null 2>&1; then break; fi
    sleep 1
done

if ! ls "$LOG_DIR"/*.log >/dev/null 2>&1; then
    echo -e "${C_ER}No log files found in $LOG_DIR${C_RS}"
    exit 1
fi

# Cleanup on Ctrl+C
trap 'kill $(jobs -p) 2>/dev/null; exit 0' INT TERM

# Tail each log file with a colored label
label_and_tail() {
    local file="$1" color="$2" label="$3"
    tail -n 0 -F "$file" 2>/dev/null | while IFS= read -r line; do
        clean=$(echo "$line" | sed 's/\x1b\[[0-9;]*m//g')
        if echo "$clean" | grep -qE "$PAT"; then
            printf "${color}[%-12s]${C_RS} %s\n" "$label" "$clean"
        fi
    done &
}

# Start tailing each component
[ -f "$LOG_DIR/blockchain.log" ] && label_and_tail "$LOG_DIR/blockchain.log" "$C_BC" "BLOCKCHAIN"
[ -f "$LOG_DIR/pool.log" ]       && label_and_tail "$LOG_DIR/pool.log" "$C_PL" "POOL"
[ -f "$LOG_DIR/metronome.log" ]  && label_and_tail "$LOG_DIR/metronome.log" "$C_MT" "METRONOME"

for f in "$LOG_DIR"/farmer*.log; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .log | tr '[:lower:]' '[:upper:]')
    label_and_tail "$f" "$C_VL" "$name"
done

for f in "$LOG_DIR"/sender*.log; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .log | tr '[:lower:]' '[:upper:]')
    label_and_tail "$f" "$C_SN" "$name"
done

# Wait for all background tail processes
wait
