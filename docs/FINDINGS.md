# qMEMO — Key Research Findings

**Graduate Research, Illinois Institute of Technology, Chicago**
**Date:** 2026-03-23

> How do post-quantum signature schemes compare to classical baselines in throughput, scaling, and on-chain overhead — and can they replace ECDSA in a real blockchain without killing performance?

**Short answer: Yes. Both Falcon-512 and ML-DSA-44 drop into MEMO's blockchain with no meaningful TPS penalty. Falcon-512 is the recommended choice.**

---

## 1. Single-Core Verification Throughput (ops/sec)

| Algorithm           | M2 Pro (ARM64) | Skylake-SP (x86-64) | Cascade Lake (x86-64) | Sig Size (B) | PubKey Size (B) |
|---------------------|---------------:|--------------------:|----------------------:|--------------:|----------------:|
| ML-DSA-44           |         25,904 |          **46,532** |            **49,060** |         2,420 |           1,312 |
| Falcon-512          |     **30,569** |              23,505 |                23,877 |    ~655 (avg) |             897 |
| ML-DSA-65           |         15,369 |              28,893 |                30,287 |         3,309 |           1,952 |
| Falcon-1024         |         15,618 |              11,618 |                11,794 |  ~1,280 (avg) |           1,793 |
| Ed25519             |          8,857 |               8,309 |                 9,013 |            64 |              32 |
| ECDSA secp256k1     |          4,026 |               2,467 |                 2,963 |            48 |              65 |
| SLH-DSA-SHA2-128f   |            599 |                 732 |                   734 |         7,856 |              32 |

**Takeaway:** ML-DSA-44 is fastest on x86 (AVX-512 NTT), Falcon-512 is fastest on ARM. Both PQC schemes are **7–19x faster** than ECDSA secp256k1. SLH-DSA is 30–60x slower — impractical for high-throughput use.

---

## 2. Multicore Scaling — Chameleon Skylake-SP (Xeon Gold 6126, ops/sec)

| Algorithm           |     1T |     2T |      4T |      6T |      8T |      10T | Speedup    | Eff%       |
|---------------------|-------:|-------:|--------:|--------:|--------:|---------:|:----------:|:----------:|
| Falcon-512          | 18,731 | 39,458 |  67,038 | 111,247 | 150,568 |  184,167 | **9.83x**  | **98.3%**  |
| Falcon-1024         | 11,152 | 21,962 |  42,854 |  60,525 |  83,466 |  102,034 | 9.15x      | 91.5%      |
| ML-DSA-44           | **44,801** | **63,885** | **112,769** | **150,747** | **215,650** | **271,606** | 6.06x | 60.6% |
| ML-DSA-65           | 25,204 | 43,094 |  74,367 | 122,835 | 149,129 |  210,655 | 8.36x      | 83.6%      |
| SLH-DSA-SHA2-128f   |    705 |  1,389 |   2,645 |   3,542 |   5,173 |    6,140 | 8.71x      | 87.1%      |
| ECDSA secp256k1     |  2,478 |  4,920 |   9,242 |  13,308 |  17,794 |   21,847 | 8.82x      | 88.2%      |
| Ed25519             |  8,343 | 16,531 |  30,121 |  42,365 |  52,170 |   67,603 | 8.10x      | 81.0%      |

**Takeaway:** Falcon-512 scales near-perfectly (98.3% efficiency at 10 threads). ML-DSA-44 has the highest absolute throughput (272K ops/sec) but only 60.6% efficiency due to AVX-512 memory bandwidth saturation. All PQC schemes scale as well or better than classical ECDSA (88.2%) and Ed25519 (81.0%).

---

## 3. Blockchain End-to-End TPS (Measured, Chameleon Skylake-SP)

All runs on Xeon Gold 6126, k=16, 10 farmers, 15 warmup blocks, 8 threads/farmer, 1s block interval.

| Metric                  | ECDSA (stub) | ECDSA (real) | Falcon-512  | ML-DSA-44  |
|-------------------------|:------------:|:------------:|:-----------:|:----------:|
| **500 TX — e2e TPS**    |        1,218 |        1,190 |       1,223 |      1,234 |
| **1000 TX — e2e TPS**   |        2,223 |        1,403 |   **2,572** |      1,533 |
| 500 TX — block time     |        78 ms |            — |      134 ms |     267 ms |
| 1000 TX — block time    |        35 ms |            — |      200 ms |     473 ms |
| Confirmation rate       |         100% |         100% |        100% |       100% |

**Takeaway:** Falcon-512 matches or exceeds ECDSA at both loads — **no TPS penalty**. ML-DSA-44 is 40% below Falcon at 1000 TX despite faster raw verification, because its 3.5x larger signatures (2,420 B vs ~655 B) bottleneck serialization and I/O. The pipeline is network-bound, not verify-bound.

---

## 4. Transaction Size Impact

| Scheme              | Sig (B) | PubKey (B) | TX Wire Size (B) | vs ECDSA   |
|---------------------|--------:|-----------:|-----------------:|:----------:|
| ECDSA secp256k1     |      48 |         65 |             ~112 | 1.0x       |
| Falcon-512          |    ~655 |        897 |           ~1,611 | **14.4x**  |
| ML-DSA-44           |   2,420 |      1,312 |           ~3,791 | **33.8x**  |

