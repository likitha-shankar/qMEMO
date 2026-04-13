#!/bin/bash

# ╔══════════════════════════════════════════════════════════════════════════════╗
# ║                    BLOCKCHAIN POS v10 - STARTUP SCRIPT                        ║
# ╠══════════════════════════════════════════════════════════════════════════════╣
# ║  Features:                                                                    ║
# ║    • BLAKE3 hashing for fast plot generation                                  ║
# ║    • Protocol Buffers for efficient serialization                             ║
# ║    • Nonce-based transactions with expiry (Ethereum-style)                    ║
# ║    • Difficulty range 1-256 (each level = 1 leading zero bit)                 ║
# ║  Run with --help for usage information                                        ║
# ╚══════════════════════════════════════════════════════════════════════════════╝

set -e

# =============================================================================
# DEFAULT PARAMETERS (can be overridden via command line)
# =============================================================================

# Network ports
BLOCKCHAIN_PORT=5555        # Blockchain server REP socket
METRONOME_REP_PORT=5556     # Metronome REP socket (for proof submissions)
POOL_PORT=5557              # Transaction pool REP socket
METRONOME_PUB_PORT=5558     # Metronome PUB socket (for challenge broadcasts)
BLOCKCHAIN_PUB_PORT=5559    # Blockchain PUB socket (block notifications, v29.2)
METRONOME_NOTIFY_PORT=5560  # Metronome PULL socket (blockchain notifications, v29)
POOL_PUB_PORT=5561          # Pool PUB socket (wallet confirmations)

# Metronome parameters
BLOCK_INTERVAL=6            # Seconds between blocks (challenge rounds)
INITIAL_DIFFICULTY=0        # Starting difficulty (0 = auto: k + log2(validators))
MINING_REWARD=10000            # Base mining reward in coins
HALVING_INTERVAL=100        # Halve reward every N blocks

# Validator parameters
K_PARAM=18                  # Plot size: 2^K entries (18 = 256K entries, good for testing)
NUM_FARMERS=3               # Number of farmers to start

# Build directory
BUILD_DIR="./build"

# Session name
SESSION_NAME="blockchain_pos"

# =============================================================================
# HELP MESSAGE
# =============================================================================

show_help() {
    cat << EOF

╔══════════════════════════════════════════════════════════════════════════════╗
║                    BLOCKCHAIN POS v10 - STARTUP SCRIPT                        ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  Features:                                                                    ║
║    • BLAKE3 hashing for fast plot generation                                  ║
║    • Protocol Buffers for efficient serialization                             ║
║    • Nonce-based transactions with expiry (Ethereum-style)                    ║
║    • Difficulty range 1-256 (each level = 1 leading zero bit)                 ║
║    • Default difficulty = k + log2(validators)                                ║
╠══════════════════════════════════════════════════════════════════════════════╣

Usage: $0 [OPTIONS]

Network Options:
  --blockchain-port PORT    Blockchain server port (default: $BLOCKCHAIN_PORT)
  --metronome-rep PORT      Metronome REP port (default: $METRONOME_REP_PORT)
  --pool-port PORT          Transaction pool port (default: $POOL_PORT)
  --metronome-pub PORT      Metronome PUB port (default: $METRONOME_PUB_PORT)

Metronome Options:
  --block-interval SECS     Seconds between blocks (default: $BLOCK_INTERVAL)
  --difficulty LEVEL        Initial difficulty 1-256, 0=auto (default: $INITIAL_DIFFICULTY)
  --mining-reward N         Base mining reward in coins (default: $MINING_REWARD)
  --halving-interval N      Halve reward every N blocks (default: $HALVING_INTERVAL)

Validator Options:
  --k-param K               Plot size parameter (default: $K_PARAM)
                            K=16: 65K entries (~2MB, very fast)
                            K=18: 256K entries (~8MB, fast)
                            K=20: 1M entries (~32MB, moderate)
                            K=22: 4M entries (~128MB, slower)
  --num-farmers N           Number of farmers (default: $NUM_FARMERS)

Other Options:
  --build-dir DIR           Build directory (default: $BUILD_DIR)
  --session NAME            Tmux session name (default: $SESSION_NAME)
  -h, --help                Show this help

Examples:
  $0                                      # Start with defaults
  $0 --k-param 16 --num-farmers 5         # Tiny plots, more farmers
  $0 --difficulty 15 --block-interval 10  # Lower difficulty, slower blocks
  $0 --mining-reward 100 --halving-interval 50  # Higher rewards, faster halving

╚══════════════════════════════════════════════════════════════════════════════╝

EOF
}

