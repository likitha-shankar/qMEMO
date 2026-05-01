#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════
# Graph 3 Scaling Benchmark: Run multiple configs to measure block size impact
# ═══════════════════════════════════════════════════════════════════════════
#
# Runs benchmark with MAX_TXS_PER_BLOCK = 10, 100, 1000, 10000
# Collects timing averages from each run into graph3_scaling.csv
# Then generates Graph 3
#
# Usage: ./run_graph3_benchmark.sh [total_transactions] [batch_size] [num_threads]
#   Default: 10000 TXs per run, batch 64, 8 threads

set -u

TOTAL_TXS=${1:-10000}
BATCH_SIZE=${2:-64}
NUM_THREADS=${3:-8}
RESULTS_DIR="./graph3_results"
CONFIGS=(10 100 1000 10000)

mkdir -p "$RESULTS_DIR"

# Output CSV
CSV="$RESULTS_DIR/graph3_scaling.csv"
echo "max_txs_per_block,avg_get_hash_ms,avg_fetch_tx_ms,avg_serialize_send_ms,avg_confirm_ms,avg_total_ms,process_tps,submit_tps,blocks_created" > "$CSV"

echo "═══════════════════════════════════════════════════════════════"
echo "  Graph 3 Scaling Benchmark"
echo "  TXs per run: $TOTAL_TXS"
echo "  Configs: ${CONFIGS[*]}"
echo "  Batch: $BATCH_SIZE, Threads: $NUM_THREADS"
echo "═══════════════════════════════════════════════════════════════"

for MAX_TXS in "${CONFIGS[@]}"; do
    echo ""
    echo "━━━ Running with MAX_TXS_PER_BLOCK=$MAX_TXS ━━━"
    
    # Run benchmark
    ./benchmark.sh "$TOTAL_TXS" 1000 16 10 auto "$MAX_TXS" "$BATCH_SIZE" "$NUM_THREADS" 2>&1 | \
        tee "$RESULTS_DIR/run_${MAX_TXS}.log" | \
        grep -E "Submission TPS|End-to-End TPS|Confirmation Rate|Avg TX"
    
    # Extract timing from graph2 CSV
    G2_CSV="./benchmark_results/graph2_block_timing.csv"
    if [ -f "$G2_CSV" ]; then
        # Calculate averages from block timing data (skip header, strip ANSI)
        AVG_LINE=$(tail -n +2 "$G2_CSV" | sed 's/\x1b\[[0-9;]*m//g' | \
            awk -F, '
            $2 > 0 {
                n++; gh+=$3; ft+=$4; ss+=$5; cf+=$6; tot+=$7
            }
            END {
                if (n>0) printf "%d,%d,%d,%d,%d", gh/n, ft/n, ss/n, cf/n, tot/n
                else printf "0,0,0,0,0"
            }')
        
        BLOCKS=$(tail -n +2 "$G2_CSV" | awk -F, '$2 > 0' | wc -l)
    else
        AVG_LINE="0,0,0,0,0"
        BLOCKS=0
    fi
    
    # Extract TPS from benchmark CSV
    BENCH_CSV=$(ls -t ./benchmark_results/benchmark_*.csv 2>/dev/null | head -1)
    PROC_TPS=0
    SUB_TPS=0
    if [ -n "$BENCH_CSV" ]; then
        PROC_TPS=$(grep "^process_tps" "$BENCH_CSV" 2>/dev/null | cut -d, -f2)
        SUB_TPS=$(grep "^submit_tps" "$BENCH_CSV" 2>/dev/null | cut -d, -f2)
    fi
    
    echo "$MAX_TXS,$AVG_LINE,${PROC_TPS:-0},${SUB_TPS:-0},${BLOCKS:-0}" >> "$CSV"
    echo "  → Saved: MAX_TXS=$MAX_TXS, blocks=$BLOCKS, process_tps=${PROC_TPS:-0}"
    
    # Brief cooldown
    sleep 2
done

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  Generating Graph 3..."
echo "═══════════════════════════════════════════════════════════════"

if command -v python3 &>/dev/null; then
    python3 ./generate_graphs.py "$RESULTS_DIR" --graph3-dir "$RESULTS_DIR"
fi

echo ""
echo "Results: $CSV"
echo "Graph:   $RESULTS_DIR/graph3_block_scaling.png"
