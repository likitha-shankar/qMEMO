# qMEMO Benchmark Run -- 20260228_223247

**Platform:** x86_64 / Linux
**Date:** 2026-02-28T22:34:21Z
**liboqs:** 0.15.0

---

## Key Material Inspection (correctness audit)

```
    [0040] 4e 82 fa 4b dd b2 c2                              |N..K...|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (383.6 µs)
    (b) Verify corrupted sig  / correct msg:  FAIL ✓ (expected)
    (c) Verify correct sig   / corrupted msg: FAIL ✓ (expected)
────────────────────────────────────────────────────────────────
  [7/7] Ed25519  (Classical -- Edwards Curve -- fixed 64-byte sig)
────────────────────────────────────────────────────────────────
  Key Sizes:
    Public key:  32 bytes  (raw)
    Secret key:  32 bytes  [NOT DISPLAYED -- secret material]
    Keygen time: 58.5 µs
  Public Key (32 bytes):
    [0000] 84 6d 4e 51 61 ed 81 6a  cc 14 e7 f3 ee e1 da e8  |.mNQa..j........|
    [0010] 3a e8 42 e9 82 fe 7a 85  82 45 0f b1 6e 95 e1 dd  |:.B...z..E..n...|
  Signature (64 bytes):
    Sign time:  48.9 µs
    [0000] 9e 4a af 3e 48 72 95 4a  67 47 15 b4 b1 a0 3d 13  |.J.>Hr.JgG....=.|
    [0010] 91 9d 29 bd 05 1e ba 07  d1 ec 25 51 16 16 97 3c  |..).......%Q...<|
    [0020] f6 44 6c 21 79 3a e0 00  1e d2 58 02 d6 0e 23 5f  |.Dl!y:....X...#_|
    [0030] 8e ea 37 b5 b6 94 d4 ef  97 69 94 bf 88 b1 8a 01  |..7......i......|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (124.5 µs)
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
  Timestamp:   2026-02-28T22:32:49Z
  [Elapsed: 81 ms]
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
       Signature produced: 653 bytes (max 752).
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
  │  Total time    :      0.419364 sec            │
  │  Ops/sec       :      23845.60                │
  │  Per operation :      0.042 ms              │
  │                       41.94 µs              │
  │  Est. cycles   :     146778  (@ 3.5 GHz)  │
  │  Signature     :   653 bytes               │
  │  Public key    :   897 bytes               │
  │  Secret key    :  1281 bytes               │
  └─────────────────────────────────────────────┘
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify",
  "timestamp": "2026-02-28T22:32:49Z",
  "algorithm": "Falcon-512",
  "iterations": 10000,
  "warmup": 100,
  "total_time_sec": 0.419364,
  "ops_per_sec": 23845.60,
  "ms_per_op": 0.042,
  "us_per_op": 41.94,
  "cycles_per_op": 146777.57,
  "signature_bytes": 653,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281
}

Benchmark complete.

  [Elapsed: 440 ms]
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
  │  Mean   (ops/sec)    :     23861.79                        │
  │  Std Dev             :       160.70                        │
  │  CV                  :        0.67%                       │
  ├───────────────────────────────────────────────────────────┤
  │  Min    (ops/sec)    :     20239.63                        │
  │  P5                  :     23797.91                        │
  │  Median (P50)        :     23884.67                        │
  │  P95                 :     23929.24                        │
  │  P99                 :     23934.21                        │
  │  Max    (ops/sec)    :     23936.93                        │
  │  IQR                 :         7.34                        │
  ├───────────────────────────────────────────────────────────┤
  │  Skewness            :     -13.3795  (left-skewed)       │
  │  Excess kurtosis     :     266.2243  (heavy tails)       │
  │  Jarque-Bera stat    : 2982975.6267                      │
  │  Normality (α=0.05)  : FAIL (non-Gauss.)                      │
  │  Outliers (> 3σ)     :     16 / 1000                        │
  └───────────────────────────────────────────────────────────┘
  → Distribution departs from Gaussian (JB = 2982975.63 > 5.991).
    Report: median and IQR.  Use non-parametric tests (Mann-Whitney U).
  → CV = 0.67% -- excellent measurement stability.
[7/7] JSON output:
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify_statistical",
  "timestamp": "2026-02-28T22:32:50Z",
  "algorithm": "Falcon-512",
  "trials": 1000,
  "iterations_per_trial": 100,
  "total_verifications": 100000,
  "signature_bytes": 653,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281,
  "statistics": {
    "mean_ops_sec": 23861.79,
    "stddev_ops_sec": 160.70,
    "cv_percent": 0.67,
    "min_ops_sec": 20239.63,
    "p5_ops_sec": 23797.91,
    "median_ops_sec": 23884.67,
    "p95_ops_sec": 23929.24,
    "p99_ops_sec": 23934.21,
    "max_ops_sec": 23936.93,
    "iqr_ops_sec": 7.34,
    "skewness": -13.379533,
    "excess_kurtosis": 266.224280,
    "jarque_bera": 2982975.626721,
    "normality_pass": false,
    "outliers_count": 16
  },
  "raw_data": [
    20239.63,
    23267.87,
    23115.71,
    23132.74,
    23073.44,
    23128.18,
    23201.39,
    23214.29,
    23265.03,
    23468.28,
    23917.83,
    23919.22,
    23930.43,
    23926.86,
    23931.09,
    23930.12,
    23930.06,
    23931.16,
    23935.03,
    23920.07,
    23934.22,
    23927.50,
    23932.85,
    23931.49,
    23931.51,
    23935.15,
    23931.83,
    23926.92,
    23929.22,
    23932.30,
    23931.42,
    23931.59,
    23931.00,
    23928.93,
    23932.51,
    23933.59,
    23929.69,
    23928.41,
    23934.36,
    23935.57,
    23934.21,
    23928.37,
    23934.52,
    23931.71,
    23927.98,
    23936.93,
    23933.33,
    23928.90,
    23930.77,
    23933.25,
    23931.67,
    23933.87,
    23929.96,
    23928.71,
    23935.17,
    23930.92,
    23932.12,
    23924.85,
    23932.71,
    23933.69,
    23929.16,
    23933.73,
    23930.22,
    23925.67,
    23926.86,
    23799.13,
    23640.33,
    23930.11,
    23934.49,
    23463.71,
    23933.41,
    23932.66,
    23928.71,
    23386.20,
    23925.06,
    23929.04,
    23927.11,
    23931.22,
    23929.01,
    23925.01,
    23933.38,
    23502.76,
    23928.94,
    23922.62,
    23922.40,
    23490.91,
    23934.84,
    23928.05,
    23932.06,
    23931.02,
    23928.57,
    23925.18,
    23481.02,
    23917.93,
    23932.07,
    23921.28,
    23929.97,
    23928.62,
    23928.53,
    23925.79,
    23926.52,
    23928.26,
    23930.07,
    23925.62,
    23927.88,
    23915.42,
    23924.94,
    23886.17,
    23922.34,
    23907.16,
    23917.10,
    23916.26,
    23915.27,
    23914.46,
    23915.76,
    23909.93,
    23918.40,
    23436.55,
    23912.86,
    23456.45,
    23918.76,
    23442.29,
    23912.33,
    23431.37,
    23920.56,
    23908.87,
    23916.12,
    23914.57,
    23685.30,
    23664.82,
    23591.45,
    23749.81,
    23367.03,
    23911.29,
    23916.84,
    23914.63,
    23911.76,
    23916.93,
    23902.78,
    23917.84,
    23864.96,
    23887.53,
    23885.63,
    23887.65,
    23694.55,
    23592.87,
    23886.69,
    23885.71,
    23887.64,
    23889.14,
    23886.99,
    23888.75,
    23886.94,
    23888.36,
    23885.16,
    23886.88,
    23885.20,
    23885.88,
    23888.62,
    23888.65,
    23887.49,
    23885.06,
    23886.55,
    23887.44,
    23888.97,
    23887.48,
    23889.70,
    23881.58,
    23885.79,
    23885.65,
    23891.34,
    23887.66,
    23887.72,
    23887.05,
    23886.62,
    23888.29,
    23885.31,
    23887.06,
    23887.10,
    23890.27,
    23887.67,
    23886.20,
    23886.06,
    23886.80,
    23888.55,
    23887.51,
    23884.93,
    23887.88,
    23880.53,
    23886.34,
    23883.32,
    23887.50,
    23884.13,
    23890.02,
    23884.87,
    23887.68,
    23886.20,
    23889.54,
    23889.26,
    23886.23,
    23888.58,
    23889.17,
    23888.31,
    23886.80,
    23889.46,
    23886.74,
    23886.02,
    23885.09,
    23884.17,
    23889.23,
    23883.12,
    23887.09,
    23887.22,
    23888.39,
    23545.20,
    23752.68,
    23887.97,
    23886.87,
    23887.03,
    23881.60,
    23889.87,
    23890.14,
    23885.21,
    23888.23,
    23888.58,
    23885.14,
    23888.16,
    23887.62,
    23888.63,
    23885.91,
    23878.93,
    23877.74,
    23888.64,
    23885.23,
    23887.24,
    23887.71,
    23888.47,
    23885.93,
    23880.64,
    23880.92,
    23880.51,
    23880.20,
    23888.49,
    23887.82,
    23887.74,
    23774.64,
    23869.68,
    23869.62,
    23867.13,
    23876.01,
    23876.12,
    23877.47,
    23871.18,
    23873.51,
    23874.78,
    23878.03,
    23878.93,
    23875.23,
    23879.76,
    23876.00,
    23868.02,
    23878.31,
    23867.74,
    23877.15,
    23878.38,
    23881.76,
    23878.83,
    23878.18,
    23878.55,
    23877.65,
    23880.75,
    23880.40,
    23880.94,
    23877.08,
    23878.62,
    23876.77,
    23877.68,
    23875.82,
    23879.00,
    23878.68,
    23876.70,
    23882.54,
    23883.02,
    23882.31,
    23883.60,
    23884.87,
    23881.79,
    23882.82,
    23883.16,
    23885.58,
    23881.16,
    23879.17,
    23882.46,
    23879.90,
    23882.06,
    23876.91,
    23880.71,
    23884.10,
    23880.61,
    23880.23,
    23884.09,
    23884.48,
    23880.85,
    23883.46,
    23882.05,
    23879.86,
    23879.35,
    23880.87,
    23884.57,
    23884.86,
    23885.91,
    23881.34,
    23884.21,
    23880.61,
    23882.99,
    23877.99,
    23871.65,
    23882.84,
    23878.82,
    23880.09,
    23879.23,
    23881.33,
    23879.25,
    23880.10,
    23877.97,
    23879.56,
    23880.71,
    23880.12,
    23880.00,
    23876.69,
    23880.47,
    23881.34,
    23881.87,
    23879.83,
    23878.88,
    23879.80,
    23879.62,
    23764.79,
    23884.95,
    23881.68,
    23885.42,
    23877.12,
    23885.23,
    23878.93,
    23884.21,
    23882.19,
    23882.21,
    23883.14,
    23878.83,
    23883.68,
    23886.63,
    23886.04,
    23882.42,
    23880.37,
    23884.00,
    23878.46,
    23883.38,
    23885.02,
    23877.60,
    23884.70,
    23880.75,
    23881.46,
    23881.62,
    23879.39,
    23883.51,
    23880.21,
    23881.20,
    23879.54,
    23880.92,
    23884.40,
    23881.66,
    23880.69,
    23881.34,
    23880.90,
    23885.51,
    23880.70,
    23880.17,
    23880.25,
    23880.34,
    23884.00,
    23875.19,
    23885.33,
    23882.62,
    23405.22,
    23882.87,
    23882.95,
    23877.84,
    23883.13,
    23879.75,
    23880.14,
    23882.56,
    23877.36,
    23884.92,
    23880.21,
    23881.26,
    23882.74,
    23881.98,
    23880.96,
    23880.13,
    23884.16,
    23880.55,
    23878.27,
    23879.95,
    23882.14,
    23882.79,
    23883.91,
    23884.54,
    23882.76,
    23881.40,
    23883.11,
    23881.27,
    23881.81,
    23882.19,
    23879.10,
    23881.57,
    23884.11,
    23880.84,
    23881.98,
    23882.64,
    23882.27,
    23876.45,
    23884.55,
    23873.07,
    23877.65,
    23879.14,
    23881.66,
    23880.52,
    23884.07,
    23887.86,
    23888.98,
    23883.74,
    23885.70,
    23885.54,
    23887.03,
    23887.60,
    23884.18,
    23887.55,
    23876.94,
    23884.05,
    23883.59,
    23885.71,
    23880.87,
    23885.18,
    23881.42,
    23886.79,
    23886.70,
    23886.79,
    23887.04,
    23884.07,
    23884.78,
    23889.11,
    23880.47,
    23884.48,
    23888.71,
    23884.61,
    23890.16,
    23888.14,
    23887.87,
    23882.55,
    23884.38,
    23882.20,
    23886.06,
    23885.74,
    23887.04,
    23884.65,
    23880.20,
    23754.36,
    23601.57,
    23886.67,
    23840.49,
    23886.96,
    23886.33,
    23886.91,
    23887.67,
    23889.49,
    23885.72,
    23885.91,
    23880.70,
    23884.16,
    23886.11,
    23889.13,
    23885.68,
    23866.75,
    23881.65,
    23882.07,
    23881.77,
    23866.88,
    23886.70,
    23876.21,
    23878.90,
    23873.72,
    23874.59,
    23880.58,
    23886.31,
    23886.07,
    23886.08,
    23883.97,
    23887.49,
    23887.08,
    23887.96,
    23883.46,
    23412.10,
    23882.83,
    23887.05,
    23876.74,
    23885.87,
    23884.01,
    23882.22,
    23888.59,
    23883.60,
    23886.66,
    23561.05,
    23759.75,
    23885.69,
    23888.10,
    23884.91,
    23886.24,
    23885.40,
    23884.87,
    23888.14,
    23882.34,
    23886.59,
    23887.39,
    23886.07,
    23885.94,
    23887.99,
    23883.16,
    23886.96,
    23880.42,
    23884.01,
    23885.26,
    23882.82,
    23885.85,
    23888.66,
    23883.03,
    23883.36,
    23885.72,
    23885.17,
    23884.14,
    23885.67,
    23886.22,
    23887.60,
    23888.33,
    23886.11,
    23887.32,
    23888.09,
    23885.26,
    23887.06,
    23887.07,
    23874.62,
    23887.51,
    23889.34,
    23891.60,
    23890.08,
    23885.34,
    23887.36,
    23888.64,
    23888.59,
    23886.61,
    23883.98,
    23889.80,
    23887.56,
    23888.67,
    23884.12,
    23886.67,
    23886.79,
    23886.58,
    23890.32,
    23888.22,
    23889.86,
    23888.09,
    23881.70,
    23888.54,
    23885.41,
    23891.64,
    23888.87,
    23887.32,
    23886.38,
    23886.64,
    23886.75,
    23886.46,
    23890.16,
    23879.26,
    23886.39,
    23887.37,
    23888.98,
    23886.09,
    23885.35,
    23886.94,
    23887.48,
    23888.07,
    23887.17,
    23878.30,
    23887.27,
    23885.83,
    23885.17,
    23884.66,
    23887.99,
    23884.86,
    23886.20,
    23890.58,
    23889.71,
    23888.65,
    23888.52,
    23884.50,
    23888.35,
    23888.45,
    23886.08,
    23886.23,
    23889.09,
    23885.59,
    23889.96,
    23886.67,
    23888.97,
    22709.29,
    23317.88,
    23323.67,
    23367.23,
    23402.44,
    23318.68,
    23885.45,
    23886.21,
    23395.73,
    23884.94,
    23884.48,
    23888.79,
    23888.16,
    23885.27,
    23884.14,
    23885.90,
    23889.75,
    23884.02,
    23890.86,
    23886.63,
    23883.36,
    23887.95,
    23884.15,
    23886.58,
    23886.11,
    23889.58,
    23888.10,
    23889.13,
    23887.17,
    23884.72,
    23886.34,
    23888.97,
    23883.00,
    23886.28,
    23885.07,
    23887.81,
    23890.16,
    23886.93,
    23880.30,
    23889.09,
    23883.39,
    23887.66,
    23881.63,
    23885.83,
    23888.12,
    23886.56,
    23883.73,
    23886.47,
    23888.33,
    23884.46,
    23887.32,
    23884.26,
    23887.18,
    23886.58,
    23887.28,
    23883.57,
    23886.63,
    23888.48,
    23886.03,
    23888.91,
    23879.60,
    23887.33,
    23888.71,
    23885.55,
    23889.47,
    23885.53,
    23886.36,
    23879.57,
    23887.34,
    23888.74,
    23889.18,
    23888.54,
    23888.78,
    23889.31,
    23811.60,
    23886.65,
    23887.02,
    23886.22,
    23880.60,
    23882.76,
    23885.07,
    23878.94,
    23883.63,
    23886.39,
    23887.97,
    23885.37,
    23886.44,
    23883.50,
    23884.46,
    23888.22,
    23874.79,
    23885.01,
    23880.62,
    23886.23,
    23885.75,
    23888.06,
    23866.87,
    23886.26,
    23886.35,
    23883.48,
    23880.55,
    23886.63,
    23882.55,
    23885.78,
    23883.63,
    23883.28,
    23883.01,
    23884.97,
    23885.45,
    23885.11,
    23885.01,
    23884.60,
    23864.52,
    23866.95,
    23869.44,
    23875.92,
    22770.47,
    23869.75,
    23879.55,
    23870.63,
    23423.01,
    23866.22,
    23854.05,
    23877.81,
    23879.21,
    23879.08,
    23877.71,
    23876.00,
    23873.38,
    23876.08,
    23395.18,
    23872.80,
    23877.93,
    23866.82,
    23877.42,
    23871.79,
    23875.53,
    23874.25,
    23875.36,
    23875.61,
    23876.27,
    23877.03,
    23878.21,
    23871.89,
    23880.74,
    23877.89,
    23875.39,
    23876.36,
    23880.53,
    23875.12,
    23879.77,
    23881.61,
    23876.44,
    23876.23,
    23878.96,
    23878.45,
    23874.18,
    23874.77,
    23877.15,
    23882.37,
    23881.69,
    23882.64,
    23879.71,
    23883.69,
    23873.56,
    23883.88,
    23883.73,
    23884.29,
    23883.48,
    23882.47,
    23884.54,
    23881.83,
    23882.05,
    23885.35,
    23879.02,
    23885.07,
    23886.33,
    23885.08,
    23879.23,
    23884.82,
    23880.45,
    23884.76,
    23884.54,
    23878.74,
    23878.57,
    23878.61,
    23873.32,
    23878.41,
    23881.99,
    23880.67,
    23879.36,
    23880.25,
    23881.12,
    23881.86,
    23879.59,
    23877.10,
    23834.74,
    23878.33,
    23881.76,
    23879.25,
    23881.72,
    23881.34,
    23876.31,
    23883.20,
    23878.11,
    23882.01,
    23883.82,
    23869.70,
    23881.44,
    23878.55,
    23880.28,
    23880.63,
    23874.51,
    23875.40,
    23881.78,
    23882.71,
    23878.88,
    23875.07,
    23879.46,
    23881.65,
    23871.24,
    23872.29,
    23881.95,
    23872.87,
    23879.44,
    23882.11,
    23884.10,
    23880.91,
    23847.08,
    23870.92,
    23874.19,
    23876.79,
    23869.95,
    23874.85,
    23869.95,
    23878.56,
    23877.96,
    23879.73,
    23880.28,
    23878.29,
    23878.50,
    23878.75,
    23881.95,
    23880.89,
    23879.10,
    23878.44,
    23880.56,
    23409.34,
    23887.41,
    23881.26,
    23884.37,
    23884.54,
    23882.87,
    23885.65,
    23888.51,
    23885.31,
    23886.96,
    23885.90,
    23883.69,
    23887.33,
    23887.42,
    23886.83,
    23882.58,
    23884.47,
    23883.93,
    23884.41,
    23887.10,
    23883.12,
    23885.79,
    23889.79,
    23877.50,
    23887.19,
    23887.09,
    23882.81,
    23885.83,
    23883.65,
    23870.61,
    23886.35,
    23877.14,
    23885.01,
    23886.19,
    23888.71,
    23884.28,
    23885.98,
    23880.47,
    23885.67,
    23884.44,
    23886.71,
    23882.91,
    23886.38,
    23881.87,
    23886.04,
    23877.09,
    23886.26,
    23889.77,
    23888.87,
    23885.27,
    23887.00,
    23884.76,
    23883.46,
    23883.53,
    23888.54,
    23886.14,
    23884.91,
    23885.80,
    23887.09,
    23885.05,
    23880.11,
    23883.73,
    23886.71,
    23887.48,
    23889.83,
    23878.68,
    23876.62,
    23889.19,
    23884.46,
    23887.58,
    23886.63,
    23887.04,
    23887.69,
    23830.73,
    23881.32,
    23885.33,
    23889.70,
    23884.68,
    23887.54,
    23887.84,
    23887.19,
    23886.00,
    23886.63,
    23880.02,
    23885.43,
    23881.71,
    23886.28,
    23390.97,
    23880.16,
    23864.43,
    23881.83,
    23882.20,
    23886.21,
    23882.32,
    23883.27,
    23885.13,
    23883.15,
    23881.49,
    23881.81,
    23885.65,
    23885.58,
    23883.47,
    23486.46,
    23883.47,
    23884.77,
    23885.17,
    23887.62,
    23888.36,
    23877.66,
    23888.03,
    23876.49,
    23883.95,
    23884.25,
    23880.93,
    23887.19,
    23890.95,
    23880.10,
    23874.49,
    23876.66,
    23881.57,
    23877.23,
    23885.70,
    23883.68,
    23886.84,
    23890.27,
    23850.27,
    23885.46,
    23884.30,
    23887.71,
    23886.53,
    23885.56,
    23884.12,
    23884.60,
    23883.83,
    23888.60,
    23889.69,
    23887.50,
    23886.78,
    23890.07
  ]
}

Statistical benchmark complete.

  [Elapsed: 4230 ms]
```

