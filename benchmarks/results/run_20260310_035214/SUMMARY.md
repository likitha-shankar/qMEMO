# qMEMO Benchmark Run -- 20260310_035214

**Platform:** x86_64 / Linux
**Date:** 2026-03-10T03:53:53Z
**liboqs:** 0.15.0

---

## Key Material Inspection (correctness audit)

```
    [0040] 40 bd 2b cc d7 42 ed 23                           |@.+..B.#|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (406.3 µs)
    (b) Verify corrupted sig  / correct msg:  FAIL ✓ (expected)
    (c) Verify correct sig   / corrupted msg: FAIL ✓ (expected)
────────────────────────────────────────────────────────────────
  [7/7] Ed25519  (Classical -- Edwards Curve -- fixed 64-byte sig)
────────────────────────────────────────────────────────────────
  Key Sizes:
    Public key:  32 bytes  (raw)
    Secret key:  32 bytes  [NOT DISPLAYED -- secret material]
    Keygen time: 58.1 µs
  Public Key (32 bytes):
    [0000] 08 76 e6 71 48 f8 8f 10  a5 a4 4b 2a 09 7d 44 e9  |.v.qH.....K*.}D.|
    [0010] f7 a2 5e a3 3e 4b fa 6d  c0 2d aa 95 57 a7 d0 c6  |..^.>K.m.-..W...|
  Signature (64 bytes):
    Sign time:  50.0 µs
    [0000] 85 99 6c 36 63 bd 80 fb  b4 74 99 84 a7 df 98 1c  |..l6c....t......|
    [0010] b1 02 03 bd f0 73 f3 03  34 48 a0 99 3c 4c 30 ac  |.....s..4H..<L0.|
    [0020] e2 21 f3 4b 74 77 05 f4  a7 72 86 4a 60 18 43 04  |.!.Ktw...r.J`.C.|
    [0030] 28 eb 31 a9 5a ef 4f 2f  13 ea b5 b8 cc 77 6e 01  |(.1.Z.O/.....wn.|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (123.4 µs)
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
  Timestamp:   2026-03-10T03:52:16Z
  [Elapsed: 94 ms]
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
       Signature produced: 654 bytes (max 752).
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
  │  Total time    :      0.423294 sec            │
  │  Ops/sec       :      23624.23                │
  │  Per operation :      0.042 ms              │
  │                       42.33 µs              │
  │  Est. cycles   :     148153  (@ 3.5 GHz)  │
  │  Signature     :   654 bytes               │
  │  Public key    :   897 bytes               │
  │  Secret key    :  1281 bytes               │
  └─────────────────────────────────────────────┘
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify",
  "timestamp": "2026-03-10T03:52:16Z",
  "algorithm": "Falcon-512",
  "iterations": 10000,
  "warmup": 100,
  "total_time_sec": 0.423294,
  "ops_per_sec": 23624.23,
  "ms_per_op": 0.042,
  "us_per_op": 42.33,
  "cycles_per_op": 148152.96,
  "signature_bytes": 654,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281
}

Benchmark complete.

  [Elapsed: 464 ms]
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
  │  Mean   (ops/sec)    :     23673.39                        │
  │  Std Dev             :       140.40                        │
  │  CV                  :        0.59%                       │
  ├───────────────────────────────────────────────────────────┤
  │  Min    (ops/sec)    :     22177.04                        │
  │  P5                  :     23653.12                        │
  │  Median (P50)        :     23696.37                        │
  │  P95                 :     23722.66                        │
  │  P99                 :     23732.42                        │
  │  Max    (ops/sec)    :     23741.00                        │
  │  IQR                 :        22.38                        │
  ├───────────────────────────────────────────────────────────┤
  │  Skewness            :      -6.7382  (left-skewed)       │
  │  Excess kurtosis     :      50.4938  (heavy tails)       │
  │  Jarque-Bera stat    :  113801.3220                      │
  │  Normality (α=0.05)  : FAIL (non-Gauss.)                      │
  │  Outliers (> 3σ)     :     26 / 1000                        │
  └───────────────────────────────────────────────────────────┘
  → Distribution departs from Gaussian (JB = 113801.32 > 5.991).
    Report: median and IQR.  Use non-parametric tests (Mann-Whitney U).
  → CV = 0.59% -- excellent measurement stability.
