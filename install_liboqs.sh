#!/usr/bin/env bash
#
# install_liboqs.sh — Download, compile, and install liboqs (Open Quantum Safe)
#
# Builds liboqs from source with aggressive optimisations for benchmarking,
# installs both static and shared libraries into a project-local prefix,
# runs the upstream test suite, and records the full build configuration.
#
# Usage:
#   chmod +x install_liboqs.sh
#   ./install_liboqs.sh
#
# Re-running is safe — the clone is reused and builds are performed fresh
# (the previous build tree is removed to guarantee a clean state).

set -euo pipefail

# ── Colours (disabled when stdout is not a terminal) ────────────────────────
if [ -t 1 ]; then
    BOLD='\033[1m'
    GREEN='\033[0;32m'
    CYAN='\033[0;36m'
    YELLOW='\033[0;33m'
    RED='\033[0;31m'
    RESET='\033[0m'
else
    BOLD='' GREEN='' CYAN='' YELLOW='' RED='' RESET=''
fi

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

LIBOQS_REPO="https://github.com/open-quantum-safe/liboqs.git"
LIBOQS_SRC="$PROJECT_ROOT/liboqs"
LIBOQS_PREFIX="$PROJECT_ROOT/liboqs_install"
BUILD_DIR_SHARED="$LIBOQS_SRC/build_shared"
BUILD_DIR_STATIC="$LIBOQS_SRC/build_static"

TIMESTAMP="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
NPROC=""

# ── Helpers ─────────────────────────────────────────────────────────────────
info()  { printf "${CYAN}[INFO]${RESET}  %s\n" "$*"; }
ok()    { printf "${GREEN}[ OK ]${RESET}  %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${RESET}  %s\n" "$*"; }
fail()  { printf "${RED}[FAIL]${RESET}  %s\n" "$*" >&2; exit 1; }

step() {
    printf "\n${BOLD}── Step %s ──${RESET}\n" "$1"
}

elapsed_since() {
    local start=$1
    local now
    now=$(date +%s)
    echo "$(( now - start ))s"
}

# ── Banner ──────────────────────────────────────────────────────────────────
printf "\n${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n"
printf "${BOLD}  qMEMO — liboqs Installation${RESET}\n"
printf "${BOLD}═══════════════════════════════════════════════════════════════${RESET}\n\n"

# ── 1. Detect OS & architecture ────────────────────────────────────────────
step "1/7: Detect platform"

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Darwin) OS_PRETTY="macOS $(sw_vers -productVersion 2>/dev/null || echo 'unknown')" ;;
    Linux)  OS_PRETTY="Linux $(uname -r)" ;;
    *)      fail "Unsupported OS: $OS (macOS or Linux required)" ;;
esac
ok "Platform: $OS_PRETTY ($ARCH)"

# Determine parallel job count
if command -v nproc &>/dev/null; then
    NPROC="$(nproc)"
elif [ "$OS" = "Darwin" ]; then
    NPROC="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
    NPROC=4
fi
ok "Parallel jobs: $NPROC"

