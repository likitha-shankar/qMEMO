# Blockchain PoS v45 Makefile
# BLAKE3 hashing + Protocol Buffers + Dual crypto backend (ECDSA / Falcon-512)
#
# Usage:
#   make                    # ECDSA build (default)
#   make SIG_SCHEME=2       # Falcon-512 build (requires liboqs)
#   make test_crypto        # Build round-trip crypto test

CC = gcc

# ── Signature scheme selector ──────────────────────────────────────────
# 1 = ECDSA secp256k1 (OpenSSL)   [default]
# 2 = Falcon-512 (liboqs)
# 3 = Hybrid (ECDSA + Falcon-512)
# 4 = ML-DSA-44 (liboqs)
SIG_SCHEME ?= 1

# ── Platform-specific OpenSSL root ─────────────────────────────────────
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    OPENSSL_ROOT ?= /opt/homebrew/opt/openssl
  else
    OPENSSL_ROOT ?= /usr/local/opt/openssl
  endif
else
  OPENSSL_ROOT ?= /usr
endif

# ── liboqs root (only needed for SIG_SCHEME=2) ────────────────────────
OQS_ROOT ?= ../liboqs_install

# ── Platform-specific dependency roots ────────────────────────────────
ifeq ($(UNAME_S),Darwin)
  PROTOBUF_C_ROOT := $(shell brew --prefix protobuf-c 2>/dev/null)
  ZMQ_ROOT        := $(shell brew --prefix zeromq 2>/dev/null)
  LIBOMP_ROOT     := $(shell brew --prefix libomp 2>/dev/null)
else
  PROTOBUF_C_ROOT :=
  ZMQ_ROOT        :=
  LIBOMP_ROOT     :=
endif

# ── Compiler & linker flags ────────────────────────────────────────────
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto \
         -I$(OPENSSL_ROOT)/include \
         -DSIG_SCHEME=$(SIG_SCHEME)
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -lpthread \
          -L$(OPENSSL_ROOT)/lib

ifneq ($(PROTOBUF_C_ROOT),)
  CFLAGS  += -I$(PROTOBUF_C_ROOT)/include
  LDFLAGS += -L$(PROTOBUF_C_ROOT)/lib
endif
ifneq ($(ZMQ_ROOT),)
  CFLAGS  += -I$(ZMQ_ROOT)/include
  LDFLAGS += -L$(ZMQ_ROOT)/lib
endif
ifneq ($(LIBOMP_ROOT),)
  CFLAGS  += -Xpreprocessor -fopenmp -I$(LIBOMP_ROOT)/include
  LDFLAGS += -L$(LIBOMP_ROOT)/lib -lomp
else ifneq ($(UNAME_S),Darwin)
  # GCC on Linux has OpenMP built-in
  CFLAGS  += -fopenmp
  LDFLAGS += -lgomp
endif

ifneq ($(filter 2 3 4,$(SIG_SCHEME)),)
  CFLAGS  += -I$(OQS_ROOT)/include
  LDFLAGS += -L$(OQS_ROOT)/lib -loqs
endif

# ── Directories ────────────────────────────────────────────────────────
SRC_DIR   = src
BUILD_DIR = build
PROTO_DIR = proto

# ── Source files ───────────────────────────────────────────────────────
COMMON_SRCS = $(SRC_DIR)/common.c \
              $(SRC_DIR)/blake3.c \
              $(SRC_DIR)/crypto_backend.c \
              $(SRC_DIR)/transaction.c \
              $(SRC_DIR)/wallet.c \
              $(SRC_DIR)/block.c \
              $(SRC_DIR)/blockchain.c \
              $(SRC_DIR)/consensus.c \
              $(SRC_DIR)/transaction_pool.c \
              $(SRC_DIR)/metronome.c \
              $(SRC_DIR)/validator.c \
              $(PROTO_DIR)/blockchain.pb-c.c

# ── Object files ───────────────────────────────────────────────────────
COMMON_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(COMMON_SRCS))) \
              $(BUILD_DIR)/blockchain.pb-c.o