[7/7] JSON output:
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify_statistical",
  "timestamp": "2026-03-10T03:52:17Z",
  "algorithm": "Falcon-512",
  "trials": 1000,
  "iterations_per_trial": 100,
  "total_verifications": 100000,
  "signature_bytes": 654,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281,
  "statistics": {
    "mean_ops_sec": 23673.39,
    "stddev_ops_sec": 140.40,
    "cv_percent": 0.59,
    "min_ops_sec": 22177.04,
    "p5_ops_sec": 23653.12,
    "median_ops_sec": 23696.37,
    "p95_ops_sec": 23722.66,
    "p99_ops_sec": 23732.42,
    "max_ops_sec": 23741.00,
    "iqr_ops_sec": 22.38,
    "skewness": -6.738160,
    "excess_kurtosis": 50.493767,
    "jarque_bera": 113801.322031,
    "normality_pass": false,
    "outliers_count": 26
  },
  "raw_data": [
    22599.43,
    23245.46,
    23594.41,
    23689.13,
    23614.93,
    23649.44,
    23616.20,
    23699.11,
    23690.55,
    23610.56,
    23703.76,
    23695.14,
    23682.56,
    23212.45,
    23670.89,
    23680.37,
    23678.60,
    23697.09,
    23697.72,
    23686.69,
    23683.30,
    23678.70,
    23677.57,
    23676.64,
    23680.93,
    23692.26,
    23688.05,
    23673.33,
    23673.82,
    23688.65,
    23708.58,
    23706.43,
    23719.77,
    23724.80,
    23715.24,
    23698.19,
    23683.71,
    23732.64,
    23705.93,
    23695.08,
    23722.25,
    23712.23,
    23715.81,
    23720.61,
    23719.96,
    23709.07,
    23712.22,
    23716.85,
    23728.68,
    23725.83,
    23705.19,
    23710.97,
    23694.50,
    23719.53,
    23697.42,
    23711.52,
    23712.65,
    23730.97,
    23734.08,
    23722.65,
    23724.73,
    23736.49,
    23687.72,
    23713.83,
    23696.10,
    23713.08,
    23720.33,
    23714.67,
    23694.02,
    23723.01,
    23713.10,
    23722.38,
    23719.15,
    23712.93,
    23728.23,
    23725.50,
    23732.58,
    23726.97,
    23707.75,
    23724.65,
    23702.76,
    23723.46,
    23701.88,
    23723.33,
    23721.47,
    23722.52,
    23702.05,
    23709.17,
    23723.51,
    23732.42,
    23718.22,
    23709.12,
    23717.09,
    23732.36,
    23718.75,
    23721.03,
    23708.23,
    23734.44,
    23719.68,
    23708.22,
    23712.64,
    23725.81,
    23728.39,
    23713.60,
    23713.65,
    23687.06,
    23721.24,
    23706.46,
    23714.48,
    23700.79,
    23703.11,
    23718.85,
    23732.24,
    23732.06,
    23721.00,
    23718.70,
    23741.00,
    23715.24,
    23727.32,
    23721.20,
    23729.98,
    23723.36,
    23727.30,
    23726.82,
    23715.06,
    23713.66,
    23730.09,
    23733.93,
    23725.20,
    23719.65,
    23711.74,
    23734.61,
    23724.61,
    23692.91,
    23708.58,
    23717.82,
    23709.67,
    23710.13,
    23711.02,
    23739.46,
    23714.20,
    23730.46,
    23708.72,
    23721.48,
    23722.95,
    23713.10,
    23733.43,
    23707.65,
    23729.38,
    23718.55,
    23725.35,
    23719.87,
    23696.90,
    23721.96,
    23715.48,
    23717.61,
    23702.61,
    23705.18,
    23714.18,
    23710.34,
    23728.31,
    23712.94,
    23721.91,
    23727.53,
    23712.89,
    23714.07,
    23721.77,
    23724.48,
    23726.12,
    23724.69,
    23705.04,
    23701.83,
    23708.09,
    23718.23,
    22615.97,
    23691.39,
    23707.65,
    23699.41,
    23680.45,
    23703.82,
    23675.16,
    23701.59,
    23686.09,
    23681.59,
    23698.52,
    23688.45,
    23695.97,
    23198.86,
    23684.60,
    23697.46,
    23704.41,
    23693.18,
    23695.96,
    23693.76,
    23706.08,
    23698.04,
    23693.16,
    23691.38,
    23688.34,
    23695.14,
    23707.21,
    23692.15,
    23702.88,
    23693.95,
    23672.40,
    23689.04,
    23695.23,
    23697.01,
    23699.72,
    23689.28,
    23686.62,
    23694.39,
    23696.54,
    23698.20,
    23681.55,
    23697.27,
    23695.97,
    23679.81,
    23703.96,
    23677.48,
    23708.80,
    23697.92,
    23697.19,
    23670.62,
    23688.90,
    23704.41,
    23699.55,
    23693.22,
    23658.15,
    23701.92,
    23697.84,
    23695.49,
    23678.05,
    23699.22,
    23470.39,
    23345.17,
    23688.53,
    23700.67,
    23703.57,
    23708.98,
    23695.53,
    23703.40,
    23685.38,
    23696.38,
    23693.53,
    23679.92,
    23689.92,
    23700.23,
    23704.27,
    23691.78,
    23706.90,
    23685.26,
    23696.15,
    23696.33,
    23701.90,
    23687.53,
    23714.23,
    23688.70,
    23680.91,
    23689.91,
    23687.76,
    23694.16,
    23690.60,
    23696.25,
    23690.74,
    23690.19,
    23694.99,
    23688.34,
    23678.01,
    23706.69,
    23713.48,
    23716.45,
    23694.80,
    23691.81,
    23689.29,
    23674.89,
    23689.56,
    23688.21,
    23669.38,
    23686.29,
    23695.66,
    23682.90,
    23683.15,
    23698.01,
    23693.70,
    23705.92,
    23691.39,
    23711.34,
    23435.26,
    23691.96,
    23699.75,
    23681.36,
    23688.39,
    23699.23,
    23689.10,
    23681.41,
    23698.05,
    23695.95,
    23707.31,
    23681.62,
    23696.72,
    23690.97,
    23696.60,
    23691.41,
    23673.32,
    23693.93,
    23699.73,
    23698.52,
    23710.81,
    23693.76,
    23430.84,
    23218.18,
    23680.52,
    22317.75,
    23157.87,
    23557.05,
    22361.44,
    23418.59,
    23271.68,
    22933.03,
    23368.93,
    23335.14,
    23276.22,
    23150.30,
    23023.86,
    22922.03,
    22872.41,
    23679.39,
    23186.99,
    23691.38,
    23691.28,
    23707.45,
    23675.53,
    23697.78,
    23716.04,
    23706.78,
    23685.09,
    23693.28,
    23699.45,
    23702.18,
    23718.36,
    23693.79,
    23719.42,
    23709.90,
    23709.11,
    23718.60,
    23679.75,
    23713.75,
    23710.75,
    23725.78,
    23688.10,
    23708.59,
    23696.60,
    23703.97,
    23721.08,
    23688.51,
    23702.54,
    23693.15,
    23715.05,
    23244.15,
    23703.76,
    23705.83,
    23720.56,
    23716.10,
    23694.44,
    23703.47,
    23714.74,
    23691.56,
    23686.29,
    23690.66,
    23709.69,
    23712.71,
    23689.46,
    23667.95,
    23694.86,
    23698.91,
    23719.20,
    23678.63,
    23697.10,
    23697.24,
    23703.09,
    23695.26,
    23692.49,
    23702.93,
    23695.45,
    23696.54,
    23685.17,
    23702.76,
    23684.43,
    23708.14,
    23706.61,
    23689.09,
    23713.29,
    23665.30,
    23697.46,
    23696.77,
    23703.06,
    23686.39,
    23685.07,
    23706.77,
    23684.83,
    23697.51,
    23691.23,
    23693.48,
    23692.08,
    23676.80,
    23691.87,
    23702.27,
    23693.47,
    23691.07,
    23705.06,
    23699.46,
    23698.32,
    23703.50,
    23694.81,
    23688.79,
    22698.46,
    23680.81,
    23720.69,
    23702.56,
    23688.05,
    23703.75,
    23701.00,
    23694.53,
    23704.10,
    23705.95,
    23693.59,
    23708.89,
    23721.08,
    23710.28,
    23715.28,
    23695.77,
    23705.33,
    23708.91,
    23695.77,
    23710.31,
    23694.83,
    23709.70,
    23708.42,
    23712.46,
    23657.76,
    23705.34,
    23707.28,
    23714.76,
    23718.41,
    23690.27,
    23718.73,
    23703.87,
    23714.18,
    23684.12,
    23713.29,
    23710.71,
    23708.21,
    23683.02,
    23689.31,
    23714.51,
    23699.72,
    23689.87,
    23696.28,
    23704.88,
    23718.12,
    23707.99,
    23717.76,
    23693.76,
    23597.16,
    23709.78,
    23711.94,
    23714.85,
    23693.19,
    23703.78,
    23698.74,
    23705.46,
    23698.36,
    23696.35,
    23702.31,
    23699.96,
    23702.37,
    23688.09,
    23701.05,
    23710.23,
    23700.69,
    23694.62,
    23706.05,
    23694.12,
    23716.63,
    23719.13,
    23691.46,
    23706.05,
    23690.55,
    23720.43,
    23684.76,
    23718.57,
    23706.00,
    23702.70,
    23706.90,
    23696.86,
    23710.49,
    23711.24,
    23715.62,
    23698.01,
    23705.88,
    23701.13,
    23713.86,
    23704.92,
    23683.20,
    23715.21,
    23719.38,
    23699.65,
    23700.26,
    23683.31,
    23701.68,
    23712.46,
    23685.64,
    23686.60,
    23697.41,
    23716.21,
    23695.67,
    23710.53,
    23711.77,
    23695.73,
    23712.76,
    23708.51,
    23709.23,
    23701.34,
    23715.28,
    23711.31,
    23710.28,
    23686.72,
    23690.83,
    23713.17,
    23710.81,
    23682.04,
    23715.08,
    23709.36,
    23702.96,
    23679.90,
    23710.57,
    23601.03,
    23705.09,
    23697.99,
    23677.93,
    23705.31,
    23699.01,
    23702.03,
    23697.97,
    23678.54,
    23710.75,
    23684.11,
    23711.77,
    23701.71,
    23694.35,
    23709.90,
    23706.23,
    23670.51,
    23692.36,
    23703.08,
    23711.06,
    23697.09,
    23708.80,
    23673.50,
    23692.16,
    23710.32,
    23689.32,
    23682.47,
    23716.54,
    23709.43,
    23705.15,
    23700.46,
    23693.42,
    23709.12,
    23700.89,
    23711.17,
    23697.19,
    23704.22,
    23714.29,
    23694.50,
    23706.19,
    23684.03,
    23695.50,
    23692.71,
    23703.46,
    23695.46,
    23714.12,
    23693.50,
    23714.72,
    23714.93,
    23701.38,
    23700.84,
    23719.71,
    23719.49,
    23708.12,
    23696.15,
    23713.24,
    23703.98,
    23706.18,
    23701.20,
    23720.05,
    23717.98,
    23717.85,
    23712.46,
    23701.14,
    23724.76,
    23233.87,
    23710.71,
    23694.52,
    23688.66,
    23696.61,
    23705.77,
    23714.84,
    23690.04,
    23709.04,
    23708.51,
    23708.33,
    23686.57,
    23711.20,
    23717.84,
    23719.72,
    23701.45,
    23686.93,
    23705.59,
    23705.22,
    23720.70,
    23699.94,
    23721.58,
    23704.76,
    23703.10,
    23698.61,
    23686.39,
    23703.33,
    23705.51,
    23695.87,
    23693.85,
    23704.01,
    23718.83,
    23707.38,
    23714.81,
    23688.26,
    23712.30,
    23711.67,
    23716.28,
    23709.61,
    23687.81,
    23710.15,
    22681.15,
    23673.82,
    23686.30,
    23683.33,
    23689.10,
    23688.03,
    23672.91,
    23690.06,
    23685.55,
    23684.89,
    23663.87,
    23680.37,
    23682.92,
    23689.21,
    23703.29,
    23685.65,
    23680.66,
    23699.54,
    23697.87,
    23677.71,
    23706.25,
    23683.85,
    23691.03,
    23701.50,
    23689.18,
    23695.55,
    23683.96,
    23715.31,
    23692.73,
    23694.81,
    23701.10,
    23691.16,
    23699.77,
    23684.06,
    23671.07,
    23697.59,
    23690.50,
    23676.63,
    23703.96,
    23692.21,
    23697.69,
    23704.83,
    23684.77,
    23690.69,
    23689.07,
    23689.57,
    23668.54,
    23695.80,
    23691.47,
    23688.13,
    23616.48,
    23671.94,
    23683.38,
    23675.46,
    23676.50,
    23667.75,
    23682.10,
    23693.79,
    23665.46,
    23644.68,
    23670.33,
    23686.90,
    23688.92,
    23702.67,
    23669.50,
    23709.46,
    23713.93,
    23700.59,
    23694.69,
    23680.11,
    23704.14,
    23697.82,
    23715.22,
    23717.88,
    23684.41,
    23690.04,
    23693.75,
    23710.54,
    23684.74,
    23691.84,
    23698.22,
    23709.49,
    23677.75,
    23678.37,
    23698.75,
    23704.39,
    23694.17,
    23683.94,
    23677.67,
    23702.16,
    23710.81,
    23695.07,
    23678.26,
    23709.51,
    23689.58,
    23702.94,
    23700.19,
    23686.81,
    23697.54,
    23696.97,
    23706.92,
    23687.17,
    23696.72,
    23702.57,
    23711.24,
    23681.27,
    23686.37,
    23700.45,
    23702.64,
    23708.64,
    23688.54,
    23707.76,
    23703.72,
    23699.20,
    23691.09,
    23692.28,
    23714.49,
    23701.69,
    23712.94,
    23700.49,
    23708.07,
    23699.82,
    23546.24,
    23684.31,
    23713.58,
    23710.50,
    23703.45,
    23701.64,
    23693.18,
    23701.02,
    23694.70,
    23698.87,
    23712.24,
    23696.46,
    23727.06,
    23694.44,
    23708.74,
    23696.21,
    23694.39,
    23709.73,
    23711.04,
    23719.69,
    23704.95,
    23709.90,
    23710.24,
    23711.89,
    23694.62,
    23713.84,
    23714.48,
    22177.04,
    22956.49,
    23682.94,
    23676.56,
    23659.52,
    23706.70,
    23691.34,
    23682.79,
    22769.01,
    22700.18,
    23702.23,
    23694.96,
    23660.28,
    23708.03,
    23709.01,
    23695.77,
    23663.41,
    23694.55,
    23693.39,
    23684.51,
    23704.08,
    23685.02,
    23694.72,
    23694.08,
    23692.30,
    23688.59,
    23682.98,
    23666.01,
    23689.31,
    23677.66,
    23693.20,
    23691.60,
    23707.92,
    23705.10,
    23671.34,
    23691.80,
    23682.61,
    23689.04,
    23700.07,
    23684.54,
    23691.75,
    23677.03,
    23688.87,
    23664.35,
    23698.20,
    23670.86,
    23691.11,
    23689.46,
    23671.02,
    23671.74,
    23682.14,
    23663.17,
    23681.23,
    23684.67,
    23700.37,
    23690.09,
    23682.55,
    23662.61,
    23689.80,
    23686.73,
    23693.04,
    23667.03,
    23679.28,
    23682.71,
    23687.32,
    23678.63,
    23664.32,
    23678.57,
    23697.58,
    23690.08,
    23680.90,
    23685.42,
    23688.77,
    23676.31,
    23695.83,
    23645.72,
    23679.05,
    23692.83,
    23686.28,
    23676.08,
    23697.55,
    23688.64,
    23686.24,
    23683.71,
    23672.35,
    23707.01,
    23704.11,
    23696.95,
    23677.70,
    23694.80,
    23680.66,
    23687.95,
    23678.97,
    23685.36,
    22636.84,
    23684.00,
    23690.63,
    23680.91,
    23651.44,
    23692.05,
    23687.10,
    23662.14,
    23680.88,
    23679.54,
    23687.97,
    23664.91,
    23664.94,
    23701.76,
    23689.61,
    23687.65,
    23680.90,
    23675.58,
    23679.92,
    23690.55,
    23670.48,
    23677.43,
    23683.76,
    23678.45,
    23684.49,
    23683.27,
    23695.44,
    23667.11,
    23659.00,
    23663.60,
    23229.11,
    23643.85,
    23653.21,
    23173.00,
    23686.06,
    23690.88,
    23681.24,
    23681.27,
    23678.59,
    23682.98,
    23695.79,
    23689.76,
    23659.04,
    23691.84,
    23671.22,
    23619.38,
    23658.93,
    23688.00,
    23685.82,
    23678.87,
    23691.39,
    23668.98,
    23656.35,
    23671.51,
    23686.34,
    23676.94,
    23697.96,
    23702.29,
    23702.38,
    23697.61,
    23688.34,
    23695.03,
    23730.93,
    23699.00,
    23700.86,
    23691.48,
    23708.57,
    23691.88,
    23696.28,
    23690.69,
    23695.46,
    23695.92,
    23685.11,
    23717.07,
    23668.40,
    23679.37,
    23707.17,
    23689.12,
    23677.19,
    23709.88,
    23686.28,
    23705.88,
    23686.41,
    23678.59,
    23703.04,
    23690.29,
    23691.15,
    23685.37,
    23690.60,
    23678.96,
    23681.48,
    23699.08,
    23677.21,
    23694.31,
    23688.08,
    23694.49,
    23668.50,
    23698.82,
    23684.37,
    23668.29,
    23694.94,
    23696.22,
    23692.29,
    23688.16,
    23680.55,
    23689.33,
    23676.25,
    23692.60,
    23704.66,
    23693.94,
    23682.32,
    23693.04,
    23685.33,
    23680.98
  ]
}

