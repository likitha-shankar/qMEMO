# MEMO Blockchain — Classical Baseline Performance

**Platform:** Intel Xeon Gold 6242 Cascade Lake, 2×16-core @ 2.80 GHz (32 physical / 64 logical),
187 GB RAM, Ubuntu 22.04, GCC 11.4.0
**Source branch:** `harsha-original` — `blockchain_pos_v45/`
**Benchmark date:** 2026-03-10
**Benchmark tool:** `benchmark.sh` (end-to-end) + `build/benchmark` (micro)

---

## 1. Test Environment

| Property | Value |
|----------|-------|
| Host alias | `chameleon-new` (129.114.108.171) |
| CPU | Intel Xeon Gold 6242 @ 2.80 GHz |
| Physical cores | 32 (2 sockets × 16 cores) |
| Logical CPUs | 64 (Hyper-Threading enabled) |
| RAM | 187 GB DDR4-2933 ECC |
| OS | Ubuntu 22.04 LTS, kernel 5.15 |
| Compiler | GCC 11.4.0, `-O2 -fopenmp` |
| OpenSSL | 3.x (system package) |
| ZeroMQ | libzmq3 (system package) |
| Protobuf-C | system package |

This is the same physical hardware used for all qMEMO PQC benchmarks
(see `docs/BENCHMARK_REPORT.md`, run `run_20260301_210825`), ensuring
directly comparable results for the eventual PQC integration.

---

## 2. Cryptographic Details

### 2.1 Signature scheme

| Property | Value |
|----------|-------|
| Algorithm | ECDSA over secp256k1 |
| API | OpenSSL 3.x EVP_PKEY |
| DER output size | 70–72 bytes (variable-length ASN.1) |
| Wire format | Truncated/padded to fixed **48 bytes** |
| Label in code | "BLS-style" (not true BLS — see §2.2) |

### 2.2 Verification status

`transaction_verify()` (`src/transaction.c:256–276`) and `wallet_verify()`
(`src/wallet.c:276–284`) are **stub implementations**:

```c
/* src/transaction.c:256 */
int transaction_verify(const Transaction *tx) {
    if (tx->is_coinbase) return 1;            // coinbase always passes
    if (is_zero(tx->signature, 48)) return 0; // all-zero sig rejected
    return 1;                                  // ← no cryptographic check
}

/* src/wallet.c:276 */
int wallet_verify(const uint8_t *pubkey, size_t pk_len,
                  const uint8_t *msg,    size_t msg_len,
                  const uint8_t *sig) {
    for (size_t i = 0; i < 48; i++)
        if (sig[i] != 0) return 1;  // any non-zero byte → accept
    return 0;
}
```

Any non-zero 48-byte value passes as a valid signature. This is a known
design choice in v45 (stub placeholder for future implementation). It is
documented here as the **baseline cryptographic state** against which the
PQC integration must improve.

---

## 3. Transaction Struct Layout

From `include/transaction.h:30–43`:

```c
#pragma pack(push, 1)
typedef struct {
    uint64_t nonce;              //  8 bytes  — replay-protection counter
    uint32_t expiry_block;       //  4 bytes  — expiry block height
    uint8_t  source_address[20]; // 20 bytes  — RIPEMD160(SHA256(pubkey))
    uint8_t  dest_address[20];   // 20 bytes
    uint64_t value;              //  8 bytes  — transfer amount
    uint32_t fee;                //  4 bytes  — miner fee
    uint8_t  signature[48];      // 48 bytes  — "BLS-style" fixed field
} Transaction;                   // TOTAL: 112 bytes
#pragma pack(pop)

_Static_assert(sizeof(Transaction) == 112, "Transaction must be exactly 112 bytes");
```

The `_Static_assert` is a hard compile-time contract. Any PQC swap that
changes signature size must remove it and redesign the sig field.

### Protobuf wire representation (`proto/blockchain.proto`)

