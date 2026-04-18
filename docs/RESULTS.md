# qMEMO — Complete Benchmark Results

**Graduate Research, Illinois Institute of Technology, Chicago**
**Repository:** https://github.com/likitha-shankar/qMEMO

> All measurements use liboqs 0.15.0 (portable build) and OpenSSL 3.x.
> Timing: `clock_gettime(CLOCK_MONOTONIC)`, nanosecond precision.
> Statistical runs: 1,000 trials x 100 ops; median and IQR reported (distributions are non-Gaussian).

---

## 1. Signature Verification Throughput (Single Core)

| Algorithm           | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|---------------------|---------------:|-------------------------:|-----------------------:|
| Falcon-512          |         30,569 |                   23,877 |                 23,505 |
| Falcon-1024         |         15,618 |                   11,794 |                 11,618 |
| ML-DSA-44           |         25,904 |               **49,060** |             **46,532** |
| ML-DSA-65           |         15,369 |               **30,287** |             **28,893** |
| SLH-DSA-SHA2-128f   |            599 |                      734 |                    732 |
| ECDSA secp256k1     |          4,026 |                    2,963 |                  2,467 |
| Ed25519             |          8,857 |                    9,013 |                  8,309 |

All values in ops/sec. Bold = fastest platform for that algorithm.

> ML-DSA-44/65 are ~1.8-1.9x faster on x86 than ARM due to liboqs AVX-512 NTT optimizations.
> Falcon-512 is ~24% faster on M2 Pro (NEON + deeper OOO pipeline).
> The two Xeon generations agree within 5% for all PQC algorithms.

**Statistical validation (Falcon-512 verify, 1,000 trials x 100 ops):**

| Platform       |  Median |   Mean |    CV | Normality (JB) |
|----------------|--------:|-------:|------:|:---------------:|
| M2 Pro         |  31,133 | 30,883 | 3.92% | Fail            |
| Cascade Lake   |  24,016 | 23,988 | 0.66% | Fail            |
| Skylake-SP     |  23,696 |      — | 0.59% | —               |

---

## 2. Signing Throughput (Single Core)

| Algorithm           | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|---------------------|---------------:|-------------------------:|-----------------------:|
| Falcon-512          |          4,805 |                    4,312 |                  4,207 |
| Falcon-1024         |          2,436 |                    2,133 |                  2,130 |
| ML-DSA-44           |         10,273 |                   15,975 |                 14,271 |
| ML-DSA-65           |          6,745 |                    9,971 |                  8,696 |
| SLH-DSA-SHA2-128f   |             36 |                       45 |                     43 |
| ECDSA secp256k1     |          3,608 |                    2,638 |                  2,181 |
| Ed25519             |         24,276 |                   23,184 |                 21,481 |

All values in ops/sec.

---

## 3. Key Generation Throughput (Single Core)

| Algorithm           | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|---------------------|---------------:|-------------------------:|-----------------------:|
| Falcon-512          |            148 |                      153 |                    147 |
| Falcon-1024         |             46 |                       52 |                     51 |
| ML-DSA-44           |         24,610 |                   51,917 |                 47,476 |
| ML-DSA-65           |         14,327 |                   29,832 |                 27,961 |
| SLH-DSA-SHA2-128f   |            836 |                    1,062 |                  1,001 |
| ECDSA secp256k1     |          3,595 |                    2,655 |                  2,189 |
| Ed25519             |         24,483 |                   23,062 |                 21,138 |

All values in ops/sec. Falcon keygen is 100-300x slower than ML-DSA due to the complex NTRU lattice key generation.

---

## 4. Signature and Key Sizes

| Algorithm           | Type             | NIST Level | Public Key (B) | Secret Key (B) | Signature (B) |
|---------------------|------------------|:----------:|---------------:|---------------:|--------------:|
| Falcon-512          | Lattice (NTRU)   |          1 |            897 |          1,281 |       666 max |
| Falcon-1024         | Lattice (NTRU)   |          5 |          1,793 |          2,305 |     1,280 max |
| ML-DSA-44           | Module Lattice   |          2 |          1,312 |          2,560 |         2,420 |
| ML-DSA-65           | Module Lattice   |          3 |          1,952 |          4,032 |         3,309 |
| SLH-DSA-SHA2-128f   | Hash-based       |          1 |             32 |             64 |        17,088 |
| ECDSA secp256k1     | Elliptic Curve   |          — |             65 |             32 |    ~72 (DER)  |
| Ed25519             | EdDSA            |          — |             32 |             32 |            64 |

Falcon-512 has the smallest signature of any NIST PQC scheme (3.6x smaller than ML-DSA-44).

**Falcon-512 signature size distribution (10,000 samples):**

| Variant                  |   Mean |  Max Observed | Std Dev | Notes                             |
|--------------------------|-------:|--------------:|--------:|-----------------------------------|
| Falcon-512 (unpadded)    | ~655 B |         666 B |  ~2.2 B | Compression effective             |
| Falcon-512 (padded)      |  666 B |         666 B |       0 | Constant-time, side-channel safe  |
| Falcon-1024 (unpadded)   |~1,271 B|       1,280 B |       — | Within FIPS 206 bound             |
| Falcon-1024 (padded)     |1,280 B |       1,280 B |       0 | Constant-time                     |

