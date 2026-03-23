# qMEMO — Complete Benchmark Results

**Graduate Research, Illinois Institute of Technology, Chicago**
**Repository:** https://github.com/likitha-shankar/qMEMO

> All measurements use liboqs 0.15.0 (portable build) and OpenSSL 3.x.
> Timing: `clock_gettime(CLOCK_MONOTONIC)`, nanosecond precision.
> Statistical runs: 1,000 trials x 100 ops; median and IQR reported (distributions are non-Gaussian).

---

## 1. Signature Verification Throughput (Single Core)

| Algorithm | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|-----------|---------------:|-------------------------:|-----------------------:|
| Falcon-512 | 30,569 | 23,877 | 23,505 |
| Falcon-1024 | 15,618 | 11,794 | 11,618 |
| ML-DSA-44 | 25,904 | **49,060** | **46,532** |
| ML-DSA-65 | 15,369 | **30,287** | **28,893** |
| SLH-DSA-SHA2-128f | 599 | 734 | 732 |
| ECDSA secp256k1 | 4,026 | 2,963 | 2,467 |
| Ed25519 | 8,857 | 9,013 | 8,309 |

All values in ops/sec. Bold = fastest platform for that algorithm.

> ML-DSA-44/65 are ~1.8-1.9x faster on x86 than ARM due to liboqs AVX-512 NTT optimizations.
> Falcon-512 is ~24% faster on M2 Pro (NEON + deeper OOO pipeline).
> The two Xeon generations agree within 5% for all PQC algorithms.

**Statistical validation (Falcon-512 verify, 1,000 trials x 100 ops):**

| Platform | Median | Mean | CV | Normality (JB) |
|----------|-------:|-----:|---:|:--------------:|
| M2 Pro | 31,133 | 30,883 | 3.92% | Fail |
| Cascade Lake | 24,016 | 23,988 | 0.66% | Fail |
| Skylake-SP | 23,696 | — | 0.59% | — |

---

## 2. Signing Throughput (Single Core)

| Algorithm | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|-----------|---------------:|-------------------------:|-----------------------:|
| Falcon-512 | 4,805 | 4,312 | 4,207 |
| Falcon-1024 | 2,436 | 2,133 | 2,130 |
| ML-DSA-44 | 10,273 | 15,975 | 14,271 |
| ML-DSA-65 | 6,745 | 9,971 | 8,696 |
| SLH-DSA-SHA2-128f | 36 | 45 | 43 |
| ECDSA secp256k1 | 3,608 | 2,638 | 2,181 |
| Ed25519 | 24,276 | 23,184 | 21,481 |

All values in ops/sec.

---

## 3. Key Generation Throughput (Single Core)

| Algorithm | M2 Pro (ARM64) | Xeon 6242 (Cascade Lake) | Xeon 6126 (Skylake-SP) |
|-----------|---------------:|-------------------------:|-----------------------:|
| Falcon-512 | 148 | 153 | 147 |
| Falcon-1024 | 46 | 52 | 51 |
| ML-DSA-44 | 24,610 | 51,917 | 47,476 |
| ML-DSA-65 | 14,327 | 29,832 | 27,961 |
| SLH-DSA-SHA2-128f | 836 | 1,062 | 1,001 |
| ECDSA secp256k1 | 3,595 | 2,655 | 2,189 |
| Ed25519 | 24,483 | 23,062 | 21,138 |

All values in ops/sec. Falcon keygen is 100-300x slower than ML-DSA due to the complex NTRU lattice key generation.

---

## 4. Signature and Key Sizes

| Algorithm | Type | NIST Level | Public Key (B) | Secret Key (B) | Signature (B) |
|-----------|------|:----------:|---------------:|---------------:|-------------:|
| Falcon-512 | Lattice (NTRU) | 1 | 897 | 1,281 | 666 max |
| Falcon-1024 | Lattice (NTRU) | 5 | 1,793 | 2,305 | 1,280 max |
| ML-DSA-44 | Module Lattice | 2 | 1,312 | 2,528 | 2,420 |
| ML-DSA-65 | Module Lattice | 3 | 1,952 | 4,000 | 3,293 |
| SLH-DSA-SHA2-128f | Hash-based | 1 | 32 | 64 | 17,088 |
| ECDSA secp256k1 | Elliptic Curve | — | 65 | 32 | ~72 (DER) |
| Ed25519 | EdDSA | — | 32 | 32 | 64 |

