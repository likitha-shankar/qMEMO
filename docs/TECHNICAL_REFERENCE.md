# qMEMO Technical Reference

This is a code-aligned technical reference for the current implementation in `blockchain/` and benchmark outputs tracked in `docs/RESULTS.md`.

## Defense-Ready Summary (What to say first)

- Crypto mode is compile-time selectable (`SIG_SCHEME=1/2/3/4`) with typed verification support.
- Transactions carry `sig_type`, `sig_len`, and `pubkey_len`, enabling mixed-scheme-safe verification.
- Validation pipeline is deadline-aware: winner validator fetches, verifies, and may send partial blocks if needed.
- Confirmation semantics are strict: only blockchain acceptance creates final confirmation; pool state follows blockchain events.
- Performance interpretation: end-to-end TPS depends on serialization + queueing + coordination, not just raw signature speed.

## Appendix Use

- **Defense/Q&A:** focus on Sections 1, 3, 4, and 5.
- **Paper appendix:** include Sections 2, 6, 7, and 8 for implementation precision and reproducibility context.

## 1) Build-Time Crypto Modes

Set `SIG_SCHEME` during build:

- `1` -> Ed25519
- `2` -> Falcon-512
- `3` -> Hybrid (runtime typed verify paths available)
- `4` -> ML-DSA-44

Core abstraction: `blockchain/include/crypto_backend.h`

Universal crypto buffer maxima:

- `CRYPTO_PUBKEY_MAX = 1312`
- `CRYPTO_SECKEY_MAX = 2560`
- `CRYPTO_SIG_MAX = 2420`

These maxima allow cross-scheme decode safety for variable-size keys/signatures.

## 2) Core Data Structures

### Transaction (`blockchain/include/transaction.h`)

Economic core:

- `nonce` (u64)
- `expiry_block` (u32)
- `source_address[20]`
- `dest_address[20]`
- `value` (u64)
- `fee` (u32)

Crypto payload:

- `signature[CRYPTO_SIG_MAX]`, `sig_len`
- `public_key[CRYPTO_PUBKEY_MAX]`, `pubkey_len`
- `sig_type`

Transaction hash:

- `TX_HASH_SIZE = 28`
- hash covers economic core fields only (not signature/public key)

### Wallet (`blockchain/include/wallet.h`)

- Address model: `hash160(pubkey)` -> 20-byte address.
- Tracks active scheme (`sig_type`) and key material.
- Supports Ed25519 and PQC key/sign paths via unified APIs.

### Blockchain Ledger (`blockchain/include/blockchain.h`)

- In-memory chain with cached last hash.
- Ledger is flat array (`address`, `balance`, `nonce`) with linear lookup.
- Suitable for current benchmark scale; replace with hash map for large-account production workloads.

## 3) Message Protocol Highlights

### Blockchain REP commands

- `ADD_BLOCK_PB`
- `GET_LAST_HASH`
- `GET_HEIGHT`
- `GET_BALANCE`
- `GET_BALANCES_BATCH`
- `GET_NONCE`
- `FUND_WALLET`

### Blockchain PUB events

- `NEW_BLOCK:<height>:<tx_count>:<hash>` (text)
- `CONFIRM_BLOCK:<height><count><hashes>` (binary payload)

### Pool REP commands

- `SUBMIT_BATCH_PB`
- `GET_FOR_WINNER:max:height`
- `GET_PENDING_NONCE`
- `GET_STATUS`

Pool response for winner fetch:

- Prefix `TXPB` + protobuf `TransactionBatch`.

## 4) Validation Pipeline (Winner Validator)

When winner receives `WINNER`:

1. Query blockchain for latest context (`GET_LAST_HASH`, `GET_HEIGHT`).
2. Compute deadline-aware fetch cap.
3. Fetch pending batch from pool.
4. Verify each candidate transaction:
   - sender address/public key consistency,
   - signature verification (typed by `sig_type`),
   - nonce ordering and balance admissibility.
5. Build coinbase + accepted user tx set.
6. Serialize block protobuf and submit `ADD_BLOCK_PB`.

If budget is tight, validator may submit partial block rather than miss deadline.

## 5) Confirmation Semantics

Authoritative confirmation occurs only after blockchain accepts and applies block state transitions.

Flow:

1. Blockchain accepts block.
2. Blockchain publishes `CONFIRM_BLOCK` hashes.
3. Pool consumes hashes and evicts confirmed pending entries.
4. Wallet confirmation logic uses blockchain balance as ground truth.

This avoids relying on potentially lossy PUB-only counters for final correctness.

## 6) Performance Notes

- OpenMP batch paths are used in wallet submission and validator verification stages.
- Protobuf is the primary high-throughput transport format (`*_PB` commands).
- System is strongly sensitive to transaction wire size at high load.
- In practice, signature speed alone does not determine e2e TPS; serialization, queueing, and coordination costs matter.

## 7) Canonical Results and Artifacts

Primary result docs:

- `docs/FINDINGS.md`
- `docs/RESULTS.md`

Canonical repeat-matrix artifact bundle:

- `benchmarks/results/hybrid_matrix_apr18_final/`

Raw history (including intermediate attempts):

- `benchmarks/results/hybrid_matrix_apr18/`

## 8) Baseline Attribution

- Baseline/control path: `blockchain_base/`
- Active PQC-evaluated path: `blockchain/`

Detailed delta documentation:

- `docs/HARSHA_BASELINE_DIFF.md`

## 9) Reproducibility Quick Commands

```bash
# Build blockchain path
cd blockchain
make clean && make -j4 SIG_SCHEME=2

# Build baseline path
cd ../blockchain_base
make clean && make -j4

# Build benchmark binaries
cd ../benchmarks
make clean && make -j4
```

Use your existing run scripts and result directories referenced in `docs/RESULTS.md` for the exact experiment matrices.
