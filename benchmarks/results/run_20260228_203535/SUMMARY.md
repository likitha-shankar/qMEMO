# qMEMO Benchmark Run -- 20260228_203535

**Platform:** arm64 / Darwin
**Date:** 2026-02-28T20:37:14Z
**liboqs:** 0.15.0

---

## Key Material Inspection (correctness audit)

```
    [0040] 88 85 18 22 d0 81 a0                              |..."...|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (277.0 µs)
    (b) Verify corrupted sig  / correct msg:  FAIL ✓ (expected)
    (c) Verify correct sig   / corrupted msg: FAIL ✓ (expected)
────────────────────────────────────────────────────────────────
  [7/7] Ed25519  (Classical -- Edwards Curve -- fixed 64-byte sig)
────────────────────────────────────────────────────────────────
  Key Sizes:
    Public key:  32 bytes  (raw)
    Secret key:  32 bytes  [NOT DISPLAYED -- secret material]
    Keygen time: 189.0 µs
  Public Key (32 bytes):
    [0000] 21 37 cf ea 0e f5 02 f9  56 05 63 b6 42 02 d5 a5  |!7......V.c.B...|
    [0010] 8d 39 3e a7 d6 a0 6c db  c3 58 2e 4f 9b 3a f6 68  |.9>...l..X.O.:.h|
  Signature (64 bytes):
    Sign time:  80.0 µs
    [0000] e8 d1 a5 cf 9e 8b 61 cb  15 2b 4f 37 c2 85 06 50  |......a..+O7...P|
    [0010] f3 14 5b b0 1a f4 95 90  cb 80 ea e1 3f dc c1 63  |..[.........?..c|
    [0020] 25 39 8a 7d d4 a3 60 b6  d0 92 f2 e1 67 31 31 07  |%9.}..`.....g11.|
    [0030] 1c 71 e3 27 e6 a7 7f f7  d9 7b 5c 97 df ae 9e 07  |.q.'.....{\.....|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (120.0 µs)
    (b) Verify corrupted sig  / correct msg:  FAIL ✓ (expected)
    (c) Verify correct sig   / corrupted msg: FAIL ✓ (expected)
════════════════════════════════════════════════════════════════
  Summary
════════════════════════════════════════════════════════════════
  Algorithm           NIST  Inspected
  ------------------  ----  ---------
  Falcon-512           L1   PASS -- all correctness checks OK
  Falcon-1024          L5   PASS -- all correctness checks OK
  ML-DSA-44            L2   PASS -- all correctness checks OK
  ML-DSA-65            L3   PASS -- all correctness checks OK
  SLH-DSA-SHA2-128f    L1   PASS -- all correctness checks OK
  ECDSA secp256k1        --  PASS -- all correctness checks OK
  Ed25519                --  PASS -- all correctness checks OK
  Test vector: "qMEMO key inspection test vector 2026-02-24 IIT Chicago!!!!!!!!"
  Timestamp:   2026-02-28T20:35:37Z
  [Elapsed: 592 ms]
```

---

## Verify Benchmark (single-pass, 10K iterations)

```
================================================================
  Falcon-512 Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Algorithm        : Falcon-512
Public key size  : 897 bytes
Secret key size  : 1281 bytes
Max signature    : 752 bytes
Warmup iterations: 100
Bench iterations : 10000
Message length   : 256 bytes (0x42 fill)
[1/6] Generating Falcon-512 keypair ...
       Key pair generated.
[2/6] Signing test message ...
       Signature produced: 657 bytes (max 752).
[3/6] Sanity check -- verifying signature ...
       Verification passed.
[4/6] Warm-up: 100 verifications ...
       Warm-up complete.
[5/6] Benchmarking: 10000 verifications ...
[6/6] Results:
  ┌─────────────────────────────────────────────┐
  │  Falcon-512 Verification Benchmark Results  │
  ├─────────────────────────────────────────────┤
  │  Iterations    :      10000                  │
  │  Total time    :      0.325130 sec            │
  │  Ops/sec       :      30756.93                │
  │  Per operation :      0.033 ms              │
  │                       32.51 µs              │
  │  Est. cycles   :     113796  (@ 3.5 GHz)  │
  │  Signature     :   657 bytes               │
  │  Public key    :   897 bytes               │
  │  Secret key    :  1281 bytes               │
  └─────────────────────────────────────────────┘
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify",
  "timestamp": "2026-02-28T20:35:37Z",
  "algorithm": "Falcon-512",
  "iterations": 10000,
  "warmup": 100,
  "total_time_sec": 0.325130,
  "ops_per_sec": 30756.93,
  "ms_per_op": 0.033,
  "us_per_op": 32.51,
  "cycles_per_op": 113795.50,
  "signature_bytes": 657,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281
}

Benchmark complete.

  [Elapsed: 549 ms]
```

</details>

---

## Statistical Benchmark (1000 trials x 100 ops)

```
       Passed.
[4/7] Warm-up: 200 verifications ...
       Complete.
[5/7] Running 1000 trials x 100 iterations ...
       ... 200 / 1000 trials
       ... 400 / 1000 trials
       ... 600 / 1000 trials
       ... 800 / 1000 trials
       ... 1000 / 1000 trials
       Data collection complete.