</details>

---

## Comparison Benchmark (Falcon-512 vs ML-DSA-44)

```
================================================================
  Falcon-512 vs ML-DSA-44 Comparison  (qMEMO / IIT Chicago)
================================================================
─── Falcon-512 ───
  [keygen] warm-up ... benchmarking 100 trials ... 158.2 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 4365.3 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 23849.7 ops/sec
─── ML-DSA-44 ───
  [keygen] warm-up ... benchmarking 100 trials ... 49401.3 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 15538.2 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 48776.9 ops/sec
================================================================
  HEAD-TO-HEAD COMPARISON
================================================================
  Metric                      Falcon-512       ML-DSA-44  Winner
  ──────────────────────  ──────────────  ──────────────  ──────────
  Keygen throughput             158.2 /s      49401.3 /s    faster ►
  Sign throughput              4365.3 /s      15538.2 /s    faster ►
  Verify throughput           23849.7 /s      48776.9 /s    faster ►
  Keygen latency              6322.4 µs         20.2 µs     faster ►
  Sign latency                 229.1 µs         64.4 µs     faster ►
  Verify latency                41.9 µs         20.5 µs     faster ►
  Public key size                897 B           1312 B     ◄ smaller
  Secret key size               1281 B           2560 B     ◄ smaller
  Signature size                 659 B           2420 B     ◄ smaller
  On-chain tx overhead          1556 B           3732 B     ◄ smaller
================================================================
  BLOCKCHAIN IMPACT ANALYSIS
================================================================
  Scenario: 4000 transactions per block (single-threaded verification)
  Falcon-512 block verify time :    167.7 ms
  ML-DSA-44  block verify time :     82.0 ms
  Speedup (Falcon / ML-DSA)    :     0.49x
  Falcon-512 block sig data    :   6078.1 KB  (1556 B/tx)
  ML-DSA-44  block sig data    :  14578.1 KB  (3732 B/tx)
  Size ratio (Falcon / ML-DSA) :     0.42x
  ► Recommendation: ML-DSA-44
    Simpler constant-time implementation and faster signing
    may outweigh the larger signature size depending on the
    target blockchain's block size limits.
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_vs_mldsa44",
  "timestamp": "2026-02-28T22:32:54Z",
  "config": {
    "keygen_trials": 100,
    "sign_trials": 1000,
    "verify_trials": 10000,
    "message_len": 256
  },
  "algorithms": {
    "Falcon-512": {
      "keygen_ops_sec": 158.17,
      "keygen_us_op": 6322.36,
      "sign_ops_sec": 4365.30,
      "sign_us_op": 229.08,
      "verify_ops_sec": 23849.73,
      "verify_us_op": 41.93,
      "pubkey_bytes": 897,
      "privkey_bytes": 1281,
      "signature_bytes": 659,
      "total_tx_overhead": 1556
    },
    "ML-DSA-44": {
      "keygen_ops_sec": 49401.28,
      "keygen_us_op": 20.24,
      "sign_ops_sec": 15538.20,
      "sign_us_op": 64.36,
      "verify_ops_sec": 48776.85,
      "verify_us_op": 20.50,
      "pubkey_bytes": 1312,
      "privkey_bytes": 2560,
      "signature_bytes": 2420,
      "total_tx_overhead": 3732
    }
  },
  "comparison": {
    "verify_speedup_falcon": 0.4890,
    "sign_speedup_falcon": 0.2809,
    "keygen_speedup_falcon": 0.0032,
    "signature_size_ratio": 0.2723,
    "pubkey_size_ratio": 0.6837,
    "total_tx_overhead_falcon": 1556,
    "total_tx_overhead_dilithium": 3732,
    "tx_overhead_ratio": 0.4169
  }
}

Comparison benchmark complete.

  [Elapsed: 1733 ms]
```