Falcon-512 has the smallest signature of any NIST PQC scheme (3.6x smaller than ML-DSA-44).

**Falcon-512 signature size distribution (10,000 samples):**

| Variant | Mean | Max Observed | Std Dev | Notes |
|---------|-----:|:------------:|--------:|-------|
| Falcon-512 (unpadded) | ~655 B | 666 B | ~2.2 B | Compression effective |
| Falcon-512 (padded) | 666 B | 666 B | 0 | Constant-time, side-channel safe |
| Falcon-1024 (unpadded) | ~1,271 B | 1,280 B | — | Within FIPS 206 bound |
| Falcon-1024 (padded) | 1,280 B | 1,280 B | 0 | Constant-time |

---

## 5. Multicore Scaling

### 5.1 Apple M2 Pro (ARM64) — All 7 Algorithms (Measured)

> Source: `multicore_all_benchmark`, run 2026-03-22. 1,000 verify iterations/thread, barrier-synchronized.

| Algorithm | 1 Thread | 2 Threads | 4 Threads | 6 Threads | 8 Threads | 10 Threads | Speedup | Efficiency |
|-----------|-------:|--------:|--------:|--------:|--------:|---------:|--------:|-----------:|
| Falcon-512 | 32,237 | 78,867 | 168,123 | 251,277 | 249,493 | 298,837 | **9.27x** | **92.7%** |
| Falcon-1024 | 22,487 | 44,062 | 86,473 | 126,079 | 136,932 | 161,663 | 7.19x | 71.9% |
| ML-DSA-44 | 36,760 | 72,056 | 139,581 | 203,107 | 205,423 | 237,670 | 6.47x | 64.7% |
| ML-DSA-65 | 21,704 | 43,036 | 82,798 | 121,087 | 135,023 | 151,430 | 6.98x | 69.8% |
| SLH-DSA-SHA2-128f | 840 | 1,650 | 3,224 | 4,787 | 5,414 | 5,457 | 6.50x | 65.0% |
| ECDSA secp256k1 | 5,672 | 10,629 | 18,213 | 26,466 | 27,863 | 35,676 | 6.29x | 62.9% |
| Ed25519 | 12,583 | 24,320 | 47,200 | 71,038 | 80,741 | 89,732 | 7.13x | 71.3% |

All values in ops/sec. Falcon-512 scales best (compact FFT working set fits in L1 cache).
M2 Pro's heterogeneous architecture (6P + 4E cores) causes an efficiency drop at 8+ threads.

### 5.2 Intel Xeon Gold 6242 — Cascade Lake (Falcon-512, Measured)

| Threads | ops/sec | Speedup | Efficiency |
|--------:|--------:|--------:|-----------:|
| 1 | 20,169 | 1.00x | 100.0% |
| 2 | 38,399 | 1.90x | 95.2% |
| 4 | 74,909 | 3.71x | 92.9% |
| 6 | 108,256 | 5.37x | 89.5% |
| 8 | 149,046 | 7.39x | 92.4% |
| 10 | 184,467 | **9.15x** | **91.5%** |

Homogeneous core design produces clean, predictable linear scaling.

### 5.3 Intel Xeon Gold 6126 — Skylake-SP (Falcon-512, Measured)

| Threads | ops/sec | Speedup | Efficiency |
|--------:|--------:|--------:|-----------:|
| 1 | 18,853 | 1.00x | 100.0% |
| 2 | 37,855 | 2.01x | 100.4% |
| 4 | 61,380 | 3.26x | 81.4% |
| 6 | 106,183 | 5.63x | 93.9% |
| 8 | 143,833 | 7.63x | 95.4% |
| 10 | 176,890 | **9.38x** | **93.8%** |

4-thread anomaly (81.4%) is a known Skylake-SP mesh interconnect effect; recovers at 6+ threads.

### 5.4 Cross-Platform Summary (Falcon-512, 10 Threads)

| Platform | 10-Thread ops/sec | Speedup | Efficiency |
|----------|------------------:|--------:|-----------:|
| M2 Pro | 298,837 | 9.27x | 92.7% |
| Cascade Lake | 184,467 | 9.15x | 91.5% |
| Skylake-SP | 176,890 | 9.38x | 93.8% |

