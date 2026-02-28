# Post-Quantum Cryptographic Signature Benchmarking Suite

**Graduate Research Project -- Illinois Institute of Technology, Chicago**

---

## Research Question

**How do post-quantum signature schemes (Falcon, ML-DSA, SLH-DSA) compare to classical
baselines (ECDSA, Ed25519) in signing throughput, verification throughput, and on-chain
overhead -- and do they scale adequately for high-TPS blockchain workloads?**

---

## Key Findings

### Throughput -- single core

| Algorithm | Apple M2 Pro (ARM64) | Intel Xeon Gold 6242 (x86) |
|-----------|---------------------:|---------------------------:|
| Falcon-512 verify | 31,133 ops/sec | 23,885 ops/sec |
| ML-DSA-44 verify | 25,904 ops/sec | **48,627 ops/sec** |
| Ed25519 verify | 8,857 ops/sec | 9,013 ops/sec |
| ECDSA secp256k1 verify | 4,026 ops/sec | 2,963 ops/sec |

> ML-DSA-44 is 2x faster than Falcon-512 on x86 due to liboqs AVX-512 optimizations.
> Falcon-512 leads on ARM. Measurement stability: CV 3.92% (ARM) vs 0.67% (x86 bare-metal).

### Other findings

| Metric | Value |
|--------|-------|
| **Falcon-512 vs ECDSA verify** | Falcon 7.6x faster (ARM), 8.1x faster (x86) |
| **Falcon-512 signature size** | Max 752 bytes vs ML-DSA-44's 2,420 bytes (3.2x smaller) |
| **SLH-DSA-SHA2-128f sign rate** | 36-45 ops/sec -- unsuitable for high-TPS |
| **10-core Falcon-512 verify** | 239K ops/sec (ARM), 177K ops/sec (x86) -- ~8.8x speedup |
| **Cycle count (RDTSC, x86)** | 146,778 cycles/verify @ 2.80 GHz |

---

## Benchmark Suite (11 programs)

| Binary | Source | What it measures |
|--------|--------|-----------------|
| `verify_benchmark` | `verify_benchmark.c` | Single-pass Falcon-512 verify throughput, 10K iterations |
| `statistical_benchmark` | `statistical_benchmark.c` | 1,000 trials x 100 ops; median, IQR, CV, Jarque-Bera |
| `comparison_benchmark` | `comparison_benchmark.c` | Falcon-512 vs ML-DSA-44 side-by-side |
| `multicore_benchmark` | `multicore_benchmark.c` | Falcon-512 verify throughput: 1/2/4/6/8/10 cores |
| `concurrent_benchmark` | `concurrent_benchmark.c` | Thread-pool concurrent verification |
| `concurrent_signing_benchmark` | `concurrent_signing_benchmark.c` | Thread-pool concurrent signing |
| `sign_benchmark` | `sign_benchmark.c` | Falcon-512 **signing** throughput: 1/2/4/6/8/10 cores |
| `signature_size_analysis` | `signature_size_analysis.c` | Falcon-512/padded-512/1024/padded-1024 size distributions (10K sigs each) |
| `classical_benchmark` | `classical_benchmark.c` | ECDSA secp256k1 + Ed25519 baselines (OpenSSL 3.x EVP API) |
| `comprehensive_comparison` | `comprehensive_comparison.c` | All 7 algorithms side-by-side: keygen / sign / verify throughput + key/sig sizes |
| `key_inspection` | `key_inspection.c` | Key material inspection and correctness audit for all 7 algorithms |

---

## Quick Start

### Prerequisites

- **OS:** macOS (Apple Silicon) or Linux
- **Build:** Git, CMake, Ninja (or Make), C compiler (Clang/GCC)
- **OpenSSL 3.x:** `brew install openssl` on macOS (`/opt/homebrew/opt/openssl/`)
- **Optional:** Python 3.10+ and NumPy for analysis scripts

### Installation

```bash
# 1. Install liboqs (Open Quantum Safe) locally
./install_liboqs.sh

# 2. Build all 10 benchmarks
cd benchmarks && make all
```

### Run All Benchmarks

```bash
cd benchmarks && make run
```

### Run Individually

```bash
./bin/verify_benchmark
./bin/statistical_benchmark
./bin/comparison_benchmark
./bin/multicore_benchmark
./bin/concurrent_benchmark
./bin/concurrent_signing_benchmark
./bin/sign_benchmark
./bin/signature_size_analysis
./bin/classical_benchmark
./bin/comprehensive_comparison
./bin/key_inspection
```

### Full Pipeline (timestamped results + Markdown report)

