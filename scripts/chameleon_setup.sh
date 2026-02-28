#!/usr/bin/env bash
# chameleon_setup.sh -- Bootstrap qMEMO benchmarks on a Chameleon Cloud bare-metal node.
#
# Tested on: CC-Ubuntu22.04 image, Cascade Lake / compute_icelake_r650
#
# Usage (run as the default 'cc' user after SSH login):
#   bash chameleon_setup.sh
#
# What this script does:
#   1. Installs all build dependencies via apt
#   2. Clones the qMEMO repo
#   3. Builds liboqs 0.15.0 from source (into qMEMO/liboqs_install/)
#   4. Builds all 11 benchmark binaries
#   5. Runs the full benchmark suite and saves timestamped logs
#
# After the run, logs are in:
#   ~/qMEMO/benchmarks/results/run_YYYYMMDD_HHMMSS/

set -euo pipefail

REPO_URL="https://github.com/likitha-shankar/qMEMO.git"
REPO_DIR="$HOME/qMEMO"

# color helpers
info()  { printf "\033[0;36m[INFO]\033[0m  %s\n" "$*"; }
ok()    { printf "\033[0;32m[ OK ]\033[0m  %s\n" "$*"; }
fail()  { printf "\033[0;31m[FAIL]\033[0m  %s\n" "$*" >&2; exit 1; }
step()  { printf "\n\033[1m== Step %s ==\033[0m\n" "$*"; }

# ── 1. System check ---------------------------------------------------------
step "1/5: System check"
if [[ "$(uname -s)" != "Linux" ]]; then
    fail "This script is for Linux (Chameleon bare-metal). On macOS use install_liboqs.sh directly."
fi
ARCH="$(uname -m)"
info "Architecture: $ARCH"
info "OS: $(lsb_release -ds 2>/dev/null || cat /etc/os-release | grep PRETTY | cut -d= -f2)"
info "Cores: $(nproc)"
info "RAM: $(free -h | awk '/^Mem:/{print $2}')"

if [[ "$ARCH" == "x86_64" ]]; then
    info "x86-64 detected -- RDTSC cycle counting will be used (exact, matches published benchmarks)"
else
    info "non-x86 detected -- cycle counts estimated from wall clock"
fi

# ── 2. Install dependencies -------------------------------------------------
step "2/5: Install build dependencies"
sudo apt-get update -qq
sudo apt-get install -y \
    git \
    cmake \
    build-essential \
    gcc \
    ninja-build \
    libssl-dev \
    pkg-config \
    python3 \
    python3-pip \
    astyle \
    doxygen \
    2>/dev/null
ok "Dependencies installed"

# ── 3. Clone repo -----------------------------------------------------------
step "3/5: Clone qMEMO repo"
if [[ -d "$REPO_DIR" ]]; then
    info "Repo already exists at $REPO_DIR, pulling latest..."
    git -C "$REPO_DIR" pull --ff-only
else
    git clone "$REPO_URL" "$REPO_DIR"
fi
ok "Repo ready at $REPO_DIR"

cd "$REPO_DIR"

# ── 4. Build liboqs + benchmarks --------------------------------------------
step "4/5: Build liboqs and benchmark binaries"
bash install_liboqs.sh
ok "liboqs built and installed"

cd benchmarks
make clean all
ok "All benchmark binaries built in benchmarks/bin/"
cd ..

# ── 5. Run benchmarks -------------------------------------------------------
step "5/5: Run full benchmark suite"
bash scripts/run_logged.sh

echo ""
ok "Setup and benchmark run complete."
echo ""
echo "Results are in: $REPO_DIR/benchmarks/results/"
echo ""
echo "To copy results back to your Mac:"
echo "  scp -r cc@<your-floating-ip>:~/qMEMO/benchmarks/results/ ."
echo ""