</details>

---

## Multicore Verification (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multicore Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair and signing message ...
OK. Signature length: 659 bytes.
Cores  |  Throughput (ops/sec)  |  Speedup  |  Efficiency
-------|------------------------|-----------|------------
   1   |               20013     |    1.00   |  100.0%
   2   |               38771     |    1.94   |   96.9%
   4   |               67317     |    3.36   |   84.1%
   6   |              107917     |    5.39   |   89.9%
   8   |              151546     |    7.57   |   94.7%
  10   |              176714     |    8.83   |   88.3%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_verify",
  "timestamp": "2026-02-28T22:32:56Z",
  "algorithm": "Falcon-512",
  "verif_per_thread": 1000,
  "warmup_per_thread": 100,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [20013, 38771, 67317, 107917, 151546, 176714],
  "speedup": [1.00, 1.94, 3.36, 5.39, 7.57, 8.83],
  "efficiency_pct": [100.0, 96.9, 84.1, 89.9, 94.7, 88.3]
}

Multicore benchmark complete.

  [Elapsed: 420 ms]
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
Concurrent (4 workers): 3.117 ms total, 0.0312 ms avg, 32081 ops/sec
Sequential (baseline):   4.766 ms total, 0.0477 ms avg, 20984 ops/sec
Concurrent yields 34.6% lower latency (better parallelism)
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_concurrent_verify",
  "timestamp": "2026-02-28T22:32:56Z",
  "algorithm": "Falcon-512",
  "concurrent": {
    "signatures": 100,
    "worker_threads": 4,
    "total_time_ms": 3.1171,
    "avg_latency_ms": 0.0312,
    "throughput": 32081
  },
  "sequential": {
    "total_time_ms": 4.7655,
    "avg_latency_ms": 0.0477,
    "throughput": 20984
  },
  "analysis": "Concurrent yields 34.6% lower latency (better parallelism)"
}

