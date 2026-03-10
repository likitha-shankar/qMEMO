# MEMO Blockchain — Classical Baseline Performance

**Platform:** Intel Xeon Gold 6126 Skylake-SP, 2×12-core @ 2.60 GHz (24 physical / 48 logical),
187 GB RAM, Ubuntu 24.04.2 LTS, GCC 13.3.0
**Source branch:** `harsha-original` — `blockchain_pos_v45/`
**Benchmark date:** 2026-03-10
**Benchmark tool:** `benchmark.sh` (end-to-end) + `build/benchmark` (micro)
**Chameleon node:** `qmemo` (chi.tacc.chameleoncloud.org, floating IP 129.114.108.20)

---

## 1. Test Environment

| Property | Value |
|----------|-------|
| Host alias | `chameleon-new` (qmemo, 129.114.108.20 → 10.52.2.136) |
| CPU | Intel Xeon Gold 6126 @ 2.60 GHz (Skylake-SP) |
| Physical cores | 24 (2 sockets × 12 cores) |
| Logical CPUs | 48 (Hyper-Threading enabled) |
| RAM | 187 GB DDR4 ECC |
| OS | Ubuntu 24.04.2 LTS, kernel 6.8.0-64-generic |
| Compiler | GCC 13.3.0, `-O2 -fopenmp` |
| OpenSSL | 3.0.13 (system package, Ubuntu 24.04) |
| ZeroMQ | libzmq3 (system package) |
| Protobuf-C | system package |

> **Note:** Prior qMEMO PQC benchmarks (`run_20260301_210825`) used the Intel Xeon Gold 6242
> Cascade Lake at the same Chameleon facility. The current node is a Gold 6126 Skylake-SP
> (no AVX-512 on Gold 6126 12-core variant). All blockchain baseline numbers in this doc
> are from the Gold 6126 node and should be compared against the Skylake-SP PQC benchmarks
> (`run_20260310_*`) rather than the Cascade Lake set.

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

Any non-zero 48-byte value passes as a valid signature. **All throughput numbers in
this document are measured without cryptographic verification.** This is the baseline
state against which the PQC integration will be compared; real verify must be
implemented before or alongside the PQC swap.

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

### Protobuf wire representation (`proto/blockchain.proto`)

```protobuf
message Transaction {
    uint64 nonce          = 1;
    uint32 expiry_block   = 2;
    bytes  source_address = 3;   // 20 bytes → avg 22 bytes protobuf-encoded
    bytes  dest_address   = 4;   // 20 bytes → avg 22 bytes protobuf-encoded
    uint64 value          = 5;
    uint32 fee            = 6;
    bytes  signature      = 7;   // 48 bytes today → 666 bytes with Falcon-512
}
// Measured wire size: 112 bytes (matches C struct — benchmark confirmed)
```

---

## 4. Build Configuration

```makefile
CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp
LDFLAGS = -lzmq -lssl -lcrypto -lm -lprotobuf-c -fopenmp
```

Binary sizes after `make -j48`:

| Binary | Size | Role |
|--------|------|------|
| `blockchain` | 411 KB | Authoritative ledger server (ZMQ REP) |
| `metronome` | 402 KB | Block challenge broadcaster (ZMQ PUB) |
| `pool` | 414 KB | Transaction mempool (ZMQ REP + PUSH/PULL) |
| `validator` | 399 KB | PoS proof search + block assembly |
| `wallet` | 464 KB | Key generation, address derivation, tx submission |
| `benchmark` | 437 KB | Micro-benchmark: GPB, ZMQ, BLAKE3, proof search |

---

## 5. Key Generation Speed

**Method:** `wallet_create_named()` → `EVP_PKEY_CTX` (secp256k1 keygen) +
`hash160(pubkey)` address derivation + wallet file write to disk.

| Metric | Value |
|--------|-------|
| 20 ECDSA keypairs | 362 ms |
| **Throughput** | **55.2 keys/sec** |

> Note: this includes disk I/O for `.dat` wallet files. Pure keygen (without file
> write) would be significantly faster; this reflects the full `wallet create` path
> as used by benchmark wallets.

---

## 6. Micro-Benchmark Results

Measured by `./build/benchmark` with 500 iterations, k=16, on the Gold 6126 node.
All numbers are the median of two identical runs (Pass 1 and Pass 2 were consistent
within 2%).