[6/7] Analysing ...
  ┌───────────────────────────────────────────────────────────┐
  │  Falcon-512 Verification -- Statistical Analysis          │
  ├───────────────────────────────────────────────────────────┤
  │  Trials              :   1000                              │
  │  Iterations / trial  :    100                              │
  │  Total verifications : 100000                              │
  ├───────────────────────────────────────────────────────────┤
  │  Mean   (ops/sec)    :     30882.78                        │
  │  Std Dev             :      1211.78                        │
  │  CV                  :        3.92%                       │
  ├───────────────────────────────────────────────────────────┤
  │  Min    (ops/sec)    :     14622.02                        │
  │  P5                  :     30083.33                        │
  │  Median (P50)        :     31133.25                        │
  │  P95                 :     31240.24                        │
  │  P99                 :     31269.54                        │
  │  Max    (ops/sec)    :     31279.32                        │
  │  IQR                 :       154.89                        │
  ├───────────────────────────────────────────────────────────┤
  │  Skewness            :      -8.4583  (left-skewed)       │
  │  Excess kurtosis     :      83.8337  (heavy tails)       │
  │  Jarque-Bera stat    :  304760.8366                      │
  │  Normality (α=0.05)  : FAIL (non-Gauss.)                      │
  │  Outliers (> 3σ)     :     17 / 1000                        │
  └───────────────────────────────────────────────────────────┘
  → Distribution departs from Gaussian (JB = 304760.84 > 5.991).
    Report: median and IQR.  Use non-parametric tests (Mann-Whitney U).
  → CV = 3.92% -- acceptable; consider closing background apps.
