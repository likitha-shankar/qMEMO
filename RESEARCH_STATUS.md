# Research Status -- qMEMO Cryptographic Benchmarking Suite

**Project:** Graduate Research, Illinois Institute of Technology Chicago
**Scope:** Pure cryptographic performance benchmarking -- post-quantum vs classical signature schemes
**Research Question:** How do NIST-standardized PQC schemes (Falcon, ML-DSA, SLH-DSA) compare to classical baselines (ECDSA, Ed25519) in signing throughput, verification throughput, key/signature sizes, and multicore scaling?

---

## Project Summary

This suite benchmarks seven cryptographic signature algorithms across the full signing lifecycle
(key generation, signing, verification) with emphasis on:

- **Throughput and latency** -- raw ops/sec on real hardware under controlled conditions
- **Multicore scaling** -- how well signing parallelizes from 1 to 10 cores
- **Signature size distributions** -- measured statistical properties of variable-length schemes
- **Side-by-side comparison** -- all 7 algorithms in a single unified run

**Scope note:** All measurements are pure cryptographic operations. No network layer, no
distributed system, no blockchain integration. This isolates the cryptographic primitive cost
cleanly from application-layer overhead.

---

## Completed Work

11 benchmark programs in `benchmarks/src/`, all producing both human-readable tables and
machine-readable JSON output.

| Program | Description |
|---------|-------------|
| `verify_benchmark` | Single-pass 10,000 Falcon-512 verifications; latency histogram |
| `statistical_benchmark` | 1,000 trials x 100 ops; mean, median, std dev, CV, Jarque-Bera normality test |
| `comparison_benchmark` | Head-to-head Falcon-512 vs ML-DSA-44 (throughput + size) |
| `concurrent_benchmark` | Thread-pool (4 workers, 100 tasks); measures scheduling overhead |
| `multicore_benchmark` | Falcon-512 verification scaling: 1/2/4/6/8/10 cores |
| `concurrent_signing_benchmark` | Thread-pool signing throughput; queue-based dispatch |
| `sign_benchmark` | Falcon-512 signing scaling: 1/2/4/6/8/10 cores, per-thread OQS context |
| `signature_size_analysis` | 10,000 real signatures each for Falcon-512, Falcon-padded-512, Falcon-1024, Falcon-padded-1024; min/max/mean/stddev/percentiles |
| `classical_benchmark` | ECDSA secp256k1 + Ed25519 via OpenSSL 3.x EVP_PKEY high-level API; 10,000 iterations |
| `comprehensive_comparison` | All 7 algorithms side-by-side; 1,000 iterations each of keygen/sign/verify |
| `key_inspection` | Displays raw public key bytes, signature bytes, and correctness checks for all 7 algorithms (audit/debugging tool) |

---

## Key Findings

All results from run `20260224_035823` on Apple M2 Pro (10-core, 16 GB, Darwin 25.3.0).
Compiler: Apple Clang 17.0.0, `-O3 -ffast-math`. liboqs 0.15.0, OpenSSL 3.6.1.

### Throughput Summary (1,000 iterations)

| Algorithm | NIST Level | Keygen/s | Sign/s | Verify/s | Sig (bytes) |
|-----------|-----------|----------|--------|----------|-------------|
| Falcon-512 | L1 | 212 | 7,014 | **43,787** | 752 max / 655 mean |
| Falcon-1024 | L5 | 65 | 3,502 | 22,102 | 1,280 max / 1,271 mean |
| ML-DSA-44 | L2 | **36,065** | 15,307 | 37,343 | 2,420 |
| ML-DSA-65 | L3 | 20,711 | 9,741 | 22,340 | 3,309 |
| SLH-DSA-SHA2-128f | L1 | 1,214 | **52** | 832 | 17,088 |
| ECDSA secp256k1 | -- | 5,108 | 5,153 | 5,621 | ~71 |
| Ed25519 | -- | 34,711 | **34,335** | 12,491 | 64 |

### Notable Results

**Falcon-512 is the fastest verifier** in the entire suite (43,787 ops/sec), outperforming
ML-DSA-44 by 17% and classical Ed25519 by 3.5x. Verification-heavy workloads (e.g., block
validation, certificate chains) benefit most from Falcon.

**ML-DSA-44 has the fastest key generation** among PQC schemes (36,065 ops/sec), on par with
Ed25519 (34,711 ops/sec). This makes ML-DSA well-suited for ephemeral key scenarios.