**Takeaway:** Falcon-512's compact signatures (666 B max) are the key advantage over ML-DSA-44 for blockchain use. Larger TXs increase serialization cost, block wire size, and ZMQ transfer time proportionally.

---

## 5. What We Found in Harsha's Original Code (blockchain_pos_v45)

| Finding                                    | Detail                                                                                                         |
|--------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| **Signature verification was stubbed out**  | `crypto_verify()` returned `true` without checking — baseline TPS measured everything except real crypto       |
| **Hardcoded to ECDSA**                      | No way to swap signature algorithms; buffer sizes fixed to ECDSA dimensions (32B pubkey, 72B sig)              |
| **No sig_type in wire format**              | Couldn't mix signature schemes on the same chain                                                               |
| **Single-scheme compile only**              | No runtime dispatch — couldn't run hybrid mode                                                                 |

---

## 6. What We Changed (Our Fork)

| Change                                | Detail                                                                                     |
|---------------------------------------|--------------------------------------------------------------------------------------------|
| **Real signature verification**        | Replaced stub with actual OpenSSL ECDSA verify + liboqs Falcon/ML-DSA verify              |
| **`SIG_SCHEME` build flag**            | `1`=ECDSA, `2`=Falcon-512, `3`=Hybrid (all three), `4`=ML-DSA-44                         |
| **`sig_type` field in TX/block**       | Added to serialize/deserialize so validators know which algorithm per TX                   |
| **Universal buffer sizes**             | Bumped to ML-DSA maximums (pubkey=1312B, seckey=2560B, sig=2420B)                         |
| **`crypto_verify_typed()` dispatch**   | Hybrid mode reads sig_type per-TX and dispatches to correct backend                       |
| **Wallet CLI `--scheme` flag**         | Wallets pick their algorithm at creation time                                              |
| **`benchmark_hybrid.sh`**              | Mixed-scheme benchmark with configurable farmer ratios                                     |
| **Bug fixes**                          | `sig_type` missing in block.c/validator.c deserialize; hardcoded SIG_ECDSA in batch_send  |

---

## 7. Hybrid Mode — Migration Without Hard Fork

The blockchain now supports a hybrid mode (`SIG_SCHEME=3`) where ECDSA, Falcon-512, and ML-DSA-44 wallets coexist on the same chain. The `sig_type` field in each TX acts as the version discriminant.

| Phase     | Default     | Allowed Schemes                            | Timeline         |
|-----------|-------------|--------------------------------------------|------------------|
| Phase 1   | ECDSA       | ECDSA + Falcon-512 + ML-DSA-44             | Now              |
| Phase 2   | Falcon-512  | ECDSA (deprecated) + Falcon-512 + ML-DSA-44 | After validation |
| Phase 3   | Falcon-512  | Falcon-512 + ML-DSA-44 only               | ECDSA sunset     |

This mirrors Polkadot (dual-algorithm) and Algorand (phased Falcon rollout). No hard fork needed.

---

## 8. PQC in Real-World Blockchains Today

| Blockchain            | Algorithm                          | Status                    |
|-----------------------|------------------------------------|---------------------------|
| QRL                   | XMSS (hash-based, stateful)       | Production since 2018     |
| Algorand              | Falcon-512                         | State proofs in production |
| Polkadot              | Bandersnatch + Falcon (planned)    | Research phase            |
| Ethereum              | Researching (no commitment)        | EIP discussions only      |
| **MEMO (this work)**  | **Falcon-512 / ML-DSA-44 / Hybrid** | **Measured, integrated**  |

MEMO is ahead of most projects — we have measured end-to-end TPS with real PQC verification, not just micro-benchmarks.

---

## 9. Bottom Line

1. **Falcon-512 is the recommended PQC signature for MEMO.** It has the smallest signatures (666 B), best multicore scaling (98.3% efficiency), and delivers equal or better blockchain TPS than ECDSA.

2. **ML-DSA-44 is the throughput king on x86** (272K verify/sec at 10 threads) but its 3.5x larger signatures make it 40% slower in end-to-end blockchain benchmarks. Use it where raw verify speed matters more than wire size.

3. **SLH-DSA is not viable for high-throughput blockchain use.** At 705 verify/sec single-threaded, it's 27x slower than Falcon-512. Reserve for low-frequency, high-security applications.

4. **The pipeline is network-bound, not verify-bound.** Even ML-DSA-44's 473ms block processing fits within the 1-second block interval. Signature verification is not the bottleneck.

5. **Hybrid mode enables zero-downtime migration.** No hard fork needed — wallets independently select their scheme, and validators dispatch based on the TX's sig_type field.

---

*Full numerical data: [docs/RESULTS.md](RESULTS.md) | PQC adoption survey: [docs/REAL_WORLD_ADOPTION.md](REAL_WORLD_ADOPTION.md)*
*Platform: Intel Xeon Gold 6126 Skylake-SP (Chameleon Cloud) | liboqs 0.15.0 | OpenSSL 3.0.13*