# Choose the right arch tuning flag.
# Apple Clang on arm64 does not support -march=native; use -mcpu=native instead.
if [ "$OS" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    ARCH_FLAG="-mcpu=native"
else
    ARCH_FLAG="-march=native"
fi

CFLAGS_OPT="-O3 ${ARCH_FLAG} -ffast-math"
ok "C optimisation flags: $CFLAGS_OPT"

# ── 2. Check / install dependencies ────────────────────────────────────────
step "2/7: Check dependencies"

MISSING=()

for cmd in git cmake; do
    if command -v "$cmd" &>/dev/null; then
        ok "Found $cmd: $(command -v "$cmd")"
    else
        MISSING+=("$cmd")
        warn "Missing: $cmd"
    fi
done

# Prefer Ninja, fall back to Make
if command -v ninja &>/dev/null; then
    CMAKE_GENERATOR="Ninja"
    BUILD_TOOL="ninja"
    ok "Found ninja: $(command -v ninja)"
elif command -v make &>/dev/null; then
    CMAKE_GENERATOR="Unix Makefiles"
    BUILD_TOOL="make"
    ok "Found make: $(command -v make)"
else
    MISSING+=("ninja-or-make")
    warn "Missing: ninja or make"
fi

# Check for a C compiler
CC_FOUND=""
for cc in cc gcc clang; do
    if command -v "$cc" &>/dev/null; then
        CC_FOUND="$cc"
        ok "Found C compiler: $(command -v "$cc")"
        break
    fi
done
[ -z "$CC_FOUND" ] && { MISSING+=("c-compiler"); warn "Missing: C compiler (gcc or clang)"; }

# Attempt to auto-install missing tools
if [ "${#MISSING[@]}" -gt 0 ]; then
    info "Attempting to install missing dependencies: ${MISSING[*]}"

    if [ "$OS" = "Darwin" ]; then
        if ! command -v brew &>/dev/null; then
            fail "Homebrew is required to install dependencies on macOS. Install from https://brew.sh"
        fi
        for pkg in "${MISSING[@]}"; do
            case "$pkg" in
                git)            brew install git ;;
                cmake)          brew install cmake ;;
                ninja-or-make)  brew install ninja ;;
                c-compiler)     fail "Install Xcode Command Line Tools: xcode-select --install" ;;
            esac
        done
        # Re-detect generator after install
        if command -v ninja &>/dev/null; then
            CMAKE_GENERATOR="Ninja"
            BUILD_TOOL="ninja"
        fi
    elif [ "$OS" = "Linux" ]; then
        if command -v apt-get &>/dev/null; then
            sudo apt-get update -qq
            for pkg in "${MISSING[@]}"; do
                case "$pkg" in
                    git)            sudo apt-get install -y git ;;
                    cmake)          sudo apt-get install -y cmake ;;
                    ninja-or-make)  sudo apt-get install -y ninja-build ;;
                    c-compiler)     sudo apt-get install -y build-essential ;;
                esac
            done
        elif command -v yum &>/dev/null; then
            for pkg in "${MISSING[@]}"; do
                case "$pkg" in
                    git)            sudo yum install -y git ;;
                    cmake)          sudo yum install -y cmake ;;
                    ninja-or-make)  sudo yum install -y ninja-build ;;
                    c-compiler)     sudo yum groupinstall -y "Development Tools" ;;
                esac
            done
        else
            fail "Cannot auto-install: no supported package manager found (brew / apt / yum)"
        fi
        if command -v ninja &>/dev/null; then
            CMAKE_GENERATOR="Ninja"
            BUILD_TOOL="ninja"
        fi
    fi

    ok "Dependencies installed"
fi

# ── 3. Clone liboqs ────────────────────────────────────────────────────────
step "3/7: Clone liboqs"

if [ -d "$LIBOQS_SRC/.git" ]; then
    info "Repository already exists — pulling latest changes …"
    git -C "$LIBOQS_SRC" fetch --tags
    git -C "$LIBOQS_SRC" pull --ff-only || warn "Pull failed (detached HEAD?); proceeding with existing checkout"
    ok "Updated: $LIBOQS_SRC"
else
    info "Cloning $LIBOQS_REPO …"
    git clone --depth 1 "$LIBOQS_REPO" "$LIBOQS_SRC"
    ok "Cloned to: $LIBOQS_SRC"
fi

LIBOQS_VERSION="$(git -C "$LIBOQS_SRC" describe --tags --always 2>/dev/null || echo 'unknown')"
ok "liboqs version: $LIBOQS_VERSION"

# ── 4. Build shared library ────────────────────────────────────────────────
step "4/7: Build shared library"

# Clean previous build tree to guarantee reproducibility
rm -rf "$BUILD_DIR_SHARED"
mkdir -p "$BUILD_DIR_SHARED"

info "Configuring (shared) …"
cmake -S "$LIBOQS_SRC" -B "$BUILD_DIR_SHARED" \
    -G "$CMAKE_GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$LIBOQS_PREFIX" \
    -DCMAKE_C_FLAGS="$CFLAGS_OPT" \
    -DBUILD_SHARED_LIBS=ON \
    -DOQS_BUILD_ONLY_LIB=OFF \
    -DOQS_USE_OPENSSL=ON \
    -DOQS_ENABLE_SIG_FALCON_512=ON \
    -DOQS_ENABLE_SIG_FALCON_1024=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_2=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_3=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_5=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_128F_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_128S_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_192F_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_256F_SIMPLE=ON \
    2>&1 | tail -5
ok "Configured shared build"

BUILD_START=$(date +%s)
info "Compiling (shared) with $NPROC jobs …"
cmake --build "$BUILD_DIR_SHARED" --parallel "$NPROC"
ok "Shared library built in $(elapsed_since $BUILD_START)"

