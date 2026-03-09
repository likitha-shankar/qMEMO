# Official Specifications vs Measured Results

**Graduate Research Project — Illinois Institute of Technology, Chicago**

---

## Table 1: Sizes

| Algorithm             | Official Pub Key | Official Sec Key      | Official Sig Size | Our Pub Key | Our Sec Key | Our Sig Size  | Note                                          |
|-----------------------|:----------------:|:---------------------:|:-----------------:|:-----------:|:-----------:|:-------------:|-----------------------------------------------|
| Falcon-512            | 897 B            | ~1,998 B (est.)       | 666 B (max)       | 897 B ✓     | 1,281 B     | 666 B ✓       | Sec key 36% smaller — official uses rough 3× sig heuristic; FIPS 206 is precise |
| Falcon-1024           | 1,793 B          | ~3,840 B (est.)       | 1,280 B (max)     | 1,793 B ✓   | 2,305 B     | 1,280 B ✓     | Same issue — FIPS 206 encoding is more compact than estimate |
| ML-DSA-44             | 1,312 B          | 2,528 B               | 2,420 B           | 1,312 B ✓   | 2,528 B ✓   | 2,420 B ✓     | Exact match                                   |
| ML-DSA-65             | 1,952 B          | 4,000 B               | 3,293 B           | 1,952 B ✓   | 4,000 B ✓   | 3,293 B ✓     | Exact match                                   |
| SLH-DSA-SHA2-128f     | 32 B             | 64 B                  | 17,088 B          | 32 B ✓      | 64 B ✓      | 17,088 B ✓    | Exact match                                   |
| ECDSA secp256k1       | 65 B             | 32 B                  | ~72 B (DER)       | 65 B ✓      | 32 B ✓      | ~72 B ✓       | Exact match                                   |
| Ed25519               | 32 B             | 32 B                  | 64 B              | 32 B ✓      | 32 B ✓      | 64 B ✓        | Exact match                                   |

> Sources: Falcon/FN-DSA → FIPS 206 | ML-DSA → FIPS 204 | SLH-DSA → FIPS 205 | Ed25519 → RFC 8032 | ECDSA → SEC 1

---

## Table 2: Signing Performance (ops/sec, single core)

| Algorithm             | Official Value       | Official Hardware       | Our Cascade Lake      | Our M2 Pro          | Why Different                                                          |
|-----------------------|:--------------------:|:-----------------------:|:---------------------:|:-------------------:|------------------------------------------------------------------------|
| Falcon-512            | 5,948                | i5-8259U @ 2.3 GHz      | 4,312  **(−28%)**     | 4,805  **(−19%)**   | i5-8259U turbos to 3.8 GHz; Xeon held at 2.8 GHz base. Clock-bound op |
| Falcon-1024           | 2,913                | i5-8259U @ 2.3 GHz      | 2,133  **(−27%)**     | 2,436  **(−16%)**   | Same turbo gap; larger lattice doubles compute cost vs Falcon-512      |
| ML-DSA-44             | 7,806  (AVX2)        | i7-6600U @ 2.6 GHz      | **15,975  (+105%)**   | **10,273  (+32%)**  | liboqs 0.15.0 uses AVX-512 NTT; official ref uses older AVX2           |
| ML-DSA-65             | 4,917  (AVX2)        | i7-6600U @ 2.6 GHz      | **9,971   (+103%)**   | **6,745   (+37%)**  | Same AVX-512 uplift; larger module dimension adds ~40% cost vs ML-DSA-44 |
| SLH-DSA-SHA2-128f     | N/A                  | —                       | 45                    | 36                  | No official benchmark published on sphincs.org                         |
| Ed25519               | ~27,415              | Xeon E5620 @ 2.4 GHz    | 23,184  **(−15%)**    | 24,276  **(−11%)**  | Westmere ref is 15-yr-old tuned asm; OpenSSL 3.x adds EVP abstraction  |
| ECDSA secp256k1       | ~4,000–8,000         | OpenSSL 3.x typical     | 2,638                 | 3,608               | Cascade Lake runs OpenSSL 3.0.2 vs M2's 3.6.1 — ~27% version gap      |

---

## Table 3: Verification Performance (ops/sec, single core)

