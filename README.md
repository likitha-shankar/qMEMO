# qMEMO — Post-Quantum Cryptographic Signatures for Blockchain

**Graduate Research, Illinois Institute of Technology, Chicago**

---

## Research Question

**How do post-quantum signature schemes (Falcon, ML-DSA, SLH-DSA) compare to classical
baselines (ECDSA, Ed25519) in signing throughput, verification throughput, and on-chain
overhead -- and do they scale adequately for high-TPS blockchain workloads?**

---

## Key Results

| Metric | Value |
|--------|-------|
| Falcon-512 verify (single core) | 23,877-31,133 ops/sec across platforms |
| ML-DSA-44 verify (x86, AVX-512) | 49,060 ops/sec (2x faster than Falcon on x86) |
| Falcon-512 vs ECDSA verify | **7.6-8.1x faster**, with quantum resistance |
| Falcon-512 signature size | 666 B max (3.2x smaller than ML-DSA-44) |
| 10-thread Falcon-512 scaling | 184K-299K ops/sec (91-94% efficiency) |
| Blockchain e2e TPS (baseline) | 2,223 TPS (stub verify), ~1,500-1,800 projected with Falcon |

Full numerical data: **[docs/RESULTS.md](docs/RESULTS.md)**

---

## Architecture

| Component | Description |
|-----------|-------------|
| `benchmarks/` | 11 standalone C benchmark programs (Falcon, ML-DSA, SLH-DSA, ECDSA, Ed25519) |
| `blockchain/` | Fork of blockchain_pos_v45 with hybrid PQC signature support (ECDSA / Falcon-512 / Hybrid) |
| `docs/` | Research documentation, analysis, and consolidated results |
| `scripts/` | Automation, Chameleon Cloud setup, analysis scripts |

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

The blockchain supports three signature schemes via compile-time selection:

```bash
cd blockchain

# ECDSA secp256k1 (default)
make

# Falcon-512 (requires liboqs)
make SIG_SCHEME=2

# Hybrid ECDSA + Falcon-512 (requires liboqs)
make SIG_SCHEME=3
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

| Path | Description |
|------|-------------|
| `benchmarks/src/` | C benchmark sources (11 programs + `bench_common.h`) |
| `benchmarks/bin/` | Compiled binaries (after `make all`) |
| `benchmarks/results/` | Timestamped per-run output logs |
| `blockchain/src/` | Blockchain PoS with pluggable crypto backend |
| `blockchain/include/` | Headers: `crypto_backend.h`, `transaction.h`, `wallet.h` |
| `blockchain/proto/` | Protobuf schema for wire format |
| `docs/` | Research documentation and analysis |
| `scripts/` | `run_logged.sh`, `chameleon_setup.sh`, analysis scripts |
| `liboqs_install/` | Local liboqs 0.15.0 install |
| `install_liboqs.sh` | Builds and installs liboqs locally |

---

## Hardware Platforms

| Platform | CPU | Cores | Location |
|----------|-----|------:|----------|
| Apple M2 Pro | ARM64, ~3.5 GHz (P-cores) | 10 (6P + 4E) | Local |
| Xeon Gold 6242 | Cascade Lake @ 2.80 GHz | 64 logical | Chameleon Cloud |
| Xeon Gold 6126 | Skylake-SP @ 2.60 GHz | 48 logical | Chameleon Cloud |

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/RESULTS.md](docs/RESULTS.md) | **All numerical results** — throughput, scaling, sizes, blockchain baseline |
| [docs/COMPREHENSIVE_COMPARISON.md](docs/COMPREHENSIVE_COMPARISON.md) | 7-algorithm trade-off analysis |
| [docs/QUANTUM_THREAT_ANALYSIS.md](docs/QUANTUM_THREAT_ANALYSIS.md) | Post-quantum threat landscape |
| [docs/REAL_WORLD_ADOPTION.md](docs/REAL_WORLD_ADOPTION.md) | PQC adoption in production systems |
| [docs/LIBRARY_SURVEY.md](docs/LIBRARY_SURVEY.md) | Survey of PQC libraries |
| [docs/BUILD_CONFIG.md](docs/BUILD_CONFIG.md) | liboqs build flags and version details |
| [docs/ARCHITECTURE_DIAGRAM.md](docs/ARCHITECTURE_DIAGRAM.md) | System architecture diagrams |
| [docs/MEMO_CODE_ANALYSIS.md](docs/MEMO_CODE_ANALYSIS.md) | Pre-integration code audit |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | Known limitations and caveats |
| [blockchain/README.md](blockchain/README.md) | Blockchain-specific documentation |

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
