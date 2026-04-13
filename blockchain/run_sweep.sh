#!/bin/bash
# =============================================================================
# PARAMETER SWEEP BENCHMARK
# =============================================================================
# Tests different block intervals and block sizes to find optimal TPS.
#
# USAGE:
#   ./run_sweep.sh                       # Full sweep (all combos)
#   ./run_sweep.sh --intervals-only      # Only block interval sweep
#   ./run_sweep.sh --sizes-only          # Only block size sweep
#   ./run_sweep.sh --quick               # Quick sweep (fewer combos)
#   ./run_sweep.sh --custom 1 10000      # Single run: interval=1, size=10000
#
# OUTPUT:
#   sweep_results/sweep_TIMESTAMP.csv    # Combined results
#   sweep_results/*/                     # Individual benchmark dirs
#
# BENCHMARK PARAMETERS:
#   ./benchmark.sh NUM_TX BLOCK_INTERVAL K_PARAM NUM_FARMERS WARMUP MAX_TXS_PER_BLOCK BATCH_SIZE THREADS
#   Example:  ./benchmark.sh 102400 1 16 10 auto 10000 64 8
#
# HOW TO CHANGE:
#   Block interval (seconds):  Parameter 2  (e.g., 1, 2, 4, 8)
#   Block size (max TXs):      Parameter 6  (e.g., 1024, 4096, 10000)
#   Total transactions:        Parameter 1  (e.g., 102400)
#   K parameter (plot size):   Parameter 3  (e.g., 16)
#   Number of farmers:         Parameter 4  (e.g., 10)
#
# EXAMPLES:
#   # Default: 102K TXs, 1s blocks, k=16, 10 farmers, 10K TXs/block
#   ./benchmark.sh 102400 1 16 10 auto 10000
#
#   # Larger blocks: 32K TXs per block
#   ./benchmark.sh 102400 1 16 10 auto 32768
#
#   # Slower blocks: 4-second interval
#   ./benchmark.sh 102400 4 16 10 auto 10000
#
#   # Small blocks: 1024 TXs per block, 2s interval
#   ./benchmark.sh 102400 2 16 10 auto 1024
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="$SCRIPT_DIR/sweep_results"
mkdir -p "$RESULTS_DIR"

CSV_FILE="$RESULTS_DIR/sweep_${TIMESTAMP}.csv"

# Default parameters
K_PARAM=16
NUM_FARMERS=10
BATCH_SIZE=64
NUM_THREADS=8
NUM_TRANSACTIONS=102400

# Sweep configurations
BLOCK_INTERVALS=(1 2 4 8 16 32)
BLOCK_SIZES=(1024 2048 4096 8192 16384 32768 65536)

# Parse arguments
MODE="full"
CUSTOM_INTERVAL=""
CUSTOM_SIZE=""

case "${1:-}" in
    --intervals-only) MODE="intervals" ;;
    --sizes-only)     MODE="sizes" ;;
    --quick)          MODE="quick" ;;
    --custom)
        MODE="custom"
        CUSTOM_INTERVAL="${2:?Usage: $0 --custom INTERVAL BLOCK_SIZE}"
        CUSTOM_SIZE="${3:?Usage: $0 --custom INTERVAL BLOCK_SIZE}"
        ;;
    --help|-h)
        head -42 "$0" | tail -40
        exit 0
        ;;
esac

# Build combinations (auto-filter impossible ones)
declare -a COMBOS=()

case "$MODE" in
    full)
        # All combinations are valid — partial blocks handle any combo
        for interval in "${BLOCK_INTERVALS[@]}"; do
            for size in "${BLOCK_SIZES[@]}"; do
                COMBOS+=("$interval:$size")
            done
        done
        ;;
    intervals)
        for interval in "${BLOCK_INTERVALS[@]}"; do
            COMBOS+=("$interval:10000")
        done
        ;;
    sizes)
        for size in "${BLOCK_SIZES[@]}"; do
            COMBOS+=("1:$size")
        done
        ;;
    quick)
        for interval in 1 2 4 8; do
            for size in 1024 4096 10000 32768; do
                COMBOS+=("$interval:$size")
            done
        done
        ;;
    custom)
        COMBOS+=("$CUSTOM_INTERVAL:$CUSTOM_SIZE")
        ;;
esac

