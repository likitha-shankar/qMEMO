# qMEMO Benchmark Run -- 20260224_035823

**Platform:** arm64 / Darwin
**Date:** 2026-02-24T03:59:33Z
**liboqs:** 0.15.0

---

## Key Material Inspection (correctness audit)

```
    [0040] b1 76 20 24 ac f9                                 |.v $..|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (202.0 µs)
    (b) Verify corrupted sig  / correct msg:  FAIL ✓ (expected)
    (c) Verify correct sig   / corrupted msg: FAIL ✓ (expected)
────────────────────────────────────────────────────────────────
  [7/7] Ed25519  (Classical -- Edwards Curve -- fixed 64-byte sig)
────────────────────────────────────────────────────────────────
  Key Sizes:
    Public key:  32 bytes  (raw)
    Secret key:  32 bytes  [NOT DISPLAYED -- secret material]
    Keygen time: 42.0 µs
  Public Key (32 bytes):
    [0000] 8d d2 e9 66 82 e4 b3 8c  7c 93 06 7d 6f 6d 2c c0  |...f....|..}om,.|
    [0010] 90 a1 22 a9 24 e0 9b 2a  15 8a d3 9e 64 5b 2d c9  |..".$..*....d[-.|
  Signature (64 bytes):
    Sign time:  37.0 µs
    [0000] ea 82 00 41 bf 04 29 de  1a 4c 5c 66 07 12 7c d5  |...A..)..L\f..|.|
    [0010] 16 25 35 23 3b ed 83 72  74 4b b2 fa 61 bf 18 f3  |.%5#;..rtK..a...|
    [0020] f1 ed 27 4a 31 dc c0 98  f8 4b 1c 6c 86 7d 81 90  |..'J1....K.l.}..|
    [0030] f8 fa fc 24 68 8b 86 83  88 83 58 99 be 35 c6 03  |...$h.....X..5..|
  Correctness Checks:
    (a) Verify correct sig / correct msg:  PASS ✓  (89.0 µs)
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
  Timestamp:   2026-02-24T03:58:24Z
  [Elapsed: 259 ms]
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
       Signature produced: 655 bytes (max 752).
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
  │  Total time    :      0.231025 sec            │
  │  Ops/sec       :      43285.36                │
  │  Per operation :      0.023 ms              │
  │                       23.10 µs              │
  │  Est. cycles   :      80859  (@ 3.5 GHz)  │
  │  Signature     :   655 bytes               │
  │  Public key    :   897 bytes               │
  │  Secret key    :  1281 bytes               │
  └─────────────────────────────────────────────┘
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify",
  "timestamp": "2026-02-24T03:58:24Z",
  "algorithm": "Falcon-512",
  "iterations": 10000,
  "warmup": 100,
  "total_time_sec": 0.231025,
  "ops_per_sec": 43285.36,
  "ms_per_op": 0.023,
  "us_per_op": 23.10,
  "cycles_per_op": 80858.75,
  "signature_bytes": 655,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281
}

Benchmark complete.

  [Elapsed: 415 ms]
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
  │  Mean   (ops/sec)    :     43929.69                        │
  │  Std Dev             :      2086.82                        │
  │  CV                  :        4.75%                       │
  ├───────────────────────────────────────────────────────────┤
  │  Min    (ops/sec)    :     18907.17                        │
  │  P5                  :     41544.63                        │
  │  Median (P50)        :     44365.57                        │
  │  P95                 :     45724.74                        │
  │  P99                 :     45787.55                        │
  │  Max    (ops/sec)    :     45829.51                        │
  │  IQR                 :      1858.49                        │
  ├───────────────────────────────────────────────────────────┤
  │  Skewness            :      -5.6157  (left-skewed)       │
  │  Excess kurtosis     :      53.0674  (heavy tails)       │
  │  Jarque-Bera stat    :  122595.5670                      │
  │  Normality (α=0.05)  : FAIL (non-Gauss.)                      │
  │  Outliers (> 3σ)     :     14 / 1000                        │
  └───────────────────────────────────────────────────────────┘
  → Distribution departs from Gaussian (JB = 122595.57 > 5.991).
    Report: median and IQR.  Use non-parametric tests (Mann-Whitney U).
  → CV = 4.75% -- acceptable; consider closing background apps.
[7/7] JSON output:
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_verify_statistical",
  "timestamp": "2026-02-24T03:58:25Z",
  "algorithm": "Falcon-512",
  "trials": 1000,
  "iterations_per_trial": 100,
  "total_verifications": 100000,
  "signature_bytes": 654,
  "pubkey_bytes": 897,
  "seckey_bytes": 1281,
  "statistics": {
    "mean_ops_sec": 43929.69,
    "stddev_ops_sec": 2086.82,
    "cv_percent": 4.75,
    "min_ops_sec": 18907.17,
    "p5_ops_sec": 41544.63,
    "median_ops_sec": 44365.57,
    "p95_ops_sec": 45724.74,
    "p99_ops_sec": 45787.55,
    "max_ops_sec": 45829.51,
    "iqr_ops_sec": 1858.49,
    "skewness": -5.615739,
    "excess_kurtosis": 53.067386,
    "jarque_bera": 122595.567018,
    "normality_pass": false,
    "outliers_count": 14
  },
  "raw_data": [
    31181.79,
    32594.52,
    33167.50,
    33749.58,
    35523.98,
    34518.47,
    35536.60,
    37425.15,
    35663.34,
    37965.07,
    38595.14,
    38865.14,
    39200.31,
    40225.26,
    39323.63,
    39920.16,
    40257.65,
    41425.02,
    42283.30,
    41254.13,
    42265.43,
    42680.32,
    43782.84,
    42158.52,
    42498.94,
    43821.21,
    43649.06,
    44091.71,
    44984.26,
    44543.43,
    45146.73,
    43554.01,
    44483.99,
    45004.50,
    45126.35,
    44863.17,
    44169.61,
    45289.86,
    42123.00,
    45167.12,
    44483.99,
    44365.57,
    45228.40,
    45289.86,
    45146.73,
    45766.59,
    45558.09,
    44014.08,
    44984.26,
    45351.47,
    44404.97,
    44326.24,
    44782.80,
    43271.31,
    44964.03,
    45495.91,
    43365.13,
    45766.59,
    44883.30,
    44702.73,
    44903.46,
    44903.46,
    45413.26,
    44169.61,
    44208.66,
    44622.94,
    45392.65,
    44682.75,
    43782.84,
    43610.99,
    44365.57,
    42247.57,
    44843.05,
    42881.65,
    44964.03,
    44984.26,
    45682.96,
    44189.13,
    44583.15,
    41649.31,
    44943.82,
    45372.05,
    44802.87,
    45745.65,
    44863.17,
    45724.74,
    44169.61,
    40387.72,
    45106.00,
    44150.11,
    45641.26,
    45766.59,
    44642.86,
    43215.21,
    43029.26,
    43029.26,
    44682.75,
    44843.05,
    45766.59,
    44286.98,
    44822.95,
    45662.10,
    44923.63,
    44483.99,
    45599.64,
    45599.64,
    44964.03,
    44802.87,
    42372.88,
    45187.53,
    43917.44,
    41736.23,
    41528.24,
    44622.94,
    45641.26,
    45045.05,
    45433.89,
    44286.98,
    43917.44,
    43308.79,
    41305.25,
    36416.61,
    20863.76,
    21159.54,
    18907.17,
    42753.31,
    43103.45,
    43383.95,
    45289.86,
    45065.34,
    43010.75,
    42826.55,
    42589.44,
    41186.16,
    40849.67,
    44404.97,
    45620.44,
    42571.31,
    43365.13,
    45475.22,
    44483.99,
    34794.71,
    44208.66,
    45269.35,
    43140.64,
    44014.08,
    45065.34,
    45126.35,
    45228.40,
    43233.90,
    45641.26,
    45516.61,
    43898.16,
    44822.95,
    44404.97,
    41771.09,
    44228.22,
    42900.04,
    43516.10,
    41034.06,
    43159.26,
    45187.53,
    43029.26,
    40716.61,
    42354.93,
    44228.22,
    42462.85,
    43687.20,
    45167.12,
    45228.40,
    45289.86,
    43610.99,
    43687.20,
    44563.28,
    44444.44,
    43215.21,
    45495.91,
    44782.80,
    44662.80,
    42087.54,
    41701.42,
    43159.26,
    43290.04,
    45745.65,
    42571.31,
    44863.17,
    45662.10,
    45454.55,
    43421.62,
    44903.46,
    45146.73,
    45724.74,
    45558.09,
    45330.92,
    45065.34,
    45578.85,
    43233.90,
    44464.21,
    45537.34,
    45433.89,
    44984.26,
    43572.98,
    45495.91,
    44247.79,
    43859.65,
    43196.54,
    42069.84,
    43290.04,
    45558.09,
    44822.95,
    43782.84,
    43084.88,
    43706.29,
    43706.29,
    42480.88,
    45682.96,
    45724.74,
    45829.51,
    45620.44,
    42973.79,
    43122.04,
    43159.26,
    43252.60,
    42826.55,
    43010.75,
    43516.10,
    43252.60,
    43308.79,
    43047.78,
    43066.32,
    43802.01,
    43802.01,
    43763.68,
    43725.40,
    42789.90,
    43215.21,
    43196.54,
    43159.26,
    43290.04,
    43763.68,
    43668.12,
    43535.05,
    41963.91,
    44563.28,
    42390.84,
    43649.06,
    43365.13,
    41911.15,
    41999.16,
    44326.24,
    42589.44,
    43859.65,
    44326.24,
    43725.40,
    43290.04,
    43554.01,
    42211.90,
    43440.49,
    43421.62,
    42337.00,
    45745.65,
    45724.74,
    45703.84,
    45641.26,
    43383.95,
    43706.29,
    43687.20,
    43290.04,
    43365.13,
    43572.98,
    42390.84,
    43159.26,
    42716.79,
    43859.65,
    43177.89,
    43725.40,
    43725.40,
    42789.90,
    42716.79,
    43554.01,
    43066.32,
    41407.87,
    43668.12,
    43196.54,
    44923.63,
    43459.37,
    45167.12,
    45310.38,
    43421.62,
    45269.35,
    42229.73,
    42498.94,
    40799.67,
    45330.92,
    44169.61,
    44662.80,
    43572.98,
    45167.12,
    45289.86,
    44189.13,
    45004.50,
    44662.80,
    45248.87,
    42936.88,
    44883.30,
    41718.82,
    42426.81,
    40849.67,
    45703.84,
    44863.17,
    44306.60,
    43103.45,
    44091.71,
    45787.55,
    44424.70,
    44603.03,
    44903.46,
    45537.34,
    45516.61,
    44762.76,
    45662.10,
    45724.74,
    45745.65,
    45745.65,
    45745.65,
    45126.35,
    43763.68,
    41050.90,
    43252.60,
    44802.87,
    43591.98,
    42808.22,
    44943.82,
    41841.00,
    41545.49,
    43725.40,
    45578.85,
    44111.16,
    44603.03,
    44483.99,
    44903.46,
    44563.28,
    44052.86,
    44111.16,
    44208.66,
    45433.89,
    45662.10,
    44483.99,
    45269.35,
    42808.22,
    44662.80,
    43840.42,
    43029.26,
    41562.76,
    43956.04,
    45537.34,
    43177.89,
    44822.95,
    45310.38,
    44964.03,
    44682.75,
    45787.55,
    44782.80,
    44702.73,
    45269.35,
    44444.44,
    44762.76,
    45599.64,
    42992.26,
    44385.26,
    45766.59,
    45126.35,
    43821.21,
    43421.62,
    44111.16,
    44702.73,
    45766.59,
    45004.50,
    44228.22,
    43215.21,
    44782.80,
    45004.50,
    43956.04,
    43630.02,
    44662.80,
    44543.43,
    44943.82,
    45766.59,
    43898.16,
    45766.59,
    45516.61,
    44483.99,
    44622.94,
    44306.60,
    41597.34,
    44345.90,
    42408.82,
    42517.01,
    40096.23,
    42571.31,
    42553.19,
    42571.31,
    42553.19,
    41511.00,
    38211.69,
    42517.01,
    42643.92,
    45167.12,
    39856.52,
    43047.78,
    41356.49,
    41631.97,
    42625.75,
    45330.92,
    45641.26,
    45372.05,
    43591.98,
    44091.71,
    45599.64,
    43668.12,
    44503.78,
    41963.91,
    43421.62,
    45207.96,
    45641.26,
    43554.01,
    41753.65,
    43725.40,
    44603.03,
    44523.60,
    45475.22,
    44682.75,
    45745.65,
    44583.15,
    45106.00,
    44306.60,
    45724.74,
    45537.34,
    44762.76,
    42498.94,
    42808.22,
    41545.49,
    44404.97,
    44722.72,
    43327.56,
    44150.11,
    45085.66,
    45475.22,
    43459.37,
    42535.09,
    45004.50,
    44722.72,
    42408.82,
    44622.94,
    45024.76,
    43084.88,
    43668.12,
    44702.73,
    45578.85,
    43308.79,
    44091.71,
    45228.40,
    45004.50,
    42789.90,
    43802.01,
    44863.17,
    44464.21,
    43610.99,
    43196.54,
    45745.65,
    42123.00,
    44091.71,
    45045.05,
    43878.89,
    42844.90,
    44464.21,
    45289.86,
    44208.66,
    45682.96,
    44622.94,
    44923.63,
    43744.53,
    43591.98,
    44543.43,
    45126.35,
    44169.61,
    42936.88,
    44111.16,
    42735.04,
    44404.97,
    44883.30,
    44943.82,
    41631.97,
    43103.45,
    45269.35,
    45146.73,
    43572.98,
    45146.73,
    45558.09,
    44782.80,
    45289.86,
    44603.03,
    44782.80,
    45454.55,
    45641.26,
    45187.53,
    45620.44,
    44365.57,
    43140.64,
    41823.50,
    42176.30,
    42643.92,
    44444.44,
    45310.38,
    43383.95,
    43140.64,
    45641.26,
    45808.52,
    45065.34,
    45745.65,
    44464.21,
    45766.59,
    41841.00,
    44464.21,
    45433.89,
    45703.84,
    45558.09,
    45045.05,
    44583.15,
    44365.57,
    42662.12,
    42283.30,
    42444.82,
    43975.37,
    44822.95,
    44802.87,
    44583.15,
    45703.84,
    45766.59,
    44345.90,
    42955.33,
    44444.44,
    44964.03,
    45085.66,
    42034.47,
    43936.73,
    43802.01,
    44365.57,
    44169.61,
    45703.84,
    43994.72,
    42992.26,
    43066.32,
    44345.90,
    45413.26,
    42771.60,
    44404.97,
    42354.93,
    45146.73,
    42301.18,
    44404.97,
    45248.87,
    44943.82,
    45289.86,
    45289.86,
    45228.40,
    44033.47,
    45745.65,
    44782.80,
    44943.82,
    44033.47,
    43554.01,
    45085.66,
    44603.03,
    42863.27,
    44603.03,
    45703.84,
    45372.05,
    43744.53,
    42176.30,
    43047.78,
    41597.34,
    45703.84,
    45766.59,
    45269.35,
    44267.37,
    43878.89,
    44782.80,
    43383.95,
    45766.59,
    44072.28,
    45187.53,
    44883.30,
    45745.65,
    43365.13,
    44563.28,
    42992.26,
    44286.98,
    45045.05,
    45248.87,
    45766.59,
    43421.62,
    45065.34,
    42444.82,
    44189.13,
    44189.13,
    44345.90,
    42735.04,
    45146.73,
    45310.38,
    42535.09,
    44843.05,
    45289.86,
    44843.05,
    44464.21,
    44923.63,
    44923.63,
    45065.34,
    45599.64,
    45433.89,
    44208.66,
    45724.74,
    45808.52,
    43478.26,
    41631.97,
    44883.30,
    45662.10,
    45703.84,
    43936.73,
    41823.50,
    44130.63,
    45537.34,
    43103.45,
    43233.90,
    45269.35,
    45207.96,
    44111.16,
    45829.51,
    44483.99,
    44802.87,
    43936.73,
    43687.20,
    44543.43,
    45766.59,
    43140.64,
    44762.76,
    44228.22,
    42462.85,
    42662.12,
    45207.96,
    43687.20,
    43084.88,
    43327.56,
    43649.06,
    43440.49,
    43898.16,
    43610.99,
    43591.98,
    43516.10,
    43687.20,
    43782.84,
    43936.73,
    42571.31,
    45703.84,
    45599.64,
    45641.26,
    45475.22,
    42863.27,
    43271.31,
    41390.73,
    45703.84,
    43936.73,
    43802.01,
    43103.45,
    43802.01,
    43668.12,
    43308.79,
    42607.58,
    45413.26,
    45662.10,
    45045.05,
    44169.61,
    42735.04,
    42716.79,
    43668.12,
    43271.31,
    44622.94,
    44111.16,
    44543.43,
    45454.55,
    45248.87,
    45207.96,
    43591.98,
    43271.31,
    42498.94,
    44091.71,
    45745.65,
    45620.44,
    45787.55,
    45065.34,
    43687.20,
    43365.13,
    43402.78,
    43840.42,
    43402.78,
    43478.26,
    43649.06,
    43591.98,
    43252.60,
    43440.49,
    43668.12,
    42319.09,
    43383.95,
    42016.81,
    43840.42,
    41339.40,
    42789.90,
    43290.04,
    43159.26,
    42753.31,
    41946.31,
    41390.73,
    42535.09,
    44345.90,
    42480.88,
    40535.06,
    45269.35,
    43084.88,
    41963.91,
    41858.52,
    44404.97,
    45167.12,
    44603.03,
    43936.73,
    44822.95,
    44483.99,
    44208.66,
    44111.16,
    44523.60,
    45766.59,
    44385.26,
    45106.00,
    43572.98,
    42069.84,
    41186.16,
    44286.98,
    42844.90,
    44943.82,
    42247.57,
    44863.17,
    45745.65,
    45766.59,
    44563.28,
    44267.37,
    45766.59,
    42680.32,
    44782.80,
    43421.62,
    45454.55,
    45537.34,
    43782.84,
    45269.35,
    44189.13,
    45787.55,
    45310.38,
    45682.96,
    43066.32,
    42589.44,
    41684.04,
    44822.95,
    42826.55,
    42283.30,
    44802.87,
    45682.96,
    44345.90,
    42863.27,
    44483.99,
    43215.21,
    44583.15,
    43478.26,
    45065.34,
    45495.91,
    44464.21,
    43010.75,
    44843.05,
    45187.53,
    45106.00,
    45351.47,
    45207.96,
    42753.31,
    42844.90,
    41858.52,
    43687.20,
    42301.18,
    44583.15,
    45085.66,
    45516.61,
    43459.37,
    42735.04,
    43290.04,
    45641.26,
    44722.72,
    45516.61,
    43687.20,
    44802.87,
    42698.55,
    44091.71,
    44883.30,
    45620.44,
    45724.74,
    44843.05,
    45558.09,
    44014.08,
    42918.45,
    45475.22,
    45330.92,
    45045.05,
    45578.85,
    45724.74,
    43271.31,
    42052.14,
    44286.98,
    44903.46,
    44802.87,
    45641.26,
    44326.24,
    44843.05,
    45228.40,
    45330.92,
    44662.80,
    45808.52,
    44802.87,
    44563.28,
    45372.05,
    43936.73,
    43459.37,
    44622.94,
    44802.87,
    44742.73,
    45745.65,
    44189.13,
    44543.43,
    44762.76,
    43572.98,
    44464.21,
    45024.76,
    45662.10,
    44444.44,
    45065.34,
    44702.73,
    45433.89,
    44503.78,
    43936.73,
    43159.26,
    43497.17,
    44923.63,
    45024.76,
    44923.63,
    43196.54,
    44208.66,
    44742.73,
    45829.51,
    43440.49,
    45167.12,
    44072.28,
    43687.20,
    43478.26,
    45662.10,
    45537.34,
    45310.38,
    43610.99,
    45766.59,
    45766.59,
    44843.05,
    45372.05,
    44722.72,
    45766.59,
    42826.55,
    45454.55,
    44385.26,
    44543.43,
    45085.66,
    45207.96,
    45454.55,
    42936.88,
    41169.21,
    41101.52,
    44603.03,
    45045.05,
    42625.75,
    42016.81,
    43898.16,
    42283.30,
    41999.16,
    43140.64,
    45413.26,
    45372.05,
    44662.80,
    44169.61,
    41946.31,
    42087.54,
    44052.86,
    41493.78,
    45454.55,
    41220.12,
    43029.26,
    43346.34,
    41580.04,
    42372.88,
    43591.98,
    45516.61,
    43535.05,
    41928.72,
    42229.73,
    43802.01,
    43459.37,
    45024.76,
    44863.17,
    45310.38,
    45289.86,
    43630.02,
    44722.72,
    45392.65,
    45289.86,
    44682.75,
    44843.05,
    45207.96,
    45787.55,
    43706.29,
    43610.99,
    42771.60,
    44843.05,
    44923.63,
    44883.30,
    44802.87,
    45413.26,
    45146.73,
    44208.66,
    45413.26,
    44802.87,
    45289.86,
    44483.99,
    45787.55,
    44052.86,
    45085.66,
    43649.06,
    43975.37,
    45289.86,
    45187.53,
    45682.96,
    44130.63,
    44883.30,
    43459.37,
    40617.38,
    45558.09,
    45106.00,
    45413.26,
    45004.50,
    44984.26,
    44682.75,
    44742.73,
    45085.66,
    45578.85,
    44722.72,
    44603.03,
    45024.76,
    44843.05,
    44662.80,
    45745.65,
    43725.40,
    44742.73,
    45413.26,
    44622.94,
    44843.05,
    44267.37,
    41806.02,
    43706.29,
    44762.76,
    42283.30,
    44622.94,
    45330.92,
    44365.57,
    42176.30,
    44543.43,
    44622.94,
    44603.03,
    45372.05,
    44762.76,
    44822.95,
    43994.72,
    43649.06,
    45167.12,
    43898.16,
    44424.70,
    45475.22,
    43159.26,
    45787.55,
    43782.84,
    43591.98,
    44306.60
  ]
}

Statistical benchmark complete.

  [Elapsed: 2483 ms]
```

