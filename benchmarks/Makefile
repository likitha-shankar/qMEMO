# ──────────────────────────────────────────────────────────────────────────────
# Makefile — qMEMO benchmark suite (Falcon-512 / ML-DSA-44)
#
# Builds all C benchmark programs against a local liboqs installation.
# Works on both macOS (Apple Clang) and Linux (GCC/Clang).
#
# Usage:
#   make          # build everything
#   make verify   # build only the verification benchmark
#   make run      # build and run all benchmarks
#   make clean    # remove compiled binaries
#   make help     # list available targets
# ──────────────────────────────────────────────────────────────────────────────

# ── Paths (all relative to this Makefile's directory: benchmarks/) ─────────
SRCDIR       := src
BINDIR       := bin
OQS_ROOT     := ../liboqs_install
OPENSSL_ROOT := /opt/homebrew/opt/openssl

# ── Toolchain ──────────────────────────────────────────────────────────────
# Use whatever `cc` resolves to (Apple Clang on macOS, GCC on most Linux).
# Override on the command line if needed:  make CC=gcc-13
CC := cc

# ── Compiler flags ─────────────────────────────────────────────────────────
#
# -O3          Highest standard optimisation level. Enables auto-vectorisation,
#              loop unrolling, and aggressive inlining — critical for getting
#              realistic throughput numbers (the library itself is built -O3).
#
# -ffast-math  Allows the compiler to reorder / reassociate floating-point
#              operations.  Safe here because our only FP work is timing
#              arithmetic (subtraction of two clock reads), not numerically
#              sensitive math.
#
# Architecture tuning:
#   Apple Clang on arm64 does not support -march=native.  The equivalent is
#   -mcpu=native, which tunes for the host Apple Silicon generation.  We
#   detect this at make-time so the Makefile works on both platforms without
#   manual edits.
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S)-$(UNAME_M),Darwin-arm64)
    ARCH_FLAG := -mcpu=native
else
    ARCH_FLAG := -march=native
endif

CFLAGS := -O3 $(ARCH_FLAG) -ffast-math -Wall -Wextra
CFLAGS += -I$(OQS_ROOT)/include
CFLAGS += -I$(OPENSSL_ROOT)/include

# ── Linker flags ───────────────────────────────────────────────────────────
#
# -L   tells the linker where to find liboqs.a / liboqs.dylib
# -loqs  links against the Open Quantum Safe library
# -lm   links libm (sqrt, fabs, pow) — needed by statistical_benchmark
#
# On macOS we also embed an rpath so the binary can find the shared library
# at runtime without setting DYLD_LIBRARY_PATH manually.
LDFLAGS := -L$(OQS_ROOT)/lib -L$(OPENSSL_ROOT)/lib
LDLIBS  := -loqs

ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -Wl,-rpath,$(abspath $(OQS_ROOT)/lib)
    LDFLAGS += -Wl,-rpath,$(abspath $(OPENSSL_ROOT)/lib)
else
    LDFLAGS += -Wl,-rpath,'$$ORIGIN/../../liboqs_install/lib'
endif

# ── Source → binary mapping ────────────────────────────────────────────────
VERIFY_SRC          := $(SRCDIR)/verify_benchmark.c
STATS_SRC           := $(SRCDIR)/statistical_benchmark.c
COMPARE_SRC         := $(SRCDIR)/comparison_benchmark.c
MULTICORE_SRC       := $(SRCDIR)/multicore_benchmark.c
CONCURRENT_SRC      := $(SRCDIR)/concurrent_benchmark.c
CONCURRENT_SIGN_SRC := $(SRCDIR)/concurrent_signing_benchmark.c
SIGN_SRC            := $(SRCDIR)/sign_benchmark.c
SIGSIZE_SRC         := $(SRCDIR)/signature_size_analysis.c
CLASSICAL_SRC       := $(SRCDIR)/classical_benchmark.c
COMPREHENSIVE_SRC   := $(SRCDIR)/comprehensive_comparison.c
KEY_INSPECT_SRC     := $(SRCDIR)/key_inspection.c

VERIFY_BIN          := $(BINDIR)/verify_benchmark
STATS_BIN           := $(BINDIR)/statistical_benchmark
COMPARE_BIN         := $(BINDIR)/comparison_benchmark
MULTICORE_BIN       := $(BINDIR)/multicore_benchmark
CONCURRENT_BIN      := $(BINDIR)/concurrent_benchmark
CONCURRENT_SIGN_BIN := $(BINDIR)/concurrent_signing_benchmark
SIGN_BIN            := $(BINDIR)/sign_benchmark
SIGSIZE_BIN         := $(BINDIR)/signature_size_analysis
CLASSICAL_BIN       := $(BINDIR)/classical_benchmark
COMPREHENSIVE_BIN   := $(BINDIR)/comprehensive_comparison
KEY_INSPECT_BIN     := $(BINDIR)/key_inspection

ALL_BINS := $(VERIFY_BIN) $(STATS_BIN) $(COMPARE_BIN) $(MULTICORE_BIN) \
            $(CONCURRENT_BIN) $(CONCURRENT_SIGN_BIN) \
            $(SIGN_BIN) $(SIGSIZE_BIN) $(CLASSICAL_BIN) $(COMPREHENSIVE_BIN) \
            $(KEY_INSPECT_BIN)

