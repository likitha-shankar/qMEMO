This directory contains the canonical final artifact set for the Apr 18 cloud repeat matrix.

Selection rules:
- Keep exactly 18 CSV files and 18 log files (3 repeats x 2 scales x 3 schemes).
- Exclude intermediate retry logs from the final bundle.
- Use the successful retry output for ML-DSA 1M repeat #3.

Important mapping:
- Canonical `s4_tx1m_r3.log` in this folder is copied from source `s4_tx1m_r3_retry2.log`.
- Source path for full raw history: `benchmarks/results/hybrid_matrix_apr18/`.

Integrity expectations for all final CSVs:
- `tx_confirmed` equals target transaction count.
- `confirm_rate` is `100.0`.
- `tx_errors` is `0`.
- `end_to_end_tps` is greater than `0`.