---

## 5. Multicore Scaling

### 5.1 Apple M2 Pro (ARM64) — All 7 Algorithms (Measured)

> Source: `multicore_all_benchmark`, run 2026-03-22. 1,000 verify iterations/thread, barrier-synchronized.

| Algorithm           | 1 Thread | 2 Threads | 4 Threads | 6 Threads | 8 Threads | 10 Threads | Speedup    | Efficiency |
|---------------------|--------:|---------:|---------:|---------:|---------:|----------:|:----------:|:----------:|
| Falcon-512          |  32,237 |   78,867 |  168,123 |  251,277 |  249,493 |   298,837 | **9.27x**  | **92.7%**  |
| Falcon-1024         |  22,487 |   44,062 |   86,473 |  126,079 |  136,932 |   161,663 | 7.19x      | 71.9%      |
| ML-DSA-44           |  36,760 |   72,056 |  139,581 |  203,107 |  205,423 |   237,670 | 6.47x      | 64.7%      |
| ML-DSA-65           |  21,704 |   43,036 |   82,798 |  121,087 |  135,023 |   151,430 | 6.98x      | 69.8%      |
| SLH-DSA-SHA2-128f   |     840 |    1,650 |    3,224 |    4,787 |    5,414 |     5,457 | 6.50x      | 65.0%      |
| ECDSA secp256k1     |   5,672 |   10,629 |   18,213 |   26,466 |   27,863 |    35,676 | 6.29x      | 62.9%      |
| Ed25519             |  12,583 |   24,320 |   47,200 |   71,038 |   80,741 |    89,732 | 7.13x      | 71.3%      |

All values in ops/sec. Falcon-512 scales best (compact FFT working set fits in L1 cache).
M2 Pro's heterogeneous architecture (6P + 4E cores) causes an efficiency drop at 8+ threads.

### 5.2 Intel Xeon Gold 6242 — Cascade Lake (Falcon-512, Measured)

| Threads | ops/sec | Speedup    | Efficiency |
|--------:|--------:|:----------:|:----------:|
|       1 |  20,169 | 1.00x      | 100.0%     |
|       2 |  38,399 | 1.90x      | 95.2%      |
|       4 |  74,909 | 3.71x      | 92.9%      |
|       6 | 108,256 | 5.37x      | 89.5%      |
|       8 | 149,046 | 7.39x      | 92.4%      |
|      10 | 184,467 | **9.15x**  | **91.5%**  |

Homogeneous core design produces clean, predictable linear scaling.

### 5.3 Intel Xeon Gold 6126 — Skylake-SP (Falcon-512, Measured)

| Threads | ops/sec | Speedup    | Efficiency |
|--------:|--------:|:----------:|:----------:|
|       1 |  18,853 | 1.00x      | 100.0%     |
|       2 |  37,855 | 2.01x      | 100.4%     |
|       4 |  61,380 | 3.26x      | 81.4%      |
|       6 | 106,183 | 5.63x      | 93.9%      |
|       8 | 143,833 | 7.63x      | 95.4%      |
|      10 | 176,890 | **9.38x**  | **93.8%**  |

4-thread anomaly (81.4%) is a known Skylake-SP mesh interconnect effect; recovers at 6+ threads.

### 5.3.1 Intel Xeon Gold 6126 — Skylake-SP — All 7 Algorithms (Measured, Core-Pinned)

> Source: `multicore_all_benchmark`, run 2026-03-23T20:52:17Z. 1,000 verify iterations/thread (100 for SLH-DSA), barrier-synchronized. Each thread pinned to a distinct physical core via `sched_setaffinity`.

| Algorithm           | 1 Core  | 2 Cores | 4 Cores | 6 Cores | 8 Cores | 10 Cores | Speedup    | Efficiency |
|---------------------|--------:|--------:|--------:|--------:|--------:|---------:|:----------:|:----------:|
| ML-DSA-44           |  42,935 |  56,428 | 105,975 | 162,719 | 207,663 |  277,987 | 6.47x      | 64.7%      |
| ML-DSA-65           |  27,900 |  37,803 |  88,961 | 126,921 | 166,740 |  211,604 | 7.58x      | 75.8%      |
| Falcon-512          |  19,022 |  32,375 |  74,960 | 111,099 | 147,920 |  183,292 | **9.64x**  | **96.4%**  |
| Falcon-1024         |  11,157 |  18,617 |  42,106 |  58,957 |  79,554 |   98,467 | 8.83x      | 88.3%      |
| Ed25519             |   8,249 |  14,134 |  29,587 |  43,357 |  56,367 |   69,420 | 8.42x      | 84.2%      |
| ECDSA secp256k1     |   2,445 |   4,698 |   9,244 |  13,551 |  17,302 |   21,342 | 8.73x      | 87.3%      |
| SLH-DSA-SHA2-128f   |     691 |   1,207 |   2,365 |   3,741 |   4,648 |    6,101 | 8.83x      | 88.3%      |