```protobuf
message Transaction {
    uint64 nonce          = 1;
    uint32 expiry_block   = 2;
    bytes  source_address = 3;   // 20 bytes
    bytes  dest_address   = 4;   // 20 bytes
    uint64 value          = 5;
    uint32 fee            = 6;
    bytes  signature      = 7;   // 48 bytes today → 666 bytes with Falcon-512
}
```

Because `signature` is a `bytes` field the schema accepts any length —
the protobuf layer is already PQC-ready. Only the fixed C struct and
callers hard-coding 48 bytes need updating.

---

## 4. Build Configuration

```makefile
CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -fopenmp
```

Produces 6 binaries in `build/`:

| Binary | Role |
|--------|------|
| `blockchain` | Authoritative ledger server (ZMQ REP) |
| `metronome` | Block challenge broadcaster (ZMQ PUB) |
| `pool` | Transaction mempool (ZMQ REP + PUSH/PULL) |
| `validator` | PoS proof search + block assembly (ZMQ SUB + REQ) |
| `wallet` | Key generation, address derivation, tx submission |
| `benchmark` | Micro-benchmark: GPB, ZMQ, BLAKE3, proof search |

---

## 5. Key Generation Speed

**Method:** `wallet_create_named()` → `EVP_PKEY_CTX` (secp256k1 keygen)
→ `hash160(pubkey)` for address derivation.

> **Status: PENDING SSH ACCESS**
>
> Measured on Chameleon Cascade Lake once SSH auth is restored:
> ```
> 20 ECDSA keypairs in [X] ms = [Y] keys/sec
> ```
>
> *Expected range (based on OpenSSL secp256k1 keygen on Cascade Lake):
> 8,000–12,000 keys/sec. Will be filled with measured value.*

---

## 6. Micro-Benchmark Results (GPB + ZMQ + BLAKE3)

Measured by `./build/benchmark [iterations] [k]` on Chameleon.

### 6.1 Protocol Buffers Serialization

> **Status: PENDING SSH ACCESS**
>
> | Operation | avg (µs) | min (µs) | max (µs) | ops/sec | avg bytes |
> |-----------|----------|----------|----------|---------|-----------|
> | Transaction serialize | — | — | — | — | — |
> | Transaction deserialize | — | — | — | — | — |
> | Block serialize (1 tx) | — | — | — | — | — |
> | Block serialize (10 tx) | — | — | — | — | — |
> | Block serialize (100 tx) | — | — | — | — | — |
> | Block deserialize (100 tx) | — | — | — | — | — |

### 6.2 ZMQ Round-Trip Latency

> **Status: PENDING SSH ACCESS**
>
> | Transport | avg RTT (µs) | min (µs) | max (µs) |
> |-----------|-------------|----------|----------|
> | inproc (same-process) | — | — | — |
> | TCP loopback (127.0.0.1) | — | — | — |

### 6.3 BLAKE3 Hash Throughput

256-byte inputs, 10,000 iterations:

> **Status: PENDING SSH ACCESS**
>
> | Metric | Value |
> |--------|-------|
> | avg per hash (µs) | — |
> | throughput (ops/sec) | — |
> | throughput (MB/s) | — |

### 6.4 Proof Search (k=16)

> **Status: PENDING SSH ACCESS**
>
> | Metric | Value |
> |--------|-------|
> | Plot generation (k=16) | — ms |
> | Proof search avg | — µs |
> | Proof search throughput | — proofs/sec |

---

## 7. End-to-End Transaction Throughput

`./benchmark.sh [N] 1 16 10` — N transactions, 1 s blocks, k=16, 10 farmers.

### 7.1 Pass 1 — 500 Transactions