</details>

---

## Comparison Benchmark (Falcon-512 vs ML-DSA-44)

```
  Falcon-512 vs ML-DSA-44 Comparison  (qMEMO / IIT Chicago)
================================================================
─── Falcon-512 ───
  [keygen] warm-up ... benchmarking 100 trials ... 212.5 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 7052.7 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 44321.7 ops/sec
─── ML-DSA-44 ───
  [keygen] warm-up ... benchmarking 100 trials ... 35906.6 ops/sec
  [sign]   warm-up ... benchmarking 1000 trials ... 14936.1 ops/sec
  [verify] warm-up ... benchmarking 10000 trials ... 37813.2 ops/sec
================================================================
  HEAD-TO-HEAD COMPARISON
================================================================
  Metric                      Falcon-512       ML-DSA-44  Winner
  ──────────────────────  ──────────────  ──────────────  ──────────
  Keygen throughput             212.5 /s      35906.6 /s    faster ►
  Sign throughput              7052.7 /s      14936.1 /s    faster ►
  Verify throughput           44321.7 /s      37813.2 /s  ◄ faster
  Keygen latency              4706.1 µs         27.9 µs     faster ►
  Sign latency                 141.8 µs         67.0 µs     faster ►
  Verify latency                22.6 µs         26.4 µs   ◄ faster
  Public key size                897 B           1312 B     ◄ smaller
  Secret key size               1281 B           2560 B     ◄ smaller
  Signature size                 657 B           2420 B     ◄ smaller
  On-chain tx overhead          1554 B           3732 B     ◄ smaller
================================================================
  BLOCKCHAIN IMPACT ANALYSIS
================================================================
  Scenario: 4000 transactions per block (single-threaded verification)
  Falcon-512 block verify time :     90.2 ms
  ML-DSA-44  block verify time :    105.8 ms
  Speedup (Falcon / ML-DSA)    :     1.17x
  Falcon-512 block sig data    :   6070.3 KB  (1554 B/tx)
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
  "timestamp": "2026-02-24T03:58:27Z",
  "config": {
    "keygen_trials": 100,
    "sign_trials": 1000,
    "verify_trials": 10000,
    "message_len": 256
  },
  "algorithms": {
    "Falcon-512": {
      "keygen_ops_sec": 212.49,
      "keygen_us_op": 4706.15,
      "sign_ops_sec": 7052.68,
      "sign_us_op": 141.79,
      "verify_ops_sec": 44321.72,
      "verify_us_op": 22.56,
      "pubkey_bytes": 897,
      "privkey_bytes": 1281,
      "signature_bytes": 657,
      "total_tx_overhead": 1554
    },
    "ML-DSA-44": {
      "keygen_ops_sec": 35906.64,
      "keygen_us_op": 27.85,
      "sign_ops_sec": 14936.07,
      "sign_us_op": 66.95,
      "verify_ops_sec": 37813.19,
      "verify_us_op": 26.45,
      "pubkey_bytes": 1312,
      "privkey_bytes": 2560,
      "signature_bytes": 2420,
      "total_tx_overhead": 3732
    }
  },
  "comparison": {
    "verify_speedup_falcon": 1.1721,
    "sign_speedup_falcon": 0.4722,
    "keygen_speedup_falcon": 0.0059,
    "signature_size_ratio": 0.2715,
    "pubkey_size_ratio": 0.6837,
    "total_tx_overhead_falcon": 1554,
    "total_tx_overhead_dilithium": 3732,
    "tx_overhead_ratio": 0.4164
  }
}

Comparison benchmark complete.

  [Elapsed: 1477 ms]
```