[7/7] JSON output:
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify_statistical",
  "timestamp": "2026-02-28T20:35:38Z",
  "algorithm": "Falcon-512",
  "trials": 1000,
  "iterations_per_trial": 100,
  "total_verifications": 100000,
  "signature_bytes": 659,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281,
  "statistics": {
    "mean_ops_sec": 30882.78,
    "stddev_ops_sec": 1211.78,
    "cv_percent": 3.92,
    "min_ops_sec": 14622.02,
    "p5_ops_sec": 30083.33,
    "median_ops_sec": 31133.25,
    "p95_ops_sec": 31240.24,
    "p99_ops_sec": 31269.54,
    "max_ops_sec": 31279.32,
    "iqr_ops_sec": 154.89,
    "skewness": -8.458285,
    "excess_kurtosis": 83.833703,
    "jarque_bera": 304760.836552,
    "normality_pass": false,
    "outliers_count": 17
  },
  "raw_data": [
    29394.47,
    25562.37,
    26434.05,
    28851.70,
    31094.53,
    31181.79,
    30284.68,
    31162.36,
    31240.24,
    30553.01,
    31104.20,
    31055.90,
    29770.77,
    30959.75,
    31094.53,
    30864.20,
    31026.99,
    30367.45,
    30778.70,
    30940.59,
    31142.95,
    30959.75,
    30998.14,
    31210.99,
    31181.79,
    31017.37,
    31075.20,
    30969.34,
    29922.20,
    30959.75,
    31162.36,
    31201.25,
    30902.35,
    31259.77,
    31269.54,
    30988.53,
    31104.20,
    31201.25,
    31104.20,
    31152.65,
    31084.86,
    31113.88,
    31084.86,
    31113.88,
    31065.55,
    31055.90,
    30202.36,
    31123.56,
    31152.65,
    30845.16,
    31055.90,
    31172.07,
    31133.25,
    30674.85,
    31123.56,
    31152.65,
    30284.68,
    31055.90,
    31084.86,
    30534.35,
    30562.35,
    30883.26,
    31065.55,
    31084.86,
    31065.55,
    31046.26,
    30998.14,
    30892.80,
    31046.26,
    30998.14,
    31026.99,
    31065.55,
    31017.37,
    31172.07,
    31113.88,
    31036.62,
    31046.26,
    31094.53,
    31084.86,
    31172.07,
    31123.56,
    31113.88,
    31142.95,
    31075.20,
    31104.20,
    31162.36,
    31142.95,
    31142.95,
    31201.25,
    31094.53,
    31046.26,
    31055.90,
    31104.20,
    31094.53,
    31065.55,
    31104.20,
    31075.20,
    31104.20,
    31123.56,
    30674.85,
    31094.53,
    31113.88,
    31104.20,
    31084.86,
    31162.36,
    31142.95,
    31084.86,
    31094.53,
    31104.20,
    31055.90,
    31065.55,
    31201.25,
    31046.26,
    31113.88,
    31162.36,
    31113.88,
    31172.07,
    31162.36,
    31094.53,
    31133.25,
    31113.88,
    30978.93,
    31084.86,
    31055.90,
    31075.20,
    31142.95,
    31162.36,
    31113.88,
    31094.53,
    31017.37,
    31142.95,
    31065.55,
    31152.65,
    31084.86,
    31075.20,
    31017.37,
    31026.99,
    31036.62,
    30940.59,
    31123.56,
    31065.55,
    31104.20,
    31162.36,
    31104.20,
    31123.56,
    31133.25,
    31094.53,
    31084.86,
    31142.95,
    31133.25,
    31162.36,
    31201.25,
    31094.53,
    31152.65,
    31162.36,
    31152.65,
    31220.73,
    31172.07,
    30193.24,
    31201.25,
    31084.86,
    31075.20,
    31210.99,
    31133.25,
    31133.25,
    31113.88,
    31162.36,
    31152.65,
    31017.37,
    31084.86,
    31133.25,
    31142.95,
    31181.79,
    31046.26,
    31065.55,
    31113.88,
    31191.52,
    31094.53,
    31084.86,
    31142.95,
    31113.88,
    31113.88,
    31152.65,
    31036.62,
    31123.56,
    31113.88,
    31084.86,
    31123.56,
    31094.53,
    31210.99,
    31017.37,
    31201.25,
    31181.79,
    31036.62,
    31123.56,
    31172.07,
    31113.88,
    31162.36,
    30998.14,
    30959.75,
    31162.36,
    31201.25,
    31191.52,
    31172.07,
    31279.32,
    31201.25,
    31210.99,
    31220.73,
    31065.55,
    31113.88,
    31220.73,
    31065.55,
    31152.65,
    31104.20,
    31191.52,
    31250.00,
    31269.54,
    31230.48,
    31269.54,
    31269.54,
    31250.00,
    31220.73,
    31279.32,
    31240.24,
    31220.73,
    31230.48,
    31181.79,
    30873.73,
    31162.36,
    31191.52,
    30835.65,
    31065.55,
    31123.56,
    31133.25,
    31240.24,
    31201.25,
    31181.79,
    31152.65,
    31259.77,
    31191.52,
    31191.52,
    31259.77,
    31191.52,
    31172.07,
    30969.34,
    31026.99,
    26983.27,
    28264.56,
    30998.14,
    30321.41,
    31259.77,
    31240.24,
    29985.01,
    30998.14,
    31191.52,
    29638.41,
    31046.26,
    31075.20,
    30759.77,
    30883.26,
    31240.24,
    31026.99,
    31181.79,
    31172.07,
    31210.99,
    31250.00,
    31259.77,
    31084.86,
    31201.25,
    31210.99,
    31133.25,
    31172.07,
    31230.48,
    31201.25,
    31220.73,
    30883.26,
    31094.53,
    31065.55,
    31172.07,
    31230.48,
    31210.99,
    31181.79,
    31230.48,
    31172.07,
    31191.52,
    31201.25,
    31142.95,
    31210.99,
    31230.48,
    31230.48,
    31240.24,
    31142.95,
    31084.86,
    31181.79,
    31172.07,
    31191.52,
    31172.07,
    31181.79,
    31142.95,
    31172.07,
    31250.00,
    31201.25,
    31230.48,
    31210.99,
    31220.73,
    31172.07,
    30627.87,
    30816.64,
    31055.90,
    31075.20,
    31133.25,
    31123.56,
    30988.53,
    31162.36,
    31181.79,
    31172.07,
    31210.99,
    31210.99,
    31201.25,
    31230.48,
    30321.41,
    31220.73,
    31220.73,
    30950.17,
    31201.25,
    31240.24,
    30788.18,
    29568.30,
    30656.04,
    31250.00,
    31220.73,
    31191.52,
    31210.99,
    31220.73,
    31220.73,
    31123.56,
    31181.79,
    31230.48,
    31181.79,
    31084.86,
    31172.07,
    31240.24,
    31191.52,
    31250.00,
    31269.54,
    31230.48,
    30969.34,
    31220.73,
    31201.25,
    31172.07,
    31240.24,
    31201.25,
    31240.24,
    31269.54,
    30969.34,
    31250.00,
    31104.20,
    31181.79,
    31210.99,
    31250.00,
    31259.77,
    31230.48,
    31259.77,
    31181.79,
    31142.95,
    31230.48,
    31172.07,
    31142.95,
    31162.36,
    31191.52,
    31055.90,
    31152.65,
    31181.79,
    31104.20,
    31172.07,
    31250.00,
    31162.36,
    31142.95,
    31142.95,
    31201.25,
    31172.07,
    31201.25,
    31191.52,
    31133.25,
    31017.37,
    31075.20,
    31046.26,
    31104.20,
    31094.53,
    31026.99,
    31152.65,
    31113.88,
    31104.20,
    31152.65,
    31065.55,
    31065.55,
    31162.36,
    31172.07,
    31055.90,
    31104.20,
    31113.88,
    30940.59,
    31036.62,
    31172.07,
    30940.59,
    30562.35,
    31142.95,
    31075.20,
    31036.62,
    31094.53,
    31162.36,
    31075.20,
    31104.20,
    31094.53,
    31055.90,
    31162.36,
    31142.95,
    31036.62,
    31075.20,
    31055.90,
    31133.25,
    31046.26,
    31123.56,
    31084.86,
    31104.20,
    30854.67,
    29188.56,
    31172.07,
    30102.35,
    31046.26,
    31123.56,
    30816.64,
    30998.14,
    30581.04,
    30731.41,
    31007.75,
    31133.25,
    31017.37,
    31142.95,
    31201.25,
    31113.88,
    31055.90,
    31026.99,
    31123.56,
    31104.20,
    31152.65,
    31113.88,
    30921.46,
    31172.07,
    31055.90,
    30931.02,
    31084.86,
    31172.07,
    31123.56,
    31007.75,
    31113.88,
    31065.55,
    31181.79,
    31142.95,
    31065.55,
    31152.65,
    31055.90,
    31210.99,
    31152.65,
    31172.07,
    31123.56,
    30950.17,
    31113.88,
    31172.07,
    29655.99,
    30988.53,
    31113.88,
    31094.53,
    31250.00,
    31220.73,
    31152.65,
    31201.25,
    31240.24,
    31162.36,
    31201.25,
    31191.52,
    31017.37,
    31220.73,
    31191.52,
    31055.90,
    31084.86,
    31113.88,
    31181.79,
    31123.56,
    31201.25,
    31142.95,
    31240.24,
    31269.54,
    31210.99,
    31046.26,
    31181.79,
    31162.36,
    31162.36,
    31240.24,
    31191.52,
    31191.52,
    31181.79,
    31220.73,
    31152.65,
    31075.20,
    31191.52,
    31133.25,
    31162.36,
    31162.36,
    31036.62,
    31055.90,
    31055.90,
    31152.65,
    31152.65,
    31269.54,
    31191.52,
    31172.07,
    31201.25,
    31201.25,
    31162.36,
    31220.73,
    31210.99,
    31220.73,
    31210.99,
    31191.52,
    31094.53,
    31201.25,
    31172.07,
    31181.79,
    31240.24,
    31240.24,
    31191.52,
    31230.48,
    31230.48,
    31142.95,
    31240.24,
    31230.48,
    31152.65,
    31220.73,
    31259.77,
    31133.25,
    31075.20,
    31094.53,
    30969.34,
    31181.79,
    30902.35,
    31162.36,
    31094.53,
    31259.77,
    31104.20,
    31172.07,
    31220.73,
    31084.86,
    31084.86,
    31094.53,
    30665.44,
    29481.13,
    31152.65,
    30731.41,
    30193.24,
    31210.99,
    29377.20,
    31104.20,
    31191.52,
    30740.85,
    29655.99,
    30864.20,
    30674.85,
    30693.68,
    31201.25,
    31104.20,
    31210.99,
    30627.87,
    30543.68,
    31191.52,
    30902.35,
    30892.80,
    30229.75,
    31094.53,
    31075.20,
    31065.55,
    31152.65,
    31210.99,
    31210.99,
    31162.36,
    31152.65,
    31191.52,
    31162.36,
    31172.07,
    31152.65,
    31210.99,
    31220.73,
    31250.00,
    31123.56,
    31240.24,
    31084.86,
    31220.73,
    31220.73,
    31152.65,
    31152.65,
    31152.65,
    31162.36,
    31181.79,
    31007.75,
    31055.90,
    31220.73,
    31240.24,
    31162.36,
    31220.73,
    31104.20,
    30599.76,
    31181.79,
    31210.99,
    30084.24,
    31210.99,
    31036.62,
    31133.25,
    31133.25,
    31123.56,
    31142.95,
    31191.52,
    30750.31,
    31181.79,
    31220.73,
    30665.44,
    31210.99,
    31191.52,
    31152.65,
    31123.56,
    31201.25,
    31230.48,
    31172.07,
    31113.88,
    31240.24,
    31152.65,
    31152.65,
    31230.48,
    31142.95,
    31007.75,
    31201.25,
    31133.25,
    31181.79,
    31113.88,
    31104.20,
    31172.07,
    31055.90,
    31123.56,
    31007.75,
    31046.26,
    30921.46,
    31084.86,
    31191.52,
    30129.56,
    31162.36,
    31172.07,
    30892.80,
    30988.53,
    31152.65,
    31210.99,
    30826.14,
    31240.24,
    31240.24,
    30175.02,
    31152.65,
    31220.73,
    30769.23,
    31007.75,
    30988.53,
    31007.75,
    30959.75,
    31123.56,
    30816.64,
    31133.25,
    31201.25,
    30156.82,
    31181.79,
    31210.99,
    31201.25,
    31250.00,
    31279.32,
    31104.20,
    31181.79,
    31113.88,
    31152.65,
    31181.79,
    31259.77,
    31250.00,
    31201.25,
    31240.24,
    31220.73,
    31191.52,
    31210.99,
    31240.24,
    31210.99,
    31220.73,
    31172.07,
    31113.88,
    31123.56,
    31152.65,
    31094.53,
    31201.25,
    31210.99,
    31142.95,
    31191.52,
    31181.79,
    31055.90,
    31172.07,
    31201.25,
    31142.95,
    31201.25,
    31220.73,
    31181.79,
    31142.95,
    31172.07,
    31210.99,
    31191.52,
    31240.24,
    31210.99,
    31123.56,
    31230.48,
    31279.32,
    31142.95,
    31075.20,
    31250.00,
    31220.73,
    31181.79,
    31220.73,
    31094.53,
    31181.79,
    31162.36,
    31113.88,
    31191.52,
    31172.07,
    31201.25,
    31201.25,
    31065.55,
    31142.95,
    31142.95,
    31181.79,
    31191.52,
    31181.79,
    31230.48,
    31220.73,
    31065.55,
    31142.95,
    31172.07,
    31046.26,
    31172.07,
    31230.48,
    31104.20,
    31240.24,
    31181.79,
    31220.73,
    31210.99,
    31046.26,
    31162.36,
    31191.52,
    31201.25,
    31123.56,
    31142.95,
    31142.95,
    31181.79,
    31181.79,
    31230.48,
    31191.52,
    31201.25,
    31065.55,
    31220.73,
    31055.90,
    31026.99,
    30998.14,
    30902.35,
    30515.72,
    30911.90,
    30988.53,
    30959.75,
    31026.99,
    31094.53,
    31113.88,
    31123.56,
    30712.53,
    31036.62,
    30816.64,
    30731.41,
    30693.68,
    31094.53,
    31210.99,
    30883.26,
    31230.48,
    31172.07,
    31181.79,
    31162.36,
    31181.79,
    31113.88,
    31191.52,
    31191.52,
    31172.07,
    31210.99,
    31201.25,
    30998.14,
    31162.36,
    31133.25,
    31142.95,
    31133.25,
    31172.07,
    31075.20,
    31104.20,
    31152.65,
    31104.20,
    31201.25,
    31172.07,
    31201.25,
    31191.52,
    31220.73,
    31075.20,
    31201.25,
    31094.53,
    31172.07,
    31172.07,
    31017.37,
    29437.74,
    31017.37,
    31172.07,
    30111.41,
    30693.68,
    31094.53,
    30931.02,
    31123.56,
    30506.41,
    30921.46,
    30385.90,
    31210.99,
    30618.49,
    31201.25,
    31210.99,
    31220.73,
    31046.26,
    31152.65,
    31240.24,
    31220.73,
    31230.48,
    31240.24,
    31201.25,
    31162.36,
    31162.36,
    31181.79,
    31210.99,
    31220.73,
    31201.25,
    31220.73,
    31240.24,
    31181.79,
    30978.93,
    31113.88,
    31133.25,
    31162.36,
    31172.07,
    31007.75,
    31036.62,
    31094.53,
    31094.53,
    31113.88,
    31036.62,
    30788.18,
    31075.20,
    31017.37,
    31201.25,
    31026.99,
    31007.75,
    31133.25,
    31230.48,
    31250.00,
    31094.53,
    31220.73,
    31191.52,
    31142.95,
    31191.52,
    31152.65,
    31162.36,
    31142.95,
    30459.95,
    31191.52,
    31162.36,
    30534.35,
    31123.56,
    31152.65,
    31152.65,
    31123.56,
    31181.79,
    31142.95,
    31191.52,
    31230.48,
    31133.25,
    30864.20,
    29146.02,
    30959.75,
    30778.70,
    30553.01,
    31075.20,
    27166.53,
    30030.03,
    22148.39,
    29359.95,
    30988.53,
    29868.58,
    21925.02,
    29940.12,
    29931.16,
    30978.93,
    30703.10,
    31065.55,
    31172.07,
    30988.53,
    27144.41,
    21026.07,
    27277.69,
    30854.67,
    30376.67,
    31133.25,
    29868.58,
    30978.93,
    31142.95,
    30646.64,
    31210.99,
    30826.14,
    30873.73,
    30988.53,
    30376.67,
    30385.90,
    31210.99,
    31210.99,
    30750.31,
    31055.90,
    31201.25,
    30684.26,
    30646.64,
    30684.26,
    30497.10,
    31026.99,
    30759.77,
    30609.12,
    30759.77,
    31094.53,
    30184.12,
    27449.90,
    30534.35,
    19275.25,
    14622.02,
    17550.02,
    18201.67,
    20132.88,
    27092.93,
    31142.95,
    31113.88,
    30712.53,
    25947.07,
    25647.60,
    24703.56,
    28137.31,
    29850.75,
    30358.23,
    28555.11,
    29027.58,
    30864.20,
    30358.23,
    30487.80,
    29913.25,
    30275.51,
    30030.03,
    30525.03,
    30066.15,
    30175.02,
    30637.25,
    31201.25,
    30459.95,
    30367.45,
    30693.68,
    29815.15,
    30731.41,
    31201.25,
    31094.53,
    30693.68,
    30684.26,
    29940.12,
    30211.48,
    30759.77,
    30902.35,
    30693.68,
    30102.35,
    30543.68,
    30478.51,
    30571.69,
    29859.66,
    30627.87,
    30674.85,
    30969.34,
    30969.34,
    31026.99,
    31084.86,
    31065.55
  ]
}

