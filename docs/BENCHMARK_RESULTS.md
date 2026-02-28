# Falcon-512 Verification Benchmark Results

**qMEMO Project — Illinois Institute of Technology, Chicago**

---

## Executive Summary

**Can Falcon-512 meet MEMO's requirements?** Yes. A single CPU core achieves **44,131 verifications per second** (median over 1,000 trials, CV = 3.43%). MEMO's conservative scenario (10,000 TPS across 4 shards) requires 2,500 verif/sec per shard, yielding **17.7× headroom**; the target scenario (50,700 TPS across 256 shards) requires 198 verif/sec per shard, yielding **223× headroom**. Our cycle count (79,210 @ 3.5 GHz) is within 3.8% of the published Falcon reference (82,339 cycles on Intel i5-8259U). Falcon-512 verification is not a performance bottleneck for MEMO at any tested configuration.

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
- **Build type:** Release (OQS_DIST_BUILD — portable)
- **Test date:** 2026-02-18

### Methodology

- **Timing:** `clock_gettime(CLOCK_MONOTONIC)`
- **Warm-up:** 100–200 iterations (cache priming)
- **Message size:** 256 bytes (blockchain transaction size)
- **Fixed payload:** Deterministic 0x42 fill
- **Zero overhead:** No I/O or allocations during timing

---

## Single-Run Benchmark Results

| Metric | Value |
|--------|-------|
| Iterations | 10,000 |
| Total time | 0.226 sec |
| Throughput | 44,187 ops/sec |
| Latency (mean) | 22.63 µs |
| CPU cycles | 79,210 @ 3.5 GHz |
| Signature size | 654–658 bytes |
| Public key size | 897 bytes |
| Secret key size | 1,281 bytes |

---

## Statistical Analysis (1,000 Trials)

Each trial: 100 verification operations.

| Statistic | Value |
|-----------|-------|
| Mean | 43,767 ops/sec |
| Median (P50) | 44,131 ops/sec |
| Std Dev (σ) | 1,503 ops/sec |
| CV | 3.43% |
| Min | 29,603 ops/sec |
| P5 | 42,444 ops/sec |
| P95 | 45,662 ops/sec |
| Max | 45,746 ops/sec |
| IQR | 1,350 ops/sec |
| Outliers (>3σ) | 16/1000 (1.6%) |

### Distribution Analysis

- **Skewness:** -1.76 (left-skewed)
- **Kurtosis:** 29.04 (heavy tails)
- **Normality test:** FAIL (Jarque–Bera >> 5.991)
- **Interpretation:** Non-Gaussian distribution typical for CPU benchmarks due to OS scheduling, thermal effects, and background processes. We report **median and IQR** as the primary central tendency; the low CV (3.43%) indicates high measurement consistency.

---

## Validation Against Published Research

### Baseline 1: Falcon Official (Intel i5-8259U @ 2.3 GHz)

- **Published:** 82,339 CPU cycles
- **Our result:** 79,210 cycles
- **Variance:** -3.8% (we're faster)
- **Expected ops/sec @ 2.3 GHz:** 27,939
- **Our ops/sec @ 3.5 GHz:** 44,131
- **Frequency scaling:** 1.52× expected vs 1.58× measured (reasonable)

### Why We're 3.8% Faster

1. Better cache hierarchy (M2 Pro: 192 KB L1 vs Intel: 128 KB)
2. Unified memory architecture
3. ARM NEON efficiency
4. Newer compiler (clang 17 vs gcc from 2017 submission)

### Note on Optimization Level

Our build uses OQS_DIST_BUILD (portable/reference implementation). Fully optimized NEON implementations could achieve 60–70K ops/sec based on published research (2.3–2.4× speedup). We intentionally use reference implementation as it represents realistic deployment scenario.

---

## MEMO Blockchain Analysis

### Conservative Scenario

- **Target:** 10,000 TPS
- **Shards:** 4
- **Per-shard requirement:** 2,500 verif/sec
- **Our performance (median):** 44,131 verif/sec
- **Headroom: 17.7×**
- **Worst-case (P5):** 42,444 verif/sec = 17.0× headroom

### Target Scenario

- **Target:** 50,700 TPS
- **Shards:** 256
- **Per-shard requirement:** 198 verif/sec
- **Our performance (median):** 44,131 verif/sec
- **Headroom: 223×**

### With Cross-Shard Overhead

Assuming 20% cross-shard transactions requiring dual verification:

- **Effective multiplier:** 1.2×
- **Adjusted requirement (4 shards):** 3,000 verif/sec
- **Adjusted headroom:** 14.7× (still ample)

---

## Conclusion

Falcon-512 signature verification is **NOT** a performance bottleneck for MEMO blockchain at any tested configuration. Even using reference implementation on single CPU core, throughput exceeds requirements by **17×** (conservative) to **223×** (target scenario).

Optimized implementations or hardware acceleration could provide additional 1.5–2× improvement if needed, but current headroom makes this unnecessary for MEMO's throughput targets.