info "Installing shared library …"
cmake --install "$BUILD_DIR_SHARED"
ok "Shared library installed to $LIBOQS_PREFIX"

# ── 5. Build static library ────────────────────────────────────────────────
step "5/7: Build static library"

rm -rf "$BUILD_DIR_STATIC"
mkdir -p "$BUILD_DIR_STATIC"

info "Configuring (static) …"
cmake -S "$LIBOQS_SRC" -B "$BUILD_DIR_STATIC" \
    -G "$CMAKE_GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$LIBOQS_PREFIX" \
    -DCMAKE_C_FLAGS="$CFLAGS_OPT" \
    -DBUILD_SHARED_LIBS=OFF \
    -DOQS_BUILD_ONLY_LIB=OFF \
    -DOQS_USE_OPENSSL=ON \
    -DOQS_ENABLE_SIG_FALCON_512=ON \
    -DOQS_ENABLE_SIG_FALCON_1024=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_2=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_3=ON \
    -DOQS_ENABLE_SIG_DILITHIUM_5=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_128F_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_128S_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_192F_SIMPLE=ON \
    -DOQS_ENABLE_SIG_SPHINCS_SHA2_256F_SIMPLE=ON \
    2>&1 | tail -5
ok "Configured static build"

BUILD_START=$(date +%s)
info "Compiling (static) with $NPROC jobs …"
cmake --build "$BUILD_DIR_STATIC" --parallel "$NPROC"
ok "Static library built in $(elapsed_since $BUILD_START)"

info "Installing static library …"
cmake --install "$BUILD_DIR_STATIC"
ok "Static library installed to $LIBOQS_PREFIX"

# ── 6. Run tests ───────────────────────────────────────────────────────────
step "6/7: Verify build (tests)"

info "Running liboqs test suite (using static build) …"
TEST_START=$(date +%s)

# Run the core KAT (Known Answer Tests) for signature schemes
if cmake --build "$BUILD_DIR_STATIC" --target run_tests 2>/dev/null; then
    ok "Test suite passed in $(elapsed_since $TEST_START)"
elif [ -f "$BUILD_DIR_STATIC/tests/test_sig" ]; then
    info "Falling back to direct test_sig execution …"
    if "$BUILD_DIR_STATIC/tests/test_sig" 2>&1 | tail -20; then
        ok "Signature tests passed in $(elapsed_since $TEST_START)"
    else
        warn "Some signature tests failed — check output above"
    fi
else
    warn "Test binaries not found; set OQS_BUILD_ONLY_LIB=OFF and rebuild to enable tests"
fi

# ── 7. Summarise installed algorithms ──────────────────────────────────────
step "7/7: Summary"

info "Installed libraries:"
LIBS_FOUND=()
for pattern in "$LIBOQS_PREFIX"/lib/liboqs.*; do
    [ -e "$pattern" ] || continue
    LIBS_FOUND+=("$(basename "$pattern")")
    printf "  ${GREEN}✓${RESET} %s\n" "$(basename "$pattern")"
done
[ "${#LIBS_FOUND[@]}" -eq 0 ] && warn "No libraries found in $LIBOQS_PREFIX/lib/"

info "Installed headers:"
if [ -d "$LIBOQS_PREFIX/include/oqs" ]; then
    HEADER_COUNT=$(find "$LIBOQS_PREFIX/include/oqs" -name '*.h' | wc -l | tr -d ' ')
    ok "$HEADER_COUNT header files in $LIBOQS_PREFIX/include/oqs/"
else
    warn "Header directory not found"
fi

# List available signature algorithms from the installed header
info "Available signature algorithms:"
SIG_ALGOS=()
if [ -f "$LIBOQS_PREFIX/include/oqs/oqs.h" ]; then
    HEADER_DIR="$LIBOQS_PREFIX/include/oqs"
    # Scan all sig headers for algorithm name macros
    while IFS= read -r alg; do
        SIG_ALGOS+=("$alg")
        printf "  ${GREEN}✓${RESET} %s\n" "$alg"
    done < <(grep -rh 'define OQS_SIG_alg_' "$HEADER_DIR"/ 2>/dev/null \
        | sed 's/.*define OQS_SIG_alg_//; s/ .*//' \
        | sort -u)
fi