Statistical benchmark complete.

  [Elapsed: 3481 ms]
```

</details>

---

## Comparison Benchmark (Falcon-512 vs ML-DSA-44)

```
  Falcon-512 vs ML-DSA-44 Comparison  (qMEMO / IIT Chicago)
================================================================
─── Falcon-512 ───
  [keygen] warm-up ... benchmarking 100 trials ... 148.4 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 4934.9 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 31087.0 ops/sec
─── ML-DSA-44 ───
  [keygen] warm-up ... benchmarking 100 trials ... 25201.6 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 10090.6 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 25622.2 ops/sec
================================================================
  HEAD-TO-HEAD COMPARISON
================================================================
  Metric                      Falcon-512       ML-DSA-44  Winner
  ──────────────────────  ──────────────  ──────────────  ──────────
  Keygen throughput             148.4 /s      25201.6 /s    faster ►
  Sign throughput              4934.9 /s      10090.6 /s    faster ►
  Verify throughput           31087.0 /s      25622.2 /s  ◄ faster
  Keygen latency              6740.7 µs         39.7 µs     faster ►
  Sign latency                 202.6 µs         99.1 µs     faster ►
  Verify latency                32.2 µs         39.0 µs   ◄ faster
  Public key size                897 B           1312 B     ◄ smaller
  Secret key size               1281 B           2560 B     ◄ smaller
  Signature size                 655 B           2420 B     ◄ smaller
  On-chain tx overhead          1552 B           3732 B     ◄ smaller
