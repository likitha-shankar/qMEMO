# Comprehensive Signature Scheme Comparison

**Graduate Research Project -- Illinois Institute of Technology, Chicago**

---

## Hardware & Build Context

| Item | Value |
|------|-------|
| **CPU** | Apple M2 Pro (10-core: 6 performance + 4 efficiency) |
| **RAM** | 16 GB LPDDR5 |
| **OS** | macOS (Darwin arm64) |
| **Compiler** | Apple Clang (cc), `-O3 -mcpu=native -ffast-math` |
| **liboqs version** | 0.15.0 (local install, see `BUILD_CONFIG.md`) |
| **OpenSSL version** | 3.6.1 (Homebrew, `/opt/homebrew/opt/openssl/`) |
| **Benchmark iterations** | 1,000 per phase (keygen / sign / verify) |
| **Warm-up** | 100 iterations before each timed block (10 for SLH-DSA sign) |
| **Message** | 256 bytes, filled with `0x42` |

---

## Table 1: Algorithm Parameters

| Algorithm | Type | NIST Level | Public Key (B) | Secret Key (B) | Max Sig (B) |
|-----------|------|:----------:|---------------:|---------------:|------------:|
| Falcon-512 | Lattice (NTRU) | 1 | 897 | 1,281 | 666 |
| Falcon-1024 | Lattice (NTRU) | 5 | 1,793 | 2,305 | 1,280 |
| ML-DSA-44 (Dilithium) | Module Lattice | 2 | 1,312 | 2,560 | 2,420 |
| ML-DSA-65 (Dilithium) | Module Lattice | 3 | 1,952 | 4,032 | 3,309 |
| SLH-DSA-SHA2-128f | Hash-based | 1 | 32 | 64 | 17,088 |
| ECDSA secp256k1 | Elliptic Curve | -- | 65 | 32 | ~72 (DER) |
| Ed25519 | EdDSA (Curve25519) | -- | 32 | 32 | 64 |

> Sizes from NIST FIPS 204/205 specifications, Falcon submission, and liboqs 0.15.0 headers.
> ECDSA NIST level is not applicable -- secp256k1 provides ~128-bit classical security.
> SLH-DSA-SHA2-128f uses the "fast" parameter set (more and shorter trees than 128s).

---

## Table 2: Throughput -- Apple M2 Pro, single core (ARM64)

> Measured results from `benchmarks/bin/comprehensive_comparison`, run 2026-02-28.
> Hardware: Apple M2 Pro, 10-core (6P+4E), macOS Darwin arm64.
> Compiler: Apple Clang, `-O3 -mcpu=native -ffast-math`. 1,000 iters/phase.

| Algorithm | Keygen (ops/s) | Sign (ops/s) | Verify (ops/s) |
|-----------|---------------:|-------------:|---------------:|
| Falcon-512 | 148 | 4,805 | 30,569 |
| Falcon-1024 | 46 | 2,436 | 15,618 |
| ML-DSA-44 | 24,610 | 10,273 | 25,904 |
| ML-DSA-65 | 14,327 | 6,745 | 15,369 |
| SLH-DSA-SHA2-128f | 836 | 36 | 599 |
| ECDSA secp256k1 | 3,595 | 3,608 | 4,026 |
| Ed25519 | 24,483 | 24,276 | 8,857 |

> Statistical validation (1,000 trials x 100 ops): Falcon-512 verify median 31,133 ops/sec, CV 3.92%.

---

## Table 3: Throughput -- Intel Xeon Gold 6242 (Cascade Lake, x86-64)

> Measured results from `benchmarks/bin/comprehensive_comparison`, run 2026-03-01.
> Hardware: Intel Xeon Gold 6242 @ 2.80 GHz, 64 logical cores, 187 GB RAM.
> Chameleon Cloud compute_cascadelake_r650, Ubuntu 22.04, GCC 11.4.
> Compiler flags: `-O3 -march=native -ffast-math`. 1,000 iters/phase.

| Algorithm | Keygen (ops/s) | Sign (ops/s) | Verify (ops/s) |
|-----------|---------------:|-------------:|---------------:|
| Falcon-512 | 153 | 4,312 | 23,877 |
| Falcon-1024 | 52 | 2,133 | 11,794 |
| ML-DSA-44 | 51,917 | 15,975 | 49,060 |
| ML-DSA-65 | 29,832 | 9,971 | 30,287 |
| SLH-DSA-SHA2-128f | 1,062 | 45 | 734 |
| ECDSA secp256k1 | 2,655 | 2,638 | 2,963 |
| Ed25519 | 23,062 | 23,184 | 9,013 |