if [ "${#SIG_ALGOS[@]}" -eq 0 ]; then
    # Fallback: search in the source tree
    while IFS= read -r alg; do
        SIG_ALGOS+=("$alg")
        printf "  ${GREEN}✓${RESET} %s\n" "$alg"
    done < <(grep -rh 'define OQS_SIG_alg_' "$LIBOQS_SRC/src/" 2>/dev/null \
        | sed 's/.*define OQS_SIG_alg_//; s/ .*//' \
        | sort -u)
fi

SIG_COUNT="${#SIG_ALGOS[@]}"
ok "Total signature algorithms available: $SIG_COUNT"

# ── Build configuration document ───────────────────────────────────────────
info "Writing build configuration to docs/BUILD_CONFIG.md …"

mkdir -p "$PROJECT_ROOT/docs"

CC_VERSION="$($CC_FOUND --version 2>&1 | head -1 || echo 'unknown')"

cat > "$PROJECT_ROOT/docs/BUILD_CONFIG.md" <<CONFIG
# liboqs Build Configuration

> Auto-generated by \`install_liboqs.sh\` — ${TIMESTAMP}

## Source

| Property | Value |
|----------|-------|
| Repository | ${LIBOQS_REPO} |
| Version | ${LIBOQS_VERSION} |
| Source path | \`${LIBOQS_SRC}\` |

## Build Environment

| Property | Value |
|----------|-------|
| OS | ${OS_PRETTY} |
| Architecture | ${ARCH} |
| C Compiler | ${CC_VERSION} |
| Build system | CMake + ${CMAKE_GENERATOR} |
| Parallel jobs | ${NPROC} |

## Compiler Flags

\`\`\`
CMAKE_BUILD_TYPE = Release
CMAKE_C_FLAGS    = ${CFLAGS_OPT}
\`\`\`

## Install Prefix

\`\`\`
${LIBOQS_PREFIX}
├── include/oqs/   (${HEADER_COUNT:-?} headers)
├── lib/
$(for lib in "${LIBS_FOUND[@]:-}"; do echo "│   ├── $lib"; done)
└── lib/cmake/     (CMake package config)
\`\`\`

## Enabled Signature Algorithms (${SIG_COUNT})

$(for alg in "${SIG_ALGOS[@]:-}"; do echo "- ${alg}"; done)

## Reproduce This Build

\`\`\`bash
cd $(printf '%q' "$PROJECT_ROOT")
./install_liboqs.sh
\`\`\`

## CMake Options Used

\`\`\`cmake
CMAKE_BUILD_TYPE          = Release
CMAKE_INSTALL_PREFIX      = ${LIBOQS_PREFIX}
CMAKE_C_FLAGS             = ${CFLAGS_OPT}
BUILD_SHARED_LIBS         = ON (shared pass) / OFF (static pass)
OQS_BUILD_ONLY_LIB        = OFF
OQS_USE_OPENSSL           = ON
OQS_ENABLE_SIG_FALCON_512 = ON
OQS_ENABLE_SIG_FALCON_1024 = ON
OQS_ENABLE_SIG_DILITHIUM_2 = ON
OQS_ENABLE_SIG_DILITHIUM_3 = ON
OQS_ENABLE_SIG_DILITHIUM_5 = ON
OQS_ENABLE_SIG_SPHINCS_SHA2_128F_SIMPLE = ON
OQS_ENABLE_SIG_SPHINCS_SHA2_128S_SIMPLE = ON
OQS_ENABLE_SIG_SPHINCS_SHA2_192F_SIMPLE = ON
OQS_ENABLE_SIG_SPHINCS_SHA2_256F_SIMPLE = ON
\`\`\`
CONFIG
ok "Wrote docs/BUILD_CONFIG.md"

# ── Done ────────────────────────────────────────────────────────────────────
printf "\n${BOLD}${GREEN}liboqs installation complete.${RESET}\n\n"
printf "  Install prefix : ${BOLD}%s${RESET}\n" "$LIBOQS_PREFIX"
printf "  Libraries      : %s\n" "${LIBS_FOUND[*]:-none}"
printf "  Sig algorithms : %s\n" "$SIG_COUNT"
printf "  Build config   : docs/BUILD_CONFIG.md\n\n"
printf "To use in your benchmarks, compile with:\n\n"
printf "  ${BOLD}cc -I%s/include -L%s/lib -loqs -O3 your_benchmark.c -o benchmark${RESET}\n\n" \
    "$LIBOQS_PREFIX" "$LIBOQS_PREFIX"