Statistical benchmark complete.

  [Elapsed: 4267 ms]
```

</details>

---

## Comparison Benchmark (Falcon-512 vs ML-DSA-44)

```
================================================================
  Falcon-512 vs ML-DSA-44 Comparison  (qMEMO / IIT Chicago)
================================================================
─── Falcon-512 ───
  [keygen] warm-up ... benchmarking 100 trials ... 146.6 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 4234.3 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 23699.8 ops/sec
─── ML-DSA-44 ───
  [keygen] warm-up ... benchmarking 100 trials ... 46717.8 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 14092.6 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 47165.8 ops/sec
================================================================
  HEAD-TO-HEAD COMPARISON
================================================================
  Metric                      Falcon-512       ML-DSA-44  Winner
  ──────────────────────  ──────────────  ──────────────  ──────────
  Keygen throughput             146.6 /s      46717.8 /s    faster ►
  Sign throughput              4234.3 /s      14092.6 /s    faster ►
  Verify throughput           23699.8 /s      47165.8 /s    faster ►
  Keygen latency              6821.6 µs         21.4 µs     faster ►
  Sign latency                 236.2 µs         71.0 µs     faster ►
  Verify latency                42.2 µs         21.2 µs     faster ►
  Public key size                897 B           1312 B     ◄ smaller
  Secret key size               1281 B           2560 B     ◄ smaller
  Signature size                 658 B           2420 B     ◄ smaller
  On-chain tx overhead          1555 B           3732 B     ◄ smaller