Concurrent benchmark complete.

  [Elapsed: 720 ms]
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
    min=651   max=658   avg=654.4  std=1.8 bytes
Running sequential signing (baseline) ...
  Signature sizes (n=100, max-buf=652 B):
    min=649   max=660   avg=655.1  std=2.0 bytes
================================================================
  RESULTS
================================================================
  Concurrent (4 workers):  15.316 ms total |  0.1532 ms/op |     6529 ops/sec
  Sequential (baseline):   39.452 ms total |  0.3945 ms/op |     2535 ops/sec
  Speedup:  2.58x
  Concurrent yields 2.6x speedup (61.2% faster than sequential)
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
  "timestamp": "2026-02-28T22:32:57Z",
  "algorithm": "Falcon-512",
  "config": {
    "signing_tasks": 100,
    "worker_threads": 4,
    "message_len": 256,
    "sig_max_bytes": 752
  },
  "concurrent": {
    "total_time_ms": 15.3157,
    "avg_latency_ms": 0.1532,
    "throughput_ops_sec": 6529
  },
  "sequential": {
    "total_time_ms": 39.4525,
    "avg_latency_ms": 0.3945,
    "throughput_ops_sec": 2535
  },
  "speedup": 2.5759,
  "sig_size_stats": {
    "min_bytes": 649,
    "max_bytes": 660,
    "avg_bytes": 655.1,
    "spec_max_bytes": 752
  },
  "analysis": "Concurrent yields 2.6x speedup (61.2% faster than sequential)"
}

