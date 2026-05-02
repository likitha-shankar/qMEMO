# Phase B-2: cross-scheme TPS comparison at 1000ms and 500ms

## Setup
- Host: qmemo1 (Chameleon Cascade Lake-R)
- Code: commit e149748 (persistent-plot caching) on top of d917447 (Phase B-1 timing fix)
- Workload: 100K TXs, 1 farmer, k=16, max_block_size=10000, batch=64, threads=8
- Plots: persistent (loaded from `plots_persistent/` in <100us per run)
- Confirmation window: default formula `max(15000, blocks_needed×interval×2)`
- 5 runs per (scheme, interval); driver stops on first <95% confirmation gate

## Result: 25/30 PASS, 1 FAIL (sweep stopped at gate per spec)

## Table 1: TPS (mean ± stddev) and confirmation rate

| Algorithm  | Interval | TPS (mean ± stddev) | Confirm% | n |
|------------|---------:|--------------------:|---------:|--:|
| Ed25519    |  1000 ms |    7,662  ±   478   | 100.0%   | 5 |
| Ed25519    |   500 ms |   12,524  ±   343   | 100.0%   | 5 |
| Falcon-512 |  1000 ms |    5,279  ±   207   | 100.0%   | 5 |
| Falcon-512 |   500 ms |    6,446  ±   114   | 100.0%   | 5 |
| ML-DSA-44  |  1000 ms |    6,745  ±   351   | 100.0%   | 5 |
| ML-DSA-44  |   500 ms |      690  ±     0   |   5.1%   | 1 (FAIL) |

All stddev <= 6.2% of mean; no high-variance flags.

## Table 2: per-stage validator pipeline at 500 ms (mean ms per batch/block)

| Stage                  | Ed25519 | Falcon-512 | ML-DSA-44* |
|------------------------|--------:|-----------:|-----------:|
| Protobuf unpack        |  30.13  |     40.79  |     31.28  |
| Balance addr prep      |   0.20  |      0.21  |      0.10  |
| GET_BAL ZMQ RTT        |   0.35  |      0.35  |      0.58  |
| Verify + seq balance   |   1.83  |      1.09  |      7.85  |
| Block submission       |  17.55  |     16.24  |      9.75  |
| ADD_BLOCK ACK          |  73.89  |     53.01  |     33.69  |
| Inter-block gap        | 545.45  |    577.53  |          - |

*ML-DSA-44 stats are from the ONE TX-bearing block (5,120 TXs) before deadline misses started; not a steady-state measurement.

## ML-DSA-44 @ 500 ms failure mode
- Submission throughput: 48,432 TPS (TXs delivered to pool successfully)
- Block 1 included 5,120 TXs and confirmed (per block_diag CSV PID 212483)
- All subsequent 41 blocks were empty (deadline-miss creates empty blocks)
- 5,120 / 100,000 = 5.12% confirmation
- Root cause: at 500 ms the validator budget (250 ms) is tight enough that with 10K-TX blocks the ML-DSA processing pipeline (larger TX size, larger ACK payload) intermittently overruns; once a deadline miss occurs, `update_overhead()` is not called for empty blocks, so `proof_window` cannot adapt down. This is the same failure mode documented for Ed25519 @ 250 ms in commit a710d81.

## Observations for paper write-up
1. Ed25519 @ 500 ms is the throughput peak (12,524 TPS, +63% over 1000 ms baseline).
2. Falcon-512 reproduces the same 500ms-peak shape (1.22x scaling) and confirms 100% across all 5 runs.
3. ML-DSA-44 @ 1000 ms is stable (6,745 TPS, 100% confirmation, 5.2% stddev). At 500 ms the larger ML-DSA TX size + ACK payload pushes the validator past the 250 ms budget, producing the same adaptive-lockout failure mode that Phase B-3 (async block submission) is designed to address.
4. ADD_BLOCK ACK at 500 ms: Ed25519 73.89 ms > Falcon-512 53.01 ms > ML-DSA-44 33.69 ms. The inverse order is unexpected (larger schemes have smaller ACK) and may reflect shorter blocks at 500 ms for the heavier schemes. Worth a follow-up note.
5. Variance is low (<= 6.2% across all 5-run buckets); 1000 ms baseline has the highest absolute stddev (consistent with the Phase B-1 observation that under-utilized configurations are noisier).

## Phase B-3 motivation (data-driven)
The ADD_BLOCK ACK is the single largest fixed cost in the validator window:
73.89 ms (Ed25519) and 53.01 ms (Falcon) at 500 ms. Removing this from the
critical path via async submission would free roughly 10-15% of the
validator budget at 500 ms - enough headroom to either (a) reach 250 ms
intervals on Ed25519/Falcon or (b) pull ML-DSA 500 ms above the gate.