</details>

---

## Multicore Verification (1/2/4/6/8/10 cores)

```
================================================================
  Falcon-512 Multicore Verification Benchmark  (qMEMO / IIT Chicago)
================================================================
Generating keypair and signing message ...
OK. Signature length: 657 bytes.
Cores  |  Throughput (ops/sec)  |  Speedup  |  Efficiency
-------|------------------------|-----------|------------
   1   |               34153     |    1.00   |  100.0%
   2   |               80502     |    2.36   |  117.9%
   4   |              170140     |    4.98   |  124.5%
   6   |              234110     |    6.85   |  114.2%
   8   |              249875     |    7.32   |   91.5%
  10   |              382175     |   11.19   |  111.9%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_verify",
  "timestamp": "2026-02-24T03:58:29Z",
  "algorithm": "Falcon-512",
  "verif_per_thread": 1000,
  "warmup_per_thread": 100,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [34153, 80502, 170140, 234110, 249875, 382175],
  "speedup": [1.00, 2.36, 4.98, 6.85, 7.32, 11.19],
  "efficiency_pct": [100.0, 117.9, 124.5, 114.2, 91.5, 111.9]
}

Multicore benchmark complete.

  [Elapsed: 378 ms]
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
Concurrent (4 workers): 0.711 ms total, 0.0071 ms avg, 140647 ops/sec
Sequential (baseline):   2.528 ms total, 0.0253 ms avg, 39557 ops/sec
Concurrent yields 71.9% lower latency (better parallelism)
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_concurrent_verify",
  "timestamp": "2026-02-24T03:58:29Z",
  "algorithm": "Falcon-512",
  "concurrent": {
    "signatures": 100,
    "worker_threads": 4,
    "total_time_ms": 0.7110,
    "avg_latency_ms": 0.0071,
    "throughput": 140647
  },
  "sequential": {
    "total_time_ms": 2.5280,
    "avg_latency_ms": 0.0253,
    "throughput": 39557
  },
  "analysis": "Concurrent yields 71.9% lower latency (better parallelism)"
}

Concurrent benchmark complete.

  [Elapsed: 701 ms]
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
  Signature sizes (n=100, max-buf=656 B):
    min=650   max=661   avg=655.1  std=2.2 bytes
Running sequential signing (baseline) ...
  Signature sizes (n=100, max-buf=652 B):
    min=650   max=661   avg=655.5  std=2.2 bytes
================================================================
  RESULTS
================================================================
  Concurrent (4 workers):   5.913 ms total |  0.0591 ms/op |    16912 ops/sec
  Sequential (baseline):   20.169 ms total |  0.2017 ms/op |     4958 ops/sec
  Speedup:  3.41x
  Concurrent yields 3.4x speedup (70.7% faster than sequential)
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
  "timestamp": "2026-02-24T03:58:30Z",
  "algorithm": "Falcon-512",
  "config": {
    "signing_tasks": 100,
    "worker_threads": 4,
    "message_len": 256,
    "sig_max_bytes": 752
  },
  "concurrent": {
    "total_time_ms": 5.9130,
    "avg_latency_ms": 0.0591,
    "throughput_ops_sec": 16912
  },
  "sequential": {
    "total_time_ms": 20.1690,
    "avg_latency_ms": 0.2017,
    "throughput_ops_sec": 4958
  },
  "speedup": 3.4110,
  "sig_size_stats": {
    "min_bytes": 650,
    "max_bytes": 661,
    "avg_bytes": 655.5,
    "spec_max_bytes": 752
  },
  "analysis": "Concurrent yields 3.4x speedup (70.7% faster than sequential)"
}

Concurrent signing benchmark complete.

  [Elapsed: 217 ms]
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
   1   |                6122     |                6122     |    1.00   |  100.0%
   2   |               13779     |                6890     |    2.25   |  112.5%
   4   |               26889     |                6722     |    4.39   |  109.8%
   6   |               35318     |                5886     |    5.77   |   96.1%
   8   |               42435     |                5304     |    6.93   |   86.6%
  10   |               52702     |                5270     |    8.61   |   86.1%
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon512_multicore_sign",
  "timestamp": "2026-02-24T03:58:30Z",
  "algorithm": "Falcon-512",
  "sign_per_thread": 500,
  "warmup_per_thread": 50,
  "cores": [1, 2, 4, 6, 8, 10],
  "ops_per_sec": [6122, 13779, 26889, 35318, 42435, 52702],
  "speedup": [1.00, 2.25, 4.39, 5.77, 6.93, 8.61],
  "efficiency_pct": [100.0, 112.5, 109.8, 96.1, 86.6, 86.1]
}

Multicore signing benchmark complete.

  [Elapsed: 746 ms]
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
Falcon-512                L1      666    647    663  655.1     2.2   654  655  657   659   660
Falcon-padded-512         L1      666    666    666  666.0     0.0   666  666  666   666   666
Falcon-1024               L5     1280   1260   1282  1270.7     3.1  1269 1271 1273  1276  1278
Falcon-padded-1024        L5     1280   1280   1280  1280.0     0.0  1280 1280 1280  1280  1280
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "falcon_signature_size_distribution",
  "timestamp": "2026-02-24T03:58:31Z",
  "num_signatures": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "spec_max_bytes": 666,
      "min": 647,
      "max": 663,
      "mean": 655.07,
      "std_dev": 2.20,
      "p25": 654,
      "p50": 655,
      "p75": 657,
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
      "max": 1282,
      "mean": 1270.71,
      "std_dev": 3.11,
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

  [Elapsed: 8761 ms]
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
ECDSA secp256k1             5105           5146             5503              71.0
Ed25519                    34512          34486            12471              64.0
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "classical_signature_baselines",
  "timestamp": "2026-02-24T03:58:40Z",
  "bench_iters": 10000,
  "message_len": 256,
  "schemes": [
    {
      "name": "ECDSA secp256k1",
      "keygen_ops_per_sec": 5105,
      "sign_ops_per_sec": 5146,
      "verify_ops_per_sec": 5503,
      "avg_sig_bytes": 71.0
    },
    {
      "name": "Ed25519",
      "keygen_ops_per_sec": 34512,
      "sign_ops_per_sec": 34486,
      "verify_ops_per_sec": 12471,
      "avg_sig_bytes": 64.0
    }
  ]
}

Classical baseline benchmark complete.

  [Elapsed: 7370 ms]
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
Falcon-512           L1      897    1281       752       212      7014     43787
Falcon-1024          L5     1793    2305      1462        65      3502     22102
ML-DSA-44            L2     1312    2560      2420     36065     15307     37343
ML-DSA-65            L3     1952    4032      3309     20711      9741     22340
SLH-DSA-SHA2-128f    L1       32      64     17088      1214        52       832
ECDSA secp256k1       -       65      32        71      5108      5153      5621
Ed25519               -       32      32        64     34711     34335     12491
```

