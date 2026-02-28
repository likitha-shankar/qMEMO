# qMEMO Architecture Diagram

**qMEMO Project — Illinois Institute of Technology, Chicago**

This document provides ASCII architecture diagrams covering:
1. [Current benchmark pipeline](#1-benchmark-pipeline-current)
2. [MEMO blockchain internals](#2-memo-blockchain-internals)
3. [Falcon-512 integration points in MEMO](#3-falcon-512-integration-points)
4. [Planned test MEMO blockchain setup](#4-planned-test-memo-blockchain-future-goal)

---

## 1. Benchmark Pipeline (Current)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        qMEMO BENCHMARK PIPELINE                              │
│                  Falcon-512 for MEMO Blockchain — IIT Chicago                │
└──────────────────────────────────────────────────────────────────────────────┘

 SETUP (one-time)
 ─────────────────
 ./install_liboqs.sh
      │
      ├─ Clone / update liboqs source
      ├─ Build shared library  (liboqs_install/lib/liboqs.0.15.0.dylib)
      ├─ Build static library  (liboqs_install/lib/liboqs.a)
      ├─ Run liboqs test suite  ← correctness gate
      └─ Write docs/BUILD_CONFIG.md

 COMPILE (benchmarks/Makefile)
 ──────────────────────────────
                        CC = Apple Clang 17
                        CFLAGS = -O3 -mcpu=native -ffast-math
                        LDFLAGS = -loqs [-lm] [-lpthread] + embedded rpath

  verify_benchmark.c ──► bin/verify_benchmark
  statistical_benchmark.c ─► bin/statistical_benchmark
  comparison_benchmark.c ──► bin/comparison_benchmark
  multicore_benchmark.c ───► bin/multicore_benchmark    ─┐ compiled but not
  concurrent_benchmark.c ──► bin/concurrent_benchmark   ─┘ in run pipeline yet

 RUN (scripts/run_all_benchmarks.sh)
 ─────────────────────────────────────
  1. make clean && make all
  2. mkdir results/run_YYYYMMDD_HHMMSS/
  3. Execute benchmarks, extract --- JSON --- blocks
  4. jq merge → summary.json
  5. Generate REPORT.md

  ┌─────────────────────────────────────────────────────────────┐
  │                      BENCHMARK HARNESSES                     │
  │                                                             │
  │  ┌──────────────────┐   ┌─────────────────────────────┐    │
  │  │ verify_benchmark │   │    statistical_benchmark     │    │
  │  │ ─────────────── │   │ ─────────────────────────── │    │
  │  │ 1 run            │   │ 1,000 trials × 100 ops       │    │
  │  │ 10,000 verif.    │   │ Stats: mean, median, SD, CV  │    │
  │  │ wall-clock timer │   │ JB normality, skew, kurtosis │    │
  │  │ → 42,853 ops/s   │   │ → median 44,228 ops/s        │    │
  │  └──────────────────┘   └─────────────────────────────┘    │
  │                                                             │
  │  ┌──────────────────────────────────────────────────────┐   │
  │  │              comparison_benchmark                     │   │
  │  │ ─────────────────────────────────────────────────── │   │
  │  │ Falcon-512 vs ML-DSA-44 (Dilithium)                   │   │
  │  │ keygen: 100 trials / sign: 1K trials / verify: 10K   │   │
  │  │ → Falcon 1.16× faster verify, 3.7× smaller signature │   │
  │  └──────────────────────────────────────────────────────┘   │
  │                                                             │
  │  ┌──────────────────┐   ┌─────────────────────────────┐    │
  │  │  multicore_      │   │    concurrent_benchmark      │    │
  │  │  benchmark       │   │ ─────────────────────────── │    │
  │  │ ─────────────── │   │ Scenario: 100 simultaneous   │    │
  │  │ N ∈ {1,2,4,6,8, │   │ transactions arrive at node  │    │
  │  │ 10} threads      │   │ 4-worker pthread pool vs     │    │
  │  │ pthread_barrier  │   │ sequential baseline          │    │
  │  │ speedup+effic.   │   │                              │    │
  │  └──────────────────┘   └─────────────────────────────┘    │
  └─────────────────────────────────────────────────────────────┘

 ANALYSE (Python)
 ─────────────────
  scripts/analyze_results.py          scripts/generate_report.py
       │                                       │
       ├─ Load summary.json                    ├─ Load all run JSONs
       ├─ Validate vs published baselines      ├─ Build 7-section report
       │   (Intel i5-8259U, predicted M2)      └─ → docs/BENCHMARK_REPORT.md
       ├─ Check MEMO TPS headroom (4 scenarios)
       ├─ Statistical quality (CV, outliers, JB)
       └─ → run_dir/ANALYSIS.md

 RESULTS STORE
 ──────────────
  benchmarks/results/run_20260217_211549/
  ├── system_specs.json
  ├── verify_results.json       (42,853 ops/s)
  ├── statistical_results.json  (median 44,228 ops/s, CV 3.59%)
  ├── comparison_results.json   (Falcon 1.16× faster, 3.7× smaller)
  ├── summary.json              (aggregated key metrics)
  ├── REPORT.md
  └── ANALYSIS.md
```

---

## 2. MEMO Blockchain Internals

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                     MEMO BLOCKCHAIN — HIGH-LEVEL ARCHITECTURE                │
└──────────────────────────────────────────────────────────────────────────────┘

  CONSENSUS LAYER: Proof-of-Space
  ─────────────────────────────────
  Validators prove allocated disk space ("plots").
  Block producer is chosen by the PoSpace protocol — no energy-heavy mining.
  PoSpace is hash-based → inherently quantum-safe (no discrete log).

  SHARDING LAYER
  ───────────────
  Conservative: 4 shards,  10,000 TPS total (2,500 TPS / shard)
  Target:      256 shards, 50,700 TPS total (  198 TPS / shard)

  ┌──────────┐  ┌──────────┐  ┌──────────┐     ┌──────────┐
  │  Shard 0 │  │  Shard 1 │  │  Shard 2 │ ... │ Shard 255│
  │ (chain)  │  │ (chain)  │  │ (chain)  │     │ (chain)  │
  └────┬─────┘  └────┬─────┘  └────┬─────┘     └────┬─────┘
       │              │              │                 │
       └──────────────┴──────────────┴─────────────────┘
                          Cross-shard coordinator
                          (receipts/proofs, ~20% of TXs)

  BLOCK STRUCTURE (per shard)
  ────────────────────────────
  ┌─────────────────────────────────────────────────────┐
  │  BLOCK HEADER                                        │
  │  ├── block_number, timestamp                         │
  │  ├── prev_block_hash  (BLAKE3-512)                   │
  │  ├── merkle_root      (BLAKE3-512 tree of TX hashes) │
  │  ├── validator_id + validator_signature (Falcon-512) │◄── sig verify #2
  │  └── PoSpace proof                                   │
  ├─────────────────────────────────────────────────────┤
  │  TRANSACTIONS [0..N]                                 │
  │  ├── TX 0: {from, to, amount, Falcon-512 signature}  │◄── sig verify #1
  │  ├── TX 1: {from, to, amount, Falcon-512 signature}  │
  │  │   ...                                             │
  │  └── TX N                                            │
  └─────────────────────────────────────────────────────┘

  CRYPTOGRAPHY MAP
  ─────────────────
  ┌────────────────────────────────────────────────────────────────────────┐
  │  Use case                │ Classical (at-risk)  │ MEMO (PQ-safe)       │
  │  ─────────────────────── │ ─────────────────── │ ──────────────────── │
  │  TX authorization        │ ECDSA secp256k1      │ Falcon-512 ✅         │
  │  Validator block signing │ ECDSA                │ Falcon-512 ✅         │
  │  Cross-shard receipts    │ ECDSA                │ Falcon-512 ✅         │
  │  Merkle tree             │ SHA-256 (128-bit PQ) │ BLAKE3-512 ✅         │
  │  Block hash chaining     │ SHA-256              │ BLAKE3-512 ✅         │
  │  Consensus (PoSpace)     │ —                    │ Hash-based ✅         │
  └────────────────────────────────────────────────────────────────────────┘

  TRANSACTION LIFECYCLE
  ──────────────────────
  Wallet             P2P Network              Shard Validator Node
    │                      │                          │
    │ 1. Build TX          │                          │
    │    (from,to,amount)  │                          │
    │ 2. Sign (Falcon-512) │                          │
    ├────────────────────► │                          │
    │ 3. Broadcast         │ 4. Gossip to shard peers │
    │                      ├─────────────────────────►│
    │                      │                5. Verify │ ◄─ HOT PATH
    │                      │              Falcon-512  │    benchmarked
    │                      │              + rules     │    by qMEMO
    │                      │                          │
    │                      │            6. Mempool    │
    │                      │            7. Include in │
    │                      │               block      │
    │                      │◄─────────────────────────┤
    │                      │  8. Broadcast new block  │
    │                      ├─────────────────────────►│ (other nodes)
    │                      │          9. Verify block │
    │                      │             + all TXs    │ ◄─ AGAIN
    │                      │          10. Append to   │
    │                      │              chain       │
```

---

## 3. Falcon-512 Integration Points

```
┌──────────────────────────────────────────────────────────────────────────────┐
│           FALCON-512 IN MEMO — WHERE THE BENCHMARKS APPLY                    │
└──────────────────────────────────────────────────────────────────────────────┘

  HOT PATH (every TX, every block, every full node)
  ──────────────────────────────────────────────────
  Node receives TX  →  OQS_SIG_verify(Falcon-512, tx_body, signature, pubkey)
                              │
                        ~22.7 µs / call
                        44,228 ops/s (median, M2 Pro)
                              │
                        Headroom at 2,500 TPS/shard:  17.7×  ← safe
                        Headroom at   198 TPS/shard: 223×   ← very safe

  COLD PATH (once per wallet address creation)
  ─────────────────────────────────────────────
  OQS_SIG_keypair(Falcon-512)
       │
  ~4,724 µs / call (211 keygen/s)
  Slow because: NTRU lattice basis sampling + Gram-Schmidt decomposition
  Irrelevant for validators — happens once, keys are long-lived.

  WARM PATH (once per TX, at the sender's wallet)
  ─────────────────────────────────────────────────
  OQS_SIG_sign(Falcon-512, tx_body, secret_key)
       │
  ~146 µs / call (6,872 sign/s)
  Uses discrete Gaussian sampling over NTRU lattice (rejection sampling).
  Signature size varies per call (654–658 B), compressed to 655 B typical.

  KEY SIZES (on-chain overhead per transaction)
  ──────────────────────────────────────────────
  ┌──────────────┬────────────┬────────────┬─────────────────┐
  │              │ Falcon-512 │ ML-DSA-44  │  ECDSA (ref)    │
  ├──────────────┼────────────┼────────────┼─────────────────┤
  │ Public key   │   897 B    │  1,312 B   │    33 B         │
  │ Signature    │   652 B    │  2,420 B   │  64–72 B        │
  │ TX overhead  │ 1,549 B    │  3,732 B   │  ~105 B         │
  ├──────────────┼────────────┼────────────┼─────────────────┤
  │ 4K-TX block  │   6.1 MB   │   14.6 MB  │   0.4 MB        │
  │ Annual (4sh) │  122 TB    │   293 TB   │    8 TB         │
  └──────────────┴────────────┴────────────┴─────────────────┘
  Trade-off accepted: larger on-chain footprint in exchange for
  quantum resistance.

  liboqs LIBRARY CALL GRAPH
  ──────────────────────────
  benchmark_main()
      │
      ├─ OQS_init()                    ← library init
      ├─ OQS_SIG_new("Falcon-512")     ← allocate algorithm context
      │      └─ sets: length_public_key=897, length_signature=809(max)
      │
      ├─ OQS_SIG_keypair(sig, pk, sk)  ← NTRU basis generation
      ├─ OQS_SIG_sign(sig, s, &slen, msg, msglen, sk)
      │      └─ discrete Gaussian sampling, variable output length
      │
      ├─ OQS_SIG_verify(sig, msg, msglen, s, slen, pk)  ◄─ TIMED LOOP
      │      └─ NTRU lattice verification, deterministic, ~80K cycles
      │
      ├─ OQS_MEM_secure_free(sk, sk_len)  ← memset then free (key material)
      ├─ OQS_MEM_insecure_free(pk)
      └─ OQS_destroy()
```

---

## 4. Planned Test MEMO Blockchain (Future Goal)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│              TEST MEMO BLOCKCHAIN — PLANNED ARCHITECTURE                      │
│              (Future work: end-to-end validation of qMEMO findings)           │
└──────────────────────────────────────────────────────────────────────────────┘

  GOAL
  ─────
  Move from isolated CPU benchmarks (current qMEMO) to a live test network
  where Falcon-512 verification runs inside actual MEMO node software,
  under real network and consensus conditions.

  PHASE 1: LOCAL DEVNET (single machine)
  ────────────────────────────────────────

  ┌─────────────────────────────────────────────────────────────────────┐
  │  MacBook Pro (M2 Pro, 10 cores, 16 GB)                              │
  │                                                                     │
  │  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐         │
  │  │  MEMO Node 0   │  │  MEMO Node 1   │  │  MEMO Node 2   │         │
  │  │  (Validator)   │  │  (Validator)   │  │  (Full Node)   │         │
  │  │  Port: 8000    │  │  Port: 8001    │  │  Port: 8002    │         │
  │  │  Shard: 0      │  │  Shard: 0      │  │  Shard: 0      │         │
  │  └───────┬────────┘  └───────┬────────┘  └───────┬────────┘         │
  │          └──────────────────┴──────────────────┘           │
  │                         localhost P2P (TCP)                          │
  │                                                                     │
  │  ┌─────────────────────────────────────────────────────────────┐   │
  │  │  TX Generator (Python / Go)                                  │   │
  │  │  ├─ Generates wallets (Falcon-512 keypairs via liboqs)       │   │
  │  │  ├─ Signs transactions                                       │   │
  │  │  └─ Submits at configurable TPS (target: 2,500 TPS/shard)   │   │
  │  └─────────────────────────────────────────────────────────────┘   │
  │                                                                     │
  │  ┌─────────────────────────────────────────────────────────────┐   │
  │  │  Monitoring & Profiling                                      │   │
  │  │  ├─ TPS counter (actual vs target)                           │   │
  │  │  ├─ Sig verification latency histogram (compare to qMEMO)   │   │
  │  │  ├─ CPU usage per node (Apple Instruments / perf)            │   │
  │  │  └─ Block propagation time                                   │   │
  │  └─────────────────────────────────────────────────────────────┘   │
  └─────────────────────────────────────────────────────────────────────┘

  PHASE 2: MULTI-MACHINE TESTNET (LAN / cloud)
  ─────────────────────────────────────────────

  ┌───────────────┐       ┌───────────────┐       ┌───────────────┐
  │  Machine A    │       │  Machine B    │       │  Machine C    │
  │  M2 Pro       │       │  Intel Xeon   │       │  ARM Graviton │
  │  Validator    │       │  Validator    │       │  Full Node    │
  │  Shard 0      │       │  Shard 1      │       │  Shard 0,1   │
  └──────┬────────┘       └──────┬────────┘       └──────┬────────┘
         │                        │                        │
         └────────────────────────┴────────────────────────┘
                          LAN / VPN (P2P gossip)
                                   │
                     ┌─────────────┴──────────────┐
                     │   Cross-shard coordinator   │
                     │   (receipts between shard 0 │
                     │    and shard 1)              │
                     └────────────────────────────┘

  WHAT THIS VALIDATES THAT qMEMO BENCHMARKS CANNOT
  ──────────────────────────────────────────────────
  ┌────────────────────────────────────┬───────────────────────────────────┐
  │  qMEMO (current)                   │  Test MEMO Blockchain (future)    │
  ├────────────────────────────────────┼───────────────────────────────────┤
  │  Isolated CPU verify loop          │  Verify inside full node pipeline  │
  │  Single fixed keypair in cache     │  Thousands of keypairs, real cache │
  │  No network I/O                    │  Block propagation latency         │
  │  No consensus overhead             │  PoSpace + sig verify interaction  │
  │  No mempool management             │  Mempool contention under load     │
  │  Single machine                    │  Multi-machine shard coordination  │
  │  Synthetic TPS targets             │  Measured real-world TPS           │
  └────────────────────────────────────┴───────────────────────────────────┘

  INTEGRATION STEPS (HIGH LEVEL)
  ────────────────────────────────
  1. Obtain or fork MEMO node source code (Go / Rust)
  2. Confirm Falcon-512 signing uses liboqs (or a compatible implementation)
  3. Configure genesis block with test Falcon-512 validator keys
  4. Wire TX generator to submit signed transactions at MEMO's expected format
  5. Run Phase 1 devnet; instrument sig verify path with timing hooks
  6. Compare node's measured verify throughput to qMEMO benchmark numbers
     → Expected: lower (network overhead, cache misses, consensus CPU contention)
     → Measure the gap; quantify real-world reduction factor
  7. Scale to Phase 2 multi-machine; measure cross-shard overhead
  8. Report findings as follow-on paper section or conference talk

  KEY QUESTIONS FOR THE TEST NETWORK
  ─────────────────────────────────────
  □ Does real-world Falcon-512 verify throughput match qMEMO predictions?
  □ What fraction of CPU is consumed by sig verify vs consensus vs networking?
  □ At what TPS does the node become CPU-bound on signature verification?
  □ Does the 17× headroom (conservative scenario) hold under real load?
  □ What is the block propagation penalty of Falcon-512's larger TX size?
  □ How does cross-shard receipt signing interact with validator throughput?
```

---

## Summary: Current vs Future

```
  TODAY (qMEMO benchmarks)              FUTURE (test MEMO blockchain)
  ─────────────────────────             ──────────────────────────────
  Proven: Falcon-512 verify             Validate: end-to-end TPS under
  achieves 44,228 ops/s on             consensus + network + multi-shard
  Apple M2 Pro (17× headroom)          conditions with real MEMO nodes
        │                                          │
        └──────────────────────────────────────────┘
         Same Falcon-512, same liboqs, same IIT research group
```
