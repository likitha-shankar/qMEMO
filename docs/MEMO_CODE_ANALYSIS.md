# MEMO Code Analysis: blockchain_pos_v45

**Date**: 2026-03-10
**Analyst**: qMEMO research team
**Purpose**: Pre-integration audit — understand Harsha's blockchain codebase before
swapping classical ECDSA signatures for quantum-resistant PQC alternatives.

Original source preserved verbatim on branch `harsha-original` at `blockchain_pos_v45/`.

---

## 1. Project Overview

| Property | Value |
|----------|-------|
| Language | C (C11, gcc) |
| Build system | GNU Make (single Makefile) |
| LOC | ~9,788 across 16 source files |
| Auxiliary | Bash scripts, Python (graph gen), Protocol Buffers v3 |
| Total files | 36 (16 `.c`, 10 `.h`, 3 proto, 4 scripts, Makefile, README, CHANGELOG) |
| Executables | 6 (`blockchain`, `metronome`, `pool`, `validator`, `wallet`, `benchmark`) |

### Blockchain concept
Proof-of-Space consensus chain inspired by Chia. Farmers generate BLAKE3 plot files,
respond to challenges with the best proof hash, and earn block rewards. Transactions
use an Ethereum-style nonce model with a fixed-size 112-byte struct.

---

## 2. Directory Structure

```
blockchain_pos_v45/
├── src/                    (16 .c files — all core logic)
│   ├── blake3.c            vendor BLAKE3 implementation
│   ├── block.c             block structure & serialization
│   ├── blockchain.c        ledger, UTXO/balance tracking
│   ├── common.c            hashing utilities (sha256, ripemd160, hash160)
│   ├── consensus.c         Proof-of-Space challenge / proof evaluation
│   ├── main_benchmark.c    internal throughput benchmark entry point
│   ├── main_blockchain.c   blockchain server entry point
│   ├── main_metronome.c    metronome entry point
│   ├── main_pool.c         transaction pool entry point
│   ├── main_validator.c    validator/farmer entry point
│   ├── main_wallet.c       wallet CLI entry point
│   ├── metronome.c         challenge broadcaster
│   ├── transaction.c       sign / verify / hash / serialization
│   ├── transaction_pool.c  mempool management
│   ├── validator.c         proof search, block assembly
│   └── wallet.c            key generation, address derivation
├── include/                (10 .h files — public API headers)
├── proto/
│   ├── blockchain.proto    protobuf v3 schema
│   ├── blockchain.pb-c.c   generated C serializer (protobuf-c)
│   └── blockchain.pb-c.h   generated C header
├── Makefile
├── README.md
├── CHANGELOG_v27.md
├── start_blockchain.sh     starts all 4 networked daemons
├── benchmark.sh            full throughput benchmark suite
├── run_graph3_benchmark.sh scaling benchmark
└── generate_graphs.py      matplotlib graph generation
```

---

## 3. Current Signature Scheme

### Algorithm

ECDSA over secp256k1 (OpenSSL 3.6.1 EVP_PKEY API), marketed as "BLS-style" in
comments. The code produces real ECDSA DER signatures but **truncates or pads them
to 48 bytes** to simulate a BLS12-381 G1 point size. True BLS aggregation is not
implemented.

### Transaction struct (`include/transaction.h`, lines 30–43)

```c
#pragma pack(push, 1)
typedef struct {
    uint64_t nonce;              //  8 bytes
    uint32_t expiry_block;       //  4 bytes
    uint8_t source_address[20];  // 20 bytes: RIPEMD160(SHA256(pubkey))
    uint8_t dest_address[20];    // 20 bytes
    uint64_t value;              //  8 bytes
    uint32_t fee;                //  4 bytes
    uint8_t signature[48];       // 48 bytes: "BLS-style" fixed field
} Transaction;                   // TOTAL: 112 bytes — compile-time asserted
#pragma pack(pop)

_Static_assert(sizeof(Transaction) == 112, "Transaction must be exactly 112 bytes");
```

The 112-byte layout is a hard contract enforced by `_Static_assert`. Any PQC swap
that changes signature size **must** remove this assert and update downstream code.

### Key generation (`src/wallet.c`, lines 15–72)

```
wallet_create()
  └─ EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL)
  └─ EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp256k1)
  └─ EVP_PKEY_keygen() → wallet->evp_key
  └─ EVP_PKEY_get_octet_string_param("pub") → wallet->public_key[65]
  └─ hash160(pubkey, 65) → wallet->address[20]
  └─ PEM_write_bio_PrivateKey() → wallet->private_key_pem[]
```

