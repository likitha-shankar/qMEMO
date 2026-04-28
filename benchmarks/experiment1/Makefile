# Makefile — Experiment 1: Signature Creation Micro-Benchmark
#
# Usage:
#   make                        # build bench_sign
#   make OQS_ROOT=/path/to/oqs  # override liboqs location
#   make avx2                   # build AVX2-only variant
#   make clean
#
# The AVX-512 variant is the default build.
# The AVX2 variant is bench_sign_avx2 — used for the AVX-512 vs AVX2 control.

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

LDFLAGS := -L$(OQS_ROOT)/lib -L$(OPENSSL_ROOT)/lib
LDLIBS  := -loqs -lcrypto -lpthread -lm

ifeq ($(UNAME_S),Linux)
    LDFLAGS += -Wl,-rpath,$(abspath $(OQS_ROOT)/lib)
endif

BIN     := $(BINDIR)/bench_sign
BIN_AVX2 := $(BINDIR)/bench_sign_avx2
SRC     := $(SRCDIR)/bench_sign.c

.PHONY: all avx2 clean help

all: $(BINDIR) $(BIN)

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BIN): $(SRC) | $(BINDIR)
	@echo "[CC] bench_sign (AVX-512 enabled)"
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

avx2: $(BINDIR)
	@echo "[CC] bench_sign_avx2 (AVX-512 disabled)"
	$(CC) $(CFLAGS) \
	      -I$(OQS_ROOT_AVX2)/include \
	      -L$(OQS_ROOT_AVX2)/lib \
	      $(LDFLAGS) \
	      $(SRC) $(LDLIBS) -o $(BIN_AVX2)

clean:
	rm -f $(BIN) $(BIN_AVX2)

help:
	@echo "Targets: all (default), avx2, clean"
	@echo "Vars:    OQS_ROOT=$(OQS_ROOT)"
	@echo "         OQS_ROOT_AVX2=$(OQS_ROOT_AVX2)"
	@echo "         OPENSSL_ROOT=$(OPENSSL_ROOT)"
