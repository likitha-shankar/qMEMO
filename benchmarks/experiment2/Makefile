# Makefile — Experiment 2: Signature Verification Micro-Benchmark

OQS_ROOT     ?= ../../liboqs_install
OQS_ROOT_AVX2 ?= ../../liboqs_install_avx2

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    OPENSSL_ROOT ?= /opt/homebrew/opt/openssl
else
    OPENSSL_ROOT ?= /usr
endif

CC      := cc
BINDIR  := ../bin
SRCDIR  := src

CFLAGS  := -O3 -march=native -ffast-math -Wall -Wextra -pthread
CFLAGS  += -I$(OQS_ROOT)/include
CFLAGS  += -I$(OPENSSL_ROOT)/include
CFLAGS  += -Wno-unused-result

LDFLAGS := -L$(OQS_ROOT)/lib -L$(OPENSSL_ROOT)/lib
LDLIBS  := -loqs -lcrypto -lpthread -lm

ifeq ($(UNAME_S),Linux)
    LDFLAGS += -Wl,-rpath,$(abspath $(OQS_ROOT)/lib)
endif

BIN  := $(BINDIR)/bench_verify
SRC  := $(SRCDIR)/bench_verify.c

.PHONY: all clean help

all: $(BINDIR) $(BIN)

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BIN): $(SRC) | $(BINDIR)
	@echo "[CC] bench_verify"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(BIN)

help:
	@echo "Targets: all (default), clean"
	@echo "Vars:    OQS_ROOT=$(OQS_ROOT)  OPENSSL_ROOT=$(OPENSSL_ROOT)"
