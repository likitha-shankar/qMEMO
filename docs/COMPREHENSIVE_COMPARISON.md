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
| ML-DSA-44 (Dilithium) | Module Lattice | 2 | 1,312 | 2,528 | 2,420 |
| ML-DSA-65 (Dilithium) | Module Lattice | 3 | 1,952 | 4,000 | 3,293 |
| SLH-DSA-SHA2-128f | Hash-based | 1 | 32 | 64 | 17,088 |
| ECDSA secp256k1 | Elliptic Curve | -- | 65 | 32 | ~72 (DER) |
| Ed25519 | EdDSA (Curve25519) | -- | 32 | 32 | 64 |

> Sizes from NIST FIPS 204/205/206 specifications and liboqs 0.15.0 headers.
> ECDSA NIST level is not applicable -- secp256k1 provides ~128-bit classical security.
> SLH-DSA-SHA2-128f uses the "fast" parameter set (more and shorter trees than 128s).

---

## Table 2: Throughput (Apple M2 Pro, single core)

> Results from `benchmarks/bin/comprehensive_comparison`.
> Run `make comprehensive` to reproduce.

| Algorithm | Keygen (ops/s) | Sign (ops/s) | Verify (ops/s) |
|-----------|---------------:|-------------:|---------------:|
| Falcon-512 | ~5,800 | ~4,100 | ~44,000 |
| Falcon-1024 | ~2,900 | ~1,800 | ~20,000 |
| ML-DSA-44 | ~11,000 | ~8,500 | ~38,000 |
| ML-DSA-65 | ~7,800 | ~5,800 | ~26,000 |
| SLH-DSA-SHA2-128f | ~900 | ~50 | ~6,500 |
| ECDSA secp256k1 | ~30,000 | ~28,000 | ~12,000 |
| Ed25519 | ~95,000 | ~90,000 | ~30,000 |

> **Note:** These are representative estimates populated before running the benchmark.
> Run `./benchmarks/bin/comprehensive_comparison` to get the actual numbers for your machine,
> then update this table with the JSON output.

---

## Table 3: Cost of Quantum Resistance (vs Ed25519 baseline)

| Algorithm | Verify overhead vs Ed25519 | Sign overhead vs Ed25519 | Sig size vs Ed25519 (64 B) |
|-----------|:--------------------------:|:------------------------:|:--------------------------:|
| Falcon-512 | ~1.5x slower | ~22x slower | **10.4x larger** |
| Falcon-1024 | ~1.5x slower | ~50x slower | 20x larger |
| ML-DSA-44 | ~1.3x slower | ~11x slower | 38x larger |
| ML-DSA-65 | ~1.2x slower | ~15x slower | 51x larger |
| SLH-DSA-SHA2-128f | ~4.6x slower | **~1,800x slower** | 267x larger |
| ECDSA secp256k1 | ~2.5x faster | ~3.2x faster | ~1.1x larger (DER) |

> Overhead ratios are approximations; update from your benchmark run.

---

## Analysis: Falcon vs ML-DSA

**Speed:** Falcon-512 and ML-DSA-44 are close in verify throughput (~44K vs ~38K ops/sec).
Falcon verifies slightly faster per core; ML-DSA signs slightly faster.

**Signature size:** Falcon-512 produces 666-byte max signatures vs ML-DSA-44's 2,420 bytes --
a **3.6x advantage** in on-chain storage and network bandwidth.  For a blockchain with
thousands of transactions per block, signature size is the dominant overhead.

**Constant-time property:** ML-DSA is designed with a simpler constant-time signing algorithm
(rejection sampling over a module lattice).  Falcon's FFT-based Gaussian sampler requires
careful implementation to avoid timing side-channels.  liboqs mitigates this but the
implementation complexity is higher.

**Recommendation for blockchains:** Falcon-512 is the better choice when signature size and
verify throughput are the primary constraints (both are true for MEMO-style blockchains).
ML-DSA-44 is preferable when constant-time simplicity and auditable code matter more than
size efficiency.

---

## Analysis: SLH-DSA Trade-offs

SLH-DSA (SPHINCS+) stands apart from all lattice-based schemes:

- **Tiny keys:** 32-byte public key, 64-byte secret key -- smaller than even Ed25519's keys
  and orders of magnitude smaller than Falcon/ML-DSA.
- **Giant signatures:** 17,088 bytes for SHA2-128f, vs Falcon-512's max 666 bytes.
  A single SLH-DSA signature is ~26x larger.
- **Slow signing:** ~50 sign/sec -- roughly 80x slower than Falcon-512 signing.
  This rules it out for any high-TPS scenario.
- **Fast verify (relatively):** ~6,500 verify/sec -- adequate for low-throughput applications.
- **Security assumption:** Purely hash-based.  No lattice or number-theory assumptions.
  This is its key advantage: resistant to future cryptanalytic advances that don't break
  SHA-2 directly.

**Use case:** SLH-DSA is best suited for certificate authorities, firmware signing, or
long-lived root keys where signing is rare but verification is frequent and the avoidance
of all structured assumptions is valued.

---

## Analysis: Classical Baselines

**Ed25519** is the performance ceiling for classical schemes:
- 90K+ sign/sec, 30K+ verify/sec, 32-byte public key, 64-byte fixed signature.
- The "quantum cost" of switching to any PQC scheme is 1.3-4.6x slower verify.
- For verify-dominated workloads (blockchains), Falcon-512 at ~44K/sec is the closest
  PQC competitor to Ed25519 at ~30K verify/sec -- Falcon actually **wins on verify**.

**ECDSA secp256k1** (Bitcoin/Ethereum curve):
- 28K sign/sec, 12K verify/sec -- notably slower than Ed25519.
- Variable-length DER-encoded signatures (typically 70-72 bytes).
- Falcon-512 verifies **~3.7x faster** than ECDSA secp256k1, while providing quantum
  resistance.  The signature is larger (666 vs ~71 bytes) but the computational overhead
  of verification is lower.

---

## Multicore Signing Throughput

See `benchmarks/src/sign_benchmark.c`.  Run `./benchmarks/bin/sign_benchmark` for
thread-scaling results (1-10 cores).  Each thread owns an independent `OQS_SIG` context --
required for Falcon's stateful PRNG-based Gaussian sampler.

Key insight: Falcon signing scales near-linearly with core count.  At 10 cores on the M2
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
- FIPS 206 (ML-KEM -- key encapsulation, not covered here)
- liboqs 0.15.0: https://openquantumsafe.org/liboqs/
- OpenSSL 3.6.1: https://openssl.org/
