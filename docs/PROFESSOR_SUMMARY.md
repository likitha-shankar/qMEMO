# qMEMO — Progress Summary for Professor

**Likitha Shankar | IIT Chicago | 2026-03-24**

This document summarizes all work completed since the March 21 meeting and provides a quick-reference guide to where each result lives in the repository.

---

## 1. What Was Done (Since March 21)

| Task                                    | Status    | Where in Repo                                                    |
|-----------------------------------------|-----------|------------------------------------------------------------------|
| ECDSA baseline with real OpenSSL verify | Done      | `docs/RESULTS.md` §10, `benchmarks/src/classical_benchmark.c`    |
| Falcon-512 blockchain integration       | Done      | `docs/RESULTS.md` §10, fork of `blockchain_pos_v45` on Chameleon |
| ML-DSA-44 blockchain integration        | Done      | `docs/RESULTS.md` §10, fork of `blockchain_pos_v45` on Chameleon |
| Hybrid mode (SIG_SCHEME=3)              | Done      | `docs/FINDINGS.md` §7, `docs/RESULTS.md` §11                     |
| Multicore scaling (1–10 cores, 7 algos) | Done      | `docs/RESULTS.md` §5, `docs/FINDINGS.md` §2                      |
| Signature size analysis                 | Done      | `docs/RESULTS.md` §4, `benchmarks/src/signature_size_analysis.c` |
| **1M TX production-scale benchmark**    | **Done**  | `docs/RESULTS.md` §12, `blockchain/benchmark_results/`            |
| **Apr 18 repeat matrix (18 runs)**      | **Done**  | `docs/RESULTS.md` §13, `benchmarks/results/hybrid_matrix_apr18_final/` |

---

## 2. Blockchain End-to-End TPS (Chameleon Cloud, Xeon Gold 6126)

### March 2026 — Config: k=16, 10 farmers, 15 warmup blocks, 8 threads/farmer, 1s block interval

| Scheme         | 500 TX e2e TPS | 1000 TX e2e TPS | Block Time (1000 TX) | Sig Size (B)  | Confirm Rate |
|----------------|---------------:|----------------:|---------------------:|--------------:|:------------:|
| ECDSA (stub)   |          1,218 |           2,223 |                35 ms |            48 |         100% |
| ECDSA (real)   |          1,190 |           1,403 |                   —  |            48 |         100% |
| **Falcon-512** |      **1,223** |       **2,572** |           **200 ms** |       **655** |     **100%** |
| ML-DSA-44      |          1,234 |           1,533 |              473 ms  |         2,420 |         100% |

**Key finding:** Falcon-512 matches or exceeds ECDSA — no TPS penalty. ML-DSA-44 is 40% below Falcon at 1000 TX because its 3.5x larger signatures bottleneck serialization/I/O, not verification.

### April 2026 — Production-Scale: Falcon-512, 1 farmer, max_txs_per_block=100K

| Scale    | e2e TPS    | Time    | Confirm Rate | Errors |
|---------:|-----------:|--------:|:------------:|:------:|
| 10,000   | 3,165      | 3.2 sec | 100%         | 0      |
| 1,000,000 (run 1) | 10,378 | 96.4 sec | 100% | 0 |
| 1,000,000 (run 2) | 10,923 | 91.5 sec | 100% | 0 |
| **1,000,000 (run 3)** | **11,182** | **89.4 sec** | **100%** | **0** |

**Key finding:** At production scale (1M TX), Falcon-512 achieves 11,182 TPS with 100% confirmation across 3 independent runs. TPS scales 4.3× from 1K to 1M transactions as fixed per-block overhead amortizes over larger batches. Raw CSV files: `blockchain/benchmark_results/benchmark_20260415_*.csv`.

### April 18 2026 — Repeat Matrix (k=16, 1 farmer, 1s block, 8 threads, batch=64)

| Scheme | 100K mean TPS | 1M mean TPS | Runs | Confirmation | Errors |
|--------|--------------:|------------:|-----:|:------------:|:------:|
| Ed25519 (`s1`) | 7,046.12 | 8,749.78 | 6 | 100% | 0 |
| Falcon-512 (`s2`) | 6,693.70 | 8,776.68 | 6 | 100% | 0 |
| ML-DSA-44 (`s4`) | 6,558.37 | 8,744.08 | 6 | 100% | 0 |

All 18 runs are valid in the canonical final bundle (`benchmarks/results/hybrid_matrix_apr18_final/`), with one failed intermediate ML-DSA run replaced by a successful retry before finalization.

---

## 3. Single-Core Verification Throughput (ops/sec)

| Algorithm         | ARM (M2 Pro) | x86 (Skylake-SP)  | x86 (Cascade Lake) | vs ECDSA | vs Reference |
|-------------------|-------------:|------------------:|--------------------:|---------:|:------------:|
| ML-DSA-44         |       25,904 |            46,532 |              49,060 |    ~19x  |    ~93%      |
| Falcon-512        |       30,569 |            23,505 |              23,877 |     ~9x  |    ~84%      |
| Ed25519           |        8,857 |             8,309 |               9,013 |     ~3x  |  ~46–21%     |
| ECDSA secp256k1   |        4,026 |             2,467 |               2,963 |     1.0x |    ~38%      |
| SLH-DSA-SHA2-128f |          599 |               732 |                 734 |   ~0.3x  |    ~13%      |

*"vs Reference" compares our Skylake-SP numbers against published benchmarks (see §3a below).*

---

## 3a. Official Reference Benchmarks

How our measured values compare to published/optimized reference implementations:

| Algorithm         | Our Verify (Skylake-SP) | Reference Verify     | Reference Platform      | Source                  |
|-------------------|------------------------:|---------------------:|-------------------------|-------------------------|
| Falcon-512        |          23,505 ops/sec | ~28,000 ops/sec      | Skylake x86-64 (AVX2)   | falcon-sign.info        |
| ML-DSA-44         |          46,532 ops/sec | ~50,000 ops/sec      | Intel Core Ultra 7 155H | Hacken / NIST PQC Round 3 |
| SLH-DSA-SHA2-128f |             732 ops/sec | ~5,500 ops/sec       | Skylake x86-64 (AVX2)   | NIST IR 8413            |
| ECDSA secp256k1   |           2,467 ops/sec | ~6,566 ops/sec       | OpenSSL 3.x reference   | OpenSSL Cookbook         |
| Ed25519           |           8,309 ops/sec | ~18,000–39,683 ops/sec | Intel (varies)        | eBACS / SUPERCOP        |

**Why our numbers differ from published references:**

- **Falcon-512 (~84% of ref):** Our liboqs 0.15.0 build uses portable C, not the hand-optimized AVX2 assembly from the Falcon reference implementation. The 16% gap is consistent with portable-vs-optimized builds.
- **ML-DSA-44 (~93% of ref):** Closest to reference — liboqs enables AVX-512 for lattice operations on our Xeon, and the algorithm is heavily optimized in CRYSTALS-Dilithium.
- **SLH-DSA (~13% of ref):** Hash-based schemes benefit enormously from AVX2/AVX-512 tree hashing. Our portable build misses these optimizations, explaining the large gap.
- **ECDSA (~38% of ref):** Our OpenSSL 3.0.13 (Ubuntu system package) is older than the reference; newer OpenSSL versions include significant secp256k1 assembly speedups.
- **Ed25519 (~46% of ref):** eBACS numbers include top-tier hand-tuned implementations (e.g., SUPERCOP's `amd64-64-24k`); our OpenSSL EVP path is general-purpose.

*Sources: [falcon-sign.info](https://falcon-sign.info), NIST IR 8413 (2022), [eBACS](https://bench.cr.yp.to), OpenSSL Cookbook.*

---

## 4. Multicore Scaling (Xeon Gold 6126, core-pinned)

| Algorithm         | 1 Core | 4 Cores | 10 Cores | Speedup | Efficiency |
|-------------------|-------:|--------:|---------:|--------:|-----------:|
| Falcon-512        | 19,022 |  74,960 |  183,292 |   9.64x |     96.4%  |
| ML-DSA-44         | 42,935 | 105,975 |  277,987 |   6.47x |     64.7%  |
| ECDSA secp256k1   |  2,445 |   9,244 |   21,342 |   8.73x |     87.3%  |
| Ed25519           |  8,249 |  29,587 |   69,420 |   8.42x |     84.2%  |

Falcon-512 achieves best scaling efficiency (96.4%) — compact FFT working set fits in per-core L2 cache.

---

## 5. Transaction Size on the Wire

| Scheme         | Sig (B) | PubKey (B) | TX Wire Size (B) | vs ECDSA |
|----------------|--------:|-----------:|-----------------:|---------:|
| ECDSA          |      48 |         65 |             ~112 |     1.0x |
| Falcon-512     |    ~655 |        897 |           ~1,611 |    14.4x |
| ML-DSA-44      |   2,420 |      1,312 |           ~3,791 |    33.8x |

---

## 6. Changes Made to Harsha's blockchain_pos_v45

| Change                             | Detail                                                            |
|------------------------------------|-------------------------------------------------------------------|
| Real signature verification        | Replaced stub with OpenSSL ECDSA + liboqs Falcon/ML-DSA verify    |
| `SIG_SCHEME` build flag            | 1=ECDSA, 2=Falcon-512, 3=Hybrid, 4=ML-DSA-44                      |
| `sig_type` in TX wire format       | Validators know which algorithm per TX                            |
| Universal buffer sizes             | Bumped to ML-DSA maximums (pubkey 1312B, sig 2420B)               |
| `crypto_verify_typed()` dispatch   | Hybrid mode dispatches to correct backend per TX                  |
| Wallet CLI `--scheme` flag         | Wallets pick algorithm at creation time                           |
| Bug fixes                          | sig_type serialization, hardcoded SIG_ECDSA in batch_send         |

---

## 7. Recommendation

**Falcon-512 is the recommended PQC signature for MEMO.**
- Smallest PQC signatures (666 B max) — 3.5x smaller than ML-DSA-44
- Best multicore scaling (96.4% efficiency at 10 cores)
- Equal or better blockchain TPS than ECDSA
- Hybrid mode enables zero-downtime migration (no hard fork)

---

## 8. Remaining Work

| Item                          | Target Date     | Notes                                                 |
|-------------------------------|-----------------|-------------------------------------------------------|
| Hybrid end-to-end benchmarks  | In progress     | SIG_SCHEME=3, mixed ECDSA+Falcon+ML-DSA workload      |
| Contact Harsha                | By 2026-03-27   | Coordinate on hybrid design, version-field approach   |
| SLH-DSA integration           | If time permits | Very large sigs (17 KB) — likely impractical          |

---

## Repository Map (Quick Reference)

| What                              | Path                                          |
|-----------------------------------|-----------------------------------------------|
| All benchmark results (tables)    | `docs/RESULTS.md`                             |
| Key findings & recommendations    | `docs/FINDINGS.md`                            |
| Benchmark C source files          | `benchmarks/src/`                             |
| Build system                      | `benchmarks/Makefile`                         |
| Blockchain fork (on Chameleon)    | `~/memo-baseline/blockchain_pos_v45/`         |
| GitHub repo                       | https://github.com/likitha-shankar/qMEMO      |

*Platform: Intel Xeon Gold 6126 Skylake-SP (Chameleon Cloud) | liboqs 0.15.0 | OpenSSL 3.0.13*