================================================================
  BLOCKCHAIN IMPACT ANALYSIS
================================================================
  Scenario: 4000 transactions per block (single-threaded verification)
  Falcon-512 block verify time :    128.7 ms
  ML-DSA-44  block verify time :    156.1 ms
  Speedup (Falcon / ML-DSA)    :     1.21x
  Falcon-512 block sig data    :   6062.5 KB  (1552 B/tx)
  ML-DSA-44  block sig data    :  14578.1 KB  (3732 B/tx)
  Size ratio (Falcon / ML-DSA) :     0.42x
  ► Recommendation: Falcon-512
    Faster verification AND smaller on-chain footprint make it
    the stronger choice for blockchain transaction signing.
    The slower keygen is irrelevant -- addresses are generated
    once, while signatures are verified millions of times.
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_vs_mldsa44",
  "timestamp": "2026-02-28T20:35:41Z",
  "config": {
    "keygen_trials": 100,
    "sign_trials": 1000,
    "verify_trials": 10000,
    "message_len": 256
  },
  "algorithms": {
    "Falcon-512": {
      "keygen_ops_sec": 148.35,
      "keygen_us_op": 6740.74,
      "sign_ops_sec": 4934.91,
      "sign_us_op": 202.64,
      "verify_ops_sec": 31086.99,
      "verify_us_op": 32.17,
      "pubkey_bytes": 897,
      "privkey_bytes": 1281,
      "signature_bytes": 655,
      "total_tx_overhead": 1552
    },
    "ML-DSA-44": {
      "keygen_ops_sec": 25201.61,
      "keygen_us_op": 39.68,
      "sign_ops_sec": 10090.61,
      "sign_us_op": 99.10,
      "verify_ops_sec": 25622.24,
      "verify_us_op": 39.03,
      "pubkey_bytes": 1312,
      "privkey_bytes": 2560,
      "signature_bytes": 2420,
      "total_tx_overhead": 3732
    }
  },
  "comparison": {
    "verify_speedup_falcon": 1.2133,
    "sign_speedup_falcon": 0.4891,
    "keygen_speedup_falcon": 0.0059,
    "signature_size_ratio": 0.2707,
    "pubkey_size_ratio": 0.6837,
    "total_tx_overhead_falcon": 1552,
    "total_tx_overhead_dilithium": 3732,
    "tx_overhead_ratio": 0.4159
  }
}

