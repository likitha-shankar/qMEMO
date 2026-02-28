# Falcon-512 Verification Benchmark Results

**qMEMO Project -- Illinois Institute of Technology, Chicago**

---

## Executive Summary

**Can Falcon-512 meet performance requirements?** Yes. A single CPU core achieves **31,133 verifications per second** (M2 Pro ARM, median over 1,000 trials, CV = 3.92%) or **23,885 verifications/sec** (Cascade Lake x86, CV = 0.67%). With 10-core parallel scaling, throughput reaches 239K ops/sec (ARM) or 176K ops/sec (x86). MEMO's conservative scenario (10,000 TPS across 4 shards) requires 2,500 verif/sec per shard, yielding **9.5x headroom** even on the slower x86 node. Falcon-512 verification is not a performance bottleneck at any tested configuration.

---

## Test Configuration

### Hardware

- **CPU:** Apple M2 Pro (10 cores: 6 performance + 4 efficiency)
- **Frequency:** ~3.5 GHz (P-cores), ~2.4 GHz (E-cores)
- **RAM:** 16 GB unified memory
- **Architecture:** ARM64 (arm64-Darwin-25.2.0)
- **Cache:** 192 KB L1 per P-core, shared L2/L3

### Software

- **OS:** macOS 26.2
- **liboqs:** 0.15.0 (commit: 1fdaa40)
- **Compiler:** clang 17.0.0 (Apple)
- **Compiler flags:** `-O3 -mcpu=native -ffast-math`
- **CPU extensions:** NEON, AES, SHA2, SHA3
- **Build type:** Release (OQS_DIST_BUILD -- portable)
- **Test date:** 2026-02-18

### Methodology

- **Timing:** `clock_gettime(CLOCK_MONOTONIC)`
- **Warm-up:** 100-200 iterations (cache priming)
- **Message size:** 256 bytes (blockchain transaction size)
- **Fixed payload:** Deterministic 0x42 fill
- **Zero overhead:** No I/O or allocations during timing

---

## Single-Run Benchmark Results (Apple M2 Pro)

| Metric | Value |
|--------|-------|
| Iterations | 10,000 |
| Total time | 0.325 sec |
| Throughput | 30,757 ops/sec |
| Latency (mean) | 32.51 us |
| CPU cycles (est.) | ~113,800 @ 3.504 GHz |
| Signature size | 654-659 bytes |
| Public key size | 897 bytes |
| Secret key size | 1,281 bytes |

---

## Statistical Analysis (Apple M2 Pro, 1,000 Trials)

Each trial: 100 verification operations.

| Statistic | Value |
|-----------|-------|
| Mean | 30,883 ops/sec |
| Median (P50) | 31,133 ops/sec |
| Std Dev | 1,212 ops/sec |
| CV | 3.92% |
| Min | ~20,000 ops/sec |
| P5 | ~29,800 ops/sec |
| P95 | ~32,100 ops/sec |
| IQR | -- |
| Outliers (>3s) | 16/1000 (1.6%) |

### Distribution Analysis

- **Normality test:** FAIL (Jarque-Bera >> 5.991, non-Gaussian)
- **Interpretation:** Left-skewed distribution with heavy tails -- typical for CPU benchmarks
  under macOS where background processes and thermal management cause occasional slow outliers.
  We report **median** as primary central tendency; CV of 3.92% is acceptable for ARM.
  Cascade Lake shows markedly tighter distribution (CV 0.67%) on bare-metal Linux.

---

## Validation Against Published Research

### Baseline: Falcon Official (Intel i5-8259U @ 2.3 GHz)

- **Published:** 82,339 CPU cycles (Falcon spec Appendix B)
- **Our Cascade Lake result (RDTSC):** 146,778 cycles @ 2.80 GHz
- **Latency comparison:** published = 35.8 us, ours = 52.4 us (Cascade Lake 1.46x slower)
- **Our M2 Pro result (estimated):** ~113,800 cycles @ 3.504 GHz = 32.5 us (9% faster than reference)

The Cascade Lake is slower in latency because liboqs 0.15.0 uses the portable reference
implementation, not the AVX2-optimized Falcon path that the reference submission targeted.
The M2 Pro's shorter latency reflects its deeper OOO pipeline compensating for the lack
of x86-specific SIMD paths.

### Note on Optimization Level

Our build uses liboqs 0.15.0 portable build. The Falcon reference implementation includes
AVX2-optimized paths for x86 that liboqs does not expose in the same configuration. A
fully AVX2-optimized build could reduce cycle count by 2-3x. We use the portable build
as it represents the most realistic general deployment scenario and allows fair
cross-architecture comparison.

---

## Performance Headroom Analysis

### Conservative Scenario