All values in ops/sec, sorted by 10-core throughput. Falcon-512 achieves the best scaling efficiency (96.4%) — its compact FFT working set fits cleanly in per-core L2 cache. ML-DSA-44 has the highest absolute throughput (278K ops/sec at 10 cores) but lower scaling efficiency (64.7%) because its high single-core throughput (43K) leaves less headroom for parallel speedup. Classical algorithms (ECDSA 87.3%, Ed25519 84.2%) scale well but trail Falcon-512.

### 5.4 Cross-Platform Summary (Falcon-512, 10 Threads)

| Platform       | 10-Thread ops/sec | Speedup    | Efficiency |
|----------------|------------------:|:----------:|:----------:|
| M2 Pro         |           298,837 | 9.27x      | 92.7%      |
| Cascade Lake   |           184,467 | 9.15x      | 91.5%      |
| Skylake-SP     |           183,292 | 9.64x      | 96.4%      |

Implied serial fraction (Amdahl's Law): ~0.4% on Skylake-SP. Verification is embarrassingly parallel.
Skylake-SP value from core-pinned run (2026-03-23T20:52:17Z, sched_setaffinity); prior non-pinned run measured 176,890.

### 5.5 Scaling Analysis — Cross-Algorithm and Cross-Platform

**Key finding: All 7 algorithms scale well under core pinning. PQC schemes are not scaling-limited.**

The Skylake-SP dataset (Section 5.3.1) is the canonical cross-algorithm comparison on server hardware, covering all 7 algorithms at 6 thread counts on homogeneous Xeon cores with `sched_setaffinity` pinning. The M2 Pro dataset (Section 5.1) provides a complementary ARM64 comparison. Key observations:

1. **Falcon-512 achieves the best scaling efficiency at 96.4%.** With 9.64x speedup at 10 cores, Falcon-512's compact FFT working set fits in per-core L2 cache, minimizing cross-core contention. The implied serial fraction is ~0.4% (Amdahl's Law). Other algorithms range from 6.5x to 8.8x speedup, with Falcon-1024 and SLH-DSA tied at 8.83x (88.3%), followed by ECDSA (8.73x, 87.3%), Ed25519 (8.42x, 84.2%), ML-DSA-65 (7.58x, 75.8%), and ML-DSA-44 (6.47x, 64.7%).

2. **ML-DSA-44 has the highest absolute throughput but lowest scaling efficiency.** At 278K ops/sec (10 cores), ML-DSA-44 dominates all other algorithms in raw throughput. However, its 64.7% efficiency is the lowest of the group because its high single-core throughput (43K ops/sec) saturates memory bandwidth at higher core counts, limiting the parallel speedup ratio. The absolute throughput still increases monotonically with cores.

3. **Classical algorithms scale respectably but trail PQC leaders.** ECDSA secp256k1 (8.73x, 87.3%) and Ed25519 (8.42x, 84.2%) both scale well under core pinning. Falcon-512 outscales both, while Falcon-1024 and SLH-DSA match them. The scaling difference is not a barrier to PQC adoption.

4. **The efficiency drop at 8+ threads on M2 Pro is architectural, not algorithmic.** The M2 Pro has 6 performance cores and 4 efficiency cores (heterogeneous big.LITTLE). At 8+ threads, work spills onto E-cores (~60% the throughput of P-cores), reducing efficiency uniformly across all algorithms. The Skylake-SP results (Section 5.3.1) confirm that homogeneous cores with pinning maintain 65–96% efficiency at 10 cores.

5. **Falcon-512 has the best scaling profile for server deployment.** At 96.4% efficiency on Skylake-SP, Falcon-512 is near-perfectly parallel. A 10-core Falcon-512 validator achieves 183K verify/sec on Skylake-SP — 73x above the 2,500 ops/sec per-shard requirement.

6. **Practical implication for MEMO:** The choice of PQC algorithm involves a throughput-vs-scaling tradeoff. ML-DSA-44 delivers the highest absolute throughput (43K single-core, 278K at 10 cores) but scales at only 64.7%. Falcon-512 scales near-perfectly (96.4%) with the smallest PQC signatures (~655 B). For maximum throughput, use ML-DSA-44. For best scaling efficiency and smallest on-chain footprint, use Falcon-512.

---

## 6. Official Specifications vs Measured (Validation)

### 6.1 Falcon-512

| Metric           | Official (i5 @ 2.3 GHz, AVX2) | M2 Pro @ 3.5 GHz | Xeon 6242 @ 2.8 GHz |
|------------------|-------------------------------:|------------------:|---------------------:|
| Verify ops/sec   |                         27,939 |            31,133 |               23,885 |
| Verify cycles    |                         82,339 |   ~112,400 (est.) |      146,778 (RDTSC) |
| Public key       |                          897 B |             897 B |                897 B |
| Secret key       |                        1,281 B |           1,281 B |              1,281 B |
| Sig size (max)   |                          666 B |             666 B |                666 B |