Named wallets use `wallet_create_named()` — same keygen flow but address derived
from `hash160(name_string)` rather than the real pubkey (demo shortcut).

### Sign flow (`src/transaction.c`, lines 152–253)

```
transaction_sign(tx, wallet)
  1. transaction_compute_hash(tx) → tx_hash[28]
     Hash = BLAKE3(nonce||expiry_block||source||dest||value||fee)[0:28]
  2. If wallet has no private key PEM:
       deterministic fallback: BLAKE3(address||tx_hash) → 48 bytes
  3. EVP_DigestSignInit(SHA-256, secp256k1 key)
     EVP_DigestSignUpdate(tx_hash, 28)
     EVP_DigestSignFinal() → DER sig (typically 70–72 bytes)
  4. Truncate/pad DER sig to exactly 48 bytes → tx->signature
     (if sig_len >= 48: memcpy first 48 bytes; else: zero-pad)
  5. On any OpenSSL error → goto deterministic_sign (BLAKE3 fallback)
```

**Critical flaw**: DER truncation to 48 bytes discards the ASN.1 length fields and
part of the `s` scalar, making the stored bytes non-verifiable with standard ECDSA.
The truncation is one-way and lossy.

### Verify flow (`src/transaction.c`, lines 256–276; `src/wallet.c`, lines 276–284)

```
transaction_verify(tx)
  - Coinbase → always true
  - is_zero(tx->signature, 48) → false (reject all-zero sig)
  - Otherwise → true  ← STUB, no cryptographic check

wallet_verify(pubkey, pk_len, message, msg_len, signature)
  - Checks any byte in signature[] != 0 → return true
  - Otherwise → false   ← also a stub
```

Both verification functions are **stubs**. The chain does not perform any real
cryptographic signature verification in v45. This is the highest-severity finding
for the PQC integration: we need to implement full verify before (or alongside) the
PQC swap.

---

## 4. Network Architecture

Four independent processes communicate over ZeroMQ:

| Process | ZMQ pattern | Role |
|---------|-------------|------|
| `blockchain` | REP server | Authoritative ledger, balance queries |
| `metronome` | PUB broadcaster | Issues block challenges every ~6 s |
| `pool` | REP + PUSH/PULL | Mempool — accepts tx, forwards to blockchain |
| `validator` | SUB + REQ | Searches for PoS proofs, assembles blocks |

All inter-process messages are Protocol Buffers v3 (`blockchain.proto`), wrapped in
`NetworkMessage` envelopes with typed payload bytes.

---

## 5. Serialization (Protocol Buffers)

`blockchain.proto` defines the `Transaction` message with a `bytes signature = 7`
field (currently expected to be 48 bytes by all callers):

```protobuf
message Transaction {
    uint64 nonce         = 1;
    uint32 expiry_block  = 2;
    bytes  source_address = 3;  // 20 bytes
    bytes  dest_address   = 4;  // 20 bytes
    uint64 value         = 5;
    uint32 fee           = 6;
    bytes  signature     = 7;   // 48 bytes today → 666 bytes with Falcon-512
}
```

The `bytes` type is variable-length in protobuf — the schema change is trivial.
However, generated `blockchain.pb-c.c` and `blockchain.pb-c.h` must be regenerated
with `make proto` after any `.proto` edit, and all callers that hard-code 48 bytes
must be updated.

---

## 6. Build Configuration

From `Makefile`:

```makefile
CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -fopenmp
```

| Library | Purpose |
|---------|---------|
| `-lssl -lcrypto` | OpenSSL 3.6.1 — ECDSA, SHA-256, RIPEMD-160 |
| `-lzmq` | ZeroMQ — inter-process messaging |
| `-lprotobuf-c` | Protocol Buffers serialization |
| `-lm` | Math (benchmark plots) |
| `-fopenmp` | OpenMP — parallel wallet batch ops |

---

## 7. Integration Points for PQC

The following files require changes to replace ECDSA with Falcon-512 (or ML-DSA-44):

| File | Change required | Difficulty |
|------|-----------------|-----------|
| `include/transaction.h` | Remove 48-byte fixed sig field; use pointer + length, or expand to 1330 bytes for Falcon-512 | Medium |
| `src/transaction.c` | Replace sign logic (lines 152–253); implement real verify (lines 256–276) | Medium |
| `src/wallet.c` | Replace `EVP_PKEY` key generation with `OQS_SIG`; rewrite `wallet_verify` | Medium |
| `include/wallet.h` | New `Wallet` struct fields: `uint8_t pqc_pubkey[]`, `OQS_SIG*` ctx | Easy |
| `Makefile` | Add `-loqs`, `OQS_ROOT`, include path | Easy |
| `proto/blockchain.proto` | `signature` field is already `bytes` — no schema change needed | Easy |
| `proto/blockchain.pb-c.*` | Regenerate with `make proto` after any `.proto` edit | Easy |
| `src/common.c` / `include/common.h` | `hash160(pubkey, 897)` already works for any size; no change | None |

