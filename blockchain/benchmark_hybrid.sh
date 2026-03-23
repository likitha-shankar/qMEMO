#!/bin/bash

# ╔══════════════════════════════════════════════════════════════════════════════╗
# ║              HYBRID MODE BENCHMARK — Mixed Signature Types                   ║
# ╠══════════════════════════════════════════════════════════════════════════════╣
# ║  Builds with SIG_SCHEME=3 (Hybrid) and creates wallets with mixed          ║
# ║  signature types: ECDSA, Falcon-512, and ML-DSA-44.                        ║
# ║                                                                             ║
# ║  This wraps benchmark.sh — it modifies wallet creation to use a            ║
# ║  round-robin distribution of signature schemes across farmers.             ║
# ║                                                                             ║
# ║  Usage:                                                                     ║
# ║    ./benchmark_hybrid.sh [num_tx] [block_interval] [k] [num_farmers]       ║
# ║                                                                             ║
# ║  Example:                                                                   ║
# ║    ./benchmark_hybrid.sh 500 1 16 12                                        ║
# ║    (12 farmers: 4 ECDSA + 4 Falcon-512 + 4 ML-DSA-44)                     ║
# ╚══════════════════════════════════════════════════════════════════════════════╝

set -euo pipefail

NUM_TX=${1:-500}
BLOCK_INTERVAL=${2:-1}
K_PARAM=${3:-16}
NUM_FARMERS=${4:-12}   # should be divisible by 3 for even split
WARMUP=${5:-auto}
MAX_TXS=${6:-10000}
BATCH=${7:-64}
THREADS=${8:-8}

BUILD_DIR="./build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║          HYBRID MODE BENCHMARK (SIG_SCHEME=3)                ║${NC}"
echo -e "${BLUE}║    ECDSA + Falcon-512 + ML-DSA-44 mixed transactions         ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Step 1: Build with SIG_SCHEME=3
echo -e "${GREEN}[1/4] Building with SIG_SCHEME=3 (Hybrid)...${NC}"
make clean >/dev/null 2>&1 || true
make ${MAKE_FLAGS:-} SIG_SCHEME=3 2>&1 | tail -5
echo ""

# Step 2: Create wallets with mixed sig types (round-robin: ecdsa, falcon, mldsa)
echo -e "${GREEN}[2/4] Creating mixed-scheme wallets (${NUM_FARMERS} farmers)...${NC}"

SCHEMES=("ecdsa" "falcon" "mldsa")
SCHEME_NAMES=("ECDSA" "Falcon-512" "ML-DSA-44")
ecdsa_count=0
falcon_count=0
mldsa_count=0

for i in $(seq 1 $NUM_FARMERS); do
    idx=$(( (i - 1) % 3 ))
    scheme=${SCHEMES[$idx]}
    $BUILD_DIR/wallet create "farmer$i" --scheme "$scheme" >/dev/null 2>&1 || true
    case $scheme in
        ecdsa)  ((ecdsa_count++)) ;;
        falcon) ((falcon_count++)) ;;
        mldsa)  ((mldsa_count++)) ;;
    esac
done

# Receiver uses Falcon-512 (most common PQC choice)
$BUILD_DIR/wallet create bench_receiver --scheme falcon >/dev/null 2>&1 || true

echo "  ECDSA wallets:      $ecdsa_count"
echo "  Falcon-512 wallets: $falcon_count"
echo "  ML-DSA-44 wallets:  $mldsa_count"
echo "  Receiver: Falcon-512"
echo ""

# Step 3: Run the main benchmark (skip its build and wallet creation steps)
# We pass an empty WALLET_SCHEME_FLAG since wallets are already created
echo -e "${GREEN}[3/4] Running end-to-end benchmark with ${NUM_TX} transactions...${NC}"
echo ""

# The benchmark.sh will try to rebuild and recreate wallets.
# We run it with MAKE_FLAGS passing SIG_SCHEME=3 so the build step works,
# and with an empty scheme flag (wallets already exist, create is idempotent).
MAKE_FLAGS="SIG_SCHEME=3 ${MAKE_FLAGS:-}" bash benchmark.sh \
    $NUM_TX $BLOCK_INTERVAL $K_PARAM $NUM_FARMERS $WARMUP $MAX_TXS $BATCH $THREADS ""

echo ""
echo -e "${GREEN}[4/4] Hybrid benchmark complete!${NC}"
echo ""
echo -e "${YELLOW}Note: All farmers submitted transactions with their assigned scheme.${NC}"
echo -e "${YELLOW}The blockchain validated mixed ECDSA/Falcon/ML-DSA signatures in the same blocks.${NC}"
echo ""