### 6.2 Frequency-Normalized Comparison (Falcon-512 Verify)

| Platform                           | Measured | Frequency | Normalized to 2.3 GHz | vs Official |
|------------------------------------|:--------:|:---------:|:---------------------:|:-----------:|
| Intel i5-8259U (official)          |   27,939 |   2.3 GHz |                27,939 | 1.00x       |
| Apple M2 Pro (this work)           |   31,133 |   3.5 GHz |                20,477 | 0.73x       |
| Intel Xeon Gold 6242 (this work)   |   23,885 |   2.8 GHz |                19,620 | 0.70x       |

The 27-30% cycle-efficiency gap is explained by the portable vs AVX2-optimized implementation path.

### 6.3 ML-DSA-44

| Metric           | Official (AVX2, i7 @ 2.6 GHz) | M2 Pro  | Xeon 6242 |
|------------------|-------------------------------:|--------:|----------:|
| Verify ops/sec   |                         21,966 |  25,904 |    48,627 |
| Public key       |                        1,312 B | 1,312 B |   1,312 B |
| Secret key       |                        2,560 B | 2,560 B |   2,560 B |
| Signature        |                        2,420 B | 2,420 B |   2,420 B |

Xeon exceeds the official reference by 121% due to liboqs AVX-512 NTT implementation.

### 6.4 Validation Verdict

| Check                                       | M2 Pro | Xeon 6242 | Status   |
|----------------------------------------------|:------:|:---------:|:--------:|
| Key sizes match spec                         |    Yes |       Yes | PASS     |
| Sig sizes within max                         |    Yes |       Yes | PASS     |
| Throughput correct order of magnitude        |    Yes |       Yes | PASS     |
| Performance gap explained (portable impl)    |   -27% |      -30% | EXPECTED |
| Single-pass vs statistical agreement         |   1.2% |     0.96% | PASS     |
| Correctness (key_inspection)                 |   PASS |      PASS | PASS     |

---

## 7. Blockchain Baseline (Classical, Stub Verify)

> Platform: Intel Xeon Gold 6126 Skylake-SP, 48 logical cores, 187 GB RAM, Ubuntu 24.04
> Source: Harsha's `blockchain_pos_v45`, ECDSA secp256k1, stub verification (no crypto check)
> Config: k=16, 10 farmers, 8 OpenMP threads/farmer, batch size 64, 1s block interval
> Date: 2026-03-10

### 7.1 Micro-Benchmarks

| Operation                          | Latency (us) | Throughput (ops/sec) | Notes                  |
|------------------------------------|-------------:|---------------------:|------------------------|
| GPB TX serialize                   |         0.17 |            5,910,655 | Protobuf-C             |
| GPB TX deserialize                 |         0.50 |            1,994,360 |                        |
| GPB Block serialize (100 tx)       |        14.18 |               70,510 | 11,452 B wire          |
| GPB Block deserialize (100 tx)     |        65.75 |               15,209 |                        |
| ZMQ inproc RTT                     |         5.78 |              173,155 | Same-process           |
| ZMQ TCP loopback RTT               |       167.58 |                5,967 | 29x slower than inproc |
| BLAKE3 hash (256 B)                |         1.13 |              886,011 | ~227 MB/s effective    |
| Proof search (k=16)                |         0.02 |           42,194,093 | Binary search in plot  |
| Plot generation (k=16)             |        72.71 |                    — | One-time, 65,536 entries|

### 7.2 End-to-End Transaction Throughput

| Metric                  |    500 TX |  1000 TX |
|-------------------------|----------:|---------:|
| Submission TPS          |     3,260 |    5,659 |
| Confirmation TPS        |     1,943 |    3,662 |
| **End-to-end TPS**      | **1,218** |**2,223** |
| Confirmation rate       |      100% |     100% |
| Block processing time   |     78 ms |    35 ms |
| Blocks used             |         1 |        1 |

Block processing time drops at higher load (batching amortizes fixed costs).

### 7.3 Validator Step Timing (per block)

| Step                                     | Time      |
|------------------------------------------|-----------|
| GET_LAST_HASH (blockchain query)         | 0-1 ms    |
| Pool-to-Validator (fetch pending TX)     | 9-13 ms   |
| Serialize + Send block                   | 1-5 ms    |

Pool-to-Validator fetch (ZMQ TCP + protobuf deserialize) dominates the critical path.

### 7.4 Transaction Structure

| Scheme                      | Sig bytes (wire) | TX struct size | vs baseline |
|-----------------------------|:----------------:|:--------------:|:-----------:|
| ECDSA secp256k1 (current)   | 48 (truncated DER)|     112 bytes | baseline    |
| Falcon-512                  |              666 |     ~730 bytes | +6.5x       |
| ML-DSA-44                   |            2,420 |   ~2,484 bytes | +22.2x      |

---

## 8. MEMO Headroom Analysis

Assumes MEMO requires up to 2,500 verifications/second per shard (10,000 TPS across 4 shards).

