#!/usr/bin/env bash
# setup_chameleon.sh — Run once on a fresh CC-Ubuntu24.04 Chameleon node.
# Installs all dependencies and builds liboqs (AVX-512 + AVX2 variants).
#
# Usage:
#   bash setup_chameleon.sh
#
# Takes ~15 minutes. Safe to re-run.

set -euo pipefail
NPROC=$(nproc)
HOME_DIR="$HOME"
INSTALL_DIR="$HOME_DIR/liboqs_install"
INSTALL_DIR_AVX2="$HOME_DIR/liboqs_install_avx2"

info() { echo -e "\n\033[1;34m==> $*\033[0m"; }
ok()   { echo -e "\033[1;32m    OK: $*\033[0m"; }

# ── 1. System packages ────────────────────────────────────────────────────────
info "Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    build-essential cmake git ninja-build \
    libssl-dev pkg-config \
    python3-pip python3-venv \
    linux-tools-common linux-tools-generic "linux-tools-$(uname -r)" \
    numactl hwloc cpufrequtils \
    perf-tools-unstable 2>/dev/null || true

# cpupower may be in a different package on Ubuntu 24.04
sudo apt-get install -y linux-cpupower 2>/dev/null || \
sudo apt-get install -y cpupower 2>/dev/null || true

ok "System packages done"

# ── 2. Python analysis dependencies ───────────────────────────────────────────
info "Installing Python packages..."
pip3 install --quiet pandas matplotlib numpy
ok "Python packages done"

# ── 3. Clone liboqs ───────────────────────────────────────────────────────────
info "Cloning liboqs..."
if [[ ! -d "$HOME_DIR/liboqs" ]]; then
    git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git "$HOME_DIR/liboqs"
else
    echo "  liboqs already cloned, pulling latest..."
    git -C "$HOME_DIR/liboqs" pull --ff-only || true
fi

# Record exact commit for reproducibility
OQS_COMMIT=$(git -C "$HOME_DIR/liboqs" rev-parse HEAD)
echo "  liboqs commit: $OQS_COMMIT"
echo "$OQS_COMMIT" > "$HOME_DIR/liboqs_commit.txt"
ok "liboqs cloned"

# ── 4. Build liboqs (AVX-512 enabled — primary build) ─────────────────────────
info "Building liboqs with AVX-512 (primary)..."
mkdir -p "$HOME_DIR/liboqs/build_avx512"
cmake -S "$HOME_DIR/liboqs" \
      -B "$HOME_DIR/liboqs/build_avx512" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DOQS_USE_OPENSSL=ON \
      -DOQS_OPT_TARGET=auto \
      -DOQS_USE_AVX2_INSTRUCTIONS=ON \
      -DOQS_USE_AVX512_INSTRUCTIONS=ON \
      -DOQS_DIST_BUILD=OFF \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

cmake --build "$HOME_DIR/liboqs/build_avx512" -j"$NPROC"
cmake --install "$HOME_DIR/liboqs/build_avx512"
ok "liboqs AVX-512 build → $INSTALL_DIR"

# ── 5. Build liboqs (AVX2 only — control build for Fig 6) ─────────────────────
info "Building liboqs with AVX2 only (AVX-512 disabled for Cascade Lake frequency comparison)..."
mkdir -p "$HOME_DIR/liboqs/build_avx2"
cmake -S "$HOME_DIR/liboqs" \
      -B "$HOME_DIR/liboqs/build_avx2" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DOQS_USE_OPENSSL=ON \
      -DOQS_OPT_TARGET=auto \
      -DOQS_USE_AVX2_INSTRUCTIONS=ON \
      -DOQS_USE_AVX512_INSTRUCTIONS=OFF \
      -DOQS_DIST_BUILD=OFF \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR_AVX2"

cmake --build "$HOME_DIR/liboqs/build_avx2" -j"$NPROC"
cmake --install "$HOME_DIR/liboqs/build_avx2"
ok "liboqs AVX2-only build → $INSTALL_DIR_AVX2"

# ── 6. Build bench_sign ────────────────────────────────────────────────────────
info "Building bench_sign..."
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../" && pwd)"
make -C "$REPO_DIR/benchmarks/experiment1" \
    OQS_ROOT="$INSTALL_DIR" \
    OQS_ROOT_AVX2="$INSTALL_DIR_AVX2"
ok "bench_sign built"

# ── 7. Quick sanity check ──────────────────────────────────────────────────────
info "Running quick sanity check (Ed25519, 1 thread, 200 iters)..."
BENCH="$REPO_DIR/benchmarks/bin/bench_sign"
"$BENCH" \
    --algo ed25519 \
    --threads 1 \
    --iterations 200 \
    --warmup 50 \
    --runs 1 \
    --output-prefix /tmp/sanity_ed25519 \
    --tag sanity

echo ""
echo "Sanity summary:"
cat /tmp/sanity_ed25519_summary.csv
ok "Sanity check passed"

# ── 8. Print next steps ────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Setup complete!  liboqs commit: $OQS_COMMIT"
echo ""
echo "  Next:"
echo "  1. sudo bash scripts/prepare_system.sh"
echo "  2. bash scripts/verify_system.sh"
echo "  3. bash scripts/run_sweep.sh"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
