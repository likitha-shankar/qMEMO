# Phase B-3: ML-DSA-44 @ 500ms, MAX_BLOCK=2000 (5 runs)

**TPS: 3845.7 ± 10.5** (min 3828.4, max 3855.2)  
**Confirmation rate: 100.0 ± 0.0%** (min 100.0, max 100.0)  

| Run | TPS | Conf % |
|-----|-----|--------|
| 1 | 3855.16 | 100.0 |
| 2 | 3828.37 | 100.0 |
| 3 | 3848.40 | 100.0 |
| 4 | 3852.33 | 100.0 |
| 5 | 3844.24 | 100.0 |

## Setup
- SIG_SCHEME=4 (ML-DSA-44), block interval=500ms, MAX_BLOCK=2000
- 100K TXs, 10 farmers, k=16, 8 threads, Cascade Lake-R (qmemo1)
- 1ms ZMQ_RCVTIMEO on blockchain process (commit 9db48ae)
- Root cause of ML-DSA 500ms budget exhaustion: 10K TXs = 38MB pool→validator
  ZMQ transfer takes ~240ms, consuming entire 250ms budget. Fix: MAX_BLOCK=2000.
