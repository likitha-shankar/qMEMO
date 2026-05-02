#!/bin/bash
# Phase B-2: cross-scheme TPS comparison at 1000ms and 500ms.
# 3 schemes (Ed25519/Falcon-512/ML-DSA-44) × 2 intervals × 5 runs = 30 invocations.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUT="$SCRIPT_DIR/benchmark_results/phase_b2_${TIMESTAMP}"
mkdir -p "$OUT"

declare -A SCHEME_NAME=([1]="ed25519" [2]="falcon512" [4]="mldsa44")
SCHEMES="1 2 4"
INTERVALS="1000 500"
RUNS=5
NUM_TX=100000

cd "$SCRIPT_DIR"

# Validate persistent plots exist
PLOTS=$(ls -1 plots_persistent/*.plot 2>/dev/null | wc -l)
if [ "$PLOTS" -lt 3 ]; then
    echo "[b2] ERROR: expected 3+ plots in plots_persistent/, found $PLOTS"
    echo "[b2] Run ./setup_plots.sh first."
    exit 1
fi
echo "[b2] $PLOTS plots present in plots_persistent/"

cleanup_procs() {
    pkill -9 -f "build/blockchain" 2>/dev/null || true
    pkill -9 -f "build/pool" 2>/dev/null || true
    pkill -9 -f "build/metronome" 2>/dev/null || true
    pkill -9 -f "build/validator" 2>/dev/null || true
    pkill -9 -f "build/wallet" 2>/dev/null || true
    sleep 2
    for port in 5555 5556 5557 5558 5559 5560 5561; do
        fuser -k ${port}/tcp 2>/dev/null || true
    done
    sleep 1
}

INVOCATION=0
for SCHEME in $SCHEMES; do
    NAME="${SCHEME_NAME[$SCHEME]}"
    export SIG_SCHEME=$SCHEME

    for INTERVAL in $INTERVALS; do
        for RUN in $(seq 1 $RUNS); do
            INVOCATION=$((INVOCATION + 1))
            TAG="${NAME}_${INTERVAL}ms_run${RUN}"
            LOG="$OUT/${TAG}.log"

            echo ""
            echo "=========================================================="
            echo "[$INVOCATION/30] $NAME @ ${INTERVAL}ms run $RUN/$RUNS  SIG_SCHEME=$SCHEME"
            echo "=========================================================="

            cleanup_procs

            # Run with default confirmation window formula.
            # benchmark.sh inherits SIG_SCHEME from the export above.
            set +e
            ./benchmark.sh $NUM_TX $INTERVAL 16 1 0 10000 64 8 > "$LOG" 2>&1
            BENCH_EXIT=$?
            set -e

            # === Validation gate ===
            # 1. Verify cc invocation contains -DSIG_SCHEME=N matching requested scheme
            if ! grep -q -- "-DSIG_SCHEME=${SCHEME} " "$LOG"; then
                echo "[$TAG] FAIL: cc invocation does not contain -DSIG_SCHEME=${SCHEME}"
                echo "[$TAG] grep output:"
                grep -- "DSIG_SCHEME" "$LOG" | head -3
                exit 1
            fi

            # 2. Confirmation rate >= 95%
            CONF=$(grep "Confirmation rate" "$LOG" 2>/dev/null | grep -oP "[\d.]+" | head -1 || echo "0")
            TPS=$(grep "End-to-end throughput" "$LOG" 2>/dev/null | grep -oP "[\d.]+" | head -1 || echo "0")

            CONF_OK=$(awk -v c="$CONF" 'BEGIN{print (c>=95)?1:0}')
            if [ "$CONF_OK" != "1" ]; then
                echo "[$TAG] FAIL: confirmation rate ${CONF}% < 95%"
                echo "[$TAG] last 30 lines of log:"
                tail -30 "$LOG"
                exit 1
            fi

            # 3. CSV artifacts written and non-empty
            for f in block_diag_*.csv pool_fetches_*.csv tx_diag_*.csv; do
                [ -f "$f" ] || continue
                if [ -s "$f" ]; then
                    cp "$f" "$OUT/${TAG}_$(basename $f)"
                fi
            done

            # Verify at least one block_diag CSV got copied
            BD_COUNT=$(ls "$OUT/${TAG}_block_diag_"*.csv 2>/dev/null | wc -l)
            if [ "$BD_COUNT" -lt 1 ]; then
                echo "[$TAG] FAIL: no block_diag CSV"
                exit 1
            fi

            echo "[$TAG] PASS: TPS=$TPS  Confirm=${CONF}%"
        done
    done
done

cleanup_procs
echo ""
echo "[b2] All 30 invocations complete. Output: $OUT"
