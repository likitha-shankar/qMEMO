# Research Limitations and Future Work

**qMEMO Project -- Illinois Institute of Technology, Chicago**

This document honestly acknowledges the limitations of this benchmarking study.

---

## Hardware Limitations

### Platform Coverage -- Partially Resolved

**Status: Partially resolved.** Benchmarks now run on two independent platforms.

**Platforms measured:**

- Apple M2 Pro (ARM64, macOS) -- single-core median 31,133 ops/sec, CV 3.92%
- Intel Xeon Gold 6242 (x86-64, Cascade Lake, Ubuntu 22.04) -- median 23,885 ops/sec,
  CV 0.67%, 146,778 RDTSC exact cycles

**Key cross-architecture finding:** ML-DSA-44 is 2x faster than Falcon-512 on x86 (AVX-512);
Falcon-512 is 18% faster on ARM. Algorithm selection should depend on deployment architecture.

**Remaining gaps:**

- AMD EPYC (Zen architecture) not yet measured
- ARM Graviton (cloud) not yet measured
- Raspberry Pi / embedded not yet measured
- RISC-V not yet measured

**Future Work:** AMD EPYC and ARM Graviton benchmarks on Chameleon Cloud.

### Thermal Management Unknown

**Limitation:** Did not measure or control CPU temperature.

**Impact:**

- Thermal throttling may have affected some trials
- Cannot distinguish thermal effects from OS scheduling
- 16 outliers (1.6%) may include throttling events

**Future Work:** Monitor CPU temperature, test with controlled cooling.

### CPU Core Affinity Not Controlled

**Limitation:** Did not pin process to performance cores.

**Impact:**

- Some trials may have run on efficiency cores (2.4 GHz vs 3.5 GHz)
- Could explain left-skewed distribution
- Outliers likely include E-core execution

**Future Work:** Use CPU affinity (taskset on Linux, thread policies on macOS).

---

## Software Limitations

### Reference Implementation Only

**Limitation:** Used liboqs OQS_DIST_BUILD (portable/reference).

**Impact:**

- Not using fully optimized NEON implementation
- Leaving ~2-3x performance on table vs fully optimized NEON/AVX-512 builds
- Results represent "safe default" not "maximum possible"

**Why This Is OK:** Reference implementation is what most systems deploy for portability.

**Future Work:** Benchmark with OQS_OPT_TARGET=native for full NEON optimization.

### Multi-core Results -- Resolved

**Status: Resolved.** Multicore scaling is now measured and documented.

**Measured scaling (1/2/4/6/8/10 cores):**

| Cores | M2 Pro | Cascade Lake |
|------:|-------:|-------------:|
| 1 | 27,022 | 20,013 |
| 10 | 239,297 (8.86x) | 176,714 (8.83x) |

Both platforms show near-linear scaling to 4 cores and approximately 8.8x at 10 cores.
Results are in `docs/COMPREHENSIVE_COMPARISON.md` and `benchmarks/results/`.

### Classical Baseline Comparison -- Resolved

**Status: Resolved.** `classical_benchmark.c` and `comprehensive_comparison.c` both cover
ECDSA secp256k1 and Ed25519 via OpenSSL 3.x EVP API.

Key results (verify ops/sec):
- Falcon-512 is 7.6x faster than ECDSA secp256k1 on ARM, 8.1x on x86
- Falcon-512 is 3.5x slower than Ed25519 on ARM, 2.7x on x86

### SLH-DSA (SPHINCS+) Comparison -- Resolved

**Status: Resolved.** `comprehensive_comparison.c` includes SLH-DSA-SHA2-128f alongside
all other algorithms. Full NIST PQC Level-1 three-way comparison is now available.

Key results: SLH-DSA signs at 36-45 ops/sec (unusable for high-TPS), verifies at 600-730
ops/sec, but has 32-byte public keys and purely hash-based security assumptions.

---

## Methodology Limitations

### Fixed Message Size

**Limitation:** Only tested 256-byte messages.

**Impact:**

- Blockchain transactions vary in size (100-1000 bytes typical)
- Cannot analyze message-size dependency
- Hashing overhead not measured independently

**Why 256 bytes:** Representative of typical blockchain transaction.

**Future Work:** Test variable message sizes (64 B, 256 B, 1 KB, 4 KB).

### No BLAKE3-512 Hashing Benchmark

**Limitation:** Did not isolate hashing performance.

**Impact:**

- Cannot separate hashing from signature verification time
- "Full transaction verification" overhead unknown

**Future Work:** Benchmark BLAKE3-512 separately, measure combined overhead.

### Single Keypair

**Limitation:** Generated one keypair, used for all verifications.

**Impact:**

- Does not test cache effects of multiple public keys
- Blockchain nodes verify transactions from thousands of addresses
- Real-world cache hit rates not modeled

**Future Work:** Benchmark with rotating set of 1,000+ public keys.

### No Network Simulation

**Limitation:** Isolated CPU benchmark, no network I/O.

**Impact:**

- Cannot measure end-to-end transaction latency
- Network deserialization overhead not measured
- Mempool management overhead not measured

**Future Work:** Full blockchain node simulation with network stack.

---

## MEMO-Specific Limitations

### Simplified Cross-Shard Model