```bash
./scripts/run_all_benchmarks.sh
python3 scripts/analyze_results.py
python3 scripts/generate_report.py
```

---

## Repository Structure

| Path | Description |
|------|-------------|
| `benchmarks/src/` | C benchmark sources (11 programs + `bench_common.h`) |
| `benchmarks/bin/` | Compiled binaries (after `make all`) |
| `benchmarks/results/` | Timestamped per-run log files |
| `docs/` | Research documentation, analysis, and system specs |
| `scripts/` | Automation: `run_logged.sh` (timestamped runs), `chameleon_setup.sh` (bare-metal bootstrap) |
| `liboqs_install/` | Local liboqs 0.15.0 install (built by `install_liboqs.sh`) |
| `install_liboqs.sh` | Builds and installs liboqs locally |

---

## Methodology

- **Implementation:** C benchmarks using **liboqs 0.15.0** (Falcon-512, Falcon-1024,
  ML-DSA-44/65, SLH-DSA-SHA2-128f) and **OpenSSL 3.6.1** (ECDSA secp256k1, Ed25519).
- **Timing:** `clock_gettime(CLOCK_MONOTONIC)` -- nanosecond-precision, monotonic.
  Warm-up phases precede every timed loop; no I/O or logging inside timed sections.
- **Statistics:** 1,000 independent trials for stability; median and IQR reported when
  distribution is non-Gaussian (Jarque-Bera test).
- **Multicore:** pthread barrier synchronisation ensures all threads enter the timed
  section simultaneously; `t_start` is recorded after the barrier releases.
- **Thread safety:** Signing benchmarks give each thread its own `OQS_SIG` context
  (required -- Falcon signing is stateful) and call `OQS_thread_stop()` on exit.

---

## Hardware

Benchmarks run on two platforms:

| Platform | CPU | Cores | RAM | OS |
|----------|-----|------:|----:|-----|
| Apple M2 Pro | ARM64, ~3.5 GHz (P-cores) | 10 | 16 GB | macOS Darwin arm64 |
| Chameleon Cloud (compute_cascadelake_r650) | Intel Xeon Gold 6242 @ 2.80 GHz | 64 | 187 GB | Ubuntu 22.04 |

The Cascade Lake node uses RDTSC for exact hardware cycle counting.
See **[docs/SYSTEM_SPECS.md](docs/SYSTEM_SPECS.md)** for full profiles.

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/BENCHMARK_REPORT.md](docs/BENCHMARK_REPORT.md) | Full methodology and result tables |
| [docs/COMPREHENSIVE_COMPARISON.md](docs/COMPREHENSIVE_COMPARISON.md) | 7-algorithm comparison, trade-off analysis |
| [docs/QUANTUM_THREAT_ANALYSIS.md](docs/QUANTUM_THREAT_ANALYSIS.md) | Post-quantum threat landscape |
| [docs/SYSTEM_SPECS.md](docs/SYSTEM_SPECS.md) | Test hardware details |
| [docs/BUILD_CONFIG.md](docs/BUILD_CONFIG.md) | liboqs build flags and library version |
| [docs/LIBRARY_SURVEY.md](docs/LIBRARY_SURVEY.md) | Survey of PQC libraries |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | Known limitations and caveats |
| [docs/VALIDATION.md](docs/VALIDATION.md) | Cross-validation against published benchmarks |

---

## Limitations

- **Reference implementation:** liboqs is a portable reference build. Platform-optimized
  builds (AVX-512 for ML-DSA on x86, NEON for Falcon on ARM) could improve throughput 2-3x
  for some algorithms. We use the reference build for fair cross-architecture comparison.
- **OpenSSL throughput:** Ed25519 and ECDSA numbers reflect OpenSSL's EVP API;
  hand-optimised libraries (libsodium, secp256k1) can be faster.
- **Two platforms only:** Results cover ARM64 (Apple M2 Pro) and x86-64 (Cascade Lake).
  Other microarchitectures (AMD Zen, RISC-V) are not yet measured.

---

## References

- [Falcon -- Fast-Fourier Lattice-based Compact Signatures over NTRU](https://falcon-sign.info/)
- [FIPS 204 -- ML-DSA (Dilithium)](https://csrc.nist.gov/pubs/fips/204/final)
- [FIPS 205 -- SLH-DSA (SPHINCS+)](https://csrc.nist.gov/pubs/fips/205/final)
- [NIST Post-Quantum Cryptography Standardization](https://csrc.nist.gov/projects/post-quantum-cryptography)
- [Open Quantum Safe (OQS) -- liboqs](https://openquantumsafe.org/liboqs/)
- [OpenSSL 3.x](https://openssl.org/)

---

## License

TBD
