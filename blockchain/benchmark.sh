#!/bin/bash

# ╔══════════════════════════════════════════════════════════════════════════════╗
# ║              BLOCKCHAIN BENCHMARK TOOL v7 (END-TO-END)                        ║
# ╠══════════════════════════════════════════════════════════════════════════════╣
# ║  COMPREHENSIVE BENCHMARKING:                                                  ║
# ║    • MICRO: GPB serialization, ZMQ latency, proof search                     ║
# ║    • MACRO: End-to-end transaction throughput (submit → confirm)             ║
# ║                                                                               ║
# ║  KEY IMPROVEMENTS:                                                            ║
# ║    • Auto-scales warmup blocks based on requested transactions               ║
# ║    • Uses ALL farmers as senders (more coins available)                       ║
# ║    • Tracks per-transaction latency (submit time → confirmation time)         ║
# ║    • Separate metrics: Submission TPS vs Confirmation TPS                     ║
# ║    • Proper end-to-end measurement including block inclusion                  ║
# ║    • Dynamic pool sizing (up to 100,000 transactions)                        ║
# ║    • Generates HTML visualization report                                      ║
# ╚══════════════════════════════════════════════════════════════════════════════╝

set -u

# Configuration
BUILD_DIR="./build"
BENCHMARK_DIR="./benchmark_results"
BLOCKCHAIN_PORT=5555
POOL_PORT=5557
METRONOME_REP_PORT=5556
METRONOME_PUB_PORT=5558
BLOCKCHAIN_PUB_PORT=5559
METRONOME_NOTIFY_PORT=5560
POOL_PUB_PORT=5561

# Benchmark parameters
NUM_TRANSACTIONS=${1:-1000}
BLOCK_INTERVAL=${2:-1000}
K_PARAM=${3:-16}
NUM_FARMERS=${4:-10}
WARMUP_BLOCKS=${5:-auto}  # "auto" = use FUND_WALLET (skip warmup mining)
MAX_TXS_PER_BLOCK=${6:-10000}  # Max transactions per block
BATCH_SIZE=${7:-64}        # TXs per batch message (64, 128, 256)
NUM_THREADS=${8:-8}        # OpenMP threads per sender
NUM_SENDERS=${NUM_FARMERS}  # Separate sender wallets (not validators)

# Mining reward per block (must match metronome.h BASE_MINING_REWARD)
BASE_MINING_REWARD=10000

# Coins to pre-fund each sender wallet via FUND_WALLET
# Each TX costs 2 coins (1 value + 1 fee). Per sender = NUM_TX / NUM_SENDERS * 2
# Add 50% safety margin
COINS_PER_SENDER=$(( (NUM_TRANSACTIONS / NUM_SENDERS * 2) * 3 / 2 + 10000 ))

# No warmup mining needed — FUND_WALLET pre-funds directly
WARMUP_BLOCKS=0

# Calculate explicit difficulty (k - 3 ensures high proof rate for testing)
EXPLICIT_DIFFICULTY=$((K_PARAM - 3))
if [ $EXPLICIT_DIFFICULTY -lt 1 ]; then
    EXPLICIT_DIFFICULTY=1
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_header() {
    echo ""
    echo -e "${BLUE}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║${NC}  $1"
    echo -e "${BLUE}╚══════════════════════════════════════════════════════════════════╝${NC}"
}

print_subheader() {
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════════════${NC}"
}

print_metric() {
    printf "  %-45s: %s\n" "$1" "$2"
}

# Kill all blockchain processes
kill_all_blockchain_processes() {
    {
        pkill -9 -f "build/blockchain" 
        pkill -9 -f "build/metronome" 
        pkill -9 -f "build/pool" 
        pkill -9 -f "build/validator" 
        pkill -9 -f "build/wallet" 
        fuser -k ${BLOCKCHAIN_PORT}/tcp 
        fuser -k ${POOL_PORT}/tcp 
        fuser -k ${METRONOME_REP_PORT}/tcp 
        fuser -k ${METRONOME_PUB_PORT}/tcp 
        fuser -k ${BLOCKCHAIN_PUB_PORT}/tcp 
        fuser -k ${METRONOME_NOTIFY_PORT}/tcp 
        fuser -k ${POOL_PUB_PORT}/tcp 
    } >/dev/null 2>&1 || true
    sleep 2
}

# Get balance
get_balance() {
    local wallet_name=$1
    local result
    result=$($BUILD_DIR/wallet balance "$wallet_name" 2>/dev/null | grep -oP 'Balance for [^:]+: \K\d+' | head -1)
    if [[ "$result" =~ ^[0-9]+$ ]]; then
        echo "$result"
    else
        echo "0"
    fi
}

# Get nonce
get_nonce() {
    local wallet_name=$1
    local result
    result=$($BUILD_DIR/wallet nonce "$wallet_name" 2>/dev/null | grep -oP 'Nonce for [^:]+: \K\d+' | head -1)
    if [[ "$result" =~ ^[0-9]+$ ]]; then
        echo "$result"
    else
        echo "0"
    fi
}