### Signature size comparison

| Algorithm | Sig size | vs. current 48 bytes |
|-----------|----------|----------------------|
| ECDSA secp256k1 (current) | 70–72 bytes (DER), truncated to 48 | baseline |
| Falcon-512 | 666 bytes | **+14×** |
| ML-DSA-44 | 2,420 bytes | **+50×** |
| SLH-DSA-SHA2-128f | 17,088 bytes | **+356×** |
| Ed25519 | 64 bytes | +1.3× |

Falcon-512 is the best PQC fit: smallest PQC sig, fast verify (see qMEMO benchmarks).

### Transaction struct options

**Option A — variable sig (pointer)**: Remove `uint8_t signature[48]` from the
packed struct, store `uint8_t* sig; size_t sig_len` outside the wire format, and
serialize only via protobuf. Removes the `_Static_assert`. Most flexible.

**Option B — fixed max-size**: Expand `signature[48]` to `signature[666]` (Falcon-512
max). Struct grows from 112 to 730 bytes. Simpler code, wastes RAM per tx in pool.

Option A is recommended — it aligns with how the protobuf layer already handles
variable-length bytes and avoids per-transaction padding overhead.

---

## 8. Risk Register

| # | Risk | Severity | Mitigation |
|---|------|----------|-----------|
| 1 | Fixed 112-byte `Transaction` struct with `_Static_assert` | **High** | Must remove assert and redesign sig field before any PQC work |
| 2 | 48-byte DER truncation is lossy and not reversible | **High** | Implement real ECDSA verify first to establish a working baseline, then swap |
| 3 | Stub `transaction_verify` / `wallet_verify` — chain accepts any non-zero sig | **High** | Both stubs must be replaced with real crypto before testing PQC correctness |
| 4 | Protobuf pb-c generated files must be manually regenerated | Medium | Add `make proto` step to CI; document in README |
| 5 | Address derivation: `hash160(pubkey)` — PQC pubkeys are 897 bytes (Falcon-512) | Low | `hash160` already takes `(const uint8_t*, size_t)` — works unchanged |
| 6 | No existing test suite | Medium | Write integration tests before swapping crypto |
| 7 | OpenMP `wallet.c` batch ops assume thread-safe `evp_key` re-use | Medium | Replace with per-thread `OQS_SIG` context (same pattern as qMEMO `sign_benchmark.c`) |

---

## 9. Recommended Integration Sequence

1. **Fix verify stubs first** — implement real ECDSA verify in `transaction_verify`
   and `wallet_verify` so the current scheme is cryptographically sound and testable.
2. **Refactor Transaction struct** — Option A (pointer + length) to decouple sig size
   from the wire format.
3. **Add liboqs to Makefile** — mirror qMEMO's `OQS_ROOT := ../liboqs_install` pattern.
4. **Implement PQC key generation** — `OQS_SIG_new(OQS_SIG_alg_falcon_512)` in
   `wallet_create()`, store public key in `wallet->pqc_pubkey`.
5. **Implement PQC sign** — replace EVP_DigestSign path in `transaction_sign()` with
   `OQS_SIG_sign()`.
6. **Implement PQC verify** — replace stubs with `OQS_SIG_verify()` in
   `wallet_verify()`.
7. **Regenerate protobuf** — run `make proto`; `signature` field is already `bytes`,
   so the only change may be field size annotations in comments.
8. **Run qMEMO benchmarks** — compare Falcon-512 throughput on the same hardware
   against the classical baseline already measured (Cascade Lake: 23,787 vs/sec
   Falcon vs 49,060 vs/sec ML-DSA-44 with AVX-512).

---

## 10. File Reference

| File | Key lines | Notes |
|------|-----------|-------|
| `include/transaction.h` | 30–43 | Transaction struct + `_Static_assert` |
| `src/transaction.c` | 139–276 | Sign (152), verify (256), hash (104–130) |
| `src/wallet.c` | 15–73 | Key gen; 231–284 sign/verify |
| `include/wallet.h` | — | `Wallet` struct with `evp_key`, `public_key[65]`, `private_key_pem[]` |
| `proto/blockchain.proto` | 19–27 | Transaction protobuf message |
| `Makefile` | 1–116 | Build flags, library deps, targets |