| Algorithm             | Official Value       | Official Hardware       | Our Cascade Lake      | Our M2 Pro          | Why Different                                                          |
|-----------------------|:--------------------:|:-----------------------:|:---------------------:|:-------------------:|------------------------------------------------------------------------|
| Falcon-512            | 27,933               | i5-8259U @ 2.3 GHz      | 23,877  **(−15%)**    | **30,569  (+9%)**   | M2 OOO pipeline + NEON beats turbo i5 on FFT poly arithmetic           |
| Falcon-1024           | 13,650               | i5-8259U @ 2.3 GHz      | 11,794  **(−14%)**    | **15,618  (+14%)**  | Same M2 advantage; Xeon clock deficit vs turbo i5                      |
| ML-DSA-44             | 21,966  (AVX2)       | i7-6600U @ 2.6 GHz      | **49,060  (+123%)**   | **25,904  (+18%)**  | AVX-512 in liboqs 0.15.0 vs AVX2 in official ref; NTT vectorizes well  |
| ML-DSA-65             | 14,483  (AVX2)       | i7-6600U @ 2.6 GHz      | **30,287  (+109%)**   | **15,369   (+6%)**  | Same AVX-512 uplift; larger module adds ~39% cost vs ML-DSA-44         |
| SLH-DSA-SHA2-128f     | N/A                  | —                       | 734                   | 599                 | No official benchmark published on sphincs.org                         |
| Ed25519               | ~8,782               | Xeon E5620 @ 2.4 GHz    | 9,013   **(+3%)**     | 8,857   **(+1%)**   | Near-identical — within noise; Ed25519 verify is platform-agnostic     |
| ECDSA secp256k1       | ~2,000–5,000         | OpenSSL 3.x typical     | 2,963                 | 4,026               | OpenSSL version gap (3.0.2 vs 3.6.1) on Cascade Lake                  |

---

## Table 4: Keygen Performance (ops/sec, single core)

| Algorithm             | Official Value       | Official Hardware       | Our Cascade Lake      | Our M2 Pro           | Why Different                                                          |
|-----------------------|:--------------------:|:-----------------------:|:---------------------:|:--------------------:|------------------------------------------------------------------------|
| Falcon-512            | ~116  (8.64 ms)      | i5-8259U @ 2.3 GHz      | **153   (+32%)**      | **148   (+28%)**     | liboqs 0.15.0 AVX2/NEON keygen path vs plain reference C impl          |
| Falcon-1024           | ~36   (27.45 ms)     | i5-8259U @ 2.3 GHz      | **52    (+44%)**      | **46    (+28%)**     | Same library improvement; NTT-based keygen benefits from SIMD          |
| ML-DSA-44             | ~20,963  (ref C)     | i7-6600U @ 2.6 GHz      | **51,917  (+148%)**   | **24,610  (+17%)**   | AVX-512 vectorized NTT dominates keygen; ref C has no SIMD             |
| ML-DSA-65             | ~4,779   (ref C)     | i7-6600U @ 2.6 GHz      | **29,832  (+524%)**   | **14,327  (+200%)**  | Same; ref C keygen for Dilithium3 is severely unoptimized vs AVX-512   |
| SLH-DSA-SHA2-128f     | N/A                  | —                       | 1,062                 | 836                  | No official benchmark published on sphincs.org                         |
| Ed25519               | ~27,415  (est.)      | Xeon E5620 @ 2.4 GHz    | 23,062  **(−16%)**    | 24,483  **(−11%)**   | Same EVP abstraction overhead as sign; inherently fast operation        |
| ECDSA secp256k1       | ~4,000–8,000         | OpenSSL 3.x typical     | 2,655                 | 3,595                | OpenSSL 3.0.2 version gap on Cascade Lake                              |

---

> **Official sources:**
> Falcon → falcon-sign.info  |  ML-DSA → pq-crystals.org/dilithium  |  SLH-DSA → sphincs.org (FIPS 205, no perf data)
> Ed25519 → ed25519.cr.yp.to  |  ECDSA → OpenSSL `openssl speed` (no canonical page)
> FIPS 204, 205, 206 → csrc.nist.gov

---

## PQC vs Classical Baselines — How Good Are the Results?

Classical baselines used: **Ed25519** (modern, fast, tight keys) and **ECDSA secp256k1** (Bitcoin/Ethereum standard).
All numbers are from our measured results. Ratios use the average of Cascade Lake + M2 Pro values.

---

### Table 5: Sign Speed — PQC vs Classical