================================================================
  BLOCKCHAIN IMPACT ANALYSIS
================================================================
  Scenario: 4000 transactions per block (single-threaded verification)
  Falcon-512 block verify time :    168.8 ms
  ML-DSA-44  block verify time :     84.8 ms
  Speedup (Falcon / ML-DSA)    :     0.50x
  Falcon-512 block sig data    :   6074.2 KB  (1555 B/tx)
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
  "timestamp": "2026-03-10T03:52:21Z",
  "config": {
    "keygen_trials": 100,
    "sign_trials": 1000,
    "verify_trials": 10000,
    "message_len": 256
  },
  "algorithms": {
    "Falcon-512": {
      "keygen_ops_sec": 146.59,
      "keygen_us_op": 6821.63,
      "sign_ops_sec": 4234.28,
      "sign_us_op": 236.17,
      "verify_ops_sec": 23699.76,
      "verify_us_op": 42.19,
      "pubkey_bytes": 897,
      "privkey_bytes": 1281,
      "signature_bytes": 658,
      "total_tx_overhead": 1555
    },
    "ML-DSA-44": {
      "keygen_ops_sec": 46717.82,
      "keygen_us_op": 21.41,
      "sign_ops_sec": 14092.64,
      "sign_us_op": 70.96,
      "verify_ops_sec": 47165.80,
      "verify_us_op": 21.20,
      "pubkey_bytes": 1312,
      "privkey_bytes": 2560,
      "signature_bytes": 2420,
      "total_tx_overhead": 3732
    }
  },
  "comparison": {
    "verify_speedup_falcon": 0.5025,
    "sign_speedup_falcon": 0.3005,
    "keygen_speedup_falcon": 0.0031,
    "signature_size_ratio": 0.2719,
    "pubkey_size_ratio": 0.6837,
    "total_tx_overhead_falcon": 1555,
    "total_tx_overhead_dilithium": 3732,
    "tx_overhead_ratio": 0.4167
  }
}