- **Target:** 10,000 TPS across 4 shards
- **Per-shard requirement:** 2,500 verif/sec
- **Performance (M2 Pro single-core median):** 31,133 verif/sec -- **12.5x headroom**
- **Performance (Cascade Lake single-core median):** 23,885 verif/sec -- **9.5x headroom**
- **Performance (Cascade Lake 10-core):** 176,714 verif/sec -- **70.7x headroom**

### Target Scenario

- **Target:** 50,700 TPS across 256 shards
- **Per-shard requirement:** 198 verif/sec
- **Performance (Cascade Lake single-core):** 23,885 verif/sec -- **120x headroom**

### With Cross-Shard Overhead

Assuming 20% cross-shard transactions requiring dual verification:

- **Effective multiplier:** 1.2x
- **Adjusted requirement (4 shards):** 3,000 verif/sec
- **Cascade Lake single-core headroom:** 7.9x (still ample)

---

---

## Cascade Lake Results (Intel Xeon Gold 6242, x86-64)

**Run:** 2026-02-28 (run_20260228_223247) -- Chameleon Cloud compute_cascadelake_r650

### Hardware

- **CPU:** Intel Xeon Gold 6242 @ 2.80 GHz (Cascade Lake)
- **Cores:** 64 logical (32 physical, 2-socket hyperthreaded)
- **RAM:** 187 GB
- **OS:** Ubuntu 22.04 (kernel 5.15)
- **Compiler:** GCC 11.4.0, `-O3 -march=native -ffast-math`
- **liboqs:** 0.15.0 (built from source)
- **OpenSSL:** 3.0.2 (system package)
- **Cycle counter:** RDTSC (exact hardware cycles, not estimated)

### Single-Pass Verify (10,000 iterations)

| Metric | Value |
|--------|-------|
| Iterations | 10,000 |
| Total time | 0.419 sec |
| Throughput | 23,846 ops/sec |
| Latency (mean) | 41.94 us |
| CPU cycles (RDTSC) | 146,778 |
| Signature size | 653 bytes |

### Statistical Analysis (1,000 trials x 100 ops)

| Statistic | Value |
|-----------|-------|
| Mean | 23,862 ops/sec |
| Median (P50) | 23,885 ops/sec |
| Std Dev | 161 ops/sec |
| CV | 0.67% |
| Min | 20,240 ops/sec |
| P5 | 23,798 ops/sec |
| P95 | 23,929 ops/sec |
| Max | 23,937 ops/sec |
| IQR | 7.3 ops/sec |
| Outliers (>3s) | 16/1000 (1.6%) |

The CV of **0.67%** is exceptional -- over 5x more stable than the M2 Pro result (3.92%).
This reflects the Cascade Lake's dedicated server-class memory controller and the absence
of efficiency-core scheduling interference.

### Cycles vs Published Reference

The Falcon specification (Appendix B, Intel i5-8259U @ 2.3 GHz) reports 82,339 cycles for
verification. Our Cascade Lake measurement of 146,778 RDTSC cycles at 2.80 GHz is not
directly comparable because:
- The i5-8259U is a Coffee Lake client chip with different cache and pipeline characteristics
- liboqs 0.15.0 uses the portable reference implementation, not the AVX2-optimized path

Converting to latency: 146,778 / 2.8 GHz = 52.4 us vs the reference's 82,339 / 2.3 GHz = 35.8 us.
Cascade Lake at reference-impl is 1.46x slower in latency. This is expected -- the optimized
Falcon implementation targets Coffee Lake/Skylake AVX2 with 256-bit vectors. Cascade Lake's
AVX-512 capabilities are not leveraged by Falcon in liboqs 0.15.0.

---

## Cross-Architecture Summary

| Platform | Falcon-512 Verify (median) | CV | Cycles/op |
|----------|:--------------------------:|:--:|:---------:|
| Apple M2 Pro (ARM64) | 31,133 ops/sec | 3.92% | ~79,200 est. |
| Intel Xeon Gold 6242 (x86) | 23,885 ops/sec | 0.67% | 146,778 (RDTSC) |

M2 Pro verifies Falcon-512 **30% faster** than Cascade Lake in throughput terms, despite a lower
clock frequency. The M2 Pro's out-of-order pipeline, unified memory bandwidth, and NEON SIMD
provide an advantage for Falcon's FFT-based operations. Cascade Lake's edge is in ML-DSA-44
(2x faster via AVX-512), not Falcon.

---

## Conclusion

Falcon-512 signature verification is **NOT** a performance bottleneck at any tested configuration.
A single CPU core achieves 23,885-31,133 verifications/sec depending on architecture. With
multicore scaling (10 cores), throughput reaches 176K-239K ops/sec.

Optimized AVX-512 or NEON implementations of Falcon could provide an additional 2-3x improvement
if needed, but current headroom is ample for any realistic blockchain deployment scenario.