Comparison benchmark complete.

  [Elapsed: 2098 ms]
```

</details>

---

## Multicore Verification (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multicore Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair and signing message ...
OK. Signature length: 655 bytes.
Cores  |  Throughput (ops/sec)  |  Speedup  |  Efficiency
-------|------------------------|-----------|------------
   1   |               27022     |    1.00   |  100.0%
   2   |               62203     |    2.30   |  115.1%
   4   |              119900     |    4.44   |  110.9%
   6   |              186463     |    6.90   |  115.0%
   8   |              195757     |    7.24   |   90.6%
  10   |              239297     |    8.86   |   88.6%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_verify",
  "timestamp": "2026-02-28T20:35:43Z",
  "algorithm": "Falcon-512",
  "verif_per_thread": 1000,
  "warmup_per_thread": 100,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [27022, 62203, 119900, 186463, 195757, 239297],
  "speedup": [1.00, 2.30, 4.44, 6.90, 7.24, 8.86],
  "efficiency_pct": [100.0, 115.1, 110.9, 115.0, 90.6, 88.6]
}

Multicore benchmark complete.

  [Elapsed: 467 ms]
```

</details>

---

## Concurrent Verification (thread pool)

```
================================================================
  Falcon-512 Concurrent Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating 100 keypairs and signatures ...
OK.
Concurrent (4 workers): 0.752 ms total, 0.0075 ms avg, 132979 ops/sec
Sequential (baseline):   3.638 ms total, 0.0364 ms avg, 27488 ops/sec
Concurrent yields 79.3% lower latency (better parallelism)
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_concurrent_verify",
  "timestamp": "2026-02-28T20:35:44Z",
  "algorithm": "Falcon-512",
  "concurrent": {
    "signatures": 100,
    "worker_threads": 4,
    "total_time_ms": 0.7520,
    "avg_latency_ms": 0.0075,
    "throughput": 132979
  },
  "sequential": {
    "total_time_ms": 3.6380,
    "avg_latency_ms": 0.0364,
    "throughput": 27488
  },
  "analysis": "Concurrent yields 79.3% lower latency (better parallelism)"
}

Concurrent benchmark complete.

  [Elapsed: 938 ms]
```

</details>

---

## Concurrent Signing (thread pool)