Concurrent signing benchmark complete.

  [Elapsed: 77 ms]
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
   1   |                4111     |                4111     |    1.00   |  100.0%
   2   |                7930     |                3965     |    1.93   |   96.4%
   4   |               15243     |                3811     |    3.71   |   92.7%
   6   |               22452     |                3742     |    5.46   |   91.0%
   8   |               29407     |                3676     |    7.15   |   89.4%
  10   |               36718     |                3672     |    8.93   |   89.3%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_sign",
  "timestamp": "2026-02-28T22:32:57Z",
  "algorithm": "Falcon-512",
  "sign_per_thread": 500,
  "warmup_per_thread": 50,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [4111, 7930, 15243, 22452, 29407, 36718],
  "speedup": [1.00, 1.93, 3.71, 5.46, 7.15, 8.93],
  "efficiency_pct": [100.0, 96.4, 92.7, 91.0, 89.4, 89.3]
}

Multicore signing benchmark complete.

  [Elapsed: 970 ms]
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
Falcon-512                L1      666    647    663  655.0     2.2   654  655  656   659   660
Falcon-padded-512         L1      666    666    666  666.0     0.0   666  666  666   666   666
Falcon-1024               L5     1280   1257   1282  1270.6     3.1  1269 1271 1273  1276  1278
Falcon-padded-1024        L5     1280   1280   1280  1280.0     0.0  1280 1280 1280  1280  1280
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon_signature_size_distribution",
  "timestamp": "2026-02-28T22:32:58Z",
  "num_signatures": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "spec_max_bytes": 666,
      "min": 647,
      "max": 663,
      "mean": 655.05,
      "std_dev": 2.19,
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
      "min": 1257,
      "max": 1282,
      "mean": 1270.60,
      "std_dev": 3.15,
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

  [Elapsed: 13964 ms]
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
ECDSA secp256k1             2655           2638             2963              71.0
Ed25519                    23062          23184             8984              64.0
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "classical_signature_baselines",
  "timestamp": "2026-02-28T22:33:12Z",
  "bench_iters": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "ECDSA secp256k1",
      "keygen_ops_per_sec": 2655,
      "sign_ops_per_sec": 2638,
      "verify_ops_per_sec": 2963,
      "avg_sig_bytes": 71.0
    },
    {
      "name": "Ed25519",
      "keygen_ops_per_sec": 23062,
      "sign_ops_per_sec": 23184,
      "verify_ops_per_sec": 8984,
      "avg_sig_bytes": 64.0
    }
  ]
}