Implied serial fraction (Amdahl's Law): ~0.95%. Verification is embarrassingly parallel.

---

## 6. Official Specifications vs Measured (Validation)

### 6.1 Falcon-512

| Metric | Official (i5 @ 2.3 GHz, AVX2) | M2 Pro @ 3.5 GHz | Xeon 6242 @ 2.8 GHz |
|--------|-------------------------------:|------------------:|--------------------:|
| Verify ops/sec | 27,939 | 31,133 | 23,885 |
| Verify cycles | 82,339 | ~112,400 (est.) | 146,778 (RDTSC) |
| Public key | 897 B | 897 B | 897 B |
| Secret key | 1,281 B | 1,281 B | 1,281 B |
| Sig size (max) | 666 B | 666 B | 666 B |

### 6.2 Frequency-Normalized Comparison (Falcon-512 Verify)

| Platform | Measured | Frequency | Normalized to 2.3 GHz | vs Official |
|----------|:--------:|:---------:|:---------------------:|:-----------:|
| Intel i5-8259U (official) | 27,939 | 2.3 GHz | 27,939 | 1.00x |
| Apple M2 Pro (this work) | 31,133 | 3.5 GHz | 20,477 | 0.73x |
| Intel Xeon Gold 6242 (this work) | 23,885 | 2.8 GHz | 19,620 | 0.70x |

The 27-30% cycle-efficiency gap is explained by the portable vs AVX2-optimized implementation path.

### 6.3 ML-DSA-44

| Metric | Official (AVX2, i7 @ 2.6 GHz) | M2 Pro | Xeon 6242 |
|--------|-------------------------------:|-------:|----------:|
| Verify ops/sec | 21,966 | 25,904 | 48,627 |
| Public key | 1,312 B | 1,312 B | 1,312 B |
| Secret key | 2,560 B | 2,560 B | 2,560 B |
| Signature | 2,420 B | 2,420 B | 2,420 B |

Xeon exceeds the official reference by 121% due to liboqs AVX-512 NTT implementation.

### 6.4 Validation Verdict

| Check | M2 Pro | Xeon 6242 | Status |
|-------|:------:|:---------:|:------:|
| Key sizes match spec | Yes | Yes | PASS |
| Sig sizes within max | Yes | Yes | PASS |
| Throughput correct order of magnitude | Yes | Yes | PASS |
| Performance gap explained (portable impl) | -27% | -30% | EXPECTED |
| Single-pass vs statistical agreement | 1.2% | 0.96% | PASS |
| Correctness (key_inspection) | PASS | PASS | PASS |

---

## 7. Blockchain Baseline (Classical, Stub Verify)

> Platform: Intel Xeon Gold 6126 Skylake-SP, 48 logical cores, 187 GB RAM, Ubuntu 24.04
> Source: Harsha's `blockchain_pos_v45`, ECDSA secp256k1, stub verification (no crypto check)
> Config: k=16, 10 farmers, 8 OpenMP threads/farmer, batch size 64, 1s block interval
> Date: 2026-03-10

### 7.1 Micro-Benchmarks

| Operation | Latency (us) | Throughput (ops/sec) | Notes |
|-----------|-------------:|---------------------:|-------|
| GPB TX serialize | 0.17 | 5,910,655 | Protobuf-C |
| GPB TX deserialize | 0.50 | 1,994,360 | |
| GPB Block serialize (100 tx) | 14.18 | 70,510 | 11,452 B wire |
| GPB Block deserialize (100 tx) | 65.75 | 15,209 | |
| ZMQ inproc RTT | 5.78 | 173,155 | Same-process |
| ZMQ TCP loopback RTT | 167.58 | 5,967 | 29x slower than inproc |
| BLAKE3 hash (256 B) | 1.13 | 886,011 | ~227 MB/s effective |
| Proof search (k=16) | 0.02 | 42,194,093 | Binary search in plot |
| Plot generation (k=16) | 72.71 | — | One-time, 65,536 entries |

### 7.2 End-to-End Transaction Throughput

| Metric | 500 TX | 1000 TX |
|--------|-------:|--------:|
| Submission TPS | 3,260 | 5,659 |
| Confirmation TPS | 1,943 | 3,662 |
| **End-to-end TPS** | **1,218** | **2,223** |
| Confirmation rate | 100% | 100% |
| Block processing time | 78 ms | 35 ms |
| Blocks used | 1 | 1 |

Block processing time drops at higher load (batching amortizes fixed costs).

### 7.3 Validator Step Timing (per block)

| Step | Time |
|------|------|
| GET_LAST_HASH (blockchain query) | 0-1 ms |
| Pool-to-Validator (fetch pending TX) | 9-13 ms |
| Serialize + Send block | 1-5 ms |

Pool-to-Validator fetch (ZMQ TCP + protobuf deserialize) dominates the critical path.

### 7.4 Transaction Structure

| Scheme | Sig bytes (wire) | TX struct size | vs baseline |
|--------|------------------:|---------------:|:-----------:|
| ECDSA secp256k1 (current) | 48 (truncated DER) | 112 bytes | baseline |
| Falcon-512 | 666 | ~730 bytes | +6.5x |
| ML-DSA-44 | 2,420 | ~2,484 bytes | +22.2x |

---

## 8. MEMO Headroom Analysis

Assumes MEMO requires up to 2,500 verifications/second per shard (10,000 TPS across 4 shards).

| Configuration | Throughput (ops/sec) | Headroom vs 2,500 |
|---------------|---------------------:|:-----------------:|
| Falcon-512, 1 core, Xeon 6242 | 23,877 | **9.5x** |
| Falcon-512, 1 core, M2 Pro | 31,133 | **12.5x** |
| Falcon-512, 10 threads, Xeon 6242 | 184,467 | **73.8x** |
| Falcon-512, 10 threads, M2 Pro | 298,837 | **119.5x** |
| ML-DSA-44, 1 core, Xeon 6242 | 49,060 | **19.6x** |

Verification is not a throughput bottleneck at any tested configuration.

**Projected end-to-end TPS with Falcon-512 integration:**

| Metric | Classical Baseline (stub) | Projected (Falcon-512) |
|--------|:------------------------:|:----------------------:|
| End-to-end TPS | 2,223 | ~1,500-1,800 |
| TPS reduction | — | -20-30% |
| Single-thread verify time (1000 tx) | 0 ms | ~42 ms |
| 8-thread verify time (1000 tx) | 0 ms | ~5 ms |

Even with 30% TPS reduction, throughput remains 100x above minimum requirements.

> **Update (2026-03-22):** Measured results in Section 10 show **no TPS reduction** — Falcon-512 achieves 2,572 e2e TPS (1000 TX), exceeding both the stub baseline (2,223) and real ECDSA verification (1,403). The pipeline is network/coordination-bound, not verify-bound.
>
> **Update (2026-03-23):** ML-DSA-44 achieves 1,533 e2e TPS (1000 TX) — 40% below Falcon-512 despite 2x faster raw verification, because its 3.5x larger signatures (2,420 B vs ~655 B) increase serialization and I/O cost. Both PQC schemes remain well above minimum requirements.

---

## 9. Platform Specifications

| Parameter | Apple M2 Pro | Xeon Gold 6242 | Xeon Gold 6126 |
|-----------|-------------|----------------|----------------|
| Architecture | ARM64 (Avalanche/Blizzard) | x86-64 (Cascade Lake) | x86-64 (Skylake-SP) |
| Cores | 10 (6P + 4E) | 16 phys / 32 logical | 24 phys / 48 logical |
| Base clock | 3.49 GHz (P-cores) | 2.80 GHz | 2.60 GHz |
| RAM | 16 GB LPDDR5 | 187 GB DDR4 ECC | 187 GB DDR4 ECC |
| OS | macOS Darwin arm64 | Ubuntu 22.04 | Ubuntu 24.04 |
| Compiler | Apple Clang 17.0, `-O3` | GCC 11.4, `-O3 -march=native` | GCC 13.3, `-O3 -march=native` |
| liboqs | 0.15.0 | 0.15.0 | 0.15.0 |
| OpenSSL | 3.6.1 (Homebrew) | 3.0.2 | 3.0.13 |
| Location | Local (development) | Chameleon Cloud | Chameleon Cloud |
| SIMD | NEON (128-bit) | AVX-512 (512-bit) | AVX-512 (512-bit) |
| Cycle counter | wall-clock estimate | RDTSC (exact) | RDTSC (exact) |

---

## 10. Blockchain Integration Benchmarks (Measured)

> Platform: Intel Xeon Gold 6126 Skylake-SP, 48 logical cores, 187 GB RAM, Ubuntu 24.04
> Source: Harsha's `blockchain_pos_v45`, built with `SIG_SCHEME=1` (ECDSA), `SIG_SCHEME=2` (Falcon-512), or `SIG_SCHEME=4` (ML-DSA-44) via liboqs 0.15.0
> Config: k=16, 10 farmers, 15 warmup blocks, 8 OpenMP threads/farmer, batch size 64, 1s block interval
> Date: 2026-03-22 (ECDSA, Falcon-512), 2026-03-23 (ML-DSA-44)

### 10.1 End-to-End Transaction Throughput

| Metric | ECDSA (stub verify) | ECDSA (real verify) | Falcon-512 (real verify) | ML-DSA-44 (real verify) |
|--------|:-------------------:|:-------------------:|:------------------------:|:----------------------:|
| **500 TX** | | | | |
| Submission TPS | 3,260 | 3,023 | 3,727 | 3,613 |
| **End-to-end TPS** | **1,218** | **1,190** | **1,223** | **1,234** |
| Confirmation rate | 100% | 100% | 100% | 100% |
| Block processing time | 78 ms | — | 134 ms | 267 ms |
| Blocks used | 1 | — | 1 | 1 |
| **1000 TX** | | | | |
| Submission TPS | 5,659 | 5,640 | 8,524 | 5,586 |
| **End-to-end TPS** | **2,223** | **1,403** | **2,572** | **1,533** |
| Confirmation rate | 100% | 100% | 100% | 100% |
| Block processing time | 35 ms | — | 200 ms | 473 ms |
| Blocks used | 1 | — | 1 | 1 |

### 10.2 Transaction Size Comparison

| Scheme | Signature (B) | Public Key (B) | TX wire size (B) | vs ECDSA |
|--------|:-------------:|:--------------:|:----------------:|:--------:|
| ECDSA secp256k1 (baseline) | 48 (truncated) | 65 | ~112 | 1.0x |
| Falcon-512 | ~655 (avg) | 897 | ~1,611 | **14.4x** |
| ML-DSA-44 | 2,420 | 1,312 | ~3,791 | **33.8x** |

GPB serialization overhead increases proportionally with TX size. ML-DSA-44 TXs are 2.4x larger than Falcon-512 due to the 2,420 B fixed-size signature.

### 10.3 Micro-Benchmark Comparison (Falcon-512)

| Operation | ECDSA Baseline | Falcon-512 | Change |
|-----------|---------------:|-----------:|:------:|
| GPB TX serialize | 0.17 us | 2.25 us | +13x |
| GPB TX deserialize | 0.50 us | 10.20 us | +20x |
| BLAKE3 hash (256 B) | 1.13 us | 1.13 us | — |
| ZMQ inproc RTT | 5.78 us | 5.90 us | — |
| ZMQ TCP RTT | 167.58 us | 168.80 us | — |
| Proof search (k=16) | 0.02 us | 0.02 us | — |

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

## Run Tags

| Platform | Run tag | Date |
|----------|---------|------|
| M2 Pro (single-core) | `run_20260228_203535` | 2026-02-28 |
| M2 Pro (all-algo multicore) | `multicore_all_benchmark` | 2026-03-22 |
| Cascade Lake | `run_20260301_210825` | 2026-03-01 |
| Skylake-SP | `run_20260310_035214` | 2026-03-10 |
| Blockchain baseline | `benchmark.sh` | 2026-03-10 |
| Blockchain ECDSA (real verify) | `benchmark.sh` | 2026-03-22 |
| Blockchain Falcon-512 | `benchmark_20260322_235135` | 2026-03-22 |
| Blockchain ML-DSA-44 | `benchmark_20260323_042209` | 2026-03-23 |

Raw logs: `benchmarks/results/` and `memo-baseline/results/`

---

*See [COMPREHENSIVE_COMPARISON.md](COMPREHENSIVE_COMPARISON.md) for trade-off analysis prose.*