Comparison benchmark complete.

  [Elapsed: 1810 ms]
```

</details>

---

## Multicore Verification (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multithreaded Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair and signing message ...
OK. Signature length: 652 bytes.
Threads |  Throughput (ops/sec)  |  Speedup  |  Efficiency
--------|------------------------|-----------|------------
   1   |               18853     |    1.00   |  100.0%
   2   |               37855     |    2.01   |  100.4%
   4   |               61380     |    3.26   |   81.4%
   6   |              106183     |    5.63   |   93.9%
   8   |              143833     |    7.63   |   95.4%
  10   |              176890     |    9.38   |   93.8%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multithreaded_verify",
  "timestamp": "2026-03-10T03:52:23Z",
  "algorithm": "Falcon-512",
  "verif_per_thread": 1000,
  "warmup_per_thread": 100,
  "threads": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [18853, 37855, 61380, 106183, 143833, 176890],
  "speedup": [1.00, 2.01, 3.26, 5.63, 7.63, 9.38],
  "efficiency_pct": [100.0, 100.4, 81.4, 93.9, 95.4, 93.8]
}

Multithreaded benchmark complete.

  [Elapsed: 468 ms]
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
Concurrent (4 workers): 4.599 ms total, 0.0460 ms avg, 21743 ops/sec
Sequential (baseline):   5.257 ms total, 0.0526 ms avg, 19021 ops/sec
Concurrent yields 12.5% lower latency (better parallelism)
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_concurrent_verify",
  "timestamp": "2026-03-10T03:52:23Z",
  "algorithm": "Falcon-512",
  "concurrent": {
    "signatures": 100,
    "worker_threads": 4,
    "total_time_ms": 4.5992,
    "avg_latency_ms": 0.0460,
    "throughput": 21743
  },
  "sequential": {
    "total_time_ms": 5.2574,
    "avg_latency_ms": 0.0526,
    "throughput": 19021
  },
  "analysis": "Concurrent yields 12.5% lower latency (better parallelism)"
}

Concurrent benchmark complete.

  [Elapsed: 730 ms]
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
  Signature sizes (n=100, max-buf=655 B):
    min=649   max=660   avg=655.3  std=2.0 bytes
Running sequential signing (baseline) ...
  Signature sizes (n=100, max-buf=657 B):
    min=648   max=661   avg=655.0  std=2.1 bytes
================================================================
  RESULTS
================================================================
  Concurrent (4 workers):  18.467 ms total |  0.1847 ms/op |     5415 ops/sec
  Sequential (baseline):   39.660 ms total |  0.3966 ms/op |     2521 ops/sec
  Speedup:  2.15x
  Concurrent yields 2.1x speedup (53.4% faster than sequential)
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
  "timestamp": "2026-03-10T03:52:24Z",
  "algorithm": "Falcon-512",
  "config": {
    "signing_tasks": 100,
    "worker_threads": 4,
    "message_len": 256,
    "sig_max_bytes": 752
  },
  "concurrent": {
    "total_time_ms": 18.4666,
    "avg_latency_ms": 0.1847,
    "throughput_ops_sec": 5415
  },
  "sequential": {
    "total_time_ms": 39.6600,
    "avg_latency_ms": 0.3966,
    "throughput_ops_sec": 2521
  },
  "speedup": 2.1477,
  "sig_size_stats": {
    "min_bytes": 648,
    "max_bytes": 661,
    "avg_bytes": 655.0,
    "spec_max_bytes": 752
  },
  "analysis": "Concurrent yields 2.1x speedup (53.4% faster than sequential)"
}

Concurrent signing benchmark complete.

  [Elapsed: 89 ms]
```