Classical baseline benchmark complete.

  [Elapsed: 13052 ms]
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
Falcon-512           L1      897    1281       752       154      4306     23906
Falcon-1024          L5     1793    2305      1462        52      2133     11794
ML-DSA-44            L2     1312    2560      2420     51466     15299     48627
ML-DSA-65            L3     1952    4032      3309     29832      9971     30287
SLH-DSA-SHA2-128f    L1       32      64     17088      1062        45       734
ECDSA secp256k1       -       65      32        71      2660      2643      2901
Ed25519               -       32      32        64     23045     23246      9013
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "comprehensive_signature_comparison",
  "timestamp": "2026-02-28T22:33:25Z",
  "bench_iters": 1000,
  "message_len": 256,
  "algorithms": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "pubkey_bytes": 897,
      "seckey_bytes": 1281,
      "sig_bytes": 752,
      "keygen_ops_per_sec": 154,
      "sign_ops_per_sec": 4306,
      "verify_ops_per_sec": 23906
    },
    {
      "name": "Falcon-1024",
      "nist_level": 5,
      "pubkey_bytes": 1793,
      "seckey_bytes": 2305,
      "sig_bytes": 1462,
      "keygen_ops_per_sec": 52,
      "sign_ops_per_sec": 2133,
      "verify_ops_per_sec": 11794
    },
    {
      "name": "ML-DSA-44",
      "nist_level": 2,
      "pubkey_bytes": 1312,
      "seckey_bytes": 2560,
      "sig_bytes": 2420,
      "keygen_ops_per_sec": 51466,
      "sign_ops_per_sec": 15299,
      "verify_ops_per_sec": 48627
    },
    {
      "name": "ML-DSA-65",
      "nist_level": 3,
      "pubkey_bytes": 1952,
      "seckey_bytes": 4032,
      "sig_bytes": 3309,
      "keygen_ops_per_sec": 29832,
      "sign_ops_per_sec": 9971,
      "verify_ops_per_sec": 30287
    },
    {
      "name": "SLH-DSA-SHA2-128f",
      "nist_level": 1,
      "pubkey_bytes": 32,
      "seckey_bytes": 64,
      "sig_bytes": 17088,
      "keygen_ops_per_sec": 1062,
      "sign_ops_per_sec": 45,
      "verify_ops_per_sec": 734
    },
    {
      "name": "ECDSA secp256k1",
      "nist_level": 0,
      "pubkey_bytes": 65,
      "seckey_bytes": 32,
      "sig_bytes": 71,
      "keygen_ops_per_sec": 2660,
      "sign_ops_per_sec": 2643,
      "verify_ops_per_sec": 2901
    },
    {
      "name": "Ed25519",
      "nist_level": 0,
      "pubkey_bytes": 32,
      "seckey_bytes": 32,
      "sig_bytes": 64,
      "keygen_ops_per_sec": 23045,
      "sign_ops_per_sec": 23246,
      "verify_ops_per_sec": 9013
    }
  ]
}

Comprehensive comparison complete.

  [Elapsed: 55953 ms]
```

</details>

---