| Configuration                          | Throughput (ops/sec) | Headroom vs 2,500 |
|----------------------------------------|---------------------:|:-----------------:|
| Falcon-512, 1 core, Xeon 6242         |               23,877 | **9.5x**          |
| Falcon-512, 1 core, M2 Pro            |               31,133 | **12.5x**         |
| Falcon-512, 10 threads, Xeon 6242     |              184,467 | **73.8x**         |
| Falcon-512, 10 threads, M2 Pro        |              298,837 | **119.5x**        |
| ML-DSA-44, 1 core, Xeon 6242          |               49,060 | **19.6x**         |

Verification is not a throughput bottleneck at any tested configuration.

**Projected end-to-end TPS with Falcon-512 integration:**

| Metric                               | Classical Baseline (stub) | Projected (Falcon-512) |
|---------------------------------------|:-------------------------:|:----------------------:|
| End-to-end TPS                        |                     2,223 | ~1,500-1,800           |
| TPS reduction                         |                         — | -20-30%                |
| Single-thread verify time (1000 tx)   |                      0 ms | ~42 ms                 |
| 8-thread verify time (1000 tx)        |                      0 ms | ~5 ms                  |

Even with 30% TPS reduction, throughput remains 100x above minimum requirements.

> **Update (2026-03-22):** Measured results in Section 10 show **no TPS reduction** — Falcon-512 achieves 2,572 e2e TPS (1000 TX), exceeding both the stub baseline (2,223) and real ECDSA verification (1,403). The pipeline is network/coordination-bound, not verify-bound.
>
> **Update (2026-03-23):** ML-DSA-44 achieves 1,533 e2e TPS (1000 TX) — 40% below Falcon-512 despite 2x faster raw verification, because its 3.5x larger signatures (2,420 B vs ~655 B) increase serialization and I/O cost. Both PQC schemes remain well above minimum requirements.

---

## 9. Platform Specifications

| Parameter    | Apple M2 Pro                   | Xeon Gold 6242                  | Xeon Gold 6126                  |
|--------------|--------------------------------|---------------------------------|---------------------------------|
| Architecture | ARM64 (Avalanche/Blizzard)     | x86-64 (Cascade Lake)           | x86-64 (Skylake-SP)            |
| Cores        | 10 (6P + 4E)                   | 16 phys / 32 logical            | 24 phys / 48 logical            |
| Base clock   | 3.49 GHz (P-cores)             | 2.80 GHz                        | 2.60 GHz                        |
| RAM          | 16 GB LPDDR5                   | 187 GB DDR4 ECC                 | 187 GB DDR4 ECC                 |
| OS           | macOS Darwin arm64             | Ubuntu 22.04                    | Ubuntu 24.04                    |
| Compiler     | Apple Clang 17.0, `-O3`       | GCC 11.4, `-O3 -march=native`  | GCC 13.3, `-O3 -march=native`  |
| liboqs       | 0.15.0                         | 0.15.0                          | 0.15.0                          |
| OpenSSL      | 3.6.1 (Homebrew)               | 3.0.2                           | 3.0.13                          |
| Location     | Local (development)            | Chameleon Cloud                 | Chameleon Cloud                 |
| SIMD         | NEON (128-bit)                 | AVX-512 (512-bit)               | AVX-512 (512-bit)               |
| Cycle counter| wall-clock estimate            | RDTSC (exact)                   | RDTSC (exact)                   |

---

## 10. Blockchain Integration Benchmarks (Measured)

> Platform: Intel Xeon Gold 6126 Skylake-SP, 48 logical cores, 187 GB RAM, Ubuntu 24.04
> Source: Harsha's `blockchain_pos_v45`, built with `SIG_SCHEME=1` (ECDSA), `SIG_SCHEME=2` (Falcon-512), or `SIG_SCHEME=4` (ML-DSA-44) via liboqs 0.15.0
> Config: k=16, 10 farmers, 15 warmup blocks, 8 OpenMP threads/farmer, batch size 64, 1s block interval
> Date: 2026-03-22 (ECDSA, Falcon-512), 2026-03-23 (ML-DSA-44)

### 10.1 End-to-End Transaction Throughput

| Metric                  | ECDSA (stub verify) | ECDSA (real verify) | Falcon-512 (real verify) | ML-DSA-44 (real verify) |
|-------------------------|:-------------------:|:-------------------:|:------------------------:|:-----------------------:|
| **500 TX**              |                     |                     |                          |                         |
| Submission TPS          |               3,260 |               3,023 |                    3,727 |                   3,613 |
| **End-to-end TPS**      |           **1,218** |           **1,190** |                **1,223** |               **1,234** |
| Confirmation rate       |                100% |                100% |                     100% |                    100% |
| Block processing time   |               78 ms |                   — |                   134 ms |                  267 ms |
| Blocks used             |                   1 |                   — |                        1 |                       1 |
| **1000 TX**             |                     |                     |                          |                         |
| Submission TPS          |               5,659 |               5,640 |                    8,524 |                   5,586 |
| **End-to-end TPS**      |           **2,223** |           **1,403** |                **2,572** |               **1,533** |
| Confirmation rate       |                100% |                100% |                     100% |                    100% |
| Block processing time   |               35 ms |                   — |                   200 ms |                  473 ms |
| Blocks used             |                   1 |                   — |                        1 |                       1 |

