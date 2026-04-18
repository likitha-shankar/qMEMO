# qMEMO Delta from Harsha Baseline

This document explains what changed from the baseline code to the current qMEMO research implementation.

## Baseline vs Current

- Baseline reference in this repo: `blockchain_base/`
- Current research implementation: `blockchain/`

`blockchain_base/` is treated as Harsha's baseline line (v27-era PoS codepath), while `blockchain/` is the actively evolved fork used for PQC experiments and production-scale runs.

## High-Level Change Summary

1. Added multi-scheme cryptography (Ed25519, Falcon-512, ML-DSA-44, hybrid dispatch).
2. Redesigned transaction and wallet data paths for variable-length signatures/keys.
3. Upgraded wire format and protobuf handling to carry `public_key`, `sig_len`, and `sig_type` safely.
4. Improved wallet CLI/benchmark path for high-throughput batch submission and reproducible cloud runs.
5. Hardened build system and dependency checks for macOS/Linux portability.
6. Fixed correctness and runtime issues discovered during repeated 100K/1M-scale benchmarking.

## Detailed Changes

### 1) Cryptography and Signature Scheme Support

**What baseline had**
- Ed25519-centric transaction/wallet assumptions.
- Fixed-size signature/public-key handling in the base path.

**What current code adds**
- Unified crypto abstraction in `blockchain/src/crypto_backend.c` with scheme-aware sign/verify:
  - `SIG_SCHEME=1` Ed25519
  - `SIG_SCHEME=2` Falcon-512
  - `SIG_SCHEME=3` Hybrid
  - `SIG_SCHEME=4` ML-DSA-44
- Runtime typed verification (`crypto_verify_typed`) used by transaction verification.
- Thread-aware guidance and cleanup for liboqs-backed paths.

**Files**
- `blockchain/src/crypto_backend.c`
- `blockchain/include/wallet.h`
- `blockchain/include/transaction.h`

### 2) Transaction Structure and Verification Path

**What baseline had**
- Compact fixed transaction struct (128-byte model) with Ed25519 assumptions.
- Hash and verification path tuned to fixed-size signature expectations.

**What current code adds**
- Transaction struct carries:
  - `signature[CRYPTO_SIG_MAX]`, `sig_len`
  - `public_key[CRYPTO_PUBKEY_MAX]`, `pubkey_len`
  - `sig_type`
- Hash remains over core economic fields, while verification dispatches by `sig_type`.
- Protobuf serialization/deserialization updated for variable lengths and typed signatures.

**Files**
- `blockchain/include/transaction.h`
- `blockchain/src/transaction.c`
- `blockchain/proto/blockchain.proto`
- `blockchain/proto/blockchain.pb-c.h`
- `blockchain/proto/blockchain.pb-c.c`

### 3) Wallet Data Model and CLI Behavior

**What baseline had**
- Ed25519-only wallet flow with legacy fixed assumptions.

**What current code adds**
- Wallet model supports per-wallet active scheme and key material for PQC modes.
- Deterministic wallet creation preserved, with scheme-aware signing behavior.
- Verification helper updated to infer scheme for legacy call-sites (canonical path uses transaction `sig_type`).
- Batch wallet command path strengthened for throughput runs and nonce coordination with chain/pool state.

**Files**
- `blockchain/include/wallet.h`
- `blockchain/src/wallet.c`
- `blockchain/src/main_wallet.c`

### 4) Validator and Pool Path Adjustments

**What baseline had**
- v27 bug-fix architecture and transaction-pool semantics.

**What current code adds**
- Validator/pool paths aligned to typed transaction verification and protobuf field changes.
- Runtime behavior tuned for repeated high-scale benchmark stability.

**Files**
- `blockchain/src/validator.c`
- `blockchain/include/validator.h`
- `blockchain/src/transaction_pool.c`
- `blockchain/include/transaction_pool.h`
- `blockchain/src/main_validator.c`
- `blockchain/src/main_pool.c`

### 5) Build and Toolchain Improvements

**What baseline had**
- Build assumptions that were less portable across environments.

**What current code adds**
- Improved dependency detection and preflight checks.
- Better handling for platform/toolchain differences used during cloud and local reproductions.

**Files**
- `blockchain/Makefile`

## File-Level Delta Snapshot

Main code files that differ between `blockchain/` and `blockchain_base/`:

- `Makefile`
- `include/blockchain.h`
- `include/transaction.h`
- `include/transaction_pool.h`
- `include/validator.h`
- `include/wallet.h`
- `proto/blockchain.proto`
- `proto/blockchain.pb-c.h`
- `proto/blockchain.pb-c.c`
- `src/main_benchmark.c`
- `src/main_pool.c`
- `src/main_validator.c`
- `src/main_wallet.c`
- `src/transaction.c`
- `src/transaction_pool.c`
- `src/validator.c`
- `src/wallet.c`

## Why These Changes Were Needed

- Baseline assumptions were not sufficient for PQC-sized signatures/keys and mixed-scheme runtime validation.
- Large-scale runs (100K/1M transactions) required stronger nonce handling, typed verification, and robust batch/protobuf handling.
- Cloud reproducibility required better build portability and clearer dependency gating.

## Practical Interpretation

Use this mental model:

- `blockchain_base/` = historical baseline/control implementation (Harsha baseline line).
- `blockchain/` = research/experimental branch with PQC integration, hybrid support, and benchmark-driven fixes.

For papers/posters/defense, cite `blockchain/` as the implementation under evaluation and `blockchain_base/` as the baseline reference.