```
================================================================
  Falcon-512 Concurrent Signing Benchmark  (qMEMO / IIT Chicago)
================================================================
Algorithm   : Falcon-512
Tasks       : 100 signing operations
Workers     : 4 concurrent threads
Message len : 256 bytes
Key sizes   : pk=897 B  sk=1281 B  sig_max=752 B
Generating keypair ... done.
Task buffers allocated.
Running concurrent signing (4 workers) ...
  Signature sizes (n=100, max-buf=653 B):
    min=648   max=662   avg=654.9  std=2.2 bytes
Running sequential signing (baseline) ...
  Signature sizes (n=100, max-buf=654 B):
    min=651   max=663   avg=655.1  std=2.3 bytes
================================================================
  RESULTS
================================================================
  Concurrent (4 workers):   8.150 ms total |  0.0815 ms/op |    12270 ops/sec
  Sequential (baseline):   22.434 ms total |  0.2243 ms/op |     4458 ops/sec
  Speedup:  2.75x
  Concurrent yields 2.8x speedup (63.7% faster than sequential)
  For context:
    Concurrent verify throughput (from concurrent_benchmark): ~141,643 ops/sec
    Signing is compute-heavier (FFT Gaussian sampling) so lower
    parallelism efficiency is expected.
  Blockchain relevance:
    Sequential signing covers 500 TPS load tests easily.
    Concurrent signing needed for >5,000 TPS stress tests.
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_concurrent_sign",
  "timestamp": "2026-02-28T20:35:45Z",
  "algorithm": "Falcon-512",
  "config": {
    "signing_tasks": 100,
    "worker_threads": 4,
    "message_len": 256,
    "sig_max_bytes": 752
  },
  "concurrent": {
    "total_time_ms": 8.1500,
    "avg_latency_ms": 0.0815,
    "throughput_ops_sec": 12270
  },
  "sequential": {
    "total_time_ms": 22.4340,
    "avg_latency_ms": 0.2243,
    "throughput_ops_sec": 4458
  },
  "speedup": 2.7526,
  "sig_size_stats": {
    "min_bytes": 651,
    "max_bytes": 663,
    "avg_bytes": 655.1,
    "spec_max_bytes": 752
  },
  "analysis": "Concurrent yields 2.8x speedup (63.7% faster than sequential)"
}

Concurrent signing benchmark complete.

  [Elapsed: 255 ms]
```

</details>

---

## Multicore Signing (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multicore Signing Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair ...
OK. Public key: 897 bytes, Secret key: 1281 bytes.
Config:  50 warm-up signs, 500 timed signs per thread
Cores  |  Throughput (ops/sec)  |  Per-thread (ops/sec)  |  Speedup  |  Efficiency
-------|------------------------|------------------------|-----------|------------
   1   |                4642     |                4642     |    1.00   |  100.0%
   2   |                9744     |                4872     |    2.10   |  104.9%
   4   |               19451     |                4863     |    4.19   |  104.7%
   6   |               29428     |                4905     |    6.34   |  105.7%
   8   |               30233     |                3779     |    6.51   |   81.4%
  10   |               35814     |                3581     |    7.71   |   77.1%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_sign",
  "timestamp": "2026-02-28T20:35:45Z",
  "algorithm": "Falcon-512",
  "sign_per_thread": 500,
  "warmup_per_thread": 50,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [4642, 9744, 19451, 29428, 30233, 35814],
  "speedup": [1.00, 2.10, 4.19, 6.34, 6.51, 7.71],
  "efficiency_pct": [100.0, 104.9, 104.7, 105.7, 81.4, 77.1]
}

Multicore signing benchmark complete.

  [Elapsed: 992 ms]
```

</details>

---

## Signature Size Distribution (10K sigs each)

```
================================================================
  Falcon Signature Size Distribution  (qMEMO / IIT Chicago)
================================================================
  Schemes: Falcon-512, Falcon-padded-512,
           Falcon-1024, Falcon-padded-1024
  Signatures per scheme: 10000
Analysing Falcon-512               ... done.
Analysing Falcon-padded-512        ... done.
Analysing Falcon-1024              ... done.
Analysing Falcon-padded-1024       ... done.
Scheme                    NIST  SpecMax  Min    Max    Mean   StdDev  p25  p50  p75  p95   p99
------------------------  ----  -------  -----  -----  -----  ------  ---  ---  ---  ----  ----
Falcon-512                L1      666    647    663  655.1     2.2   654  655  656   659   660
Falcon-padded-512         L1      666    666    666  666.0     0.0   666  666  666   666   666
Falcon-1024               L5     1280   1260   1283  1270.6     3.1  1269 1271 1273  1276  1278
Falcon-padded-1024        L5     1280   1280   1280  1280.0     0.0  1280 1280 1280  1280  1280
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon_signature_size_distribution",
  "timestamp": "2026-02-28T20:35:46Z",
  "num_signatures": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "spec_max_bytes": 666,
      "min": 647,
      "max": 663,
      "mean": 655.06,
      "std_dev": 2.16,
      "p25": 654,
      "p50": 655,
      "p75": 656,
      "p95": 659,
      "p99": 660
    },
    {
      "name": "Falcon-padded-512",
      "nist_level": 1,
      "spec_max_bytes": 666,
      "min": 666,
      "max": 666,
      "mean": 666.00,
      "std_dev": 0.00,
      "p25": 666,
      "p50": 666,
      "p75": 666,
      "p95": 666,
      "p99": 666
    },
    {
      "name": "Falcon-1024",
      "nist_level": 5,
      "spec_max_bytes": 1280,
      "min": 1260,
      "max": 1283,
      "mean": 1270.63,
      "std_dev": 3.14,
      "p25": 1269,
      "p50": 1271,
      "p75": 1273,
      "p95": 1276,
      "p99": 1278
    },
    {
      "name": "Falcon-padded-1024",
      "nist_level": 5,
      "spec_max_bytes": 1280,
      "min": 1280,
      "max": 1280,
      "mean": 1280.00,
      "std_dev": 0.00,
      "p25": 1280,
      "p50": 1280,
      "p75": 1280,
      "p95": 1280,
      "p99": 1280
    }
  ]
}

