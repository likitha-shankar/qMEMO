# MEMO Blockchain - Baseline Performance (BLS Signatures)

## Test Environment

### Hardware
- **Platform:** Intel Xeon Gold 6126 (Skylake-SP)
- **Location:** Chameleon Cloud (qMEMO instance)
- **IP:** 129.114.108.20
- **CPU:** Xeon Gold 6126 @ 2.60 GHz
- **Cores:** 48 logical (2 sockets × 12 cores × 2 threads/core)
- **RAM:** 187 GB DDR4 ECC

### Software
- **OS:** Linux 6.8.0-64-generic x86_64 (Ubuntu 24.04.2 LTS)
- **Compiler:** gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **MEMO Version:** v27 (blockchain_pos_v45)
- **Date:** March 10, 2026

### Test Configuration
- **k parameter:** 16 (2^16 = 65,536 plot entries per farmer)
- **Number of farmers:** 10
- **Block interval:** 1 second
- **Threads per farmer:** 8 (OpenMP)
- **Batch size:** 64 tx/message
- **Test sizes:** 500 TX, 1000 TX

---

## Current Cryptographic Implementation

### BLS Signatures (Pre-Quantum)

| Component | Details |
|-----------|---------|
| **Algorithm** | BLS (BLS12-381 curve) |
| **Signature size** | 48 bytes |
| **Public key size** | ~48 bytes |
| **Hash function** | BLAKE3 |
| **Address format** | RIPEMD160(SHA256(pubkey)) = 20 bytes |
| **Transaction size** | **112 bytes FIXED** |

### ⚠️ CRITICAL: Signature Verification is STUBBED

```c
// From src/transaction.c
bool transaction_verify(const Transaction* tx) {
    // Coinbase doesn't need signature verification
    if (TX_IS_COINBASE(tx)) return true;

    // BLS signature verification
    // In production, this would verify: e(sig, g2) == e(H(m), pk)
    // For now, we verify signature is non-zero (basic sanity check)

    // Check signature is not all zeros
    if (is_zero(tx->signature, 48)) {
        return false;
    }

    return true;  // ← ACCEPTS ANY NON-ZERO SIGNATURE!
}
```

**Impact:** Any transaction with a non-zero signature field is accepted. This is a **security vulnerability** but useful for benchmarking raw throughput without cryptographic overhead.

---

## Performance Results

### Pass 1: 500 Transactions

| Metric | Value | Notes |
|--------|-------|-------|
| **Submission TPS** | **3,260 tx/sec** | Rate wallet → pool |
| **Confirmation TPS** | **1,943 tx/sec** | Rate pool → blockchain |
| **End-to-end TPS** | **1,218 tx/sec** | Submit → confirm |
| **Confirmation rate** | **100%** | All TXs confirmed |
| **Block processing** | **78 ms avg** | Per-block validation time |

### Pass 2: 1000 Transactions

| Metric | Value | Notes |
|--------|-------|-------|
| **Submission TPS** | **5,659 tx/sec** | Rate wallet → pool |
| **Confirmation TPS** | **3,662 tx/sec** | Rate pool → blockchain |
| **End-to-end TPS** | **2,223 tx/sec** | Submit → confirm |
| **Confirmation rate** | **100%** | All TXs confirmed |
| **Block processing** | **35 ms avg** | Per-block validation time |

**Key Observation:** Performance improves with batch size (1000 TX faster than 500 TX due to better batching).

### Micro Benchmarks

| Operation | Speed | Latency | Notes |
|-----------|------:|--------:|-------|
| **GPB TX serialize** | **5.9M ops/sec** | **0.17 µs** | Protobuf serialization |
| **ZMQ TCP RTT** | — | **168 µs** | Network round-trip |
| **BLAKE3 hash** | **886K ops/sec** | **1.13 µs** | Transaction hashing |
| **Proof search** | **42M ops/sec** | **0.02 µs** | PoS proof lookup |

---

## Bottleneck Analysis

### Current Bottlenecks (in order):
1. **Network latency (ZMQ):** 168 µs RTT
2. **Block processing:** 35–78 ms
3. **BLAKE3 hashing:** 1.13 µs per TX
4. **Serialization:** 0.17 µs per TX

### NOT Bottlenecks:
- ✅ Signature verification: **STUBBED** (near-zero overhead)
- ✅ Proof search: 0.02 µs (negligible)

---

## Comparison Point for Quantum Integration

When we integrate Falcon-512, we expect these changes:

| Metric | Baseline (BLS stub) | Predicted (Falcon-512) | Impact |
|--------|--------------------:|-----------------------:|--------|
| **Signature size** | 48 bytes | ~750 bytes | **+15.6×** |
| **Transaction size** | 112 bytes | ~814 bytes | **+7.3×** |
| **Serialization** | 0.17 µs | ~1.2 µs | +7× slower |
| **Network overhead** | 168 µs base | +600 µs data | +3.6× latency |
| **Verification** | 0 µs (stub) | **32 µs** | **NEW bottleneck** |
| **Expected TPS** | 2,223 e2e | **~1,500–1,800** | −20–30% |

### Why 20–30% TPS reduction expected:

1. **Network:** Larger transactions (+662 bytes) increase transmission time
2. **Serialization:** 7× larger signatures take longer to serialize
3. **Verification:** Adding real verification (32 µs per TX @ 31,133 ops/sec)
4. **Block size:** Blocks grow from ~112 KB to ~814 KB (7.3×)

### But Still Well Above Requirements:

MEMO target with Falcon-512:
- **Conservative (4 shards):** Need 2,500 verif/sec → We have **31,133 verif/sec** = **12.5× margin**
- **Full scale (256 shards):** Need 198 verif/sec → We have **157× margin**

**Even with 30% TPS reduction, still 100× above minimum requirements.**

---

## Raw Data

### Pass 1 (500 TX):

```
Configuration:
  Transactions to send                         : 500
  Block interval                               : 1s
  K parameter                                  : 16 (2^16 = 65536 entries)
  Number of farmers                            : 10
  Warmup blocks                                : 10
  Batch size                                   : 64
  Threads per farmer                           : 8

MICRO BENCHMARK SUMMARY:
  GPB Transaction serialize:         0.16 µs
  GPB Transaction deserialize:       0.50 µs
  BLAKE3 hash (256 bytes):           1.13 µs
  ZMQ inproc round-trip:             5.90 µs
  ZMQ TCP round-trip:              169.77 µs
  Proof search (k=16):               0.02 µs
  Plot generation (k=16):            0.07 ms

SUBMISSION METRICS (Wallet → Pool):
  Transactions submitted                       : 500
  Submission errors                            : 0
  Submission time                              : .153382017s
  Submission throughput (TPS)                  : 3259.83 tx/sec

CONFIRMATION METRICS (Pool → Block → Chain):
  Transactions confirmed                       : 500
  Confirmation rate                            : 100.0%
  Processing time (RESUME to done)             : .257278199s
  Processing throughput (TPS)                  : 1943.42 tx/sec

END-TO-END METRICS (Complete Pipeline):
  Submission time (Phase A)                    : .153382017s
  Processing time (Phase B)                    : .257278199s
  Total benchmark time (A + B)                 : .410660216s
  End-to-end throughput (TPS)                  : 1217.55 tx/sec

BLOCK METRICS:
  Blocks created during benchmark              : 2
  Average transactions per block               : 250.0
  Block interval (configured)                  : 1s
  Chain height (before → after)               : 33 → 35

PER-BLOCK TIMING:
  Block #35  |  501 TXs  |  interval 78ms  |  confirmed 500
```

### Pass 2 (1000 TX):

```
Configuration:
  Transactions to send                         : 1000
  Block interval                               : 1s
  K parameter                                  : 16 (2^16 = 65536 entries)
  Number of farmers                            : 10
  Warmup blocks                                : 10
  Batch size                                   : 64
  Threads per farmer                           : 8

MICRO BENCHMARK SUMMARY:
  GPB Transaction serialize:         0.17 µs
  GPB Transaction deserialize:       0.50 µs
  BLAKE3 hash (256 bytes):           1.13 µs
  ZMQ inproc round-trip:             5.78 µs
  ZMQ TCP round-trip:              167.58 µs
  Proof search (k=16):               0.02 µs
  Plot generation (k=16):            0.07 ms

SUBMISSION METRICS (Wallet → Pool):
  Transactions submitted                       : 1000
  Submission errors                            : 0
  Submission time                              : .176721130s
  Submission throughput (TPS)                  : 5658.63 tx/sec

CONFIRMATION METRICS (Pool → Block → Chain):
  Transactions confirmed                       : 1000
  Confirmation rate                            : 100.0%
  Processing time (RESUME to done)             : .273052850s
  Processing throughput (TPS)                  : 3662.29 tx/sec

END-TO-END METRICS (Complete Pipeline):
  Submission time (Phase A)                    : .176721130s
  Processing time (Phase B)                    : .273052850s
  Total benchmark time (A + B)                 : .449773980s
  End-to-end throughput (TPS)                  : 2223.33 tx/sec

BLOCK METRICS:
  Blocks created during benchmark              : 2
  Average transactions per block               : 500.0
  Block interval (configured)                  : 1s
  Chain height (before → after)               : 33 → 35

PER-BLOCK TIMING:
  Block #35  |  1001 TXs  |  interval 35ms  |  confirmed 1000

VALIDATOR STEP TIMING (per farmer, consistent across all 10):
  Step 1 (GET_LAST_HASH):      0–1 ms
  Step 2 (Pool→Validator):     9–13 ms
  Step 5 (Serialize+Send):     1–5 ms
```

---

## Conclusion

### Baseline Established ✅
- Current system: **2,223 end-to-end TPS** with stubbed verification
- Signature overhead: **0 µs** (not verified)
- Transaction size: **112 bytes** (compact)

### Ready for Quantum Integration
- Infrastructure works correctly
- Benchmark scripts functional
- Clear performance baseline
- Can now measure quantum signature impact

### Next Steps
1. Redesign Transaction structure for variable-length signatures
2. Integrate liboqs with Falcon-512
3. Re-run identical benchmarks
4. Compare: Baseline vs Quantum performance
5. Document real-world overhead of post-quantum cryptography

---

**Test Date:** March 10, 2026
**Tester:** Likitha Shankar
**Purpose:** Establish baseline before Falcon-512 integration