# =============================================================================
# PARSE COMMAND LINE ARGUMENTS
# =============================================================================

while [[ $# -gt 0 ]]; do
    case $1 in
        --blockchain-port) BLOCKCHAIN_PORT="$2"; shift 2 ;;
        --metronome-rep) METRONOME_REP_PORT="$2"; shift 2 ;;
        --pool-port) POOL_PORT="$2"; shift 2 ;;
        --metronome-pub) METRONOME_PUB_PORT="$2"; shift 2 ;;
        --block-interval) BLOCK_INTERVAL="$2"; shift 2 ;;
        --difficulty) INITIAL_DIFFICULTY="$2"; shift 2 ;;
        --mining-reward) MINING_REWARD="$2"; shift 2 ;;
        --halving-interval) HALVING_INTERVAL="$2"; shift 2 ;;
        --k-param) K_PARAM="$2"; shift 2 ;;
        --num-farmers) NUM_FARMERS="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --session) SESSION_NAME="$2"; shift 2 ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "Unknown option: $1"; show_help; exit 1 ;;
    esac
done

# =============================================================================
# BUILD PROJECT
# =============================================================================

echo ""
echo "╔══════════════════════════════════════════════════════════════════════════════╗"
echo "║                         BUILDING PROJECT                                      ║"
echo "╚══════════════════════════════════════════════════════════════════════════════╝"
echo ""

make clean 2>/dev/null || true
make all

