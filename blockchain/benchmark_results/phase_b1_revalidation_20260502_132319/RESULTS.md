# Phase B-1 Re-validation (Ed25519, post-amend)

## Purpose
Confirm that the amended commit `a710d81` (Patch 5 sched_initial + Patch 2
sched_min_budget formulas) reproduces the v6 sweep numbers within tolerance,
binding the paper claims to a specific committed code state.

## Setup
- Host: qmemo1 (Chameleon Cascade Lake-R)
- Commit at time of run: a710d81 + 7bbcd64 (benchmark.sh SIG_SCHEME passthrough)
- SIG_SCHEME=1 (Ed25519)
- Workload: 100,000 TXs, 1 farmer, k=16, max_block_size=10000, batch=64, threads=8
- Date: 2026-05-02 13:19:46 UTC

## Results vs v6 baseline

| Interval | v6 Baseline      | Re-validation     | Δ%      | Tolerance | Status |
|----------|------------------|-------------------|---------|-----------|--------|
| 1000ms   | 8,032 TPS, 100%  | 7,676.25 TPS, 100% | -4.43%  | ±5%      | PASS   |
| 500ms    | 11,962 TPS, 100% | 11,972.79 TPS, 100%| +0.09%  | ±5%      | PASS   |
| 250ms    | 5,454 TPS, 100%  | 5,427.32 TPS, 100% | -0.49%  | ±10%     | PASS   |
| 125ms    | 0%, gate fail    | 0%, gate fail      | matched | gate      | PASS   |

All four intervals match the v6 reference within stated tolerance.
Saturation point confirmed at 125ms.

## Constants applied (commit a710d81)

```
sched_initial_ms = min(300, block_time_ms × 0.6 − margin)  // floor-aligned
sched_min_budget = min(200, block_time_ms / 2 − margin)    // budget+margin <= block_time/2
sched_margin     = min(50,  block_time_ms / 10)            // proportional
proof_window floor = block_time_ms × 2/5                    // 40%
```

## Artifacts in this directory
- interval_<X>ms.log              - benchmark.sh stdout per interval (gitignored)
- interval_<X>ms_block_diag_<PID>.csv  - per-block timing (T3_last,T4,T5,...)
- interval_<X>ms_pool_fetches_<PID>.csv - per-fetch pool stats

The highest-PID *_block_diag_* CSV per interval is the canonical one for
that runs validator process. Lower-PID files are stale copies from the
shared benchmark working directory across earlier sweep iterations.