### 10.2 Transaction Size Comparison

| Scheme                      | Signature (B) | Public Key (B) | TX wire size (B) | vs ECDSA   |
|-----------------------------|:-------------:|:--------------:|:----------------:|:----------:|
| ECDSA secp256k1 (baseline)  | 48 (truncated)|             65 |             ~112 | 1.0x       |
| Falcon-512                  |    ~655 (avg) |            897 |           ~1,611 | **14.4x**  |
| ML-DSA-44                   |         2,420 |          1,312 |           ~3,791 | **33.8x**  |

GPB serialization overhead increases proportionally with TX size. ML-DSA-44 TXs are 2.4x larger than Falcon-512 due to the 2,420 B fixed-size signature.

### 10.3 Micro-Benchmark Comparison (Falcon-512)

| Operation                | ECDSA Baseline | Falcon-512 | Change |
|--------------------------|---------------:|-----------:|:------:|
| GPB TX serialize         |        0.17 us |    2.25 us | +13x   |
| GPB TX deserialize       |        0.50 us |   10.20 us | +20x   |
| BLAKE3 hash (256 B)      |        1.13 us |    1.13 us | —      |
| ZMQ inproc RTT           |        5.78 us |    5.90 us | —      |
| ZMQ TCP RTT              |      167.58 us |  168.80 us | —      |
| Proof search (k=16)      |        0.02 us |    0.02 us | —      |

Serialization cost scales with TX size (14x larger), but non-crypto operations (hash, ZMQ, proof search) are unchanged.

### 10.4 Analysis

**Four-way comparison reveals that the pipeline is network-bound, not verify-bound:**

1. **ECDSA real verify vs stub verify:** At 500 TX, ECDSA with real OpenSSL secp256k1 verification achieves 1,190 e2e TPS — only 2.3% below the 1,218 stub-verify baseline. At 1000 TX, the gap widens to 37% (1,403 vs 2,223), suggesting that real ECDSA verification does impose measurable cost at higher loads but remains within the same order of magnitude.

2. **Falcon-512 vs ECDSA (real verify):** Falcon-512 achieves **higher** e2e TPS than real ECDSA at both loads: 1,223 vs 1,190 (+2.8%) at 500 TX, and 2,572 vs 1,403 (+83%) at 1000 TX. The Falcon-512 run benefited from benchmark infrastructure improvements (wallet_load, MAKE_FLAGS passthrough) that also increased submission TPS.

3. **ML-DSA-44 vs Falcon-512:** ML-DSA-44 achieves 1,234 e2e TPS (500 TX) and 1,533 e2e TPS (1000 TX), roughly on-par and 40% below Falcon-512 respectively. Block processing time is 267ms (500 TX) and 473ms (1000 TX), roughly double Falcon's 134/200ms. This is expected: although ML-DSA-44 has 2x faster micro-benchmark verify (46,532 vs 23,505 ops/sec on Skylake-SP), its 3.5x larger signatures (2,420 B vs ~655 B) increase serialization/deserialization cost and ZMQ transfer time per TX. The bottleneck shifts from verify to serialization and I/O.

4. **Block processing time scales with TX size:** ECDSA 35ms → Falcon 200ms → ML-DSA 473ms for 1000 TX. The ~2.4x increase from Falcon to ML-DSA is consistent with ML-DSA's ~2.4x larger wire size. Despite the 473ms processing time, all 1000 TX still fit in a single block within the 1-second interval.

5. **The pipeline is not verify-bound.** Block validation runs asynchronously and even ML-DSA-44's 473ms processing cost fits within the 1-second block interval. The bottleneck is ZMQ TCP round-trip latency, GPB serialization of large TXs, and pool coordination.

**Conclusion:** Both Falcon-512 and ML-DSA-44 can replace ECDSA in MEMO's blockchain with **no significant throughput penalty** at current transaction volumes. Falcon-512 is the better fit for blockchain integration due to its 3.5x smaller signatures (666 B vs 2,420 B), despite ML-DSA-44's faster raw verification. The signature size advantage translates directly to lower block wire size, faster serialization, and lower block processing time. All three PQC candidates (Falcon-512, ML-DSA-44, and eventually SLH-DSA) maintain 100% confirmation rates and fit within the 1-second block interval.

---

## 11. Hybrid Mode — Mixed-Signature Blockchain

### 11.1 Architecture

The blockchain supports a hybrid mode (`SIG_SCHEME=3`) where all three signature backends (ECDSA secp256k1, Falcon-512, ML-DSA-44) are compiled into a single binary. Each wallet independently selects its signature type at creation time, and the blockchain validates transactions by dispatching to the correct backend based on the `sig_type` field in each transaction.