> **Status: PENDING SSH ACCESS**
>
> | Metric | Value |
> |--------|-------|
> | Warmup blocks | — |
> | TX submitted | 500 |
> | Submission time | — s |
> | **Submission TPS** | **— tx/sec** |
> | Confirmation time | — s |
> | **Confirmation TPS** | **— tx/sec** |
> | Avg tx latency (submit→confirm) | — ms |
> | p50 latency | — ms |
> | p95 latency | — ms |
> | p99 latency | — ms |
> | Blocks used | — |
> | Avg tx/block | — |

### 7.2 Pass 2 — 1000 Transactions

> **Status: PENDING SSH ACCESS**
>
> | Metric | Value |
> |--------|-------|
> | Warmup blocks | — |
> | TX submitted | 1000 |
> | Submission time | — s |
> | **Submission TPS** | **— tx/sec** |
> | Confirmation time | — s |
> | **Confirmation TPS** | **— tx/sec** |
> | Avg tx latency (submit→confirm) | — ms |
> | p50 latency | — ms |
> | p95 latency | — ms |
> | p99 latency | — ms |
> | Blocks used | — |
> | Avg tx/block | — |

---

## 8. Baseline vs PQC Projection

Classical baseline (v45 as-is) vs projected post-integration metrics.
qMEMO Falcon-512 verify throughput from `run_20260301_210825` on same hardware.

### 8.1 Signature size impact on transaction struct

| Scheme | Sig bytes (wire) | Transaction struct | vs baseline |
|--------|------------------|--------------------|-------------|
| ECDSA secp256k1 (current) | 48 (truncated DER) | 112 bytes | baseline |
| Ed25519 | 64 | 128 bytes | +14% |
| **Falcon-512** | **666** | **730 bytes** | **+6.4×** |
| ML-DSA-44 | 2,420 | 2,484 bytes | +22.2× |
| SLH-DSA-SHA2-128f | 17,088 | 17,152 bytes | +153× |

### 8.2 Verification throughput on Cascade Lake

| Scheme | Verify (ops/sec) | Notes |
|--------|------------------|-------|
| ECDSA secp256k1 (current v45) | STUB — no real verify | acceptance = any non-zero sig |
| ECDSA secp256k1 (real) | ~30,000–40,000 | estimated, OpenSSL EVP |
| **Falcon-512** | **23,787** | measured, qMEMO `run_20260301_210825` |
| ML-DSA-44 | 49,060 | measured, qMEMO `run_20260301_210825` (AVX-512 boost) |
| Ed25519 | ~95,000 | measured, qMEMO `comprehensive_comparison` |

Falcon-512 imposes a ~20–40% verify overhead vs real ECDSA, offset by quantum
resistance. ML-DSA-44 is actually **faster** than ECDSA on this hardware due
to AVX-512 acceleration in the NIST reference implementation.

### 8.3 Projected end-to-end TPS impact

With stub verify removed and real Falcon-512 verify inserted:

| Path | Classical (stub) | Falcon-512 (projected) |
|------|-----------------|------------------------|
| Verify cost per tx | ~0 µs (stub) | ~42 µs (1/23,787 ops/sec) |
| Sig bytes per tx (protobuf) | 48 B | 666 B |
| Network overhead per tx | — | +618 B (+13×) |
| End-to-end confirmation TPS | — (measured above) | ~(measured × 0.8) est. |

The serialization cost increase (~618 extra bytes/tx protobuf) is expected to be
the dominant bottleneck on high-throughput runs, not the verify compute time.

---

## 9. Filling in PENDING Sections

Once SSH access to `chameleon-new` (129.114.108.171) is restored:

