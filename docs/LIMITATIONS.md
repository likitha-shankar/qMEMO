# Research Limitations and Future Work

**qMEMO Project -- Illinois Institute of Technology, Chicago**

This document honestly acknowledges the limitations of this benchmarking study.

---

## Hardware Limitations

### Single Platform Testing

**Limitation:** All benchmarks conducted on Apple M2 Pro only.

**Impact:**

- Cannot generalize to other ARM processors (Cortex-A, Graviton, etc.)
- Cannot generalize to x86-64 platforms (Intel, AMD)
- Cannot validate on embedded devices (Raspberry Pi)
- Cannot test on server-grade hardware (EPYC, Xeon)

**Mitigation:** Results validated against published Intel baseline (3.8% variance).

**Future Work:** Cross-platform benchmarking on:

- Intel Xeon (server)
- AMD EPYC (server)
- ARM Graviton (cloud)
- Raspberry Pi 4 (embedded)
- RISC-V platforms

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
- Leaving ~40% performance on table (64K vs 44K ops/sec)
- Results represent "safe default" not "maximum possible"

**Why This Is OK:** Reference implementation is what most systems deploy for portability.

**Future Work:** Benchmark with OQS_OPT_TARGET=native for full NEON optimization.

### Multi-core Results Not Integrated Into Main Analysis

**Limitation:** `multicore_benchmark.c` (scaling across 1/2/4/6/8/10 cores) and `concurrent_benchmark.c` (4-worker thread pool) exist and compile, but their results are not referenced in `THROUGHPUT_ANALYSIS.md`, `ANALYSIS.md`, or the main `BENCHMARK_REPORT.md`.

**Impact:**

- The paper's throughput headroom claims rest on single-core numbers only. Multi-core scaling data (which would show near-linear speedup) is collected but unused.
- The `make run` target and `run_all_benchmarks.sh` do not execute these two benchmarks, so their results are never aggregated into `summary.json`.

**Future Work:** Run `multicore_benchmark` and `concurrent_benchmark` in the automated pipeline; add a "Multi-core Scaling" section to `THROUGHPUT_ANALYSIS.md` showing measured speedup and parallel efficiency across core counts.

### No Comparison to Classical ECDSA

**Limitation:** Did not benchmark secp256k1 or Ed25519 for comparison.

**Impact:**

- Cannot quantify "cost of quantum resistance"
- Missing baseline for blockchain developers
- Cannot show verification speed advantage

**Future Work:** Add ECDSA/EdDSA benchmarks using same methodology.

### No Comparison to SPHINCS+ (SLH-DSA)

**Limitation:** `comparison_benchmark.c` covers Falcon-512 vs ML-DSA-44 (Dilithium). SPHINCS+ / SLH-DSA is not yet benchmarked.

**Impact:**

- SPHINCS+ is a stateless hash-based scheme with no algebraic structure -- a more conservative security assumption than lattice-based schemes. Missing it leaves a gap for long-lived validator keys where algebraic hardness assumptions may be undesirable.
- Cannot present a full NIST PQC Level-1 three-way comparison (Falcon / ML-DSA / SLH-DSA) in the paper.

**Future Work:** Add `sphincs_benchmark.c` using `OQS_SIG_alg_sphincs_sha2_128f_simple` and integrate into `comparison_benchmark.c`.

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

### No Hardware Performance Counters

**Limitation:** Used wall-clock time, not CPU cycle counters (PMCCNTR, RDTSC).

**Impact:**

- Includes ~1% OS overhead
- Cannot measure exact instruction counts
- Cannot analyze cache misses, branch mispredictions

**Future Work:** Use Linux perf or Apple Instruments for cycle-accurate measurements.

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

These limitations **do not** invalidate the core finding:

**Falcon-512 provides 17x headroom for MEMO blockchain (conservative scenario).**

Even accounting for:

- Slower hardware (10x headroom still ample)
- Cross-shard overhead (14.7x after adjustment)
- Thermal throttling (P5 = 17x headroom)
- OS interference (statistical analysis shows consistency)

**The research question is answered conclusively** within the limitations described.

Future work should address these limitations for production deployment, but current results are sufficient for algorithm selection and architectural planning.
