# qMEMO — Post-Quantum Cryptographic Signatures for Blockchain

**Graduate Research, Illinois Institute of Technology, Chicago**

---

## Research Question

**How do post-quantum signature schemes (Falcon, ML-DSA, SLH-DSA) compare to classical
baselines (ECDSA, Ed25519) in signing throughput, verification throughput, and on-chain
overhead -- and do they scale adequately for high-TPS blockchain workloads?**

## Quick Navigation

- **Start with outcomes:** `docs/FINDINGS.md`
- **Full numbers and tables:** `docs/RESULTS.md`
- **Run-level evidence artifacts:** `docs/RUN_EVIDENCE.md`
- **System architecture:** `docs/ARCHITECTURE.md`
- **Implementation details:** `docs/TECHNICAL_REFERENCE.md`
- **Baseline vs qMEMO changes:** `docs/HARSHA_BASELINE_DIFF.md`

---

## Key Results

| Metric                              | Value                                                                  |
|--------------------------------------|------------------------------------------------------------------------|
| Falcon-512 verify (single core)      | 23,877 ops/sec (Xeon 6242); 23,505–30,569 ops/sec across platforms     |
| ML-DSA-44 verify (x86, AVX-512)      | 49,060 ops/sec (Xeon 6242) — 2.1x faster verify than Falcon on x86    |
| Falcon-512 vs ECDSA verify           | **7.6–8.1x faster**, with quantum resistance                           |
| Falcon-512 signature size            | 666 B max (3.7x smaller than ML-DSA-44)                               |
| 10-thread Falcon-512 scaling         | 183K ops/sec, **96.4% efficiency** (Skylake-SP)                        |
| Blockchain e2e TPS (1M TX, Falcon)   | **11,182 TPS** best run; 10,378 / 10,923 / 11,182 (3 independent runs) |
| Blockchain e2e TPS (ML-DSA-44)       | 1,533 TPS — 40% below Falcon (larger sigs bottleneck I/O)              |
| Confirmation rate (1M TX)            | **100%** across all three Falcon-512 production-scale runs              |
| Apr 18 repeat matrix (18 runs)       | **All valid** (100% confirmation, 0 errors); canonical bundle in `benchmarks/results/hybrid_matrix_apr18_final/` |
| Hybrid mode                          | ECDSA + Falcon + ML-DSA coexist on same chain, no hard fork            |

**Start here:** [docs/FINDINGS.md](docs/FINDINGS.md) — consolidated research findings
**Full numerical data:** [docs/RESULTS.md](docs/RESULTS.md)

---

## Architecture

| Component       | Description                                                                           |
|-----------------|---------------------------------------------------------------------------------------|
| `benchmarks/`   | 11 standalone C benchmark programs (Falcon, ML-DSA, SLH-DSA, ECDSA, Ed25519)         |
| `blockchain/`   | Fork of blockchain_pos_v45 with hybrid PQC signature support (ECDSA / Falcon / Hybrid)|
| `docs/`         | Research documentation, analysis, and consolidated results                            |
| `scripts/`      | Automation, Chameleon Cloud setup, analysis scripts                                   |

---

## Getting Started

### Prerequisites

- **OS:** macOS (Apple Silicon) or Linux (x86-64)
- **Build tools:** Git, CMake, Ninja (or Make), C compiler (Clang/GCC)
- **OpenSSL 3.x:** `brew install openssl` on macOS
- **liboqs 0.15.0:** Built locally via `install_liboqs.sh`
- **Blockchain additional:** ZeroMQ, Protobuf-C, OpenMP

### Build Benchmarks

```bash
# 1. Build liboqs locally
./install_liboqs.sh

# 2. Build all benchmark binaries
cd benchmarks && make all

# 3. Run all benchmarks
make run
```

### Build Blockchain

The blockchain supports four signature configurations via compile-time selection:

```bash
cd blockchain

# ECDSA secp256k1 (default)
make

# Falcon-512 (requires liboqs)
make SIG_SCHEME=2

# Hybrid — ECDSA + Falcon-512 + ML-DSA-44 (requires liboqs)
make SIG_SCHEME=3

# ML-DSA-44 (requires liboqs)
make SIG_SCHEME=4
```