**Key design properties:**
- **Runtime dispatch:** `crypto_verify_typed()` reads `tx->sig_type` and creates a temporary context for the correct backend. No protocol changes needed — the sig_type field is already serialized in protobuf.
- **Universal buffers:** All buffer maximums use ML-DSA-44 sizes (pubkey=1312B, seckey=2560B, sig=2420B) regardless of scheme. This ensures any node can deserialize any transaction.
- **Per-wallet choice:** The wallet CLI (`--scheme ecdsa|falcon|mldsa`) determines the signature type at creation. Wallets can coexist on the same chain with different schemes.
- **Validator agnostic:** Validators do not need to know which schemes are in use ahead of time — they verify each TX according to its embedded sig_type.

### 11.2 Build Instructions

```bash
# Build hybrid binary (compiles all 3 backends)
make all SIG_SCHEME=3 OQS_ROOT=~/liboqs_install

# Create wallets with different schemes
./build/wallet create alice --scheme ecdsa
./build/wallet create bob --scheme falcon
./build/wallet create carol --scheme mldsa

# Run hybrid benchmark (12 farmers: 4 ECDSA + 4 Falcon + 4 ML-DSA)
./benchmark_hybrid.sh 500 1 16 12
```

### 11.3 Measured Results (Xeon Gold 6126, 2026-03-28)

**Configuration:** 12 farmers (4 ECDSA + 4 Falcon-512 + 4 ML-DSA-44), k=16, 8 threads/farmer, batch=64.

| Metric                    | 500 TX       | 1000 TX      |
|---------------------------|-------------:|-------------:|
| Submission TPS            |        2,397 |        4,943 |
| End-to-End TPS            |          944 |        1,505 |
| Processing TPS            |        1,556 |        2,165 |
| Confirmation Rate         |         100% |         100% |
| Blocks (benchmark)        |            2 |            2 |
| Warmup Blocks             |           15 |           30 |

**Comparison with single-scheme benchmarks (same hardware):**

| Scheme                 | 500 TX e2e TPS | 1000 TX e2e TPS |
|------------------------|---------------:|----------------:|
| Stub baseline          |          3,260 |           2,223 |
| ECDSA (real verify)    |          3,023 |           1,403 |
| Falcon-512             |          3,727 |           2,572 |
| ML-DSA-44              |          3,613 |           1,533 |
| **Hybrid (mixed)**     |      **2,397** |       **1,505** |

**Analysis:**
- At 1000 TX, hybrid TPS (1,505) falls between Falcon-512 (2,572) and ML-DSA-44 (1,533), as expected with a 1/3 mix of each signature type.
- The pipeline remains network/coordination-bound: all 1000 transactions fit in a single block with 100% confirmation.
- Mixed-signature dispatch adds negligible overhead — `crypto_verify_typed()` creates a temporary context per transaction, but the context creation cost (~µs) is dwarfed by network serialization.
- The dominant cost differentiator is **signature size**: ML-DSA-44 signatures (2,420B) are 3.5x larger than Falcon-512 (666B) and ~34x larger than ECDSA (70B), increasing serialization and I/O time for mixed blocks.

### 11.4 Migration Path

The hybrid mode enables a phased PQC migration strategy:

| Phase     | Default Scheme | Allowed Schemes                              | Timeline          |
|-----------|----------------|----------------------------------------------|-------------------|
| Phase 1   | ECDSA          | ECDSA + Falcon-512 + ML-DSA-44               | Now (hybrid mode) |
| Phase 2   | Falcon-512     | ECDSA (deprecated) + Falcon-512 + ML-DSA-44  | After validation  |
| Phase 3   | Falcon-512     | Falcon-512 + ML-DSA-44 only                  | ECDSA sunset      |

This mirrors the approach taken by Polkadot (dual-algorithm) and Algorand (phased Falcon rollout). The `sig_type` field in the TX header acts as the version discriminant — no hard fork needed to add or deprecate schemes.

---

## 12. Production-Scale Benchmark — April 2026 (Falcon-512)

> Platform: Intel Xeon Gold 6126 Skylake-SP, Chameleon Cloud, Ubuntu 24.04
> Config: 1 farmer, k=16, max_txs_per_block=100,000, 8 threads, batch size 64, 1s block interval, no warmup
> Raw files: `blockchain/benchmark_results/benchmark_20260415_*.csv`

### 12.1 10K Transaction Run

| Metric | Value | Source |
|--------|------:|--------|
| TX submitted / confirmed | 10,000 / 10,000 | `benchmark_20260415_013936.csv` |
| End-to-end TPS | **3,165** | |
| Total time | 3.2 sec | |
| Confirmation rate | 100% | |
| avg TX/block | 2,500 | |
| Blocks created | 4 | |

### 12.2 1M Transaction Runs (3 independent runs)

| Run | e2e TPS | Total Time | Confirm Rate | Blocks | avg TX/block | Source CSV |
|-----|--------:|-----------:|:------------:|:------:|:------------:|-----------|
| 1 | 10,378 | 96.4 sec | 100% | 82 | 12,195 | `benchmark_20260415_021545.csv` |
| 2 | 10,923 | 91.5 sec | 100% | 77 | 12,987 | `benchmark_20260415_021757.csv` |
| **3 (best)** | **11,182** | **89.4 sec** | **100%** | **74** | **13,514** | `benchmark_20260415_021959.csv` |
| Average | 10,828 | 92.4 sec | 100% | 78 | 12,899 | |

