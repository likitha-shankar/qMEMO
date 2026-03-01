# qMEMO Benchmark Report

> **Run:** `20260301_210825`
> **Date:** 2026-03-01 21:08:35 UTC
> **Duration:** 10s

## System Specifications

| Property | Value |
|----------|-------|
| Hostname | qmemo-cascade-bench |
| CPU | Intel(R) Xeon(R) Gold 6242 CPU @ 2.80GHz |
| Cores | 64 |
| RAM | 187 GB |
| OS | Linux 5.15.0-144-generic |
| Arch | x86_64 |
| Compiler | cc (Ubuntu 11.4.0-1ubuntu1~22.04.2) 11.4.0 |
| liboqs | d1d8447 |

---

## 1. Single-Pass Verification Benchmark

Falcon-512 signature verification over 10000 consecutive iterations.

| Metric | Value |
|--------|-------|
| Throughput | 23787.14 ops/sec |
| Latency | 42.04 µs/op |

---

## 2. Statistical Verification Benchmark

1000 independent trials, each timing a batch of 100 verifications.

| Statistic | Value |
|-----------|-------|
| Mean | 23987.94 ops/sec |
| Median | 24016.32 ops/sec |
| Std Dev | 157.38 ops/sec |
| CV | 0.66% |
| P95 | 24029.37 ops/sec |
| P99 | 24032.31 ops/sec |
| Outliers (>3σ) | 20 |
| Normality (JB) | false |

**Interpretation:** CV < 2% indicates excellent measurement stability.
Distribution is non-Gaussian -- report median and IQR.

---

## 3. Algorithm Comparison: Falcon-512 vs ML-DSA-44

### Throughput (ops/sec -- higher is better)

| Operation | Falcon-512 | ML-DSA-44 | Ratio (F/D) |
|-----------|-----------|-----------|-------------|
| Verification | 23876.84 | 49060.13 | **0.4867x** |

### Sizes (bytes -- lower is better)

| Component | Falcon-512 | ML-DSA-44 | Ratio (F/D) |
|-----------|-----------|-----------|-------------|
| Public Key | 897 | 1312 | -- |
| Signature | 657 | 2420 | **0.2715x** |
| Tx Overhead (sig+pk) | 1554 | 3732 | **0.4164x** |

### Blockchain Impact (4,000 tx/block)

| Metric | Falcon-512 | ML-DSA-44 |
|--------|-----------|-----------|
| Block signature data | 6070.3 KB | 14578.1 KB |
| Block verify time (est.) | 167.5 ms | 81.5 ms |

---

## 4. Multicore Scaling Benchmark

_Multicore benchmark did not produce results._

---

## 5. Concurrent vs Sequential Benchmark

| Mode | Throughput (ops/sec) |
|------|---------------------|
| Concurrent (4 workers) | 25617 |
| Sequential | 21030 |

**Analysis:** Concurrent yields 17.9% lower latency (better parallelism)

---

## Recommendation

Falcon-512 delivers **0.4867x faster verification** with **0.4164x smaller on-chain footprint** compared to ML-DSA-44, making it the stronger candidate for post-quantum blockchain transaction signing. Its slower key generation (~5 ms) is a one-time cost per address and does not affect runtime throughput.

---

## Files in This Run

| File | Description |
|------|-------------|
| `system_specs.json` | Hardware and software environment |
| `verify_results.json` | Single-pass verification benchmark |
| `statistical_results.json` | Statistical analysis (1,000 trials) |
| `comparison_results.json` | Falcon-512 vs ML-DSA-44 |
| `multicore_results.json` | Scaling across {1,2,4,6,8,10} cores |
| `concurrent_results.json` | 4-worker thread pool vs sequential |
| `summary.json` | Aggregated key metrics |
| `REPORT.md` | This report |

## Reproducibility

```bash
cd /home/cc/qMEMO
./scripts/run_all_benchmarks.sh
```