# ── Executables ────────────────────────────────────────────────────────
EXECUTABLES = $(BUILD_DIR)/blockchain \
              $(BUILD_DIR)/metronome \
              $(BUILD_DIR)/pool \
              $(BUILD_DIR)/validator \
              $(BUILD_DIR)/wallet \
              $(BUILD_DIR)/benchmark

.PHONY: all clean proto test test_crypto help

all: $(BUILD_DIR) $(EXECUTABLES)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  BUILD COMPLETE — SIG_SCHEME=$(SIG_SCHEME)                          ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
ifeq ($(SIG_SCHEME),4)
	@echo "║  Crypto backend: ML-DSA-44 (liboqs)                        ║"
else ifeq ($(SIG_SCHEME),3)
	@echo "║  Crypto backend: Hybrid (ECDSA+Falcon-512+ML-DSA-44)       ║"
else ifeq ($(SIG_SCHEME),2)
	@echo "║  Crypto backend: Falcon-512 (liboqs)                       ║"
else
	@echo "║  Crypto backend: ECDSA secp256k1 (OpenSSL)                 ║"
endif
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Executables:                                                ║"
	@echo "║    build/blockchain  - Blockchain server                     ║"
	@echo "║    build/metronome   - Challenge broadcaster                 ║"
	@echo "║    build/pool        - Transaction pool                      ║"
	@echo "║    build/validator   - Farmer/Validator                      ║"
	@echo "║    build/wallet      - Wallet CLI                            ║"
	@echo "║    build/benchmark   - Internal benchmark tool               ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Protobuf code generation ──────────────────────────────────────────
proto:
	cd $(PROTO_DIR) && protoc-c --c_out=. blockchain.proto

# ── Object compilation ────────────────────────────────────────────────
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/blockchain.pb-c.o: $(PROTO_DIR)/blockchain.pb-c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Main executables ──────────────────────────────────────────────────
$(BUILD_DIR)/blockchain: $(SRC_DIR)/main_blockchain.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/metronome: $(SRC_DIR)/main_metronome.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/pool: $(SRC_DIR)/main_pool.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/validator: $(SRC_DIR)/main_validator.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/wallet: $(SRC_DIR)/main_wallet.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/benchmark: $(SRC_DIR)/main_benchmark.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Crypto round-trip test ────────────────────────────────────────────
$(BUILD_DIR)/test_crypto: test_crypto.c $(SRC_DIR)/crypto_backend.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ -L$(OPENSSL_ROOT)/lib $(if $(filter 2 3 4,$(SIG_SCHEME)),-L$(OQS_ROOT)/lib -loqs) -lssl -lcrypto

test_crypto: $(BUILD_DIR)/test_crypto
	@echo "Running crypto round-trip test (SIG_SCHEME=$(SIG_SCHEME))..."
	$(BUILD_DIR)/test_crypto

# ── Cleanup ────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.dat *.log

test: all
	@echo "Running basic tests..."
	$(BUILD_DIR)/wallet create test_wallet
	$(BUILD_DIR)/wallet info test_wallet.dat
	$(BUILD_DIR)/wallet address farmer1
	rm -f test_wallet.dat
	@echo "Tests passed!"

help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║           Blockchain PoS v45 Build System                    ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Targets:                                                    ║"
	@echo "║    all          - Build all executables (default)            ║"
	@echo "║    test_crypto  - Build & run crypto round-trip test         ║"
	@echo "║    proto        - Regenerate protobuf C code                 ║"
	@echo "║    clean        - Remove build directory                     ║"
	@echo "║    test         - Run basic wallet tests                     ║"
	@echo "║    help         - Show this help                             ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Variables:                                                  ║"
	@echo "║    SIG_SCHEME=1   ECDSA secp256k1 (default)                 ║"
	@echo "║    SIG_SCHEME=2   Falcon-512 (requires liboqs)              ║"
	@echo "║    SIG_SCHEME=3   Hybrid ECDSA+Falcon+ML-DSA (requires liboqs)║"
	@echo "║    SIG_SCHEME=4   ML-DSA-44 (requires liboqs)              ║"
	@echo "║    OQS_ROOT=path  liboqs install prefix                     ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