All three runs: 0 errors, 0 dropped transactions, receiver_final = 1,000,000 coins.

### 12.3 TPS Scaling Across Transaction Counts (Falcon-512)

| TX Count | e2e TPS | Config | Source |
|---------:|--------:|--------|--------|
| 500 | 1,223 | 10 farmers, 15 warmup blocks | `docs/RESULTS.md` §10.1 (2026-03-22) |
| 1,000 | 2,572 | 10 farmers, 15 warmup blocks | `docs/RESULTS.md` §10.1 (2026-03-22) |
| 10,000 | 3,165 | 1 farmer, no warmup | `benchmark_20260415_013936.csv` |
| 1,000,000 (avg) | 10,828 | 1 farmer, no warmup | `benchmark_20260415_021*.csv` |
| 1,000,000 (best) | **11,182** | 1 farmer, no warmup | `benchmark_20260415_021959.csv` |

**Analysis:** TPS scales 4.3× from 1K to 1M transactions. Fixed per-block and per-batch startup
costs amortize over more transactions at higher scale. avg TX/block grows from ~1 (at 1K TX)
to 13,514 (at 1M TX), driving the efficiency gain. The pipeline ceiling has not been reached.

---

## 13. Cloud Repeat Matrix — April 18, 2026 (Canonical Final Set)

> Platform: Intel Xeon Gold 6126 Skylake-SP (Chameleon Cloud)  
> Config: `k=16`, `1 farmer`, `1s block interval`, `8 threads`, `batch size=64`  
> Repeats: `3x @ 100K` + `3x @ 1M` for each scheme (`18` runs total)

### 13.1 Aggregate End-to-End TPS (mean over 3 repeats)

| Scheme | 100K mean TPS | 100K std | 1M mean TPS | 1M std | Confirmation |
|--------|--------------:|---------:|------------:|-------:|:------------:|
| Ed25519 (`s1`) | 7,046.12 | 474.09 | 8,749.78 | 3.19 | 100% |
| Falcon-512 (`s2`) | 6,693.70 | 69.05 | 8,776.68 | 30.36 | 100% |
| ML-DSA-44 (`s4`) | 6,558.37 | 212.77 | 8,744.08 | 51.54 | 100% |

### 13.2 Integrity and Artifact Selection

- Final set integrity: all 18 CSVs satisfy `tx_confirmed==target`, `confirm_rate=100.0`, `tx_errors=0`, `end_to_end_tps>0`.
- One intermediate ML-DSA run (`s4_tx1m_r3`) initially failed, then was rerun and replaced in the canonical set.
- Reviewer-safe canonical bundle:
  - `benchmarks/results/hybrid_matrix_apr18_final/`
  - `benchmarks/results/hybrid_matrix_apr18_final/README_FINAL_SELECTION.md`
- Raw history (including intermediate attempts): `benchmarks/results/hybrid_matrix_apr18/`

### 13.3 Total Runtime Across Final 18 Runs

Computed as `target_tx / end_to_end_tps` per run, summed over all final runs:

- Total end-to-end benchmark time: **1161.163 sec** (**19.353 min**, **0.323 hr**)

---

## Run Tags

| Platform                             | Run tag                       | Date       |
|--------------------------------------|-------------------------------|------------|
| M2 Pro (single-core)                 | `run_20260228_203535`         | 2026-02-28 |
| M2 Pro (all-algo multicore)          | `multicore_all_benchmark`     | 2026-03-22 |
| Cascade Lake                         | `run_20260301_210825`         | 2026-03-01 |
| Skylake-SP                           | `run_20260310_035214`         | 2026-03-10 |
| Blockchain baseline                  | `benchmark.sh`                | 2026-03-10 |
| Blockchain ECDSA (real verify)       | `benchmark.sh`                | 2026-03-22 |
| Blockchain Falcon-512 (1K TX)        | `benchmark_20260322_235135`   | 2026-03-22 |
| Blockchain ML-DSA-44 (1K TX)         | `benchmark_20260323_042209`   | 2026-03-23 |
| Blockchain Hybrid (mixed-sig)        | `benchmark_20260328_235826`   | 2026-03-28 |
| Skylake-SP (all-algo multicore)      | `multicore_all_benchmark`     | 2026-03-23 |
| Blockchain Falcon-512 (10K TX)       | `benchmark_20260415_013936`   | 2026-04-15 |
| Blockchain Falcon-512 (1M TX, run 1) | `benchmark_20260415_021545`   | 2026-04-15 |
| Blockchain Falcon-512 (1M TX, run 2) | `benchmark_20260415_021757`   | 2026-04-15 |
| Blockchain Falcon-512 (1M TX, run 3) | `benchmark_20260415_021959`   | 2026-04-15 |

Raw logs: `benchmarks/results/`, `blockchain/benchmark_results/`

---
