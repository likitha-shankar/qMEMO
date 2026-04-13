# blockchain_v2_pqc Makefile
# PQC-aware build: Ed25519 (default), Falcon-512, ML-DSA-44, Hybrid
#
# Usage:
#   make                   # Ed25519 (scheme 1)
#   make SIG_SCHEME=2      # Falcon-512
#   make SIG_SCHEME=3      # Hybrid
#   make SIG_SCHEME=4      # ML-DSA-44
#   make OQS_ROOT=/path    # Custom liboqs install path

SIG_SCHEME ?= 1
OQS_ROOT   ?= $(HOME)/liboqs_install

CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp \
         -DSIG_SCHEME=$(SIG_SCHEME)
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -fopenmp

# Link liboqs for Falcon, ML-DSA, or Hybrid
ifeq ($(filter $(SIG_SCHEME),2 3 4),$(SIG_SCHEME))
CFLAGS  += -I$(OQS_ROOT)/include
LDFLAGS += -L$(OQS_ROOT)/lib -loqs
endif

# Directories
SRC_DIR   = src
BUILD_DIR = build
PROTO_DIR = proto

# Source files shared across all executables
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

# Object files
COMMON_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(COMMON_SRCS))) \
              $(BUILD_DIR)/blockchain.pb-c.o

# Executables
EXECUTABLES = $(BUILD_DIR)/blockchain \
              $(BUILD_DIR)/metronome \
              $(BUILD_DIR)/pool \
              $(BUILD_DIR)/validator \
              $(BUILD_DIR)/wallet \
              $(BUILD_DIR)/benchmark

.PHONY: all clean proto test

all: $(BUILD_DIR) $(EXECUTABLES)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════════╗"
	@echo "║        blockchain_v2_pqc — BUILD COMPLETE                        ║"
	@echo "╠══════════════════════════════════════════════════════════════════╣"
	@echo "║  SIG_SCHEME=$(SIG_SCHEME)  (1=Ed25519 2=Falcon 3=Hybrid 4=ML-DSA-44)    ║"
	@echo "╠══════════════════════════════════════════════════════════════════╣"
	@echo "║  Executables:                                                    ║"
	@echo "║    build/blockchain  - Blockchain server                         ║"
	@echo "║    build/metronome   - Challenge broadcaster                     ║"
	@echo "║    build/pool        - Transaction pool                          ║"
	@echo "║    build/validator   - Farmer/Validator (PQC-aware)              ║"
	@echo "║    build/wallet      - Wallet CLI (batch_send + OpenMP)          ║"
	@echo "║    build/benchmark   - Benchmark tool                            ║"
	@echo "╚══════════════════════════════════════════════════════════════════╝"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Regenerate protobuf C code (run once when blockchain.proto changes)
proto:
	cd $(PROTO_DIR) && protoc-c --c_out=. blockchain.proto

# Common object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Protobuf object file
$(BUILD_DIR)/blockchain.pb-c.o: $(PROTO_DIR)/blockchain.pb-c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Executables
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

clean:
	rm -rf $(BUILD_DIR)
	rm -f *.dat *.log

test: all
	@echo "Running basic tests (SIG_SCHEME=$(SIG_SCHEME))..."
	$(BUILD_DIR)/wallet create test_wallet
	$(BUILD_DIR)/wallet info test_wallet.dat
	$(BUILD_DIR)/wallet address farmer1
	rm -f test_wallet.dat
	@echo "Tests passed!"

help:
	@echo ""
	@echo "Usage: make [SIG_SCHEME=N] [OQS_ROOT=path]"
	@echo "  SIG_SCHEME=1  Ed25519 (default, no liboqs needed)"
	@echo "  SIG_SCHEME=2  Falcon-512 (requires liboqs)"
	@echo "  SIG_SCHEME=3  Hybrid Ed25519+Falcon (requires liboqs)"
	@echo "  SIG_SCHEME=4  ML-DSA-44 / Dilithium (requires liboqs)"
	@echo ""
	@echo "Targets: all proto clean test help"
	@echo ""
