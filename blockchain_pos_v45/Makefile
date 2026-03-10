# Blockchain PoS v29 Makefile
# BLAKE3 hashing + Protocol Buffers + BLS signatures + OpenMP Wallet

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -fopenmp

# Directories
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include
PROTO_DIR = proto

# Source files
COMMON_SRCS = $(SRC_DIR)/common.c \
              $(SRC_DIR)/blake3.c \
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
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║        BUILD COMPLETE (v26 - Optimized + BLS)                ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  NEW Features in v26:                                        ║"
	@echo "║    ✓ Multi-threaded wallet (batch_send command)             ║"
	@echo "║    ✓ Batch TX submission (SUBMIT_BATCH protocol)            ║"
	@echo "║    ✓ BLS signatures (48 bytes, like Ethereum 2.0)           ║"
	@echo "║    ✓ 10-100x faster TX submission throughput                ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Core Features:                                              ║"
	@echo "║    ✓ BLAKE3 hashing for plots                               ║"
	@echo "║    ✓ Protocol Buffers for serialization                     ║"
	@echo "║    ✓ Validators create blocks (correct PoS architecture)    ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Executables:                                                ║"
	@echo "║    build/blockchain  - Blockchain server                     ║"
	@echo "║    build/metronome   - Challenge broadcaster                 ║"
	@echo "║    build/pool        - Transaction pool (with SUBMIT_BATCH) ║"
	@echo "║    build/validator   - Farmer/Validator (creates blocks!)   ║"
	@echo "║    build/wallet      - Wallet CLI (with batch_send)         ║"
	@echo "║    build/benchmark   - Internal benchmark tool               ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Scripts:                                                    ║"
	@echo "║    ./start_blockchain.sh  - Start blockchain system          ║"
	@echo "║    ./benchmark.sh         - Run full benchmark suite         ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile protobuf
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
	@echo "Running basic tests..."
	$(BUILD_DIR)/wallet create test_wallet
	$(BUILD_DIR)/wallet info test_wallet.dat
	$(BUILD_DIR)/wallet address farmer1
	rm -f test_wallet.dat
	@echo "Tests passed!"

help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║              Blockchain PoS v10 Build System                 ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Available targets:                                          ║"
	@echo "║    all    - Build all executables (default)                  ║"
	@echo "║    proto  - Regenerate protobuf C code                       ║"
	@echo "║    clean  - Remove build directory and generated files       ║"
	@echo "║    test   - Run basic tests                                  ║"
	@echo "║    help   - Show this help                                   ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