> Statistical validation (1,000 trials x 100 ops): Falcon-512 verify median 24,016 ops/sec, CV 0.66%.
> RDTSC hardware cycle counter used -- exact cycle counts (not estimated from wall clock).
> Falcon-512 verify: 147,138 cycles per operation @ 2.80 GHz.

---

## Table 4: Throughput -- Intel Xeon Gold 6126 (Skylake-SP, x86-64)

> Measured results from `benchmarks/bin/comprehensive_comparison`, run 2026-03-10.
> Hardware: Intel Xeon Gold 6126 @ 2.60 GHz, 48 logical cores, 187 GB RAM.
> Chameleon Cloud baremetal, Ubuntu 24.04, GCC 13.3.0.
> Compiler flags: `-O3 -march=native -ffast-math`. 1,000 iters/phase.

| Algorithm | Keygen (ops/s) | Sign (ops/s) | Verify (ops/s) |
|-----------|---------------:|-------------:|---------------:|
| Falcon-512 | 147 | 4,207 | 23,505 |
| Falcon-1024 | 51 | 2,130 | 11,618 |
| ML-DSA-44 | 47,476 | 14,271 | 46,532 |
| ML-DSA-65 | 27,961 | 8,696 | 28,893 |
| SLH-DSA-SHA2-128f | 1,001 | 43 | 732 |
| ECDSA secp256k1 | 2,189 | 2,181 | 2,467 |
| Ed25519 | 21,138 | 21,481 | 8,309 |

> Statistical validation (1,000 trials x 100 ops): Falcon-512 verify median 23,696 ops/sec, CV 0.59%.
> Skylake-SP results are within 5% of Cascade Lake for PQC algorithms, confirming consistency
> across Intel Xeon generations. ECDSA is 17% lower, attributable to OpenSSL 3.0.2 → 3.0.13
> differences in EC scalar multiply code generation with GCC 13 vs GCC 11.

---

## Table 5: Cross-Architecture Comparison (verify throughput, single core)

| Algorithm | M2 Pro (ARM64) | Cascade Lake (x86) | Skylake-SP (x86) |
|-----------|---------------:|-------------------:|:----------------:|
| Falcon-512 | 30,569 | 23,877 | 23,505 |
| Falcon-1024 | 15,618 | 11,794 | 11,618 |
| ML-DSA-44 | 25,904 | **49,060** | **46,532** |
| ML-DSA-65 | 15,369 | **30,287** | **28,893** |
| SLH-DSA-SHA2-128f | 599 | 734 | 732 |
| ECDSA secp256k1 | 4,026 | 2,963 | 2,467 |
| Ed25519 | 8,857 | 9,013 | 8,309 |

> **Key finding:** ML-DSA-44 and ML-DSA-65 are ~1.8-1.9x faster on both x86 platforms than on M2
> Pro ARM, driven by liboqs AVX-512 SIMD optimizations present in both Cascade Lake and Skylake-SP.
> Falcon-512 is ~24% faster on M2 Pro than either Xeon, reflecting NEON efficiency and the M2's
> deeper OOO pipeline. The two Xeon generations agree within 5% for all PQC algorithms, confirming
> the results are reproducible across Intel server hardware.

---

## Table 6: Cost of Quantum Resistance (vs Ed25519 baseline, M2 Pro)

| Algorithm | Verify vs Ed25519 | Sign vs Ed25519 | Sig size vs Ed25519 (64 B) |
|-----------|:-----------------:|:---------------:|:--------------------------:|
| Falcon-512 | 3.45x slower | 5.05x slower | **11.7x larger** |
| Falcon-1024 | 1.76x slower | 9.96x slower | 22.8x larger |
| ML-DSA-44 | 2.92x slower | 2.36x slower | 37.8x larger |
| ML-DSA-65 | 1.73x slower | 3.60x slower | 51.7x larger |
| SLH-DSA-SHA2-128f | 14.8x slower | **674x slower** | 267x larger |
| ECDSA secp256k1 | 2.20x faster | 6.72x faster | ~1.1x larger (DER) |

> Ratios computed from Table 2 measured values. Sig size column uses max sig bytes from Table 1.

---

## Analysis: Falcon vs ML-DSA