See [blockchain/README.md](blockchain/README.md) for full blockchain documentation.

### Full Pipeline (timestamped results + Markdown report)

```bash
./scripts/run_all_benchmarks.sh
python3 scripts/analyze_results.py
python3 scripts/generate_report.py
```

---

## Repository Structure

| Path                  | Description                                                    |
|-----------------------|----------------------------------------------------------------|
| `benchmarks/src/`     | C benchmark sources (11 programs + `bench_common.h`)           |
| `benchmarks/bin/`     | Compiled binaries (after `make all`)                           |
| `benchmarks/results/` | Timestamped per-run output logs                                |
| `blockchain/src/`     | Blockchain PoS with pluggable crypto backend                   |
| `blockchain/include/` | Headers: `crypto_backend.h`, `transaction.h`, `wallet.h`       |
| `blockchain/proto/`   | Protobuf schema for wire format                                |
| `docs/`               | Research documentation and analysis                            |
| `scripts/`            | `run_logged.sh`, `chameleon_setup.sh`, analysis scripts        |
| `liboqs_install/`     | Local liboqs 0.15.0 install                                   |
| `install_liboqs.sh`   | Builds and installs liboqs locally                             |

## Folder READMEs

To keep the repository self-explanatory, each major folder includes a local `README.md`:

| Folder | Local README |
|--------|--------------|
| `benchmarks/` | `benchmarks/README.md` |
| `benchmarks/src/` | `benchmarks/src/README.md` |
| `benchmarks/bin/` | `benchmarks/bin/README.md` |
| `benchmarks/results/` | `benchmarks/results/README.md` |
| `blockchain/` | `blockchain/README.md` |
| `blockchain/include/` | `blockchain/include/README.md` |
| `blockchain/src/` | `blockchain/src/README.md` |
| `blockchain/proto/` | `blockchain/proto/README.md` |
| `blockchain/build/` | `blockchain/build/README.md` |
| `blockchain/benchmark_results/` | `blockchain/benchmark_results/README.md` |
| `blockchain_base/` | `blockchain_base/README.md` |
| `blockchain_base/include/` | `blockchain_base/include/README.md` |
| `blockchain_base/src/` | `blockchain_base/src/README.md` |
| `blockchain_base/proto/` | `blockchain_base/proto/README.md` |
| `blockchain_base/build/` | `blockchain_base/build/README.md` |
| `docs/` | `docs/README.md` |
| `scripts/` | `scripts/README.md` |

---

## Hardware Platforms

| Platform        | CPU                          | Cores          | Location         |
|-----------------|------------------------------|---------------:|------------------|
| Apple M2 Pro    | ARM64, ~3.5 GHz (P-cores)   | 10 (6P + 4E)  | Local            |
| Xeon Gold 6242  | Cascade Lake @ 2.80 GHz     | 64 logical     | Chameleon Cloud  |
| Xeon Gold 6126  | Skylake-SP @ 2.60 GHz       | 48 logical     | Chameleon Cloud  |

---

## Documentation

| Document                                                     | Description                                                           |
|--------------------------------------------------------------|-----------------------------------------------------------------------|
| [docs/FINDINGS.md](docs/FINDINGS.md)                        | **Key research findings** — concise conclusions and recommendations   |
| [docs/RESULTS.md](docs/RESULTS.md)                          | **Canonical metrics** — benchmark tables and run metadata             |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)                | Detailed runtime architecture: components, ports, and message flow    |
| [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md)  | Implementation-level reference: structures, protocol, validation path |
| [docs/RUN_EVIDENCE.md](docs/RUN_EVIDENCE.md)                | Two canonical 1M runs (Ed25519 + Falcon) with log/CSV consistency     |
| [docs/HARSHA_BASELINE_DIFF.md](docs/HARSHA_BASELINE_DIFF.md)| Baseline-vs-current delta (`blockchain_base/` to `blockchain/`)       |
| [docs/PROFESSOR_SUMMARY.md](docs/PROFESSOR_SUMMARY.md)      | High-level progress snapshot for advisor updates                      |
| [blockchain/README.md](blockchain/README.md)                | Blockchain-specific build/run documentation                           |

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

This project is licensed under the [MIT License](LICENSE).