TOTAL=${#COMBOS[@]}

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           PARAMETER SWEEP BENCHMARK                             ║"
echo "╠══════════════════════════════════════════════════════════════════╣"
echo "║  Mode:           $(printf '%-48s' "$MODE")║"
echo "║  Combinations:   $(printf '%-48s' "$TOTAL")║"
echo "║  Transactions:   $(printf '%-48s' "$NUM_TRANSACTIONS")║"
echo "║  K parameter:    $(printf '%-48s' "$K_PARAM")║"
echo "║  Farmers:        $(printf '%-48s' "$NUM_FARMERS")║"
echo "║  Output:         $(printf '%-48s' "$CSV_FILE")║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# CSV header
echo "block_interval,block_size,submit_tps,process_tps,e2e_tps,confirm_rate,tx_submitted,tx_confirmed,blocks_created,avg_tx_per_block,total_time_s,submit_time_s,process_time_s" > "$CSV_FILE"

RUN=0
for combo in "${COMBOS[@]}"; do
    IFS=':' read -r INTERVAL SIZE <<< "$combo"
    RUN=$((RUN + 1))
    
    echo "═══════════════════════════════════════════════════════════════════"
    echo "  [$RUN/$TOTAL] Block interval=${INTERVAL}s, Block size=${SIZE}"
    echo "═══════════════════════════════════════════════════════════════════"
    
    # Adjust total TXs: enough for 5+ blocks but cap to keep runtime reasonable
    # For large intervals (8s+), reduce TX count to avoid 30+ minute warmups
    ADJUSTED_TX=$NUM_TRANSACTIONS
    MIN_TX=$((SIZE * 5))
    if [ "$ADJUSTED_TX" -lt "$MIN_TX" ]; then
        ADJUSTED_TX=$MIN_TX
    fi
    # Cap TXs for large intervals to keep total runtime reasonable
    MAX_REASONABLE_TX=$((SIZE * 20))  # 20 blocks worth
    if [ "$INTERVAL" -ge 8 ] && [ "$ADJUSTED_TX" -gt "$MAX_REASONABLE_TX" ]; then
        ADJUSTED_TX=$MAX_REASONABLE_TX
    fi
    
    echo "  TXs: $ADJUSTED_TX (${ADJUSTED_TX}/$SIZE = $((ADJUSTED_TX / SIZE + 1)) blocks)"
    
    # Run benchmark
    RUN_DIR="$RESULTS_DIR/run_${INTERVAL}s_${SIZE}tx_${TIMESTAMP}"
    mkdir -p "$RUN_DIR"
    
    cd "$SCRIPT_DIR"
    
    # Timeout: plot generation (~10s) + benchmark + margin
    # No warmup mining with FUND_WALLET — dramatically faster!
    PLOT_EST=15  # Plot generation time
    BENCH_EST=$(( (ADJUSTED_TX / SIZE + 10) * INTERVAL ))
    SWEEP_TIMEOUT=$(( PLOT_EST + BENCH_EST + 120 ))
    if [ "$SWEEP_TIMEOUT" -lt 180 ]; then SWEEP_TIMEOUT=180; fi
    if [ "$SWEEP_TIMEOUT" -gt 1800 ]; then SWEEP_TIMEOUT=1800; fi
    
    # Capture output and extract metrics
    set +e
    timeout "$SWEEP_TIMEOUT" bash benchmark.sh \
        "$ADJUSTED_TX" "$INTERVAL" "$K_PARAM" "$NUM_FARMERS" auto "$SIZE" "$BATCH_SIZE" "$NUM_THREADS" \
        2>&1 | tee "$RUN_DIR/full_output.log"
    EXIT_CODE=$?
    set -e
    
    # Copy benchmark results if available
    if [ -d "$SCRIPT_DIR/benchmark_results" ]; then
        cp -r "$SCRIPT_DIR/benchmark_results"/* "$RUN_DIR/" 2>/dev/null || true
    fi
    
    # Parse results from output
    OUTPUT="$RUN_DIR/full_output.log"
    
    SUBMIT_TPS=$(grep "Submission throughput" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?= tx/sec)' | head -1 || echo "0")
    PROCESS_TPS=$(grep "Processing throughput" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?= tx/sec)' | head -1 || echo "0")
    E2E_TPS=$(grep "End-to-end throughput" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?= tx/sec)' | head -1 || echo "0")
    CONFIRM_RATE=$(grep "Confirmation rate" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?=%)' | head -1 || echo "0")
    TX_SUB=$(grep "Transactions submitted" "$OUTPUT" 2>/dev/null | grep -oP '\d+' | tail -1 || echo "0")
    TX_CONF=$(grep "Transactions confirmed" "$OUTPUT" 2>/dev/null | grep -oP '\d+' | tail -1 || echo "0")
    BLOCKS=$(grep "Blocks created during" "$OUTPUT" 2>/dev/null | grep -oP '\d+' | tail -1 || echo "0")
    AVG_TX=$(grep "Average transactions per block" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+' | tail -1 || echo "0")
    TOTAL_TIME=$(grep "Total benchmark time" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?=s)' | head -1 || echo "0")
    SUBMIT_TIME=$(grep "Submission time.*Phase" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?=s)' | head -1 || echo "0")
    PROCESS_TIME=$(grep "Processing time.*RESUME" "$OUTPUT" 2>/dev/null | grep -oP '[\d.]+(?=s)' | head -1 || echo "0")
    
    # Write CSV row
    echo "${INTERVAL},${SIZE},${SUBMIT_TPS:-0},${PROCESS_TPS:-0},${E2E_TPS:-0},${CONFIRM_RATE:-0},${TX_SUB:-0},${TX_CONF:-0},${BLOCKS:-0},${AVG_TX:-0},${TOTAL_TIME:-0},${SUBMIT_TIME:-0},${PROCESS_TIME:-0}" >> "$CSV_FILE"
    
    echo ""
    echo "  → Submit TPS: $SUBMIT_TPS | E2E TPS: $E2E_TPS | Confirmed: $TX_CONF | Blocks: $BLOCKS"
    echo ""
    
    # Cool down between runs — kill any stale processes
    if [ "$RUN" -lt "$TOTAL" ]; then
        echo "  Cleaning up between runs..."
        pkill -9 -f "build/blockchain" 2>/dev/null || true
        pkill -9 -f "build/pool" 2>/dev/null || true
        pkill -9 -f "build/metronome" 2>/dev/null || true
        pkill -9 -f "build/validator" 2>/dev/null || true
        pkill -9 -f "build/wallet" 2>/dev/null || true
        sleep 2
        # Clean up stale port bindings
        for port in 5555 5556 5557 5558 5559 5560 5561; do
            fuser -k ${port}/tcp 2>/dev/null || true
        done
        sleep 1
    fi
done

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  SWEEP COMPLETE                                                  ║"
echo "╠══════════════════════════════════════════════════════════════════╣"
echo "║  Results: $CSV_FILE"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "Results CSV:"
echo "─────────────────────────────────────────────────────────────────"
column -t -s',' "$CSV_FILE"
echo ""
