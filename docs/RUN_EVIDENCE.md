# qMEMO Run Evidence (Canonical Examples)

This file records concrete canonical runs with log-level evidence that can be cited during poster/demo Q&A.

## Evidence Run A (Ed25519)

- Scheme: `s1` (Ed25519)
- Scale: `1,000,000` transactions
- Repeat: `r1`
- CSV: `benchmarks/results/hybrid_matrix_apr18_final/s1_tx1m_r1.csv`
- Log: `benchmarks/results/hybrid_matrix_apr18_final/s1_tx1m_r1.log`

### CSV Metrics (Ground Truth)

From `s1_tx1m_r1.csv`:

- `tx_submitted=1000000`
- `tx_confirmed=1000000`
- `tx_errors=0`
- `confirm_rate=100.0`
- `end_to_end_tps=8746.83`
- `total_e2e_time=114.327064131s`
- `blocks_created=104`

### Log Evidence (Excerpt)

```text
Submission complete: 1000000 submitted in 10.827064131s
Submission throughput: 92361.14 tx/sec
...
✅ All 1000000 transactions confirmed!
📊 Blocks received: 104, Elapsed: 103772.5 ms
...
Transactions submitted                       : 1000000
Submission errors                            : 0
Transactions confirmed                       : 1000000
Confirmation rate                            : 100.0%
Total benchmark time (A + B)                 : 114.327064131s
End-to-end throughput (TPS)                  : 8746.83 tx/sec
Average block interval                       : 997ms (target: 1000ms)
Min / Max interval                           : 892ms / 1128ms
```

### Consistency Check

CSV and log values agree on all key fields:

- confirmations (`1,000,000 / 1,000,000`)
- errors (`0`)
- confirmation rate (`100.0%`)
- end-to-end TPS (`8746.83`)
- total benchmark time (`114.327064131s`)

## Evidence Run B (Falcon-512)

- Scheme: `s2` (Falcon-512)
- Scale: `1,000,000` transactions
- Repeat: `r3`
- CSV: `benchmarks/results/hybrid_matrix_apr18_final/s2_tx1m_r3.csv`
- Log: `benchmarks/results/hybrid_matrix_apr18_final/s2_tx1m_r3.log`

### CSV Metrics (Ground Truth)

From `s2_tx1m_r3.csv`:

- `tx_submitted=1000000`
- `tx_confirmed=1000000`
- `tx_errors=0`
- `confirm_rate=100.0`
- `end_to_end_tps=8817.80`
- `total_e2e_time=113.406959185s`
- `blocks_created=103`

### Log Evidence (Excerpt)

```text
Submission complete: 1000000 submitted in 10.906959185s
Submission throughput: 91684.58 tx/sec
...
✅ All 1000000 transactions confirmed!
📊 Blocks received: 103, Elapsed: 102838.6 ms
...
Transactions submitted                       : 1000000
Submission errors                            : 0
Transactions confirmed                       : 1000000
Confirmation rate                            : 100.0%
Total benchmark time (A + B)                 : 113.406959185s
End-to-end throughput (TPS)                  : 8817.80 tx/sec
Average block interval                       : 998ms (target: 1000ms)
Min / Max interval                           : 906ms / 1127ms
```

### Consistency Check

CSV and log values agree on all key fields:

- confirmations (`1,000,000 / 1,000,000`)
- errors (`0`)
- confirmation rate (`100.0%`)
- end-to-end TPS (`8817.80`)
- total benchmark time (`113.406959185s`)

Both runs are part of the reviewer-safe canonical bundle (`hybrid_matrix_apr18_final`) used for aggregate reporting.
