# Quantum Signature Benchmarking — Quick Summary

**Likitha Shankar | Graduate Research, Illinois Institute of Technology, Chicago**
**Repository:** https://github.com/likitha-shankar/qMEMO

---

## What Was Benchmarked

7 signature algorithms across 2 hardware platforms:

| Algorithm | Type | NIST Level |
|-----------|------|:----------:|
| Falcon-512 | Lattice (NTRU) | 1 |
| Falcon-1024 | Lattice (NTRU) | 5 |
| ML-DSA-44 | Module Lattice | 2 |
| ML-DSA-65 | Module Lattice | 3 |
| SLH-DSA-SHA2-128f | Hash-based | 1 |
| ECDSA secp256k1 | Elliptic Curve | — |
| Ed25519 | EdDSA | — |

Platforms: Apple M2 Pro (ARM64) · Intel Xeon Gold 6242 @ 2.8 GHz (x86-64, Chameleon Cloud)

---

## Key Results — Verification Throughput (Single Core)

| Algorithm | M2 Pro | Xeon Gold 6242 | Sig Size |
|-----------|-------:|---------------:|:--------:|
| **Falcon-512** | **31,133 ops/sec** | **23,877 ops/sec** | 666 B max |
| ML-DSA-44 | 25,904 ops/sec | 49,060 ops/sec | 2,420 B |
| ML-DSA-65 | 15,369 ops/sec | 30,287 ops/sec | 3,293 B |
| SLH-DSA-SHA2-128f | 599 ops/sec | 734 ops/sec | 17,088 B |
| ECDSA secp256k1 | 4,026 ops/sec | 2,963 ops/sec | ~72 B |
| Ed25519 | 8,857 ops/sec | 9,013 ops/sec | 64 B |

---

## Recommendation: Falcon-512

**Why Falcon-512 for a blockchain integration:**

| Factor | Falcon-512 | Next best PQC |
|--------|:----------:|:-------------:|
| Verify speed (single core) | 23,877–31,133 ops/sec | ML-DSA-44: 25,904–49,060 |
| Signature size | **666 bytes** | ML-DSA-44: 2,420 bytes (3.6× larger) |
| Key size (public) | 897 bytes | ML-DSA-44: 1,312 bytes |
| NIST standard | FIPS 206 (final Aug 2025) | FIPS 204 (final Aug 2024) |
| Production blockchain use | Algorand (since 2022) | Solana testnet (Dec 2025) |

Falcon-512 verifies **7.6× faster than ECDSA secp256k1** and **3.5× faster than Ed25519**,
while providing full quantum resistance. The main cost is signature size: 666 bytes vs
64 bytes for Ed25519 (~10× larger).

---

## MEMO Headroom Analysis

Assuming MEMO requires up to **2,500 verifications/second per shard**
(10,000 TPS across 4 shards):

| Configuration | Throughput | Headroom vs 2,500 req |
|--------------|:----------:|:---------------------:|
| Falcon-512, 1 core, Xeon | 23,877 ops/sec | **9.5×** |
| Falcon-512, 1 core, M2 Pro | 31,133 ops/sec | **12.5×** |
| Falcon-512, 10 threads, Xeon | 184,467 ops/sec | **73.8×** |
| Falcon-512, 10 threads, M2 Pro | 239,297 ops/sec | **95.7×** |

Verification is **not a throughput bottleneck** at any tested configuration.

---

## Multicore Scaling (Falcon-512)

| Threads | Xeon ops/sec | Speedup | M2 Pro ops/sec | Speedup |
|--------:|-------------:|:-------:|---------------:|:-------:|
| 1 | 20,169 | 1.00× | 27,022 | 1.00× |
| 4 | 74,909 | 3.71× | 119,900 | 4.44× |
| 10 | 184,467 | **9.15×** | 239,297 | **8.86×** |

Near-linear scaling — verification is embarrassingly parallel (stateless, read-only).

---

*Full methodology, raw logs, and source code: https://github.com/likitha-shankar/qMEMO*
*Benchmarking library: liboqs 0.15.0 · OpenSSL 3.6.1 · 1,000 iterations per algorithm*