</details>

---

## Multicore Signing (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multithreaded Signing Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair ...
OK. Public key: 897 bytes, Secret key: 1281 bytes.
Config:  50 warm-up signs, 500 timed signs per thread
Threads |  Throughput (ops/sec)  |  Per-thread (ops/sec)  |  Speedup  |  Efficiency
--------|------------------------|------------------------|-----------|------------
   1   |                4130     |                4130     |    1.00   |  100.0%
   2   |                8207     |                4103     |    1.99   |   99.4%
   4   |               16066     |                4016     |    3.89   |   97.3%
   6   |               22362     |                3727     |    5.41   |   90.2%
   8   |               27295     |                3412     |    6.61   |   82.6%
  10   |               35980     |                3598     |    8.71   |   87.1%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multithreaded_sign",
  "timestamp": "2026-03-10T03:52:24Z",
  "algorithm": "Falcon-512",
  "sign_per_thread": 500,
  "warmup_per_thread": 50,
  "threads": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [4130, 8207, 16066, 22362, 27295, 35980],
  "speedup": [1.00, 1.99, 3.89, 5.41, 6.61, 8.71],
  "efficiency_pct": [100.0, 99.4, 97.3, 90.2, 82.6, 87.1]
}

Multithreaded signing benchmark complete.

  [Elapsed: 1006 ms]
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
Falcon-512                L1      666    647    664  655.0     2.2   654  655  656   659   660
Falcon-padded-512         L1      666    666    666  666.0     0.0   666  666  666   666   666
Falcon-1024               L5     1280   1258   1284  1270.7     3.1  1269 1271 1273  1276  1278
Falcon-padded-1024        L5     1280   1280   1280  1280.0     0.0  1280 1280 1280  1280  1280
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon_signature_size_distribution",
  "timestamp": "2026-03-10T03:52:25Z",
  "num_signatures": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "spec_max_bytes": 666,
      "min": 647,
      "max": 664,
      "mean": 655.01,
      "std_dev": 2.22,
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
      "min": 1258,
      "max": 1284,
      "mean": 1270.65,
      "std_dev": 3.10,
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

  [Elapsed: 14266 ms]
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
ECDSA secp256k1             2196           2186             2456              71.0
Ed25519                    21153          21453             8382              64.0
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "classical_signature_baselines",
  "timestamp": "2026-03-10T03:52:39Z",
  "bench_iters": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "ECDSA secp256k1",
      "keygen_ops_per_sec": 2196,
      "sign_ops_per_sec": 2186,
      "verify_ops_per_sec": 2456,
      "avg_sig_bytes": 71.0
    },
    {
      "name": "Ed25519",
      "keygen_ops_per_sec": 21153,
      "sign_ops_per_sec": 21453,
      "verify_ops_per_sec": 8382,
      "avg_sig_bytes": 64.0
    }
  ]
}