# Get chain height - try multiple methods
get_height() {
    local result
    
    # Method 1: Try parsing from metronome log (most reliable during benchmark)
    if [ -f "$BENCHMARK_DIR/metronome.log" ]; then
        result=$(grep -oP 'Block #\K\d+' "$BENCHMARK_DIR/metronome.log" 2>/dev/null | tail -1)
        if [[ "$result" =~ ^[0-9]+$ ]]; then
            echo "$result"
            return
        fi
    fi
    
    # Method 2: Try netcat
    result=$(echo "GET_HEIGHT" | timeout 2 nc -q1 localhost $BLOCKCHAIN_PORT 2>/dev/null | tr -d '\0' | head -1)
    if [[ "$result" =~ ^[0-9]+$ ]]; then
        echo "$result"
        return
    fi
    
    # Method 3: Query blockchain directly with a simple Python script
    result=$(timeout 2 python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(1)
try:
    s.connect(('localhost', $BLOCKCHAIN_PORT))
    s.send(b'GET_HEIGHT')
    data = s.recv(1024).decode().strip()
    print(data)
except:
    print('0')
finally:
    s.close()
" 2>/dev/null)
    if [[ "$result" =~ ^[0-9]+$ ]]; then
        echo "$result"
        return
    fi
    
    echo "0"
}

# Cleanup on exit
cleanup() {
    print_header "CLEANUP"
    echo "  Stopping blockchain processes..."
    kill_all_blockchain_processes
    echo "  ✓ Done"
}

trap cleanup EXIT

# Create results directory
mkdir -p "$BENCHMARK_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$BENCHMARK_DIR/benchmark_${TIMESTAMP}.csv"
INTERNAL_CSV="$BENCHMARK_DIR/internal_${TIMESTAMP}.csv"
LATENCY_FILE="$BENCHMARK_DIR/latency_${TIMESTAMP}.csv"

echo "metric,value,unit" > "$CSV_FILE"
echo "tx_id,submit_time_ms,confirm_time_ms,latency_ms,sender,block_height" > "$LATENCY_FILE"

print_header "BLOCKCHAIN BENCHMARK v6 (END-TO-END)"
echo ""
echo "Configuration:"
print_metric "Transactions to send" "$NUM_TRANSACTIONS"
print_metric "Block interval" "${BLOCK_INTERVAL}ms"
print_metric "K parameter" "$K_PARAM (2^$K_PARAM = $((1 << K_PARAM)) entries)"
print_metric "Number of farmers" "$NUM_FARMERS"
print_metric "Warmup blocks" "$WARMUP_BLOCKS"
print_metric "Max TXs per block" "$MAX_TXS_PER_BLOCK"
print_metric "Batch size" "$BATCH_SIZE"
print_metric "Threads per farmer" "$NUM_THREADS"
print_metric "Explicit difficulty" "$EXPLICIT_DIFFICULTY"
echo ""

# Kill existing processes
print_header "PREPARING ENVIRONMENT"
echo "  Stopping any existing blockchain processes..."
kill_all_blockchain_processes
echo "  ✓ Environment ready"

# Build
print_header "BUILDING PROJECT"
make clean >/dev/null 2>&1 || true
if make SIG_SCHEME=${SIG_SCHEME:-1} 2>&1 | tail -5; then
    echo "✓ Build successful"
else
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

# Verify executables
for exe in blockchain metronome pool validator wallet benchmark; do
    if [ ! -f "$BUILD_DIR/$exe" ]; then
        echo -e "${RED}ERROR: $BUILD_DIR/$exe not found${NC}"
        exit 1
    fi
done

# Run internal benchmark for GPB/ZMQ stats
print_header "RUNNING MICRO BENCHMARK (GPB & ZMQ)"
echo ""
$BUILD_DIR/benchmark -n 500 -k $K_PARAM --csv "$INTERNAL_CSV" 2>&1 | grep -E "^║|µs|ms|/sec|SUMMARY|═══"
echo ""

# Create wallets
print_header "CREATING TEST WALLETS"
for i in $(seq 1 $NUM_FARMERS); do
    $BUILD_DIR/wallet create "farmer$i" >/dev/null 2>&1 || true
done
for i in $(seq 1 $NUM_SENDERS); do
    $BUILD_DIR/wallet create "sender$i" >/dev/null 2>&1 || true
done
$BUILD_DIR/wallet create bench_receiver >/dev/null 2>&1 || true
echo "✓ Wallets ready (${NUM_FARMERS} validators + ${NUM_SENDERS} senders + 1 receiver)"

# Start components
print_header "STARTING BLOCKCHAIN COMPONENTS"

$BUILD_DIR/blockchain "tcp://*:$BLOCKCHAIN_PORT" --metronome-notify "tcp://localhost:$METRONOME_NOTIFY_PORT" --pub "tcp://*:$BLOCKCHAIN_PUB_PORT" > "$BENCHMARK_DIR/blockchain.log" 2>&1 &
BLOCKCHAIN_PID=$!
sleep 1
if ! kill -0 $BLOCKCHAIN_PID 2>/dev/null; then
    echo -e "${RED}Blockchain failed to start${NC}"
    exit 1
fi
echo "  ✓ Blockchain server (PID: $BLOCKCHAIN_PID)"

$BUILD_DIR/pool "tcp://*:$POOL_PORT" --sub "tcp://localhost:$BLOCKCHAIN_PUB_PORT" --pub "tcp://*:$POOL_PUB_PORT" > "$BENCHMARK_DIR/pool.log" 2>&1 &
POOL_PID=$!
sleep 1
if ! kill -0 $POOL_PID 2>/dev/null; then
    echo -e "${RED}Pool failed to start${NC}"
    exit 1
fi
echo "  ✓ Transaction pool (PID: $POOL_PID)"

$BUILD_DIR/metronome \
    --rep "tcp://*:$METRONOME_REP_PORT" \
    --pub "tcp://*:$METRONOME_PUB_PORT" \
    --notify "tcp://*:$METRONOME_NOTIFY_PORT" \
    --blockchain "tcp://localhost:$BLOCKCHAIN_PORT" \
    --pool "tcp://localhost:$POOL_PORT" \
    -i "$BLOCK_INTERVAL" \
    -k "$K_PARAM" \
    -v "$NUM_FARMERS" \
    -d "$EXPLICIT_DIFFICULTY" \
    -r "$BASE_MINING_REWARD" \
    > "$BENCHMARK_DIR/metronome.log" 2>&1 &
METRONOME_PID=$!
sleep 1
if ! kill -0 $METRONOME_PID 2>/dev/null; then
    echo -e "${RED}Metronome failed to start${NC}"
    exit 1
fi
echo "  ✓ Metronome (PID: $METRONOME_PID)"

for i in $(seq 1 $NUM_FARMERS); do
    $BUILD_DIR/validator "farmer$i" \
        --metronome-sub "tcp://localhost:$METRONOME_PUB_PORT" \
        --metronome-req "tcp://localhost:$METRONOME_REP_PORT" \
        --pool "tcp://localhost:$POOL_PORT" \
        --blockchain "tcp://localhost:$BLOCKCHAIN_PORT" \
        -k "$K_PARAM" \
        --max-txs "$MAX_TXS_PER_BLOCK" \
        > "$BENCHMARK_DIR/farmer${i}.log" 2>&1 &
    sleep 0.3
done
echo "  ✓ $NUM_FARMERS farmers started"

# Pre-fund sender wallets via FUND_WALLET (skip warmup mining!)
print_header "PRE-FUNDING SENDER WALLETS"
echo "  Funding $NUM_SENDERS sender wallets with $COINS_PER_SENDER coins each..."
echo "  (Using FUND_WALLET — direct ledger credit, no mining needed)"
echo ""

for i in $(seq 1 $NUM_SENDERS); do
    # Get the sender's address using the wallet binary
    SENDER_ADDR=$($BUILD_DIR/wallet address "sender$i" 2>/dev/null | grep -oP '[0-9a-f]{40}' | head -1)
    if [ -z "$SENDER_ADDR" ]; then
        printf "  ✗ sender%d: failed to get address\n" "$i"
        continue
    fi
    
    FUND_RESP=$(timeout 5 python3 -c "
import zmq
ctx = zmq.Context()
s = ctx.socket(zmq.REQ)
s.setsockopt(zmq.RCVTIMEO, 3000)
s.connect('tcp://localhost:$BLOCKCHAIN_PORT')
s.send_string('FUND_WALLET:${SENDER_ADDR}:$COINS_PER_SENDER')
print(s.recv_string())
s.close(); ctx.term()
" 2>/dev/null || echo "FAILED")
    if echo "$FUND_RESP" | grep -q "FUNDED"; then
        printf "  ✓ sender%d: %d coins\n" "$i" "$COINS_PER_SENDER"
    else
        printf "  ✗ sender%d: FAILED (%s)\n" "$i" "$FUND_RESP"
    fi
done

echo ""
echo "  Waiting for validators to generate plots..."
PLOT_GEN_TIME=$((8 + K_PARAM / 4))
for i in $(seq 1 $PLOT_GEN_TIME); do
    printf "\r  Plot generation: %d / %d seconds" "$i" "$PLOT_GEN_TIME"
    sleep 1
done
echo ""
echo "  ✓ Plots should be ready"

# Verify balances
print_header "VERIFYING INITIAL STATE"
TOTAL_AVAILABLE_COINS=0
declare -A SENDER_BALANCES
declare -A SENDER_NONCES
for i in $(seq 1 $NUM_SENDERS); do
    SENDER_BALANCES[$i]=$(get_balance "sender$i")
    SENDER_NONCES[$i]=$(get_nonce "sender$i")
    TOTAL_AVAILABLE_COINS=$((TOTAL_AVAILABLE_COINS + ${SENDER_BALANCES[$i]}))
    echo "  Sender$i: ${SENDER_BALANCES[$i]} coins, nonce ${SENDER_NONCES[$i]}"
done

RECEIVER_INITIAL=$(get_balance bench_receiver)
echo "  Receiver: $RECEIVER_INITIAL coins"
echo "  Total available: $TOTAL_AVAILABLE_COINS coins"
HEIGHT_BEFORE=$(get_height)

if [ "$TOTAL_AVAILABLE_COINS" -lt $((NUM_TRANSACTIONS * 2)) ]; then
    echo ""
    echo -e "${YELLOW}⚠️  Insufficient coins: have $TOTAL_AVAILABLE_COINS, need $((NUM_TRANSACTIONS * 2))${NC}"
    MAX_POSSIBLE_TX=$((TOTAL_AVAILABLE_COINS / 2))
    NUM_TRANSACTIONS=$MAX_POSSIBLE_TX
    echo "  Adjusted to $NUM_TRANSACTIONS transactions"
fi

# ═══════════════════════════════════════════════════════════════════════════
# BENCHMARK: Submit TXs + Process Blocks IN PARALLEL (no pause/resume)
# ═══════════════════════════════════════════════════════════════════════════
# This matches real-world usage: wallets submit TXs while blocks are being
# created. The metronome runs continuously. Validators create blocks with
# whatever TXs are in the pool at the time.
# ═══════════════════════════════════════════════════════════════════════════
print_header "RUNNING END-TO-END TRANSACTION BENCHMARK"

TX_PER_SENDER=$((NUM_TRANSACTIONS / NUM_SENDERS))
REMAINING_TX=$((NUM_TRANSACTIONS % NUM_SENDERS))

echo ""
echo "  Architecture: $NUM_SENDERS sender wallets → Pool → $NUM_FARMERS validators"
echo "  Transactions per sender: $TX_PER_SENDER"
echo "  Extra transactions for sender1: $REMAINING_TX"
echo "  Submission runs IN PARALLEL with block creation (no pause/resume)"
echo ""

BLOCK_TIMES_FILE="$BENCHMARK_DIR/block_times.tmp"
EFFECTIVE_BLOCK_SIZE=$MAX_TXS_PER_BLOCK
if [ "$EFFECTIVE_BLOCK_SIZE" -gt 10000 ]; then
    EFFECTIVE_BLOCK_SIZE=$((MAX_TXS_PER_BLOCK / 2))
fi
if [ "$EFFECTIVE_BLOCK_SIZE" -lt 1 ]; then EFFECTIVE_BLOCK_SIZE=1; fi
BLOCKS_NEEDED=$((NUM_TRANSACTIONS / EFFECTIVE_BLOCK_SIZE + 5))
MAX_WAIT=$((BLOCKS_NEEDED * BLOCK_INTERVAL / 1000 + 60))
if [ "$MAX_WAIT" -lt 120 ]; then MAX_WAIT=120; fi
if [ "$MAX_WAIT" -gt 600 ]; then MAX_WAIT=600; fi

RECEIVER_INITIAL=$(get_balance bench_receiver)
SUBMIT_PROGRESS_DIR="$BENCHMARK_DIR/submit_progress"
mkdir -p "$SUBMIT_PROGRESS_DIR"

# Start PUB/SUB confirmation monitor FIRST (subscribes to pool PUB now)
$BUILD_DIR/wallet wait_confirm bench_receiver "$NUM_TRANSACTIONS" "$RECEIVER_INITIAL" \
    --timeout "$MAX_WAIT" \
    --pub-addr "tcp://localhost:$POOL_PUB_PORT" \
    2>"$BENCHMARK_DIR/confirm_progress.log" \
    >"$BLOCK_TIMES_FILE" &
CONFIRM_PID=$!
sleep 0.3
echo "  ✓ Block monitor started (subscribing to pool PUB on port $POOL_PUB_PORT)"

# Reset difficulty to known-good value for benchmark
DIFF_RESP=$(timeout 5 python3 -c "
import zmq
ctx = zmq.Context()
s = ctx.socket(zmq.REQ)
s.setsockopt(zmq.RCVTIMEO, 3000)
s.connect('tcp://localhost:$METRONOME_REP_PORT')
s.send_string('SET_DIFFICULTY:$EXPLICIT_DIFFICULTY')
print(s.recv_string())
s.close(); ctx.term()
" 2>/dev/null || echo "FAILED")
echo "  Difficulty set: $DIFF_RESP (target: $EXPLICIT_DIFFICULTY)"

echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo "  SUBMITTING $NUM_TRANSACTIONS TXs from $NUM_SENDERS senders (parallel with blocks)"
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""
echo "  Using $NUM_THREADS OpenMP threads per sender × $NUM_SENDERS parallel senders (batch=$BATCH_SIZE)"
echo ""

BENCH_START=$(date +%s.%N)
BENCH_START_MS=$(date +%s%3N)
TX_SUBMITTED=0
TX_ERRORS=0

# Launch all senders in PARALLEL
declare -A SENDER_PIDS
declare -A SENDER_RESULTS
for i in $(seq 1 $NUM_SENDERS); do
    TX_COUNT=$TX_PER_SENDER
    if [ "$i" -eq 1 ]; then
        TX_COUNT=$((TX_PER_SENDER + REMAINING_TX))
    fi
    
    if [ "$TX_COUNT" -gt 0 ]; then
        NONCE=${SENDER_NONCES[$i]}
        SENDER_RESULTS[$i]="$BENCHMARK_DIR/sender${i}_submit.tmp"
        
        $BUILD_DIR/wallet batch_send "sender$i" bench_receiver 1 $TX_COUNT \
            --threads $NUM_THREADS --batch $BATCH_SIZE --nonce "$NONCE" \
            > "${SENDER_RESULTS[$i]}" 2>"$SUBMIT_PROGRESS_DIR/sender${i}.progress" &
        SENDER_PIDS[$i]=$!
    fi
done

# Wait for all senders to complete
echo "  Waiting for all senders to complete..."
for i in $(seq 1 $NUM_SENDERS); do
    if [ -n "${SENDER_PIDS[$i]}" ]; then
        wait ${SENDER_PIDS[$i]} 2>/dev/null
        
        TX_COUNT=$TX_PER_SENDER
        if [ "$i" -eq 1 ]; then
            TX_COUNT=$((TX_PER_SENDER + REMAINING_TX))
        fi
        
        SUBMITTED=0
        if [ -f "${SENDER_RESULTS[$i]}" ]; then
            SUBMITTED=$(grep -oP 'BATCH_RESULT:\K\d+' "${SENDER_RESULTS[$i]}" | tail -1)
            if [ -z "$SUBMITTED" ]; then SUBMITTED=0; fi
        fi
        
        if [ "$SUBMITTED" -gt 0 ]; then
            echo "  Sender$i: submitted $SUBMITTED/$TX_COUNT transactions"
            TX_SUBMITTED=$((TX_SUBMITTED + SUBMITTED))
        else
            echo -e "  Sender$i: ${RED}ERRORS${NC} ($TX_COUNT requested)"
            TX_ERRORS=$((TX_ERRORS + TX_COUNT))
        fi
    fi
done

SUBMIT_END=$(date +%s.%N)
SUBMIT_END_MS=$(date +%s%3N)
SUBMIT_TIME=$(echo "$SUBMIT_END - $BENCH_START" | bc)

# Collect submission progress data for Graph 1
SUBMIT_PROGRESS_CSV="$BENCHMARK_DIR/graph1_submit_progress.csv"
echo "time_sec,cumulative_txs,farmer" > "$SUBMIT_PROGRESS_CSV"
for i in $(seq 1 $NUM_SENDERS); do
    if [ -f "$SUBMIT_PROGRESS_DIR/sender${i}.progress" ]; then
        grep "SUBMIT_PROGRESS:" "$SUBMIT_PROGRESS_DIR/sender${i}.progress" 2>/dev/null | \
            sed 's/SUBMIT_PROGRESS://' | \
            awk -F: '{printf "%.3f,%s,sender'$i'\n", $1/1000.0, $2}' >> "$SUBMIT_PROGRESS_CSV"
    fi
done

echo ""
echo "Submission complete: $TX_SUBMITTED submitted in ${SUBMIT_TIME}s"
if [ "$TX_SUBMITTED" -gt 0 ]; then
    SUBMIT_TPS=$(echo "scale=2; $TX_SUBMITTED / $SUBMIT_TIME" | bc)
    echo "Submission throughput: $SUBMIT_TPS tx/sec"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo "  WAITING FOR CONFIRMATIONS (blocks being created in parallel)"
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""

PROCESS_START=$(date +%s.%N)
PROCESS_START_MS=$(date +%s%3N)

echo "  Watching for $TX_SUBMITTED transactions to be confirmed..."
echo ""

# Wait for confirmations
wait $CONFIRM_PID 2>/dev/null
CONFIRM_EXIT=$?

PROCESS_END=$(date +%s.%N)
PROCESS_TIME=$(echo "$PROCESS_END - $BENCH_START" | bc)

# Show the progress output
if [ -f "$BENCHMARK_DIR/confirm_progress.log" ]; then
    cat "$BENCHMARK_DIR/confirm_progress.log"
fi

# Parse CONFIRMED line from wait_confirm output
CONFIRM_LINE=$(grep "^CONFIRMED:" "$BLOCK_TIMES_FILE" 2>/dev/null | tail -1)
if [ -n "$CONFIRM_LINE" ]; then
    TX_FROM_PUBSUB=$(echo "$CONFIRM_LINE" | cut -d: -f2)
    CONFIRM_HEIGHT=$(echo "$CONFIRM_LINE" | cut -d: -f3)
    CONFIRM_ELAPSED_MS=$(echo "$CONFIRM_LINE" | cut -d: -f4)
fi

# ═══════════════════════════════════════════════════════════════════════════
# Compute processing time from block_times data
# Find the LAST block where NEW transactions were confirmed
# ═══════════════════════════════════════════════════════════════════════════
REAL_CONFIRM_ELAPSED_S=""
PREV_CONFIRMED_BT=0
while IFS=: read -r prefix height tx_count confirmed interval_ms elapsed_s; do
    if [ "$prefix" = "BLOCK_TIME" ] 2>/dev/null; then
        if [ "$confirmed" -gt "$PREV_CONFIRMED_BT" ] 2>/dev/null; then
            REAL_CONFIRM_ELAPSED_S="$elapsed_s"
            PREV_CONFIRMED_BT="$confirmed"
        fi
    fi
done < "$BLOCK_TIMES_FILE"

echo ""
echo "  PUB/SUB reported: ${TX_FROM_PUBSUB:-0} confirmed"

CONFIRM_END=$(date +%s.%N)
CONFIRM_END_MS=$(date +%s%3N)

# Final measurements
BENCH_END=$(date +%s.%N)

# Get confirmed count — use PUB/SUB result as PRIMARY source
# The PUB/SUB monitor tracks balance after each block in real-time.
# The balance query below can fail if the blockchain REP socket is
# momentarily held by a validator's in-flight request.
sleep 0.5  # Let any in-flight validator requests complete
RECEIVER_FINAL=$(get_balance bench_receiver)
TX_CONFIRMED_BALANCE=$((RECEIVER_FINAL - RECEIVER_INITIAL))

# Use PUB/SUB result if available (more reliable than post-hoc balance query)
TX_FROM_PUBSUB=${TX_FROM_PUBSUB:-0}
if [ "$TX_FROM_PUBSUB" -gt "$TX_CONFIRMED_BALANCE" ] 2>/dev/null; then
    TX_CONFIRMED=$TX_FROM_PUBSUB
    echo "  📊 Using PUB/SUB confirmed count: $TX_CONFIRMED (balance query: $TX_CONFIRMED_BALANCE)"
elif [ "$TX_CONFIRMED_BALANCE" -gt 0 ] 2>/dev/null; then
    TX_CONFIRMED=$TX_CONFIRMED_BALANCE
    echo "  📊 Using balance query confirmed count: $TX_CONFIRMED"
else
    TX_CONFIRMED=${TX_FROM_PUBSUB:-0}
    echo "  📊 Confirmed count: $TX_CONFIRMED (PUB/SUB: ${TX_FROM_PUBSUB:-0}, balance: $TX_CONFIRMED_BALANCE)"
fi
HEIGHT_AFTER=$(get_height)
BLOCKS_CREATED=$((HEIGHT_AFTER - HEIGHT_BEFORE))

# 2-PHASE TIMING:
# TOTAL_TIME = processing time (Phase B only)
# Use PUB/SUB elapsed if available, fall back to wall clock
if [ -n "$REAL_CONFIRM_ELAPSED_S" ]; then
    # PUB/SUB started 0.3s before RESUME, adjust
    TOTAL_TIME=$(echo "$REAL_CONFIRM_ELAPSED_S - 0.3" | bc)
    # If too small (all TXs in first block), use wall clock
    TOO_SMALL=$(echo "$TOTAL_TIME < 0.5" | bc 2>/dev/null || echo "0")
    if [ "$TOO_SMALL" = "1" ]; then
        TOTAL_TIME="$PROCESS_TIME"
    fi
    echo ""
    echo -e "  ${GREEN}✓ Processing time: ${TOTAL_TIME}s (RESUME to last confirmed block)${NC}"
else
    TOTAL_TIME="$PROCESS_TIME"
fi

# Guard against zero/empty TOTAL_TIME
ZERO_CHECK=$(echo "${TOTAL_TIME:-0} < 0.01" | bc 2>/dev/null || echo "0")
if [ "$ZERO_CHECK" = "1" ] || [ -z "$TOTAL_TIME" ]; then
    TOTAL_TIME="$PROCESS_TIME"
fi
TOTAL_E2E_TIME=$(echo "$SUBMIT_TIME + $TOTAL_TIME" | bc)

# Parse logs for additional metrics
VALID_PROOFS=$(grep -c "Proof received" "$BENCHMARK_DIR/metronome.log" 2>/dev/null || echo "0")
PROOFS_FOUND=0
for i in $(seq 1 $NUM_FARMERS); do
    if [ -f "$BENCHMARK_DIR/farmer${i}.log" ]; then
        COUNT=$(grep -c "PROOF FOUND" "$BENCHMARK_DIR/farmer${i}.log" 2>/dev/null || echo "0")
        PROOFS_FOUND=$((PROOFS_FOUND + COUNT))
    fi
done

# Calculate rates (guard against divide-by-zero)
SUBMIT_TPS="0.00"
PROCESS_TPS="0.00"
END_TO_END_TPS="0.00"
CONFIRM_RATE="0.0"
BPS="0.0000"
AVG_TX_BLOCK="0.0"

# Submission TPS: ALWAYS calculate (wallet→pool throughput is independent of confirmation)
if [ "$TX_SUBMITTED" -gt 0 ]; then
    NOT_ZERO_SUB=$(echo "$SUBMIT_TIME > 0.001" | bc 2>/dev/null || echo "0")
    [ "$NOT_ZERO_SUB" = "1" ] && SUBMIT_TPS=$(echo "scale=2; $TX_SUBMITTED / $SUBMIT_TIME" | bc)
fi

# Confirmation TPS: only when TXs were confirmed
if [ "$TX_CONFIRMED" -gt 0 ]; then
    NOT_ZERO_PROC=$(echo "$TOTAL_TIME > 0.001" | bc 2>/dev/null || echo "0")
    NOT_ZERO_E2E=$(echo "$TOTAL_E2E_TIME > 0.001" | bc 2>/dev/null || echo "0")
    
    [ "$NOT_ZERO_PROC" = "1" ] && PROCESS_TPS=$(echo "scale=2; $TX_CONFIRMED / $TOTAL_TIME" | bc)
    [ "$NOT_ZERO_E2E" = "1" ] && END_TO_END_TPS=$(echo "scale=2; $TX_CONFIRMED / $TOTAL_E2E_TIME" | bc)
    CONFIRM_RATE=$(echo "scale=1; $TX_CONFIRMED * 100 / $TX_SUBMITTED" | bc)
fi

if [ "$BLOCKS_CREATED" -gt 0 ]; then
    NOT_ZERO=$(echo "$TOTAL_TIME > 0.001" | bc 2>/dev/null || echo "0")
    [ "$NOT_ZERO" = "1" ] && BPS=$(echo "scale=4; $BLOCKS_CREATED / $TOTAL_TIME" | bc)
    AVG_TX_BLOCK=$(echo "scale=1; $TX_CONFIRMED / $BLOCKS_CREATED" | bc)
fi

AVG_LATENCY="N/A"

print_header "BENCHMARK RESULTS"

print_subheader "SUBMISSION METRICS (Wallet → Pool)"
print_metric "Transactions submitted" "$TX_SUBMITTED"
print_metric "Submission errors" "$TX_ERRORS"
print_metric "Submission time" "${SUBMIT_TIME}s"
print_metric "Submission throughput (TPS)" "$SUBMIT_TPS tx/sec"

print_subheader "CONFIRMATION METRICS (Pool → Block → Chain)"
print_metric "Transactions confirmed" "$TX_CONFIRMED"
print_metric "Confirmation rate" "${CONFIRM_RATE}%"
print_metric "Processing time (RESUME to done)" "${TOTAL_TIME}s"
print_metric "Processing throughput (TPS)" "$PROCESS_TPS tx/sec"

print_subheader "END-TO-END METRICS (Complete Pipeline)"
print_metric "Submission time (Phase A)" "${SUBMIT_TIME}s"
print_metric "Processing time (Phase B)" "${TOTAL_TIME}s"
print_metric "Total benchmark time (A + B)" "${TOTAL_E2E_TIME}s"
print_metric "End-to-end throughput (TPS)" "$END_TO_END_TPS tx/sec"

print_subheader "BLOCK METRICS"
print_metric "Blocks created during benchmark" "$BLOCKS_CREATED"
print_metric "Blocks per second" "$BPS"
print_metric "Average transactions per block" "$AVG_TX_BLOCK"
print_metric "Block interval (configured)" "${BLOCK_INTERVAL}ms"
print_metric "Chain height (before → after)" "$HEIGHT_BEFORE → $HEIGHT_AFTER"

# ═══════════════════════════════════════════════════════════════════════════
# PER-BLOCK TIMING ANALYSIS (v29.2)
# ═══════════════════════════════════════════════════════════════════════════
print_subheader "PER-BLOCK TIMING ANALYSIS (where time is spent)"

if [ -f "$BLOCK_TIMES_FILE" ] && grep -q "^BLOCK_TIME:" "$BLOCK_TIMES_FILE" 2>/dev/null; then
    BLOCK_COUNT=0
    TOTAL_INTERVAL_MS=0
    MAX_INTERVAL_MS=0
    MIN_INTERVAL_MS=999999
    PREV_CONFIRMED=0
    
    echo ""
    printf "  %-8s  %-8s  %-12s  %-12s  %-10s  %s\n" \
           "Block#" "TXs" "Interval" "Elapsed" "Confirmed" "New TXs"
    printf "  %-8s  %-8s  %-12s  %-12s  %-10s  %s\n" \
           "──────" "────" "────────" "───────" "─────────" "───────"
    
    while IFS=: read -r prefix height tx_count confirmed interval_ms elapsed_s; do
        if [ "$prefix" = "BLOCK_TIME" ]; then
            BLOCK_COUNT=$((BLOCK_COUNT + 1))
            NEW_TXS=$((confirmed - PREV_CONFIRMED))
            PREV_CONFIRMED=$confirmed
            
            INT_MS=$(printf "%.0f" "$interval_ms")
            TOTAL_INTERVAL_MS=$((TOTAL_INTERVAL_MS + INT_MS))
            if [ "$INT_MS" -gt "$MAX_INTERVAL_MS" ]; then MAX_INTERVAL_MS=$INT_MS; fi
            if [ "$INT_MS" -lt "$MIN_INTERVAL_MS" ] && [ "$BLOCK_COUNT" -gt 1 ]; then MIN_INTERVAL_MS=$INT_MS; fi
            
            if [ "$INT_MS" -lt 1200 ]; then COLOR="${GREEN}";
            elif [ "$INT_MS" -lt 2000 ]; then COLOR="${YELLOW}";
            else COLOR="${RED}"; fi
            
            printf "  %-8s  %-8s  ${COLOR}%-12s${NC}  %-12s  %-10s  %s\n" \
                   "#$height" "$tx_count" "${INT_MS}ms" "${elapsed_s}s" "$confirmed" "+$NEW_TXS"
        fi
    done < "$BLOCK_TIMES_FILE"
    
    echo ""
    if [ "$BLOCK_COUNT" -gt 1 ]; then
        AVG_INTERVAL_MS=$((TOTAL_INTERVAL_MS / BLOCK_COUNT))
        OVERHEAD_MS=$((AVG_INTERVAL_MS - BLOCK_INTERVAL))
        
        print_metric "Blocks observed (PUB/SUB)" "$BLOCK_COUNT"
        print_metric "Average block interval" "${AVG_INTERVAL_MS}ms (target: ${BLOCK_INTERVAL}ms)"
        if [ "$MIN_INTERVAL_MS" -lt 999999 ]; then
            print_metric "Min / Max interval" "${MIN_INTERVAL_MS}ms / ${MAX_INTERVAL_MS}ms"
        fi
        print_metric "Average overhead per block" "${OVERHEAD_MS}ms"
        
        echo ""
        echo "  📊 OVERHEAD BREAKDOWN (v45.3 strict 1-second blocks):"
        echo "     Block time (configured):              ${BLOCK_INTERVAL} ms (HARD DEADLINE)"
        echo "     ├─ Proof collection window:            adaptive (~800 ms)"
        echo "     ├─ Winner announce + block creation:   adaptive (~85 ms)"
        echo "     ├─ Blockchain validation + confirm:    async via PUB/SUB"
        echo "     └─ Safety margin:                      50 ms"
        echo "     Average measured interval:             ~${AVG_INTERVAL_MS} ms"
    elif [ "$BLOCK_COUNT" -eq 1 ]; then
        print_metric "Blocks observed (PUB/SUB)" "$BLOCK_COUNT"
        echo "  (Need 2+ blocks for interval analysis)"
    fi
    
    # Parse validator logs for per-step timing
    echo ""
    print_subheader "VALIDATOR STEP TIMING (from logs)"
    echo ""
    
    for i in $(seq 1 $NUM_FARMERS); do
        LOG_FILE="$BENCHMARK_DIR/farmer${i}.log"
        if [ -f "$LOG_FILE" ]; then
            STEP1=$(grep "Step 1 (GET_LAST_HASH)" "$LOG_FILE" 2>/dev/null | tail -1 | grep -oP '\d+ ms' | grep -oP '^\d+')
            STEP2=$(grep "Step 2" "$LOG_FILE" 2>/dev/null | tail -1 | grep -oP '\d+ ms' | head -1 | grep -oP '^\d+')
            STEP5=$(grep "Step 5" "$LOG_FILE" 2>/dev/null | tail -1 | grep -oP 'total: \K\d+')
            STEP6=$(grep "Step 6 (Pool CONFIRM)" "$LOG_FILE" 2>/dev/null | tail -1 | grep -oP '\d+ ms' | head -1 | grep -oP '^\d+')
            
            if [ -n "$STEP2" ] || [ -n "$STEP5" ]; then
                echo "  Farmer $i (last block):"
                [ -n "$STEP1" ] && echo "    Step 1 (GET_LAST_HASH):      ${STEP1} ms"
                [ -n "$STEP2" ] && echo "    Step 2 (Pool→Validator):      ${STEP2} ms"
                [ -n "$STEP5" ] && echo "    Step 5 (Serialize+Send):      ${STEP5} ms"
                [ -n "$STEP6" ] && echo "    Step 6 (Pool CONFIRM):        ${STEP6} ms"
                echo ""
            fi
        fi
    done
else
    echo "  (No per-block timing data available from PUB/SUB)"
fi

print_subheader "PROOF METRICS"
print_metric "Proofs found by farmers" "$PROOFS_FOUND"
print_metric "Valid proofs accepted" "$VALID_PROOFS"
print_metric "Proofs per farmer per block" "$(echo "scale=1; $PROOFS_FOUND / $NUM_FARMERS / $BLOCKS_CREATED" | bc 2>/dev/null || echo "N/A")"

print_subheader "BALANCE VERIFICATION"
# Show sender balances (these are the ones that sent TXs)
echo "  --- Sender Wallets (TX senders) ---"
TOTAL_SPENT=0
for i in $(seq 1 $NUM_SENDERS); do
    FINAL_BAL=$(get_balance "sender$i")
    INITIAL=${SENDER_BALANCES[$i]:-0}
    SPENT=$((INITIAL - FINAL_BAL))
    TOTAL_SPENT=$((TOTAL_SPENT + SPENT))
    print_metric "Sender$i (initial → final)" "$INITIAL → $FINAL_BAL (Δ: $((FINAL_BAL - INITIAL)))"
done

# Show validator balances (these gain mining rewards)
echo ""
echo "  --- Validator Wallets (miners) ---"
TOTAL_MINING_REWARDS=0
for i in $(seq 1 $NUM_FARMERS); do
    FINAL_BAL=$(get_balance "farmer$i")
    MINING_GAIN=$FINAL_BAL  # Validators start at 0 (not pre-funded), gain only mining rewards
    TOTAL_MINING_REWARDS=$((TOTAL_MINING_REWARDS + MINING_GAIN))
    print_metric "Farmer$i (mining rewards)" "$FINAL_BAL coins"
done

# Calculate expected values
TOTAL_DEBITED=$((TX_CONFIRMED * 2))  # Each TX: sender pays value(1) + fee(1) = 2
TOTAL_VALUE_TRANSFERRED=$TX_CONFIRMED  # Receiver gets 1 per TX
TOTAL_FEES=$TX_CONFIRMED  # 1 fee per TX
MINING_BASE=$((BLOCKS_CREATED * $BASE_MINING_REWARD))  # base reward per block

echo ""
echo "  ═══════════════════════════════════════════════════════"
echo "  Fee Flow Analysis:"
echo "  ═══════════════════════════════════════════════════════"
echo "  DEBITS (from senders):"
echo "    Value transferred:    $TX_CONFIRMED coins ($TX_CONFIRMED TXs × 1 coin)"
echo "    Fees paid:            $TX_CONFIRMED coins ($TX_CONFIRMED TXs × 1 coin)"
echo "    Total debited:        $TOTAL_DEBITED coins"
echo ""
echo "  CREDITS (to recipients):"
echo "    Receiver got:         $(get_balance bench_receiver) coins"
echo "    Mining base rewards:  $MINING_BASE coins ($BLOCKS_CREATED blocks × $BASE_MINING_REWARD)"
echo "    Mining fee rewards:   $TOTAL_FEES coins (all TX fees → winners)"
echo "    Total mining rewards: $((MINING_BASE + TOTAL_FEES)) coins"
echo ""
echo "  NET BALANCE CHECK:"
echo "    Total debited:        $TOTAL_DEBITED coins"
echo "    Total credited:       $((TX_CONFIRMED + MINING_BASE + TOTAL_FEES)) coins"
echo "    Net farmer Δ:         $((0 - TOTAL_SPENT)) coins (mining rewards - TX costs)"
echo "  ═══════════════════════════════════════════════════════"

# Save CSV
cat >> "$CSV_FILE" << EOF
tx_submitted,$TX_SUBMITTED,count
tx_confirmed,$TX_CONFIRMED,count
tx_errors,$TX_ERRORS,count
submit_time_phase_a,$SUBMIT_TIME,seconds
process_time_phase_b,$TOTAL_TIME,seconds
total_e2e_time,$TOTAL_E2E_TIME,seconds
submit_tps,$SUBMIT_TPS,tx/sec
process_tps,$PROCESS_TPS,tx/sec
end_to_end_tps,$END_TO_END_TPS,tx/sec
confirm_rate,$CONFIRM_RATE,percent
blocks_created,$BLOCKS_CREATED,count
bps,$BPS,blocks/sec
avg_tx_per_block,$AVG_TX_BLOCK,count
chain_height_before,$HEIGHT_BEFORE,blocks
chain_height_after,$HEIGHT_AFTER,blocks
proofs_found,$PROOFS_FOUND,count
valid_proofs,$VALID_PROOFS,count
k_param,$K_PARAM,value
num_farmers,$NUM_FARMERS,count
block_interval,$BLOCK_INTERVAL,ms
max_txs_per_block,$MAX_TXS_PER_BLOCK,value
batch_size,$BATCH_SIZE,value
num_threads,$NUM_THREADS,value
difficulty,$EXPLICIT_DIFFICULTY,value
receiver_initial,$RECEIVER_INITIAL,coins
receiver_final,$RECEIVER_FINAL,coins
total_available_coins,$TOTAL_AVAILABLE_COINS,coins
total_spent,$TOTAL_SPENT,coins
warmup_blocks,$WARMUP_BLOCKS,blocks
EOF

# Generate HTML Report
HTML_FILE="$BENCHMARK_DIR/report_$(date +%Y%m%d_%H%M%S).html"
cat > "$HTML_FILE" << 'HTMLEOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Blockchain Benchmark Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); color: #eee; min-height: 100vh; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 30px; font-size: 2.5em; background: linear-gradient(90deg, #00d4ff, #7c3aed); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 30px; }
        .card { background: rgba(255,255,255,0.05); border-radius: 15px; padding: 20px; backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.1); }
        .card h3 { color: #00d4ff; margin-bottom: 15px; font-size: 1.2em; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 10px; }
        .metric { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid rgba(255,255,255,0.05); }
        .metric:last-child { border-bottom: none; }
        .metric-label { color: #aaa; }
        .metric-value { font-weight: bold; color: #00d4ff; }
        .metric-value.good { color: #00ff88; }
        .metric-value.warn { color: #ffaa00; }
        .metric-value.bad { color: #ff4444; }
        .big-number { font-size: 3em; text-align: center; margin: 20px 0; }
        .big-number span { font-size: 0.4em; color: #aaa; }
        .chart-container { height: 300px; margin-top: 20px; }
        .summary-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; }
        .summary-item { text-align: center; padding: 20px; background: rgba(0,212,255,0.1); border-radius: 10px; }
        .summary-item .value { font-size: 2em; color: #00d4ff; }
        .summary-item .label { color: #aaa; font-size: 0.9em; margin-top: 5px; }
        table { width: 100%; border-collapse: collapse; margin-top: 15px; }
        th, td { padding: 10px; text-align: left; border-bottom: 1px solid rgba(255,255,255,0.1); }
        th { color: #00d4ff; }
        .progress-bar { background: rgba(255,255,255,0.1); border-radius: 10px; height: 20px; overflow: hidden; }
        .progress-fill { height: 100%; background: linear-gradient(90deg, #00d4ff, #00ff88); transition: width 0.5s; }
        @media (max-width: 768px) { .summary-grid { grid-template-columns: repeat(2, 1fr); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔗 Blockchain Benchmark Report</h1>
        
        <div class="summary-grid">
            <div class="summary-item">
                <div class="value" id="e2e-tps">--</div>
                <div class="label">End-to-End TPS</div>
            </div>
            <div class="summary-item">
                <div class="value" id="confirm-rate">--</div>
                <div class="label">Confirmation Rate</div>
            </div>
            <div class="summary-item">
                <div class="value" id="tx-confirmed">--</div>
                <div class="label">TX Confirmed</div>
            </div>
            <div class="summary-item">
                <div class="value" id="avg-tx-block">--</div>
                <div class="label">Avg TX/Block</div>
            </div>
        </div>

        <div class="grid">
            <div class="card">
                <h3>📊 Throughput Comparison</h3>
                <div class="chart-container">
                    <canvas id="tpsChart"></canvas>
                </div>
            </div>
            <div class="card">
                <h3>⏱️ Time Breakdown</h3>
                <div class="chart-container">
                    <canvas id="timeChart"></canvas>
                </div>
            </div>
        </div>

        <div class="grid">
            <div class="card">
                <h3>📤 Submission Metrics</h3>
                <div class="metric"><span class="metric-label">Transactions Submitted</span><span class="metric-value" id="tx-submitted">--</span></div>
                <div class="metric"><span class="metric-label">Submission Errors</span><span class="metric-value" id="tx-errors">--</span></div>
                <div class="metric"><span class="metric-label">Submission Time</span><span class="metric-value" id="submit-time">--</span></div>
                <div class="metric"><span class="metric-label">Submission TPS</span><span class="metric-value" id="submit-tps">--</span></div>
            </div>
            <div class="card">
                <h3>✅ Confirmation Metrics</h3>
                <div class="metric"><span class="metric-label">Transactions Confirmed</span><span class="metric-value" id="tx-confirmed-2">--</span></div>
                <div class="metric"><span class="metric-label">Confirmation Rate</span><span class="metric-value" id="confirm-rate-2">--</span></div>
                <div class="metric"><span class="metric-label">Confirmation Time</span><span class="metric-value" id="confirm-time">--</span></div>
                <div class="metric"><span class="metric-label">Average Latency</span><span class="metric-value" id="avg-latency">--</span></div>
            </div>
            <div class="card">
                <h3>🧱 Block Metrics</h3>
                <div class="metric"><span class="metric-label">Blocks Created</span><span class="metric-value" id="blocks-created">--</span></div>
                <div class="metric"><span class="metric-label">Blocks/Second</span><span class="metric-value" id="bps">--</span></div>
                <div class="metric"><span class="metric-label">Avg TX/Block</span><span class="metric-value" id="avg-tx-block-2">--</span></div>
                <div class="metric"><span class="metric-label">Chain Height</span><span class="metric-value" id="chain-height">--</span></div>
            </div>
            <div class="card">
                <h3>⚙️ Configuration</h3>
                <div class="metric"><span class="metric-label">K Parameter</span><span class="metric-value" id="k-param">--</span></div>
                <div class="metric"><span class="metric-label">Block Interval</span><span class="metric-value" id="block-interval">--</span></div>
                <div class="metric"><span class="metric-label">Difficulty</span><span class="metric-value" id="difficulty">--</span></div>
                <div class="metric"><span class="metric-label">Farmers</span><span class="metric-value" id="num-farmers">--</span></div>
            </div>
        </div>

        <div class="card">
            <h3>📈 Performance Analysis</h3>
            <div class="metric">
                <span class="metric-label">Theoretical Max TPS (micro benchmark)</span>
                <span class="metric-value" id="theoretical-tps">~115,000 tx/sec</span>
            </div>
            <div class="metric">
                <span class="metric-label">Actual End-to-End TPS</span>
                <span class="metric-value" id="actual-tps">--</span>
            </div>
            <div class="metric">
                <span class="metric-label">Efficiency (Actual/Theoretical)</span>
                <span class="metric-value" id="efficiency">--</span>
            </div>
            <p style="margin-top: 15px; color: #aaa; font-size: 0.9em;">
                Note: Theoretical TPS measures raw serialization + ZMQ latency. Actual E2E TPS includes 
                consensus, block creation, and confirmation latency. The gap represents the overhead of 
                the full blockchain pipeline.
            </p>
        </div>
    </div>

    <script>
HTMLEOF

# Inject the data
cat >> "$HTML_FILE" << EOF
        const data = {
            txSubmitted: $TX_SUBMITTED,
            txConfirmed: $TX_CONFIRMED,
            txErrors: $TX_ERRORS,
            submitTime: $SUBMIT_TIME,
            confirmTime: $TOTAL_TIME,
            totalTime: $TOTAL_TIME,
            submitTps: $SUBMIT_TPS,
            processTps: $PROCESS_TPS,
            e2eTps: $END_TO_END_TPS,
            confirmRate: $CONFIRM_RATE,
            blocksCreated: $BLOCKS_CREATED,
            bps: $BPS,
            avgTxBlock: $AVG_TX_BLOCK,
            heightBefore: $HEIGHT_BEFORE,
            heightAfter: $HEIGHT_AFTER,
            kParam: $K_PARAM,
            blockInterval: $BLOCK_INTERVAL,
            difficulty: $EXPLICIT_DIFFICULTY,
            numFarmers: $NUM_FARMERS,
            avgLatency: "$AVG_LATENCY",
            warmupBlocks: $WARMUP_BLOCKS
        };
EOF

cat >> "$HTML_FILE" << 'HTMLEOF'
        // Populate values
        document.getElementById('e2e-tps').textContent = data.e2eTps.toFixed(2);
        document.getElementById('confirm-rate').textContent = data.confirmRate.toFixed(1) + '%';
        document.getElementById('tx-confirmed').textContent = data.txConfirmed.toLocaleString();
        document.getElementById('avg-tx-block').textContent = data.avgTxBlock.toFixed(1);
        
        document.getElementById('tx-submitted').textContent = data.txSubmitted.toLocaleString();
        document.getElementById('tx-errors').textContent = data.txErrors;
        document.getElementById('submit-time').textContent = data.submitTime.toFixed(2) + 's';
        document.getElementById('submit-tps').textContent = data.submitTps.toFixed(2) + ' tx/sec';
        
        document.getElementById('tx-confirmed-2').textContent = data.txConfirmed.toLocaleString();
        document.getElementById('confirm-rate-2').textContent = data.confirmRate.toFixed(1) + '%';
        document.getElementById('confirm-time').textContent = data.confirmTime.toFixed(2) + 's';
        document.getElementById('avg-latency').textContent = data.avgLatency;
        
        document.getElementById('blocks-created').textContent = data.blocksCreated;
        document.getElementById('bps').textContent = data.bps.toFixed(4);
        document.getElementById('avg-tx-block-2').textContent = data.avgTxBlock.toFixed(1);
        document.getElementById('chain-height').textContent = data.heightBefore + ' → ' + data.heightAfter;
        
        document.getElementById('k-param').textContent = data.kParam + ' (2^' + data.kParam + ' entries)';
        document.getElementById('block-interval').textContent = data.blockInterval + 'ms';
        document.getElementById('difficulty').textContent = data.difficulty;
        document.getElementById('num-farmers').textContent = data.numFarmers;
        
        document.getElementById('actual-tps').textContent = data.e2eTps.toFixed(2) + ' tx/sec';
        const efficiency = (data.e2eTps / 115000 * 100).toFixed(3);
        document.getElementById('efficiency').textContent = efficiency + '%';

        // TPS Chart
        new Chart(document.getElementById('tpsChart'), {
            type: 'bar',
            data: {
                labels: ['Submission TPS', 'End-to-End TPS', 'Theoretical Max'],
                datasets: [{
                    data: [data.submitTps, data.e2eTps, 115000],
                    backgroundColor: ['#00d4ff', '#00ff88', '#7c3aed'],
                    borderRadius: 5
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { display: false } },
                scales: {
                    y: { 
                        type: 'logarithmic',
                        grid: { color: 'rgba(255,255,255,0.1)' },
                        ticks: { color: '#aaa' }
                    },
                    x: { 
                        grid: { display: false },
                        ticks: { color: '#aaa' }
                    }
                }
            }
        });

        // Time Chart
        new Chart(document.getElementById('timeChart'), {
            type: 'doughnut',
            data: {
                labels: ['Submission', 'Confirmation'],
                datasets: [{
                    data: [data.submitTime, data.confirmTime],
                    backgroundColor: ['#00d4ff', '#00ff88'],
                    borderWidth: 0
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: { 
                        position: 'bottom',
                        labels: { color: '#aaa' }
                    }
                }
            }
        });
    </script>
</body>
</html>
HTMLEOF

# Cleanup temp files
rm -f "$BENCHMARK_DIR/submit_times.tmp"
rm -f "$BENCHMARK_DIR/block_times2.tmp"
rm -f "$BENCHMARK_DIR"/farmer*_submit.tmp

# ═══════════════════════════════════════════════════════════════════════════
# GRAPH DATA EXPORT
# ═══════════════════════════════════════════════════════════════════════════
print_header "EXPORTING GRAPH DATA"

# Graph 2 data: BLOCK_TIMING from validator logs (strip ANSI escape codes!)
BLOCK_TIMING_CSV="$BENCHMARK_DIR/graph2_block_timing.csv"
echo "block_height,tx_count,get_hash_ms,fetch_tx_ms,serialize_send_ms,confirm_ms,total_ms" > "$BLOCK_TIMING_CSV"
for i in $(seq 1 $NUM_FARMERS); do
    LOG_FILE="$BENCHMARK_DIR/farmer${i}.log"
    if [ -f "$LOG_FILE" ]; then
        grep "BLOCK_TIMING:" "$LOG_FILE" 2>/dev/null | \
            sed 's/\x1b\[[0-9;]*m//g' | \
            sed 's/.*BLOCK_TIMING://' | tr ':' ',' >> "$BLOCK_TIMING_CSV"
    fi
done
TIMING_ROWS=$(wc -l < "$BLOCK_TIMING_CSV")
echo "  ✓ Graph 2 data: $((TIMING_ROWS - 1)) blocks → $BLOCK_TIMING_CSV"

# Graph 4 data: ROUND_TIMING from metronome log (full pipeline per round)
ROUND_TIMING_CSV="$BENCHMARK_DIR/graph4_round_timing.csv"
echo "block_height,proof_count,challenge_ms,proof_ms,block_ms,sleep_ms" > "$ROUND_TIMING_CSV"
if [ -f "$BENCHMARK_DIR/metronome.log" ]; then
    grep "ROUND_TIMING:" "$BENCHMARK_DIR/metronome.log" 2>/dev/null | \
        sed 's/\x1b\[[0-9;]*m//g' | \
        sed 's/.*ROUND_TIMING://' | tr ':' ',' >> "$ROUND_TIMING_CSV"
fi
ROUND_ROWS=$(wc -l < "$ROUND_TIMING_CSV")
echo "  ✓ Graph 4 data: $((ROUND_ROWS - 1)) rounds → $ROUND_TIMING_CSV"

# Graph 1 data: submission progress  
PROGRESS_ROWS=$(wc -l < "$SUBMIT_PROGRESS_CSV" 2>/dev/null || echo "1")
echo "  ✓ Graph 1 data: $((PROGRESS_ROWS - 1)) progress points → $SUBMIT_PROGRESS_CSV"

# Block times from PUB/SUB
echo "  ✓ Block times: $BLOCK_TIMES_FILE"

# Auto-generate graphs if matplotlib available
echo ""
if [ -f "./generate_graphs.py" ] && command -v python3 &>/dev/null; then
    echo "  Generating graphs..."
    python3 ./generate_graphs.py "$BENCHMARK_DIR" 2>&1 | sed 's/^/  /'
fi

echo ""
echo "Results saved to:"
echo "  • $CSV_FILE (system benchmark)"
echo "  • $INTERNAL_CSV (micro benchmark)"
echo "  • $HTML_FILE (visual report)"
echo "  • Logs in $BENCHMARK_DIR/"
echo ""

print_header "BENCHMARK COMPLETE"

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                      SUMMARY                                      ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
printf "${GREEN}║${NC}  %-20s: %-40s${GREEN}║${NC}\n" "Submission TPS" "$SUBMIT_TPS tx/sec"
printf "${GREEN}║${NC}  %-20s: %-40s${GREEN}║${NC}\n" "End-to-End TPS" "$END_TO_END_TPS tx/sec"
printf "${GREEN}║${NC}  %-20s: %-40s${GREEN}║${NC}\n" "Confirmation Rate" "${CONFIRM_RATE}%"
printf "${GREEN}║${NC}  %-20s: %-40s${GREEN}║${NC}\n" "Avg TX/Block" "$AVG_TX_BLOCK"
printf "${GREEN}║${NC}  %-20s: %-40s${GREEN}║${NC}\n" "Warmup Blocks" "$WARMUP_BLOCKS"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