**Ed25519 is the fastest signer** overall (34,335 ops/sec) -- roughly 5x faster than ML-DSA-44
sign and 4.9x faster than Falcon-512 sign. The signing cost is the main penalty for Falcon
in signing-heavy workloads.

**SLH-DSA-SHA2-128f signing is 52 ops/sec** -- roughly 660x slower than Ed25519 sign. Its
advantage is the smallest key sizes in the suite (32-byte pubkey, 64-byte seckey) and no
lattice-based assumptions, but the 17 KB signature and slow signing make it impractical for
high-throughput applications.

**Falcon-512 multicore signing scales 8.6x from 1→10 cores** (6,122 → 52,702 ops/sec at 86%
efficiency), demonstrating near-linear throughput scaling when signing is parallelized.

**Falcon-512 signature sizes are highly concentrated**: mean 655 B with std dev 2.2 B (CV < 0.35%)
over 10,000 real signatures. The NIST-specified maximum is 666 B. The 99th percentile is only
660 B -- 99% of signatures are <= 660 B even though the protocol reserves up to 666 B.

---

## What Remains

| Item | Notes |
|------|-------|
| Larger message sizes | Current benchmarks use 256-byte messages; typical TLS certificates are 1-4 KB, blockchain transactions 500 B-10 KB -- impact on signing latency is worth measuring |
| ARM vs x86 comparison | M2 Pro is high-end ARM; Intel/AMD results needed to assess portability of the speedup story |
| ML-DSA-65 and Falcon-1024 scaling | Only Falcon-512 multicore scaling was measured; the higher-security variants deserve the same treatment |
| Cold-cache vs warm-cache analysis | Current benchmarks warm up before timing; cold-start latency (first-call cost) is relevant for infrequent signing scenarios |
| Integration overhead measurement | Attaching these primitives to a real protocol layer (TLS handshake, JWT signing) would quantify how much non-crypto overhead matters |

---

## Methodology

### Hardware
- **Machine:** Apple M2 Pro (MacBook Pro), 10-core (8P + 2E), 16 GB unified memory
- **OS:** macOS 15 (Darwin 25.3.0, xnu-12377.81.4~5, ARM64 T6020)
- **Frequency:** ~3.5 GHz (P-cores); used only for cycle estimates, not for timing

### Software Stack
- **Compiler:** Apple Clang 17.0.0, flags: `-O3 -march=native -ffast-math -Wall -Wextra`
- **PQC library:** liboqs 0.15.0 (built from source, installed to `liboqs_install/`)
- **Classical library:** OpenSSL 3.6.1 (Homebrew, `/opt/homebrew/opt/openssl`)
- **Timing:** `clock_gettime(CLOCK_MONOTONIC)` -- nanosecond resolution wall clock

### Measurement Protocol
- **Warm-up:** 100 iterations before any timed block (prevents cold-cache effects and JIT-style CPU state issues)
- **Iteration counts:** 10,000 for single-algorithm benchmarks; 1,000 per algorithm for the 7-way comparison (SLH-DSA is slow enough that 1,000 iterations already takes ~20 seconds)
- **Thread safety:** Each thread in multicore benchmarks creates its own `OQS_SIG *` context (required by liboqs for Falcon signing -- Falcon's key expansion is not thread-safe if contexts are shared)
- **Synchronization:** Custom `barrier_t` ensures all threads start timing simultaneously; `t_start` is recorded after barrier release
- **Output:** Each benchmark emits structured JSON in addition to human-readable tables; all results logged to `benchmarks/results/run_YYYYMMDD_HHMMSS/`

---

## Repository Layout

```
qMEMO/
├── benchmarks/
│   ├── src/           11 C benchmark programs + bench_common.h
│   ├── bin/           compiled binaries (git-ignored)
│   ├── results/       timestamped run logs with JSON output
│   └── Makefile       build + run targets
├── docs/              BENCHMARK_REPORT.md, COMPREHENSIVE_COMPARISON.md,
│                      QUANTUM_THREAT_ANALYSIS.md, SYSTEM_SPECS.md, ...
├── archive/
│   ├── blockchain_prototype/   earlier Python devnet (not in scope)
│   ├── session_notes/          working notes
│   └── docs/                   superseded architecture docs
├── liboqs_install/    liboqs headers + static library (git-ignored)
├── install_liboqs.sh  reproducible liboqs build script
└── README.md
```