# ══════════════════════════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════════════════════════

.PHONY: all verify stats compare multicore concurrent concurrent_sign \
        sign sigsize classical comprehensive key_inspection clean run help

all: $(ALL_BINS) ## Build all benchmarks
	@echo ""
	@echo "All benchmarks built in $(BINDIR)/."

# ── Individual builds ──────────────────────────────────────────────────────

verify: $(VERIFY_BIN) ## Build the single-pass verification benchmark

stats: $(STATS_BIN) ## Build the statistical verification benchmark

compare: $(COMPARE_BIN) ## Build the Falcon-512 vs ML-DSA-44 comparison

multicore: $(MULTICORE_BIN) ## Build the multicore verification benchmark

concurrent: $(CONCURRENT_BIN) ## Build the concurrent (thread-pool) verification benchmark

concurrent_sign: $(CONCURRENT_SIGN_BIN) ## Build the concurrent signing benchmark

sign: $(SIGN_BIN) ## Build the multicore signing throughput benchmark

sigsize: $(SIGSIZE_BIN) ## Build the Falcon signature size distribution benchmark

classical: $(CLASSICAL_BIN) ## Build the ECDSA/Ed25519 classical baseline benchmark

comprehensive: $(COMPREHENSIVE_BIN) ## Build the 7-algorithm comprehensive comparison

key_inspection: $(KEY_INSPECT_BIN) ## Build the key material inspection and correctness audit

# ── Compilation rules ──────────────────────────────────────────────────────

$(BINDIR):
	@mkdir -p $(BINDIR)

$(VERIFY_BIN): $(VERIFY_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@
	@echo "      done."

# statistical_benchmark needs -lm for sqrt / fabs / pow
$(STATS_BIN): $(STATS_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lm -o $@
	@echo "      done."

$(COMPARE_BIN): $(COMPARE_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@
	@echo "      done."

$(MULTICORE_BIN): $(MULTICORE_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lpthread -o $@
	@echo "      done."

$(CONCURRENT_BIN): $(CONCURRENT_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lpthread -o $@
	@echo "      done."

$(CONCURRENT_SIGN_BIN): $(CONCURRENT_SIGN_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lpthread -lm -o $@
	@echo "      done."

# sign_benchmark: per-thread OQS_SIG contexts, needs -lpthread
$(SIGN_BIN): $(SIGN_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lpthread -o $@
	@echo "      done."

# signature_size_analysis: sqrt/fabs, needs -lm
$(SIGSIZE_BIN): $(SIGSIZE_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lm -o $@
	@echo "      done."

# classical_benchmark: OpenSSL EVP API
$(CLASSICAL_BIN): $(CLASSICAL_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lcrypto -o $@
	@echo "      done."

# comprehensive_comparison: liboqs + OpenSSL + math
$(COMPREHENSIVE_BIN): $(COMPREHENSIVE_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lcrypto -lm -o $@
	@echo "      done."

# key_inspection: liboqs + OpenSSL (needs x509.h for i2d_PUBKEY)
$(KEY_INSPECT_BIN): $(KEY_INSPECT_SRC) | $(BINDIR)
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -lcrypto -o $@
	@echo "      done."

# ── Run ────────────────────────────────────────────────────────────────────

run: all ## Build and run all benchmarks sequentially
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: verify_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(VERIFY_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: statistical_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(STATS_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: comparison_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(COMPARE_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: multicore_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(MULTICORE_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: concurrent_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(CONCURRENT_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: concurrent_signing_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(CONCURRENT_SIGN_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: sign_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(SIGN_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: signature_size_analysis"
	@echo "════════════════════════════════════════════════════════════"
	$(SIGSIZE_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: classical_benchmark"
	@echo "════════════════════════════════════════════════════════════"
	$(CLASSICAL_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: comprehensive_comparison"
	@echo "════════════════════════════════════════════════════════════"
	$(COMPREHENSIVE_BIN)
	@echo ""
	@echo "════════════════════════════════════════════════════════════"
	@echo "  Running: key_inspection"
	@echo "════════════════════════════════════════════════════════════"
	$(KEY_INSPECT_BIN)
	@echo ""
	@echo "All benchmarks finished."

# ── Clean ──────────────────────────────────────────────────────────────────

clean: ## Remove all compiled binaries
	@echo "Cleaning $(BINDIR)/ …"
	rm -f $(ALL_BINS)
	@echo "Done."

# ── Help ───────────────────────────────────────────────────────────────────

help: ## Print this help message
	@echo ""
	@echo "qMEMO Benchmark Makefile"
	@echo "────────────────────────"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) \
		| awk -F ':.*## ' '{ printf "  %-12s %s\n", $$1, $$2 }'
	@echo ""
	@echo "Variables (override on command line):"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  OQS_ROOT=$(OQS_ROOT)"
	@echo ""
	@echo "Examples:"
	@echo "  make                  # build all"
	@echo "  make run              # build and run all"
	@echo "  make verify           # build just verify_benchmark"
	@echo "  make CC=gcc-13 all         # use a specific compiler"
	@echo "  make clean all             # rebuild from scratch"
	@echo "  make concurrent_sign       # build just the signing benchmark"
	@echo ""