### 6.1 Protocol Buffers Serialization

| Operation | avg (µs) | ops/sec | avg bytes |
|-----------|----------|---------|-----------|
| Transaction serialize | 0.17 | 5,910,655 | 112 |
| Transaction deserialize | 0.50 | 1,994,360 | 112 |
| Block serialize (1 tx) | 0.55 | 1,828,354 | 314 |
| Block deserialize (1 tx) | 5.53 | 180,972 | 314 |
| Block serialize (10 tx) | 2.40 | 416,889 | 1,327 |
| Block deserialize (10 tx) | 9.08 | 110,144 | 1,327 |
| Block serialize (100 tx) | 14.18 | 70,510 | 11,452 |
| Block deserialize (100 tx) | 65.75 | 15,209 | 11,452 |

GPB serialization scales sub-linearly: 100× more transactions → only 83× more
serialization time. Deserialization is 4–5× slower than serialization.

### 6.2 ZMQ Round-Trip Latency

| Transport | avg RTT (µs) | ops/sec |
|-----------|-------------|---------|
| inproc (same-process) | **5.78** | 173,155 |
| TCP loopback (127.0.0.1) | **167.58** | 5,967 |

The 29× gap between inproc and TCP loopback is normal for ZMQ on Linux; the TCP
path includes kernel socket buffers, even for loopback. All inter-process
blockchain communication uses TCP, so 167 µs is the practical per-hop baseline.

### 6.3 BLAKE3 Hash Throughput

256-byte inputs, 500 iterations:

| Metric | Value |
|--------|-------|
| avg per hash | 1.13 µs |
| **throughput** | **886,011 ops/sec** |
| effective bandwidth | ~227 MB/s (256 B × 886 Kops/s) |

### 6.4 Proof Search (k=16)

| Metric | Value |
|--------|-------|
| Plot generation (k=16, 2^16 = 65,536 entries) | **72.71 µs** (0.07 ms) |
| Proof search (binary search in plot) | **0.02 µs** |
| Proof search throughput | **42,194,093 proofs/sec** |

Plot generation is a one-time cost per farmer at startup (~73 µs). Proof search
per challenge is negligible at 0.02 µs; the 695 proofs/benchmark run at 34.7
proofs/farmer/block represents healthy PoS participation.

---

## 7. End-to-End Transaction Throughput

Config: `./benchmark.sh [N] 1 16 10` — N transactions, 1 s block interval,
k=16, 10 farmers, 8 OpenMP threads/farmer, batch size 64.

### 7.1 Pass 1 — 500 Transactions

