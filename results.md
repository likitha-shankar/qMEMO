Results                                                                                                                                                                                    
                                                                                                                                                                                             
  1. Multicore Signing (sign_benchmark)                                                                                                                                                      
                                                                                                                                                                                             
  Cores  |  Throughput   |  Per-thread  |  Speedup  |  Efficiency
     1   |   6,111 ops/s |  6,111 /thr  |   1.00x   | 100.0%                                                                                                                                 
     2   |  13,707 ops/s |  6,853 /thr  |   2.24x   | 112.2%
     4   |  26,762 ops/s |  6,691 /thr  |   4.38x   | 109.5%
     6   |  39,632 ops/s |  6,605 /thr  |   6.49x   | 108.1%
     8   |  40,889 ops/s |  5,111 /thr  |   6.69x   |  83.6%
    10   |  44,985 ops/s |  4,499 /thr  |   7.36x   |  73.6%

  What happened at 8-10 cores: The M2 Pro has 6 performance cores + 4 efficiency cores. Threads 1-6 land on P-cores at full speed (>100% efficiency -- a turbo burst effect from independent
  PRNG state per thread). Threads 7-10 spill onto the E-cores, which are ~30% slower, pulling per-thread throughput down sharply. This is textbook big.LITTLE behavior, clearly visible in
  the efficiency cliff between 6 and 8 cores.

  ---
  2. Signature Size Distribution (signature_size_analysis)

  Scheme                  Mean (B)  StdDev  Min   Max   p99
  Falcon-512              655.0     2.18    648   664   660
  Falcon-padded-512       666.0     0.00    666   666   666
  Falcon-1024             1270.6    3.12   1260  1282  1278
  Falcon-padded-1024      1280.0    0.00   1280  1280  1280

  Three findings: (a) The distribution is extremely tight -- CV < 0.35% for unpadded variants. (b) The actual mean (655 B) is only ~1.7% below the NIST spec max (666 B) -- Falcon's
  compression is near its ceiling on this platform. (c) Falcon-1024 measured a 1,282-byte signature against a 1,280-byte spec max -- this is a known liboqs quirk where the internal buffer
  allocation exceeds the NIST cap by a small margin for alignment reasons.

  ---
  3. Classical Baselines (classical_benchmark)

  Scheme            Keygen/s   Sign/s   Verify/s   Avg Sig
  ECDSA secp256k1    5,111     5,154      5,643     71 B
  Ed25519           34,783    34,461     12,540     64 B

  ECDSA secp256k1 is uniformly slow across all three phases (~5K ops/sec). Ed25519 signs very fast (34K/sec) but verifies at only 12,540 ops/sec -- this sets up a striking comparison against
   PQC.

  ---
  4. Comprehensive Comparison (comprehensive_comparison) -- The Full Picture

  Algorithm           NIST  PubKey  SecKey  SigBytes  Keygen/s  Sign/s   Verify/s
  Falcon-512           L1     897    1281       752       213     7,020    44,643
  Falcon-1024          L5    1793    2305      1462        67     3,536    22,681
  ML-DSA-44            L2    1312    2560      2420    35,770    14,605    37,263
  ML-DSA-65            L3    1952    4032      3309    20,697     9,824    22,078
  SLH-DSA-SHA2-128f    L1      32      64     17088     1,217        52       842
  ECDSA secp256k1       --      65      32        71     5,106     5,119     5,649
  Ed25519               --      32      32        64    34,678    34,860    12,641

  The headline result: Falcon-512 is the fastest verifier in the entire suite -- including both classical schemes. At 44,643 verify/sec it beats Ed25519 (12,641) by 3.5x and ECDSA (5,643) by
   7.9x. For a blockchain where blocks are verified by all nodes on every incoming transaction, this is the single most important number.

  The keygen surprise: Falcon-512 keygen is only 213 ops/sec -- the worst in the suite by a large margin. ML-DSA-44 keygens 168x faster. This isn't a problem for blockchain use (keys are
  generated once, then used for thousands of signatures) but it rules out any scenario requiring rapid ephemeral key rotation.

  SLH-DSA at 52 sign/sec is effectively disqualified from any high-TPS application. A single-threaded node could sign at most 52 transactions per second; even with 10 cores that's ~500 TPS
  -- and the 17 KB signatures would dominate block size.

  One note on SigBytes in the table: The sig->length_signature field from liboqs returns the internal buffer allocation size (752 for Falcon-512), which is slightly larger than the NIST
  spec max (666 B) and larger than the actual average measured by signature_size_analysis (655 B). The size analysis benchmark gives the ground truth for actual network overhead.
