# Blockchain Benchmark Raw Data

All runs on Intel Xeon Gold 6126 (Skylake-SP), Chameleon Cloud.
Config: k=16, batch=64, 8 threads/farmer unless noted.

## Run Index

| CSV Timestamp | Scheme | TX Count | e2e TPS | Notes |
|---------------|--------|----------|--------:|-------|
| 20260310_224656 | Stub baseline | 500 | 1,218 | No real sig verify |
| 20260310_224755 | Stub baseline | 1000 | 2,223 | No real sig verify |
| 20260323_003543 | Falcon-512 | 500 | 1,119 | Early run, fewer warmup |
| 20260323_003640 | ECDSA | 1000 | 265 | Early run, balance issue |
| 20260323_003747 | ECDSA | 1000 | 1,403 | Canonical ECDSA run |
| 20260323_003848 | ECDSA | 500 | 1,190 | Canonical ECDSA run |
| 20260328_215845 | Hybrid | — | — | Internal micro-benchmark only |
| 20260328_235826 | Hybrid | 500 | 944 | 12 farmers (4 per scheme) |
| 20260328_235941 | Hybrid | 1000 | 268 | Failed: farmer had 0 coins |
| 20260329_000120 | Hybrid | 1000 | 1,505 | Canonical hybrid (30 warmup) |

### Canonical Results (used in docs)

| Scheme | 500 TX e2e TPS | 1000 TX e2e TPS |
|--------|---------------:|----------------:|
| Stub baseline | 1,218 | 2,223 |
| ECDSA (real verify) | 1,190 | 1,403 |
| Falcon-512 | 1,223 | 2,572 |
| ML-DSA-44 | 1,234 | 1,533 |
| Hybrid (mixed) | 944 | 1,505 |

Note: Falcon-512 and ML-DSA-44 canonical CSVs were from earlier runs
stored separately on Chameleon. The numbers above come from
`docs/RESULTS.md` Section 10.

### File Types

- `benchmark_*.csv` — End-to-end metrics (TPS, confirmation, blocks)
- `internal_*.csv` — Micro-benchmarks (GPB serialize, ZMQ RTT, BLAKE3)
- `latency_*.csv` — Per-transaction latency data
- `report_*.html` — Visual HTML reports with charts
- `graph*.png` — Submission progress and block pipeline charts
- `graph*.csv` — Raw data for graph generation