<details><summary>JSON output</summary>

```json
{
  "test_name": "comprehensive_signature_comparison",
  "timestamp": "2026-02-24T03:58:47Z",
  "bench_iters": 1000,
  "message_len": 256,
  "algorithms": [
    {
      "name": "Falcon-512",
      "nist_level": 1,
      "pubkey_bytes": 897,
      "seckey_bytes": 1281,
      "sig_bytes": 752,
      "keygen_ops_per_sec": 212,
      "sign_ops_per_sec": 7014,
      "verify_ops_per_sec": 43787
    },
    {
      "name": "Falcon-1024",
      "nist_level": 5,
      "pubkey_bytes": 1793,
      "seckey_bytes": 2305,
      "sig_bytes": 1462,
      "keygen_ops_per_sec": 65,
      "sign_ops_per_sec": 3502,
      "verify_ops_per_sec": 22102
    },
    {
      "name": "ML-DSA-44",
      "nist_level": 2,
      "pubkey_bytes": 1312,
      "seckey_bytes": 2560,
      "sig_bytes": 2420,
      "keygen_ops_per_sec": 36065,
      "sign_ops_per_sec": 15307,
      "verify_ops_per_sec": 37343
    },
    {
      "name": "ML-DSA-65",
      "nist_level": 3,
      "pubkey_bytes": 1952,
      "seckey_bytes": 4032,
      "sig_bytes": 3309,
      "keygen_ops_per_sec": 20711,
      "sign_ops_per_sec": 9741,
      "verify_ops_per_sec": 22340
    },
    {
      "name": "SLH-DSA-SHA2-128f",
      "nist_level": 1,
      "pubkey_bytes": 32,
      "seckey_bytes": 64,
      "sig_bytes": 17088,
      "keygen_ops_per_sec": 1214,
      "sign_ops_per_sec": 52,
      "verify_ops_per_sec": 832
    },
    {
      "name": "ECDSA secp256k1",
      "nist_level": 0,
      "pubkey_bytes": 65,
      "seckey_bytes": 32,
      "sig_bytes": 71,
      "keygen_ops_per_sec": 5108,
      "sign_ops_per_sec": 5153,
      "verify_ops_per_sec": 5621
    },
    {
      "name": "Ed25519",
      "nist_level": 0,
      "pubkey_bytes": 32,
      "seckey_bytes": 32,
      "sig_bytes": 64,
      "keygen_ops_per_sec": 34711,
      "sign_ops_per_sec": 34335,
      "verify_ops_per_sec": 12491
    }
  ]
}

Comprehensive comparison complete.

  [Elapsed: 45621 ms]
```

</details>

---