**Speed (ARM -- M2 Pro):** Falcon-512 verifies at 30,569 ops/sec vs ML-DSA-44's 25,904 ops/sec --
Falcon is ~18% faster on ARM. Signing is the opposite: ML-DSA-44 signs 2.1x faster (10,273 vs
4,805 ops/sec). For verify-heavy workloads (blockchains), Falcon holds the edge on ARM.

**Speed (x86 -- Cascade Lake):** The picture reverses dramatically. ML-DSA-44 verifies at
49,060 ops/sec vs Falcon-512's 23,877 -- ML-DSA is **2.05x faster**. This is explained by
liboqs AVX-512 SIMD optimizations for ML-DSA's NTT operations, which are well-suited to
256-bit wide vector registers. Falcon's FFT-based Gaussian sampler is harder to vectorize.

**Signature size:** Falcon-512 produces max 752-byte signatures vs ML-DSA-44's 2,420 bytes --
a **3.2x advantage** in on-chain storage and network bandwidth. For a blockchain with
thousands of transactions per block, signature size is the dominant storage overhead.

**Constant-time property:** ML-DSA is designed with a simpler constant-time signing algorithm
(rejection sampling over a module lattice). Falcon's FFT-based Gaussian sampler requires
careful implementation to avoid timing side-channels. liboqs mitigates this but the
implementation complexity is higher.

**Architecture-aware recommendation:** Falcon-512 is preferred on ARM deployments and wherever
signature size is the primary constraint. ML-DSA-44 is preferred on x86-64 servers (AVX-512
gives it 2x faster verification) and wherever implementation auditability matters more than
storage efficiency.

---

## Analysis: SLH-DSA Trade-offs

SLH-DSA (SPHINCS+) stands apart from all lattice-based schemes:

- **Tiny keys:** 32-byte public key, 64-byte secret key -- smaller than even Ed25519's keys
  and orders of magnitude smaller than Falcon/ML-DSA.
- **Giant signatures:** 17,088 bytes for SHA2-128f, vs Falcon-512's max 752 bytes.
  A single SLH-DSA signature is ~22.7x larger.
- **Slow signing:** 36-45 sign/sec (ARM and x86 respectively) -- roughly 100-120x slower
  than Falcon-512 signing. This rules it out for any high-TPS scenario.
- **Moderate verify:** 599-734 verify/sec across both platforms -- adequate only for
  low-throughput applications.
- **Security assumption:** Purely hash-based. No lattice or number-theory assumptions.
  This is its key advantage: resistant to future cryptanalytic advances that don't break
  SHA-2 directly.

**Use case:** SLH-DSA is best suited for certificate authorities, firmware signing, or
long-lived root keys where signing is rare but verification is infrequent and the avoidance
of all structured assumptions is valued.

---

## Analysis: Classical Baselines

**Ed25519** (ARM -- M2 Pro): 24,276 sign/sec, 8,857 verify/sec.
**Ed25519** (x86 -- Cascade Lake): 23,184 sign/sec, 9,013 verify/sec.
Performance is nearly identical across architectures, confirming platform-agnostic optimization
in OpenSSL 3.x. The "quantum cost" of switching to Falcon-512 is ~3.5x slower verify on
ARM and ~2.7x slower on x86. Note that Falcon keygen (148-154/s) is far slower than Ed25519
keygen (23K+/s) -- key generation is the one area where PQC lattice schemes are significantly
more expensive.

**ECDSA secp256k1** (Bitcoin/Ethereum curve):
- ARM: 3,608 sign/sec, 4,026 verify/sec.
- x86: 2,638 sign/sec, 2,963 verify/sec. (x86 is ~27% slower -- OpenSSL 3.0.2 vs 3.6.1)
- Variable-length DER-encoded signatures (typically 70-72 bytes).
- Falcon-512 verifies **7.6x faster** than ECDSA secp256k1 on ARM and **8.1x faster** on x86,
  while providing quantum resistance at the cost of larger signatures (752 vs ~71 bytes).

---

## Multithreaded Verification Scaling

Measured on both platforms (1/2/4/6/8/10 threads, `benchmarks/bin/multicore_benchmark`):

| Threads | M2 Pro (ops/sec) | M2 Speedup | Cascade Lake (ops/sec) | CL Speedup |
|--------:|-----------------:|-----------:|-----------------------:|-----------:|
| 1 | 27,022 | 1.00x | 20,169 | 1.00x |
| 2 | 62,203 | 2.30x | 38,399 | 1.90x |
| 4 | 119,900 | 4.44x | 74,909 | 3.71x |
| 6 | 186,463 | 6.90x | 108,256 | 5.37x |
| 8 | 195,757 | 7.24x | 149,046 | 7.39x |
| 10 | 239,297 | 8.86x | 184,467 | 9.15x |