Signature size analysis complete.

  [Elapsed: 12542 ms]
```

</details>

---

## Classical Baselines (ECDSA secp256k1 + Ed25519)

```
================================================================
  Classical Signature Baselines  (qMEMO / IIT Chicago)
  OpenSSL 3.x -- EVP_PKEY high-level API
================================================================
  Iterations: 10000 (+ 100 warm-up) per phase
Benchmarking ECDSA secp256k1  ... done.
Benchmarking Ed25519          ... done.
Scheme            Keygen (ops/s)   Sign (ops/s)   Verify (ops/s)   Avg Sig (bytes)
----------------  --------------   ------------   --------------   ---------------
ECDSA secp256k1             3583           3598             3966              71.0
Ed25519                    24193          23929             8795              64.0
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "classical_signature_baselines",
  "timestamp": "2026-02-28T20:35:58Z",
  "bench_iters": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "ECDSA secp256k1",
      "keygen_ops_per_sec": 3583,
      "sign_ops_per_sec": 3598,
      "verify_ops_per_sec": 3966,
      "avg_sig_bytes": 71.0
    },
    {
      "name": "Ed25519",
      "keygen_ops_per_sec": 24193,
      "sign_ops_per_sec": 23929,
      "verify_ops_per_sec": 8795,
      "avg_sig_bytes": 64.0
    }
  ]
}

Classical baseline benchmark complete.

  [Elapsed: 10409 ms]
```

</details>

---

## Comprehensive Comparison (all 7 algorithms)

```
================================================================
  Comprehensive Signature Comparison  (qMEMO / IIT Chicago)
  7 algorithms: 5 PQC (liboqs) + 2 classical (OpenSSL 3.x)
================================================================
  1000 iterations per phase  |  message: 256 bytes 0x42
  [1/7] Falcon-512 ... done.
  [2/7] Falcon-1024 ... done.
  [3/7] ML-DSA-44 ... done.
  [4/7] ML-DSA-65 ... done.
  [5/7] SLH-DSA-SHA2-128f ... done.
  [6/7] ECDSA secp256k1 ... done.
  [7/7] Ed25519 ... done.
Algorithm           NIST  PubKey  SecKey  SigBytes  Keygen/s    Sign/s    Verify/s
------------------  ----  ------  ------  --------  --------  --------  --------
Falcon-512           L1      897    1281       752       148      4805     30569
Falcon-1024          L5     1793    2305      1462        46      2436     15618
ML-DSA-44            L2     1312    2560      2420     24610     10273     25904
ML-DSA-65            L3     1952    4032      3309     14327      6745     15369
SLH-DSA-SHA2-128f    L1       32      64     17088       836        36       599
ECDSA secp256k1       -       65      32        71      3595      3608      4026
Ed25519               -       32      32        64     24483     24276      8857
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "comprehensive_signature_comparison",
  "timestamp": "2026-02-28T20:36:09Z",
  "bench_iters": 1000,
  "message_len": 256,
  "algorithms": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "pubkey_bytes": 897,
      "seckey_bytes": 1281,
      "sig_bytes": 752,
      "keygen_ops_per_sec": 148,
      "sign_ops_per_sec": 4805,
      "verify_ops_per_sec": 30569
    },
    {
      "name": "Falcon-1024",
      "nist_level": 5,
      "pubkey_bytes": 1793,
      "seckey_bytes": 2305,
      "sig_bytes": 1462,
      "keygen_ops_per_sec": 46,
      "sign_ops_per_sec": 2436,
      "verify_ops_per_sec": 15618
    },
    {
      "name": "ML-DSA-44",
      "nist_level": 2,
      "pubkey_bytes": 1312,
      "seckey_bytes": 2560,
      "sig_bytes": 2420,
      "keygen_ops_per_sec": 24610,
      "sign_ops_per_sec": 10273,
      "verify_ops_per_sec": 25904
    },
    {
      "name": "ML-DSA-65",
      "nist_level": 3,
      "pubkey_bytes": 1952,
      "seckey_bytes": 4032,
      "sig_bytes": 3309,
      "keygen_ops_per_sec": 14327,
      "sign_ops_per_sec": 6745,
      "verify_ops_per_sec": 15369
    },
    {
      "name": "SLH-DSA-SHA2-128f",
      "nist_level": 1,
      "pubkey_bytes": 32,
      "seckey_bytes": 64,
      "sig_bytes": 17088,
      "keygen_ops_per_sec": 836,
      "sign_ops_per_sec": 36,
      "verify_ops_per_sec": 599
    },
    {
      "name": "ECDSA secp256k1",
      "nist_level": 0,
      "pubkey_bytes": 65,
      "seckey_bytes": 32,
      "sig_bytes": 71,
      "keygen_ops_per_sec": 3595,
      "sign_ops_per_sec": 3608,
      "verify_ops_per_sec": 4026
    },
    {
      "name": "Ed25519",
      "nist_level": 0,
      "pubkey_bytes": 32,
      "seckey_bytes": 32,
      "sig_bytes": 64,
      "keygen_ops_per_sec": 24483,
      "sign_ops_per_sec": 24276,
      "verify_ops_per_sec": 8857
    }
  ]
}

Comprehensive comparison complete.

  [Elapsed: 65266 ms]
```

</details>

---