Classical baseline benchmark complete.

  [Elapsed: 15513 ms]
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
Falcon-512           L1      897    1281       752       147      4207     23505
Falcon-1024          L5     1793    2305      1462        51      2130     11618
ML-DSA-44            L2     1312    2560      2420     47476     14271     46532
ML-DSA-65            L3     1952    4032      3309     27961      8696     28893
SLH-DSA-SHA2-128f    L1       32      64     17088      1001        43       732
ECDSA secp256k1       -       65      32        71      2189      2181      2467
Ed25519               -       32      32        64     21138     21481      8309
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "comprehensive_signature_comparison",
  "timestamp": "2026-03-10T03:52:55Z",
  "bench_iters": 1000,
  "message_len": 256,
  "algorithms": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "pubkey_bytes": 897,
      "seckey_bytes": 1281,
      "sig_bytes": 752,
      "keygen_ops_per_sec": 147,
      "sign_ops_per_sec": 4207,
      "verify_ops_per_sec": 23505
    },
    {
      "name": "Falcon-1024",
      "nist_level": 5,
      "pubkey_bytes": 1793,
      "seckey_bytes": 2305,
      "sig_bytes": 1462,
      "keygen_ops_per_sec": 51,
      "sign_ops_per_sec": 2130,
      "verify_ops_per_sec": 11618
    },
    {
      "name": "ML-DSA-44",
      "nist_level": 2,
      "pubkey_bytes": 1312,
      "seckey_bytes": 2560,
      "sig_bytes": 2420,
      "keygen_ops_per_sec": 47476,
      "sign_ops_per_sec": 14271,
      "verify_ops_per_sec": 46532
    },
    {
      "name": "ML-DSA-65",
      "nist_level": 3,
      "pubkey_bytes": 1952,
      "seckey_bytes": 4032,
      "sig_bytes": 3309,
      "keygen_ops_per_sec": 27961,
      "sign_ops_per_sec": 8696,
      "verify_ops_per_sec": 28893
    },
    {
      "name": "SLH-DSA-SHA2-128f",
      "nist_level": 1,
      "pubkey_bytes": 32,
      "seckey_bytes": 64,
      "sig_bytes": 17088,
      "keygen_ops_per_sec": 1001,
      "sign_ops_per_sec": 43,
      "verify_ops_per_sec": 732
    },
    {
      "name": "ECDSA secp256k1",
      "nist_level": 0,
      "pubkey_bytes": 65,
      "seckey_bytes": 32,
      "sig_bytes": 71,
      "keygen_ops_per_sec": 2189,
      "sign_ops_per_sec": 2181,
      "verify_ops_per_sec": 2467
    },
    {
      "name": "Ed25519",
      "nist_level": 0,
      "pubkey_bytes": 32,
      "seckey_bytes": 32,
      "sig_bytes": 64,
      "keygen_ops_per_sec": 21138,
      "sign_ops_per_sec": 21481,
      "verify_ops_per_sec": 8309
    }
  ]
}

Comprehensive comparison complete.

  [Elapsed: 58246 ms]
```

</details>

---