| PQC Algorithm         | PQC Sign (avg)  | vs ECDSA secp256k1 (avg ~3,100/s) | vs Ed25519 (avg ~23,700/s) | Verdict              |
|-----------------------|:---------------:|:---------------------------------:|:--------------------------:|:--------------------:|
| Falcon-512            | ~4,560/s        | **+47% faster**                   | 5.2× slower                | Acceptable           |
| Falcon-1024           | ~2,280/s        | −26% slower                       | 10.4× slower               | Poor                 |
| ML-DSA-44             | ~13,100/s       | **+323% faster**                  | 1.8× slower                | **Good**             |
| ML-DSA-65             | ~8,360/s        | **+170% faster**                  | 2.8× slower                | Acceptable           |
| SLH-DSA-SHA2-128f     | ~41/s           | 76× slower                        | 578× slower                | **Unacceptable**     |

> Falcon-512 and ML-DSA already beat ECDSA secp256k1 at signing.
> Against Ed25519, ML-DSA-44 comes closest — only 1.8× slower while being quantum-resistant.

---

### Table 6: Verify Speed — PQC vs Classical

| PQC Algorithm         | PQC Verify (avg) | vs ECDSA secp256k1 (avg ~3,500/s) | vs Ed25519 (avg ~8,930/s) | Verdict              |
|-----------------------|:----------------:|:---------------------------------:|:-------------------------:|:--------------------:|
| Falcon-512            | ~27,200/s        | **7.8× faster**                   | **3.0× faster**           | **Excellent**        |
| Falcon-1024           | ~13,700/s        | **3.9× faster**                   | **1.5× faster**           | **Good**             |
| ML-DSA-44             | ~37,500/s        | **10.7× faster**                  | **4.2× faster**           | **Excellent**        |
| ML-DSA-65             | ~22,800/s        | **6.5× faster**                   | **2.6× faster**           | **Good**             |
| SLH-DSA-SHA2-128f     | ~665/s           | −81% slower                       | 13.4× slower              | **Poor**             |

> Counterintuitive result: Falcon-512 and ML-DSA verify **faster than Ed25519** — PQC wins at verification.
> This is the critical metric for high-TPS systems where millions of signatures are verified per second.

---

### Table 7: Signature & Key Size — PQC vs Classical

| PQC Algorithm         | Sig Size  | vs ECDSA (~72 B)    | vs Ed25519 (64 B)   | Pub Key   | vs Ed25519 (32 B)  | Verdict              |
|-----------------------|:---------:|:-------------------:|:-------------------:|:---------:|:------------------:|:--------------------:|
| Falcon-512            | 666 B     | 9.3× larger         | 10.4× larger        | 897 B     | 28× larger         | Moderate overhead    |
| Falcon-1024           | 1,280 B   | 17.8× larger        | 20× larger          | 1,793 B   | 56× larger         | High overhead        |
| ML-DSA-44             | 2,420 B   | 33.6× larger        | 37.8× larger        | 1,312 B   | 41× larger         | High overhead        |
| ML-DSA-65             | 3,293 B   | 45.7× larger        | 51.5× larger        | 1,952 B   | 61× larger         | Very high overhead   |
| SLH-DSA-SHA2-128f     | 17,088 B  | 237× larger         | 267× larger         | 32 B      | 1× (same)          | Prohibitive sig size |

> Falcon-512 has the best size/performance tradeoff among all PQC schemes.
> SLH-DSA's tiny keys (matching Ed25519) are offset entirely by its 17 KB signatures.

---

### Table 8: Overall PQC Assessment

| Algorithm             | Sign         | Verify        | Size          | Overall Verdict   | Best Use Case                                          |
|-----------------------|:------------:|:-------------:|:-------------:|:-----------------:|--------------------------------------------------------|
| **Falcon-512**        | Acceptable   | Excellent     | Moderate      | **Recommended**   | Verify-heavy systems, ARM deployments, tight bandwidth |
| Falcon-1024           | Poor         | Good          | High          | Selective use     | Max security (Level 5) where signing rate is not critical |
| **ML-DSA-44**         | Good         | Excellent     | High          | **Recommended**   | x86 servers (AVX-512), sign-heavy workloads            |
| ML-DSA-65             | Acceptable   | Good          | Very high     | Selective use     | Level 3 security when ML-DSA-44 is insufficient        |
| SLH-DSA-SHA2-128f     | Unacceptable | Poor          | Prohibitive   | Not recommended   | Root CAs, firmware signing — rare sign, rare verify    |

> **Key takeaway:** PQC verification is already faster than classical. The cost of quantum resistance
> is paid primarily in signing speed (1.8–10× slower vs Ed25519) and signature size (10–267× larger).
> Falcon-512 and ML-DSA-44 are production-viable for verify-dominated workloads today.