```bash
# Phase 1 — Install deps
ssh chameleon-new "sudo apt-get update -q && sudo apt-get install -y -q \
    build-essential libssl-dev libprotobuf-c-dev protobuf-c-compiler \
    libzmq3-dev tmux python3 python3-matplotlib bc"

# Phase 2 — Clone harsha-original
ssh chameleon-new "cd ~ && rm -rf memo-baseline && \
    git clone --branch harsha-original --single-branch \
        https://github.com/likitha-shankar/qMEMO.git memo-baseline"

# Phase 3 — Build
ssh chameleon-new "cd ~/memo-baseline/blockchain_pos_v45 && make -j\$(nproc)"

# Phase 4 — Benchmark
ssh chameleon-new "cd ~/memo-baseline/blockchain_pos_v45 && \
    ./benchmark.sh 1000 1 16 10 2>&1 | tee /tmp/bench_1000.log"

# Phase 5 — Pull results locally
mkdir -p ~/Desktop/hmm/qMEMO/memo-baseline/results
scp chameleon-new:/tmp/bench_1000.log \
    ~/Desktop/hmm/qMEMO/memo-baseline/results/
scp -r "chameleon-new:~/memo-baseline/blockchain_pos_v45/benchmark_results/" \
    ~/Desktop/hmm/qMEMO/memo-baseline/results/benchmark_results/
```

SSH public key to add via Chameleon web console (instance → console):
```
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDiZ+x13S6SALnFBRRU2lbIEfYevOmQ32lQMiSXvR/vEnJWQ1j3thOX7JANvFSKrPFcN03mttmTO7/BU/2LYhrFH9eNDa6Y3geYZL8Ooa4/+yR3DmLIivruz8hC0+fVOMOTjkCYgXXtBlv98sX0PEMtJ7fVEmNtreUy0jCCbZQmTTjTJ8s4DEEWVxq8WW37dz7zwYp7CW3AlZ0pI+M/vSJvlfttgVHlk8I6xxTWmFXLpM0v4YpeMOrHi7eKnJzEYzdnDfYFhuNJ0ZXGZhNUsR9P4tAzmA/zkjLkaijZCyetByeAAp+3IROkhf5bG7gsH3II5BgfYQQvcaaFi6p7Gi/e7BDLbJpF6ABab1hOufxzRMEBy0AMG3WOhKHV+tZCgIDz78FpxUMUDvDZ01lLcPTefsQwYJHatmk1jYlwZN6n7dpXqKxkbOCcR3EhNpZBUGo+TRHKM2Pw0/RF40PyrI8DYFdxAANtAwxzp17Q7nlcTjX6MIo1omnDUOSDBHF9YSmj9fcY5kofVkRDk6mtbiCMbPTTYDXj/x5L+CtK5jWv6GVq/N8eePrRba7FxXHamz6bt+qgP0iQFgsr6+WLTg4o7bAMIcSHk/vGpaaW3EjHW0ja5ZUxNJ5WxFfXEVSSiVW/tqiJt0OW5MT3JLRLldd54zQXRLo8M5izXv87KaSMzw== lshankar@hawk.illinoistech.edu
```

After pulling results, replace all `PENDING SSH ACCESS` sections above with
the actual numbers from `bench_1000.log` and `benchmark_results/*.csv`.

---

## 10. Key Findings (Static Analysis)

1. **Stub verify** — the most critical finding: `transaction_verify()` accepts
   any non-zero 48-byte value. Throughput numbers in this doc measure the chain
   *without* cryptographic verification. The PQC integration must add real verify.

2. **DER truncation is lossy** — the stored 48-byte signature cannot be
   verified even with the original public key. Baseline "classical" performance
   must be re-benchmarked after implementing real ECDSA verify.

3. **112-byte struct is a hard contract** — `_Static_assert` will fail at
   compile time if sig field size changes. All PQC paths must remove this.

4. **Protobuf layer is PQC-ready** — `signature = bytes` field accepts any
   length; no schema change needed for Falcon-512 or ML-DSA-44.

5. **Falcon-512 is the optimal PQC candidate** — smallest sig among NIST
   finalists (666 bytes), fastest verify on ARM (30,569 vs/sec), competitive
   on x86 (23,787 vs/sec vs ML-DSA-44's 49,060 with AVX-512).
