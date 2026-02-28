# Post-Quantum Cryptography Library Survey

**qMEMO Project -- Illinois Institute of Technology, Chicago**

This document surveys major post-quantum cryptography (PQC) libraries, compares their features and suitability for blockchain use, and recommends **liboqs** for the MEMO project.

---

## 1. liboqs (Open Quantum Safe)

**Project:** [openquantumsafe.org](https://openquantumsafe.org/) | **Source:** [github.com/open-quantum-safe/liboqs](https://github.com/open-quantum-safe/liboqs)

| Attribute | Details |
|-----------|---------|
| **Language(s)** | C (core); language bindings for Python, Go, Rust, Java (via OQS project) |
| **Algorithms** | **Falcon** (512, 1024), **Dilithium** (2, 3, 5), **SPHINCS+** (multiple variants), **Kyber** (512, 768, 1024); NTRU, FrodoKEM, SIKE, Classic McEliece, and others (optional at build time) |
| **Maturity** | **Production-oriented** -- used in research and early deployment; NIST-aligned API; regular releases (e.g. 0.11.x in 2024) |
| **Performance** | Benchmarks included (`speed_sig`, `speed_kem`); Falcon-512 verify ~44K ops/sec (single core, typical); optional optimized builds (e.g. ARM NEON) for higher throughput |
| **License** | MIT (core); some algorithm code under CC0, Apache 2.0, or BSD (documented per file) |
| **Active development** | **Yes** -- Post-Quantum Cryptography Alliance (Linux Foundation); active maintainers and issue tracking |
| **Platform support** | x86_64, ARM64 (including Apple Silicon); Windows, macOS, Linux; optional embedded-friendly builds (no OS deps) |
| **Pros** | Unified C API; NIST standards first-class; TLS/X.509 prototypes; used by qMEMO benchmarks; good docs and build options |
| **Cons** | Some algorithms from PQClean (reference); production use should follow best practices and optional audits |

---

## 2. PQClean

**Project:** [github.com/PQClean/PQClean](https://github.com/PQClean/PQClean)

| Attribute | Details |
|-----------|---------|
| **Language(s)** | C (portable, minimal-dependency implementations) |
| **Algorithms** | **Falcon**, **Dilithium**, **SPHINCS+**, **Kyber**, Classic McEliece, NTRU, SABER, FrodoKEM, and many other NIST PQC submissions; organized by `crypto_kem` and `crypto_sign` |
| **Maturity** | **Reference / experimental** -- "clean" implementations for testing and integration; not intended as a standalone production library; security disclaimer (no formal audit) |
| **Performance** | Portable C; no assembly; used as dependency by liboqs and others for some algorithms; performance varies by algorithm |
| **License** | **Varies by algorithm** -- many public domain (CC0) or permissive (MIT, BSD); see repo and per-directory LICENSE |
| **Active development** | **Yes** -- updates for NIST finalists and standards; maintained by PQC community |
| **Platform support** | Any platform with a C compiler; 32/64-bit; no platform-specific optimizations |
| **Pros** | Clean, readable code; broad algorithm coverage; easy to integrate into other projects; minimal dependencies |
| **Cons** | No single "library" API; not a drop-in replacement for application use; reference only; no TLS/protocol integration |

---

## 3. BouncyCastle (Java)

**Project:** [bouncycastle.org](https://www.bouncycastle.org/) | **Java:** `org.bouncycastle.pqc.*`

| Attribute | Details |
|-----------|---------|
| **Language(s)** | **Java** (JCE provider); C# (BouncyCastle.Cryptography) with overlapping but not identical PQC support |
| **Algorithms** | **Falcon**, **Dilithium**, **SPHINCS+**, **Kyber** (NIST standards); plus Classic McEliece, Picnic, NTRU, NTRU Prime, SABER, FrodoKEM, HQC, BIKE; hybrid (PQC + classical) via `HybridValueParameterSpec` |
| **Maturity** | **Production** -- widely used in Java ecosystem; NIST PQC support from 1.79+; stable API |
| **Performance** | JVM-dependent; no low-level benchmarks in this survey; adequate for server/mobile; not comparable to optimized C for high-TPS verification |
| **License** | **MIT** (Bouncy Castle License) |
| **Active development** | **Yes** -- regular releases; NIST standard updates (e.g. 1.79, 1.83) |
| **Platform support** | Any Java platform (x86, ARM, embedded JVMs); C# on .NET / .NET Core |
| **Pros** | Familiar JCE API; one-stop crypto in Java; hybrid and NIST PQC; good for wallets, servers, enterprise |
| **Cons** | Java/C# only -- not suitable for C-based MEMO node or high-throughput C benchmarks; GC and JVM overhead for bulk verify |

---

## 4. golang.org/x/crypto

**Project:** [pkg.go.dev/golang.org/x/crypto](https://pkg.go.dev/golang.org/x/crypto) | **Source:** [go.googlesource.com/crypto](https://go.googlesource.com/crypto)

| Attribute | Details |
|-----------|---------|
| **Language(s)** | **Go** |
| **Algorithms** | Primarily **classical** (ChaCha20, Poly1305, Ed25519, X25519, etc.). **Post-quantum:** Kyber (ML-KEM) and Dilithium (ML-DSA) exist in Go community packages (e.g. circl, custom bindings); `golang.org/x/crypto` itself has limited first-party PQC--focus remains on classical and hash-based primitives. NIST PQC standards (FIPS 203/204/205) are being adopted in third-party Go libs. |
| **Maturity** | **Production** for classical; PQC in `x/crypto` is **limited or experimental**; full NIST stack (e.g. Falcon) often via wrappers or other repos |
| **Performance** | Go is fast but typically behind optimized C for raw crypto; no Falcon-first benchmarking in standard tree |
| **License** | **BSD-3-Clause** |
| **Active development** | **Yes** -- Google-maintained; PQC adoption in ecosystem growing |
| **Platform support** | All Go platforms (x86, ARM, WASM, etc.) |
| **Pros** | Standard Go crypto; good for Go services and tooling; permissive license |
| **Cons** | **Falcon not in core**; PQC coverage less complete than liboqs; not ideal for MEMO's C-based, Falcon-512-focused stack |

---

## 5. libpqcrypto

**Project:** [libpqcrypto.org](https://libpqcrypto.org/) | **PQCRYPTO project**

| Attribute | Details |
|-----------|---------|
| **Language(s)** | **C** (primary); **Python** (unified interface); CLI tools |
| **Algorithms** | **77 systems** (50 signature, 27 KEM) from NIST submissions: **Kyber**, **Dilithium**, **SPHINCS+**, Falcon, Classic McEliece, NTRU, SABER, FrodoKEM, and many others; unified `pqcrypto_*` API (NaCl/libsodium style) |
| **Maturity** | **Research / academic** -- broad coverage; automatic "fastest" implementation selection; not widely deployed as primary PQC library in production systems |
| **Performance** | Auto-selects fastest implementation per system; includes symmetric (AES, ChaCha20, SHA-512); no MEMO-specific benchmarks cited here |
| **License** | **Mixed** -- components from many submissions; public domain and various open licenses per algorithm; see project and subpackages |
| **Active development** | **Moderate** -- PQCRYPTO project; updates for NIST and algorithm changes; smaller community than liboqs |
| **Platform support** | C compilers; 32/64-bit; dependencies (e.g. OpenSSL, GMP) for full build; ~200MB, many inodes for full install |
| **Pros** | Very broad algorithm set; single C API; Python and CLI; good for comparison and research |
| **Cons** | Heavier dependency story; less focus on NIST-only production use; Falcon available but ecosystem and TLS story weaker than liboqs for MEMO |

---

## 6. Microsoft PQCrypto-VPN

**Project:** [github.com/microsoft/PQCrypto-VPN](https://github.com/microsoft/PQCrypto-VPN) | **Microsoft Research**

| Attribute | Details |
|-----------|---------|
| **Language(s)** | C (OpenVPN + PQC integration); uses algorithms from OQS and others |
| **Algorithms** | **Frodo**, **SIKE**, **Picnic** (early NIST candidates); later integrations use **Open Quantum Safe** stack (Kyber, Dilithium, etc.) for TLS 1.3 hybrid PQC |
| **Maturity** | **Experimental** -- explicitly not for protecting sensitive data; testbed for PQC in VPNs |
| **Performance** | VPN-focused; not a general-purpose signature/KEM library for blockchain verification |
| **License** | **MIT** |
| **Active development** | **Low / archival** -- research project; OpenVPN fork; users directed to mainline OpenVPN and OQS for current PQC |
| **Platform support** | Platforms supported by OpenVPN build |
| **Pros** | Early PQC-in-VPN research; Microsoft backing; MIT |
| **Cons** | **Not a standalone PQC library**; no Falcon-first use case; experimental only; not suitable as MEMO's crypto backend |

---

## 7. wolfSSL (PQC support)

**Project:** [wolfssl.com](https://www.wolfssl.com/) | **Commercial + open-source options**

| Attribute | Details |
|-----------|---------|
| **Language(s)** | C (core); used from C/C++, Rust, Go, etc. |
| **Algorithms** | **Kyber (ML-KEM)** -- all levels, pure and hybrid with ECDHE; **Dilithium (ML-DSA)** -- Levels 2, 3, 5, pure and hybrid; **Falcon** (signatures); **SPHINCS+** / SLH-DSA; stateful (LMS/HSS, XMSS); ARM assembly for Kyber |
| **Maturity** | **Production** -- FIPS 203/204 (and 205) aligned; used in TLS, DTLS, wolfSSH, wolfMQTT; commercial support available |
| **Performance** | Optimized (including ARM); Kyber included for commercial customers; Falcon ~28K verify/sec cited in docs; suitable for embedded and server |
| **License** | **Dual** -- GPLv2 or commercial; some PQC (e.g. Kyber) offered free for commercial customers; check current terms |
| **Active development** | **Yes** -- active PQC roadmap; FIPS and NIST alignment |
| **Platform support** | x86, ARM (including 32/64, Cortex-M); embedded; Linux, Windows, macOS |
| **Pros** | Full TLS/protocol stack; Falcon + Dilithium + Kyber; production use; good for appliances and VPNs |
| **Cons** | **License** -- GPL or commercial may not fit all MEMO/opensource-only stacks; less "research benchmark" focus than liboqs; MEMO already standardized on liboqs for benchmarking |

---

## 8. OpenSSL (NIST PQC branch / mainline)

**Project:** [openssl.org](https://www.openssl.org/) | **OQS fork:** [github.com/open-quantum-safe/openssl](https://github.com/open-quantum-safe/openssl)

| Attribute | Details |
|-----------|---------|
| **Language(s)** | C |
| **Algorithms** | **OpenSSL 3.5 (mainline):** **ML-KEM** (Kyber) 512/768/1024, **ML-DSA** (Dilithium) 44/65/87, **SLH-DSA** (SPHINCS+); hybrid TLS (e.g. X25519MLKEM768). **Falcon** -- not in 3.5 mainline; NIST FIPS 206 (Falcon) due later; may appear in future or via provider. **OQS-OpenSSL fork** -- used to integrate liboqs (Falcon, etc.); now **unsupported**; migration path is **OQS-Provider** for OpenSSL 3. |
| **Maturity** | **Production** (mainline 3.5); PQC in mainline is new (2025); OQS fork is deprecated |
| **Performance** | Mainline 3.5 PQC performance good on modern hardware; key sizes larger than classical; hybrid default in TLS |
| **License** | **Apache 2.0** (OpenSSL 3.x) |
| **Active development** | **Yes** -- OpenSSL 3.5 LTS with PQC; oqs-provider for experimental algorithms (e.g. Falcon) |
| **Platform support** | All major platforms; x86, ARM; wide deployment |
| **Pros** | Ubiquitous; mainline PQC (Kyber, Dilithium, SPHINCS+); hybrid TLS; Apache 2.0 |
| **Cons** | **No Falcon in mainline yet**; MEMO needs Falcon-512; using OpenSSL for MEMO would require provider/fork or waiting for FIPS 206 integration; heavier dependency for "just signatures" in a dedicated benchmark |

---

## Comparison Table

| Library | Language(s) | Falcon | Dilithium | SPHINCS+ | Kyber | Maturity | License | Best for |
|---------|--------------|--------|-----------|----------|-------|----------|---------|----------|
| **liboqs** | C (+ bindings) | ✅ | ✅ | ✅ | ✅ | Production-oriented | MIT (mixed in deps) | **Unified PQC; MEMO; research** |
| **PQClean** | C | ✅ | ✅ | ✅ | ✅ | Reference | Per-algorithm | **Integration into other libs** |
| **BouncyCastle** | Java, C# | ✅ | ✅ | ✅ | ✅ | Production | MIT | **Java/.NET apps** |
| **golang.org/x/crypto** | Go | ❌ | Limited | Limited | Community | Production (classical) | BSD-3 | **Go services (not Falcon-first)** |
| **libpqcrypto** | C, Python | ✅ | ✅ | ✅ | ✅ | Research | Mixed | **Broad algorithm research** |
| **Microsoft PQCrypto-VPN** | C (VPN) | Via OQS | Via OQS | -- | Via OQS | Experimental | MIT | **VPN research only** |
| **wolfSSL** | C | ✅ | ✅ | ✅ | ✅ | Production | GPL / commercial | **TLS, embedded, commercial** |
| **OpenSSL** | C | ❌ (mainline) | ✅ ML-DSA | ✅ SLH-DSA | ✅ ML-KEM | Production | Apache 2.0 | **TLS/mainline PQC (no Falcon yet)** |

---

## Recommendation: Why liboqs Is Best for MEMO

MEMO uses **Falcon-512** for transaction and validator signatures and **BLAKE3-512** for hashing. The qMEMO project measures **Falcon-512 verification throughput** to validate that MEMO can meet its TPS targets. For that purpose, **liboqs is the best fit** for the following reasons.

1. **Falcon-512 first-class**  
   liboqs provides a single, stable C API for Falcon-512 (and Falcon-1024), with optional optimized code paths. OpenSSL mainline does not yet include Falcon; wolfSSL does but under a different license model. liboqs is already the basis of the qMEMO benchmarks and documentation.

2. **C core and performance**  
   MEMO nodes and our benchmarks are in C. liboqs is a C library with no runtime dependency on Java, Go, or a full TLS stack. We can build with `-O3` and architecture flags (e.g. `-mcpu=native` on Apple Silicon, `-march=native` on x86) and get reproducible, high-throughput verification numbers (e.g. ~44K verify/sec per core) that align with published results.

3. **NIST alignment without lock-in**  
   liboqs centers NIST-standardized algorithms (Falcon, Dilithium, SPHINCS+, Kyber) and is maintained under the Post-Quantum Cryptography Alliance. That matches MEMO's choice of NIST PQC and keeps the option to compare or add Dilithium/SPHINCS+ (as in the comparison benchmark) without changing libraries.

4. **Minimal dependency footprint**  
   For a benchmark or a node that only needs Falcon (and possibly BLAKE3 elsewhere), liboqs can be built as a single library with only the algorithms MEMO uses. No TLS, no OpenSSL dependency, no GPL--reducing legal and integration cost.

5. **Documentation and reproducibility**  
   Build options, algorithm list, and license are documented; the project's `install_liboqs.sh` and Makefile produce a known configuration. That supports reproducible research and clear citation (e.g. in LIMITATIONS.md and BENCHMARK_REPORT.md).

6. **Consistency across the project**  
   All current qMEMO benchmarks (verify, statistical, comparison, multicore, concurrent) use liboqs. Standardizing on one library avoids cross-library performance and API differences and keeps the "why liboqs" narrative consistent in docs (e.g. THROUGHPUT_ANALYSIS, VALIDATION, LIBRARY_SURVEY).

**Summary:** For MEMO's Falcon-512-based, C-based verification and benchmarking, **liboqs** offers the right algorithm set, API, license (MIT), performance, and ecosystem fit. Other libraries are better suited to Java (BouncyCastle), full TLS in C (wolfSSL, OpenSSL), or research breadth (libpqcrypto, PQClean), but they do not replace liboqs as the recommended core PQC library for MEMO and the qMEMO project.