if [ ! -f "$BUILD_DIR/blockchain" ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

# =============================================================================
# CREATE WALLETS
# =============================================================================

echo ""
echo "╔══════════════════════════════════════════════════════════════════════════════╗"
echo "║                         CREATING WALLETS                                      ║"
echo "╚══════════════════════════════════════════════════════════════════════════════╝"
echo ""

# Create farmer wallets
for i in $(seq 1 $NUM_FARMERS); do
    $BUILD_DIR/wallet create "farmer$i" 2>/dev/null || true
done

# Create user wallets for testing
$BUILD_DIR/wallet create "alice" 2>/dev/null || true
$BUILD_DIR/wallet create "bob" 2>/dev/null || true

# =============================================================================
# KILL EXISTING SESSION
# =============================================================================

tmux kill-session -t $SESSION_NAME 2>/dev/null || true
sleep 1

# =============================================================================
# CALCULATE DERIVED VALUES FOR DISPLAY
# =============================================================================

# Calculate plot size for display
PLOT_ENTRIES=$((1 << K_PARAM))
PLOT_SIZE_MB=$(( (PLOT_ENTRIES * 28) / 1024 / 1024 ))

# Calculate auto difficulty if not specified
if [ "$INITIAL_DIFFICULTY" -eq 0 ]; then
    # Auto: k + log2(validators)
    LOG2_VALIDATORS=$(python3 -c "import math; print(int(math.log2($NUM_FARMERS)))" 2>/dev/null || echo "1")
    AUTO_DIFFICULTY=$((K_PARAM + LOG2_VALIDATORS))
    DIFFICULTY_DISPLAY="auto ($AUTO_DIFFICULTY = $K_PARAM + $LOG2_VALIDATORS)"
else
    AUTO_DIFFICULTY=$INITIAL_DIFFICULTY
    DIFFICULTY_DISPLAY="$INITIAL_DIFFICULTY"
fi

# =============================================================================
# CLEANUP EXISTING PROCESSES AND SESSIONS
# =============================================================================

echo ""
echo "Checking for existing blockchain processes..."

# Kill existing tmux session if it exists
if tmux has-session -t $SESSION_NAME 2>/dev/null; then
    echo "  Killing existing tmux session '$SESSION_NAME'..."
    tmux kill-session -t $SESSION_NAME 2>/dev/null || true
    sleep 1
fi

# Kill any blockchain processes that might still be running
echo "  Stopping any remaining blockchain processes..."
pkill -9 -f "$BUILD_DIR/blockchain" 2>/dev/null || true
pkill -9 -f "$BUILD_DIR/metronome" 2>/dev/null || true
pkill -9 -f "$BUILD_DIR/pool" 2>/dev/null || true
pkill -9 -f "$BUILD_DIR/validator" 2>/dev/null || true

# Also try killing by executable name directly
killall -9 blockchain 2>/dev/null || true
killall -9 metronome 2>/dev/null || true
killall -9 pool 2>/dev/null || true
killall -9 validator 2>/dev/null || true

# Free up ports if they're still bound (fuser may not be available on all systems)
echo "  Releasing ports..."
if command -v fuser &> /dev/null; then
    fuser -k ${BLOCKCHAIN_PORT}/tcp 2>/dev/null || true
    fuser -k ${POOL_PORT}/tcp 2>/dev/null || true
    fuser -k ${METRONOME_REP_PORT}/tcp 2>/dev/null || true
    fuser -k ${METRONOME_PUB_PORT}/tcp 2>/dev/null || true
    fuser -k ${METRONOME_NOTIFY_PORT}/tcp 2>/dev/null || true
fi

# Also try ss/netstat to find and kill processes on our ports
if command -v ss &> /dev/null; then
    for PORT in $BLOCKCHAIN_PORT $POOL_PORT $METRONOME_REP_PORT $METRONOME_PUB_PORT $METRONOME_NOTIFY_PORT; do
        PID=$(ss -tlnp 2>/dev/null | grep ":$PORT " | grep -oP 'pid=\K\d+' | head -1)
        if [ -n "$PID" ]; then
            kill -9 "$PID" 2>/dev/null || true
        fi
    done
fi

# Wait for ports to be released (increased wait time)
sleep 3
echo "  ✓ Cleanup complete"

# =============================================================================
# START TMUX SESSION
# =============================================================================

echo ""
echo "╔══════════════════════════════════════════════════════════════════════════════╗"
echo "║                         STARTING BLOCKCHAIN                                   ║"
echo "╠══════════════════════════════════════════════════════════════════════════════╣"
echo "║  Network Configuration:                                                       ║"
echo "║    Blockchain Port:    $BLOCKCHAIN_PORT                                                ║"
echo "║    Pool Port:          $POOL_PORT                                                ║"
echo "║    Metronome REP:      $METRONOME_REP_PORT                                                ║"
echo "║    Metronome PUB:      $METRONOME_PUB_PORT                                                ║"
echo "╠══════════════════════════════════════════════════════════════════════════════╣"
echo "║  Consensus Configuration:                                                     ║"
echo "║    Block Interval:     ${BLOCK_INTERVAL}s                                                 ║"
printf "║    Difficulty:         %-43s║\n" "$DIFFICULTY_DISPLAY"
echo "║    Mining Reward:      $MINING_REWARD coins                                            ║"
echo "║    Halving Interval:   ${HALVING_INTERVAL} blocks                                          ║"
echo "╠══════════════════════════════════════════════════════════════════════════════╣"
echo "║  Farmer Configuration:                                                        ║"
echo "║    K Parameter:        $K_PARAM (2^$K_PARAM = $PLOT_ENTRIES entries)                    ║"
echo "║    Plot Size:          ~${PLOT_SIZE_MB}MB per farmer                                     ║"
echo "║    Number of Farmers:  $NUM_FARMERS                                                  ║"
echo "╚══════════════════════════════════════════════════════════════════════════════╝"
echo ""

# Create new tmux session with blockchain window
# Window 0: Blockchain Server
tmux new-session -d -s $SESSION_NAME -n "blockchain"
tmux send-keys -t $SESSION_NAME:0 "$BUILD_DIR/blockchain tcp://*:$BLOCKCHAIN_PORT --metronome-notify tcp://localhost:$METRONOME_NOTIFY_PORT --pub tcp://*:$BLOCKCHAIN_PUB_PORT" C-m

# Window 1: Transaction Pool
tmux new-window -t $SESSION_NAME -n "pool"
sleep 0.5
tmux send-keys -t $SESSION_NAME:1 "$BUILD_DIR/pool tcp://*:$POOL_PORT --sub tcp://localhost:$BLOCKCHAIN_PUB_PORT --pub tcp://*:$POOL_PUB_PORT" C-m

# Window 2: Metronome
tmux new-window -t $SESSION_NAME -n "metronome"
sleep 0.5

# Build metronome command
METRONOME_CMD="$BUILD_DIR/metronome"
METRONOME_CMD="$METRONOME_CMD --rep tcp://*:$METRONOME_REP_PORT"
METRONOME_CMD="$METRONOME_CMD --pub tcp://*:$METRONOME_PUB_PORT"
METRONOME_CMD="$METRONOME_CMD --blockchain tcp://localhost:$BLOCKCHAIN_PORT"
METRONOME_CMD="$METRONOME_CMD --pool tcp://localhost:$POOL_PORT"
METRONOME_CMD="$METRONOME_CMD -i $BLOCK_INTERVAL"
METRONOME_CMD="$METRONOME_CMD -k $K_PARAM"
METRONOME_CMD="$METRONOME_CMD -v $NUM_FARMERS"
METRONOME_CMD="$METRONOME_CMD -r $MINING_REWARD"
METRONOME_CMD="$METRONOME_CMD --halving $HALVING_INTERVAL"
METRONOME_CMD="$METRONOME_CMD --notify tcp://*:$METRONOME_NOTIFY_PORT"

# Add difficulty only if explicitly set
if [ "$INITIAL_DIFFICULTY" -ne 0 ]; then
    METRONOME_CMD="$METRONOME_CMD -d $INITIAL_DIFFICULTY"
fi

tmux send-keys -t $SESSION_NAME:2 "$METRONOME_CMD" C-m

# Window 3: Farmers (all in one window with splits)
tmux new-window -t $SESSION_NAME -n "farmers"
sleep 1  # Wait for metronome to start

# Build validator command base
# Validators now need pool and blockchain connections to create blocks!
VALIDATOR_OPTS="--metronome-sub tcp://localhost:$METRONOME_PUB_PORT"
VALIDATOR_OPTS="$VALIDATOR_OPTS --metronome-req tcp://localhost:$METRONOME_REP_PORT"
VALIDATOR_OPTS="$VALIDATOR_OPTS --pool tcp://localhost:$POOL_PORT"
VALIDATOR_OPTS="$VALIDATOR_OPTS --blockchain tcp://localhost:$BLOCKCHAIN_PORT"
VALIDATOR_OPTS="$VALIDATOR_OPTS -k $K_PARAM"
if [ -n "${MAX_TXS_PER_BLOCK:-}" ]; then
    VALIDATOR_OPTS="$VALIDATOR_OPTS --max-txs $MAX_TXS_PER_BLOCK"
fi

# Start first farmer
tmux send-keys -t $SESSION_NAME:3 "$BUILD_DIR/validator farmer1 $VALIDATOR_OPTS" C-m

# Start additional farmers in splits
for i in $(seq 2 $NUM_FARMERS); do
    tmux split-window -t $SESSION_NAME:3 -v
    tmux select-layout -t $SESSION_NAME:3 tiled
    tmux send-keys -t $SESSION_NAME:3 "$BUILD_DIR/validator farmer$i $VALIDATOR_OPTS" C-m
done

# Window 4: Wallet/Commands Help Window
tmux new-window -t $SESSION_NAME -n "wallet"
tmux send-keys -t $SESSION_NAME:4 "cat << 'HELP'

╔══════════════════════════════════════════════════════════════════════════════╗
║                         WALLET COMMANDS                                       ║
╠══════════════════════════════════════════════════════════════════════════════╣
║                                                                               ║
║  QUICK REFERENCE:                                                             ║
║  ────────────────                                                             ║
║                                                                               ║
║  Check balance:                                                               ║
║    $BUILD_DIR/wallet balance farmer1                                          ║
║    $BUILD_DIR/wallet balance alice                                            ║
║                                                                               ║
║  Check nonce (next transaction number):                                       ║
║    $BUILD_DIR/wallet nonce farmer1                                            ║
║                                                                               ║
║  Send transaction:                                                            ║
║    $BUILD_DIR/wallet send <from> <to> <amount> [expiry_blocks]                ║
║                                                                               ║
║    Examples:                                                                  ║
║      $BUILD_DIR/wallet send farmer1 alice 40       # Default expiry           ║
║      $BUILD_DIR/wallet send farmer1 alice 40 50    # Expires in ~5 min        ║
║      $BUILD_DIR/wallet send farmer1 alice 40 0     # Never expires            ║
║                                                                               ║
║  Create new wallet:                                                           ║
║    $BUILD_DIR/wallet create mywallet                                          ║
║                                                                               ║
║  View wallet info:                                                            ║
║    $BUILD_DIR/wallet info farmer1.dat                                         ║
║                                                                               ║
║  Convert name to address:                                                     ║
║    $BUILD_DIR/wallet address farmer1                                          ║
║                                                                               ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  EXPIRY OPTIONS:                                                              ║
║  ───────────────                                                              ║
║    0        - Never expires (not recommended)                                 ║
║    50       - ~5 minutes (time-sensitive)                                     ║
║    100      - ~10 minutes (default, recommended)                              ║
║    600      - ~1 hour (congested network)                                     ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  MINING REWARDS:                                                              ║
║  ───────────────                                                              ║
║    Base reward: $MINING_REWARD coins                                                   ║
║    Halving every: $HALVING_INTERVAL blocks                                             ║
║    Winner gets: mining_reward + sum(transaction_fees)                         ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  EXISTING WALLETS:                                                            ║
║  ────────────────                                                             ║
$(for i in $(seq 1 $NUM_FARMERS); do printf "║    farmer%d.dat                                                               ║\n" $i; done)
║    alice.dat                                                                  ║
║    bob.dat                                                                    ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  TMUX SHORTCUTS:                                                              ║
║  ───────────────                                                              ║
║    Ctrl+b n     Next window                                                   ║
║    Ctrl+b p     Previous window                                               ║
║    Ctrl+b 0-4   Go to window number                                           ║
║    Ctrl+b d     Detach session                                                ║
║    Ctrl+b [     Scroll mode (q to exit)                                       ║
╚══════════════════════════════════════════════════════════════════════════════╝

HELP" C-m

# Window 5: Monitor (unified log viewer)
tmux new-window -t $SESSION_NAME -n "monitor"
tmux send-keys -t $SESSION_NAME:5 "echo '╔══════════════════════════════════════════╗'; echo '║  MEMO Blockchain — Monitor               ║'; echo '╚══════════════════════════════════════════╝'; echo 'Watching component logs for important events...'; echo 'Run benchmark.sh to generate logs, then use: ./monitor.sh ./benchmark_results'; echo ''" C-m

# Select metronome window to watch the action
tmux select-window -t $SESSION_NAME:2

echo ""
echo "╔══════════════════════════════════════════════════════════════════════════════╗"
echo "║                         BLOCKCHAIN STARTED!                                   ║"
echo "╠══════════════════════════════════════════════════════════════════════════════╣"
echo "║                                                                               ║"
echo "║  Tmux Windows:                                                                ║"
echo "║    0: blockchain  - Blockchain server (stores blocks, manages ledger)         ║"
echo "║    1: pool        - Transaction pool (mempool for pending transactions)       ║"
echo "║    2: metronome   - Challenge broadcaster (coordinates consensus)             ║"
echo "║    3: farmers     - All validators/farmers (compete for blocks)               ║"
echo "║    4: wallet      - Wallet CLI (help displayed)                               ║"
echo "║    5: monitor     - Unified log viewer (important events)                     ║"
echo "║                                                                               ║"
echo "║  Architecture:                                                                ║"
echo "║    Validators (farmer1-N): Find proofs, create blocks, earn rewards           ║"
echo "║    Sender wallets: Separate wallets for sending TXs (not validators)          ║"
echo "║    Pool PUB port $POOL_PUB_PORT: Forwards confirmations to wallets                     ║"
echo "║                                                                               ║"
echo "║  Commands:                                                                    ║"
echo "║    Attach:   tmux attach -t $SESSION_NAME                                       ║"
echo "║    Detach:   Ctrl+b d                                                         ║"
echo "║    Stop:     tmux kill-session -t $SESSION_NAME                                 ║"
echo "║                                                                               ║"
echo "╚══════════════════════════════════════════════════════════════════════════════╝"
echo ""

# Attach to session
tmux attach -t $SESSION_NAME