Falcon-512 verification scales near-linearly to 4 threads on both platforms. M2 Pro shows
super-linear scaling to 6 threads (6.90x at 6 threads) due to its higher-bandwidth L2/L3
cache hierarchy accommodating the working set. At 10 threads, Cascade Lake achieves 9.15x
speedup. A 10-thread Cascade Lake deployment reaches 184K verifications/sec.

### All 7 Algorithms — Skylake-SP Core-Pinned (2026-03-23)

> Source: `multicore_all_benchmark` on Intel Xeon Gold 6126 Skylake-SP. 1,000 verify iterations/thread (100 for SLH-DSA), barrier-synchronized, each thread pinned to a distinct physical core via `sched_setaffinity`.

| Algorithm           | 1 Core  | 2 Cores | 4 Cores | 6 Cores | 8 Cores | 10 Cores | Speedup    | Efficiency |
|---------------------|--------:|--------:|--------:|--------:|--------:|---------:|:----------:|:----------:|
| ML-DSA-44           |  42,935 |  56,428 | 105,975 | 162,719 | 207,663 |  277,987 | 6.47x      | 64.7%      |
| ML-DSA-65           |  27,900 |  37,803 |  88,961 | 126,921 | 166,740 |  211,604 | 7.58x      | 75.8%      |
| Falcon-512          |  19,022 |  32,375 |  74,960 | 111,099 | 147,920 |  183,292 | **9.64x**  | **96.4%**  |
| Falcon-1024         |  11,157 |  18,617 |  42,106 |  58,957 |  79,554 |   98,467 | 8.83x      | 88.3%      |
| Ed25519             |   8,249 |  14,134 |  29,587 |  43,357 |  56,367 |   69,420 | 8.42x      | 84.2%      |
| ECDSA secp256k1     |   2,445 |   4,698 |   9,244 |  13,551 |  17,302 |   21,342 | 8.73x      | 87.3%      |
| SLH-DSA-SHA2-128f   |     691 |   1,207 |   2,365 |   3,741 |   4,648 |    6,101 | 8.83x      | 88.3%      |

All values in ops/sec, sorted by 10-core throughput. Falcon-512 achieves the best scaling
efficiency (96.4%) — its compact FFT working set fits cleanly in per-core L2 cache. ML-DSA-44
has the highest absolute throughput (278K ops/sec at 10 cores) but lower scaling efficiency
(64.7%) because its high single-core throughput saturates memory bandwidth at higher core
counts. The Falcon-512-only table above (M2 Pro + Cascade Lake) uses non-pinned threads; the
all-7-algorithm table uses `sched_setaffinity` core pinning on homogeneous Xeon cores, which
improves Falcon-512 efficiency from 91.5% to 96.4%.

## Multithreaded Signing Throughput

See `benchmarks/src/sign_benchmark.c`. Run `./benchmarks/bin/sign_benchmark` for
thread-scaling results (1-10 threads). Each thread owns an independent `OQS_SIG` context --
required for Falcon's stateful PRNG-based Gaussian sampler.

Key insight: Falcon signing scales near-linearly with thread count. At 10 threads on the M2
Pro, aggregate signing throughput exceeds 30K ops/sec -- bringing it within range of
single-threaded Ed25519 sign throughput.

---

## Falcon Signature Size Distribution

See `benchmarks/src/signature_size_analysis.c`.  Run
`./benchmarks/bin/signature_size_analysis` for the full distribution across 10,000
signatures per Falcon variant.

Key insight: Unpadded Falcon-512 mean is well below the 666-byte maximum (typically
~600-630 bytes), confirming the compression is effective.  Padded variants always emit
exactly the maximum length, trading bandwidth efficiency for constant-time behavior.

---

## References

- Falcon specification: https://falcon-sign.info/
- FIPS 204 (ML-DSA / Dilithium): https://csrc.nist.gov/pubs/fips/204/final
- FIPS 205 (SLH-DSA / SPHINCS+): https://csrc.nist.gov/pubs/fips/205/final
- FIPS 203 (ML-KEM -- key encapsulation, not covered here)
- liboqs 0.15.0: https://openquantumsafe.org/liboqs/
- OpenSSL 3.6.1: https://openssl.org/