| Metric | Value |
|--------|-------|
| Warmup blocks | 10 |
| TX submitted | 500 |
| Submission time | 153 ms |
| **Submission TPS** | **3,260 tx/sec** |
| Block processing time | 78 ms |
| **Confirmation TPS** | **1,943 tx/sec** |
| **End-to-end TPS** | **1,218 tx/sec** |
| Confirmation rate | 100% |
| Avg tx/block | 250 |
| Blocks used | 1 (block #35, 501 TXs) |

### 7.2 Pass 2 — 1000 Transactions

| Metric | Value |
|--------|-------|
| Warmup blocks | 10 |
| TX submitted | 1000 |
| Submission time | 177 ms |
| **Submission TPS** | **5,659 tx/sec** |
| Block processing time | 35 ms |
| **Confirmation TPS** | **3,662 tx/sec** |
| **End-to-end TPS** | **2,223 tx/sec** |
| Confirmation rate | 100% |
| Avg tx/block | 500 |
| Blocks used | 1 (block #35, 1001 TXs) |

**Scaling observation:** doubling transaction count from 500→1000 increased
submission TPS by 1.74× (3,260→5,659) and end-to-end TPS by 1.83×
(1,218→2,223). The system is not saturated at 1000 tx. The block processing
time dropped from 78→35 ms at higher load, suggesting the block assembly
pipeline runs more efficiently with a larger mempool batch.

### 7.3 Validator Step Timing (from logs, per block)

All 10 farmers showed nearly identical per-block timing:

| Step | Time |
|------|------|
| Step 1: GET_LAST_HASH (blockchain query) | 0–1 ms |
| Step 2: Pool→Validator (fetch pending TX) | 9–13 ms |
| Step 5: Serialize + Send block | 1–5 ms |

Pool→Validator fetch (10 ms) dominates the validator critical path — this is
the ZMQ TCP round-trip plus protobuf deserialization of the mempool batch.

---

## 8. Baseline vs PQC Projection

Classical baseline (v45 as-is, stub verify) vs projected post-integration
metrics. Falcon-512 verify throughput from qMEMO `run_20260301_210825`
on Intel Xeon Gold 6242 (closest available benchmark; Gold 6126 results
pending a qMEMO benchmark run on this node).

### 8.1 Signature size impact on transaction struct

| Scheme | Sig bytes (wire) | Transaction struct | vs baseline |
|--------|------------------|--------------------|-------------|
| ECDSA secp256k1 (current) | 48 (truncated DER) | 112 bytes | baseline |
| Ed25519 | 64 | 128 bytes | +14% |
| **Falcon-512** | **666** | **730 bytes** | **+6.5×** |
| ML-DSA-44 | 2,420 | 2,484 bytes | +22.2× |
| SLH-DSA-SHA2-128f | 17,088 | 17,152 bytes | +153× |

### 8.2 Protobuf wire-size projection

Current `signature = bytes` field in the protobuf message is already variable-length.
With Falcon-512 (666-byte sig), the per-transaction protobuf wire size grows from
112 B to ~728 B (+6.5×). The Pool→Validator fetch (currently 10 ms for 500–1000 tx)
will see proportional growth in serialization time and ZMQ payload size.

| Scenario | Sig bytes | TX protobuf | 1000-tx block (wire) |
|----------|-----------|-------------|----------------------|
| Current (stub ECDSA) | 48 | ~112 B | ~112 KB |
| Falcon-512 (PQC) | 666 | ~730 B | ~730 KB |
| ML-DSA-44 (PQC) | 2,420 | ~2,484 B | ~2.4 MB |

### 8.3 Verification throughput — current vs projected

| Scheme | Verify (ops/sec) | Notes |
|--------|------------------|-------|
| ECDSA secp256k1 (v45 current) | **STUB** | any non-zero 48-byte sig passes |
| ECDSA secp256k1 (real, projected) | ~30,000–40,000 | estimate, OpenSSL EVP secp256k1 |
| **Falcon-512** | **23,787** | measured, qMEMO Cascade Lake node |
| ML-DSA-44 | 49,060 | measured, qMEMO Cascade Lake (AVX-512) |
| Ed25519 | ~95,000 | measured, qMEMO comprehensive comparison |

With Falcon-512 verify at 23,787 ops/sec, a 1000-tx block requires ~42 ms of
pure verify time (single-threaded). At the current block processing time of 35 ms
(zero-verify baseline), inserting real Falcon verify will increase block assembly
time by ~120% unless parallelised. The existing `OMP_NUM_THREADS=8` per farmer
can parallelise verify across cores, reducing per-block verify to ~5 ms at 8 threads.

---

## 9. Key Findings

1. **Stub verify baseline** — all throughput numbers (5,659 submission TPS,
   2,223 end-to-end TPS) are measured **without** any cryptographic verification.
   This is the theoretical maximum ceiling; real ECDSA verify will reduce TPS.

2. **100% confirmation rate at both 500 and 1000 tx** — the PoS/mempool pipeline
   is reliable; all submitted transactions are confirmed in a single block.

3. **Block processing time decreases under load** — 78 ms at 500 tx vs 35 ms
   at 1000 tx. The serialization pipeline amortises fixed costs (ZMQ setup,
   BLAKE3 block hash) more efficiently at higher batch sizes.

4. **ZMQ TCP loopback is the dominant latency source** — 167 µs per hop.
   The Pool→Validator fetch (10 ms) represents ~60 ZMQ round-trips for
   large transaction batches, consistent with the measured latency.

5. **Protobuf layer is PQC-ready** — `signature = bytes` accepts any length.
   No schema change needed for Falcon-512 or ML-DSA-44; only the C struct
   `_Static_assert` and fixed-48-byte callers need updating.

6. **`wallet create` throughput (55 keys/sec) includes disk I/O** — actual
   ECDSA secp256k1 keygen is much faster; the bottleneck is the `.dat` file
   write per wallet. PQC keygen (OQS_SIG_keypair for Falcon-512) will not
   significantly change this figure.