**Limitation:** Assumed 20% cross-shard rate with 2x verification cost.

**Impact:**

- Real cross-shard rate depends on transaction patterns
- May underestimate or overestimate actual load
- Did not model cross-shard validation complexity

**Future Work:** Analyze actual MEMO transaction patterns, model precisely.

### No Proof-of-Space Integration

**Limitation:** Did not test signature verification within PoSpace consensus.

**Impact:**

- Cannot measure interference between signature verification and plotting
- Cannot measure memory bandwidth contention
- Cannot validate on actual MEMO node

**Future Work:** Integrate with MEMO prototype, measure full node performance.

### No Sharding Simulation

**Limitation:** Single-core throughput, did not simulate sharded network.

**Impact:**

- Cannot measure shard coordination overhead
- Cannot test cross-shard transaction latency
- Cannot validate 256-shard scalability

**Future Work:** Implement multi-shard simulation with cross-shard transactions.

---

## Statistical Limitations

### Non-Normal Distribution

**Limitation:** Distribution is non-Gaussian (Jarque-Bera test failed).

**Impact:**

- Cannot use parametric statistical tests (t-test, ANOVA)
- Must use non-parametric tests (Mann-Whitney U)
- Outliers affect mean more than median

**Why This Happened:** OS scheduling, thermal effects, background processes.

**Reporting:** We report median (robust) alongside mean.

### Limited Trial Duration

**Limitation:** Each trial = 100 iterations = ~2.3 ms.

**Impact:**

- Short duration may not capture thermal effects
- Cannot measure sustained performance over minutes/hours
- Cannot detect gradual performance degradation

**Future Work:** Long-running benchmark (1 hour sustained load).

### Background Processes Not Controlled

**Limitation:** Did not disable background processes during testing.

**Impact:**

- OS interrupts cause outliers
- System updates, indexing, etc. affect variance
- Cannot achieve minimal CV (<1%)

**Future Work:** Boot to single-user mode, disable all non-essential processes.

---

## Validation Limitations

### Single Published Baseline

**Limitation:** Only validated against one published source (Intel i5 NIST submission).

**Impact:**

- Limited confidence in correctness
- Cannot cross-validate with multiple independent sources
- Paper from 2020, may use different liboqs version

**Future Work:** Validate against multiple published benchmarks.

### Hardware Performance Counters -- Partially Resolved

**Status: Partially resolved.** RDTSC exact cycle counting is now implemented for x86-64.

- **x86-64 (Cascade Lake):** `get_cycles()` uses RDTSC -- exact hardware cycle counts,
  zero OS overhead. Result: 146,778 cycles/verify (gold standard for comparison).
- **ARM (M2 Pro):** `get_cycles()` estimates from wall clock at BENCH_FREQ_GHZ (3.504 GHz).
  ARM PMU cycle counter (PMCCNTR_EL0) requires kernel privilege on macOS -- not available
  without a custom kernel extension.

**Future Work:** Use `perf stat` on Linux ARM (Graviton) for exact ARM PMU cycle counts.

---

## Generalizability Limitations

### Academic Context Only

**Limitation:** Research benchmark, not production deployment.

**Impact:**

- Missing security hardening (constant-time guarantees)
- Missing error recovery (what if verification fails?)
- Missing monitoring and logging
- Missing key management

**Future Work:** Production-grade implementation with full security analysis.

### Blockchain-Specific Assumptions

**Limitation:** Optimized for blockchain use case.

**Impact:**

- Assumes high-throughput verification (tight loop)
- Assumes fixed message format (transactions)
- Not applicable to other signature use cases (TLS, email, code signing)

**Generalizability:** Results apply to blockchain verification specifically.

---

## Reproducibility Limitations

### Incomplete Environment Documentation

**Limitation:** Did not capture all environment variables, system settings.

**Impact:**

- Cannot guarantee exact reproduction on another M2 Pro
- Compiler cache state unknown
- System load at test time unknown

**Future Work:** Docker container with frozen environment.

### No Continuous Integration

**Limitation:** Manual benchmark execution, no CI/CD pipeline.

**Impact:**

- Performance regression detection not automated
- Cannot track performance across liboqs updates
- Results only valid for liboqs 0.15.0

**Future Work:** GitHub Actions CI with automated benchmarking.

---

## Conclusion

These limitations **do not** invalidate the core findings:

**Falcon-512 provides 9.5-12.5x single-core headroom (10,000 TPS / 4 shards scenario),
expanding to 70x+ with 10-core scaling. Both ARM and x86 platforms confirm this.**

Key limitations resolved in current work:
- [x] Multi-platform (ARM + x86 now measured)
- [x] Classical baselines (ECDSA + Ed25519 included)
- [x] SLH-DSA comparison (comprehensive_comparison covers all 7 algorithms)
- [x] Multicore scaling (measured on both platforms)
- [x] Hardware cycle counters (RDTSC on x86)

Remaining limitations for future work:
- [ ] AMD and Graviton platforms
- [ ] ARM PMU cycle counters
- [ ] Long-duration sustained-load testing
- [ ] CPU affinity pinning on macOS

**The research question is answered conclusively** within these limitations. Results are
sufficient for algorithm selection and architectural planning.
