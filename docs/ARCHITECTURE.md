# qMEMO Architecture

This document describes the runtime architecture of the active implementation in `blockchain/`.

## Defense-Ready Summary (2-minute version)

- qMEMO runs as five ZeroMQ-connected services: `wallet`, `pool`, `validator`, `metronome`, `blockchain`.
- `metronome` coordinates rounds (`CHALLENGE`/`WINNER`), but **does not own final confirmation**.
- Winner validator assembles candidate blocks from pool transactions and submits to blockchain.
- `blockchain` is the **single source of truth**: it accepts/rejects blocks, updates ledger state, and emits `CONFIRM_BLOCK`.
- `pool` only buffers pending transactions and evicts them after blockchain confirmation.
- Wallet confirmation is balance-derived from blockchain (authoritative), using block events as timing signals.

## Appendix Use

Use this document in two ways:

- **Defense/Q&A:** read Sections 1-5 plus the summary above.
- **Paper appendix:** use Sections 6-9 for invariants, lifecycle semantics, and baseline attribution.

## 1) System Overview

qMEMO is a multi-process blockchain testbed with a Proof-of-Space style coordinator loop and pluggable signature verification. The runtime is intentionally decomposed into independent services connected with ZeroMQ:

- `blockchain` (ledger authority)
- `pool` (pending transaction queue)
- `metronome` (round coordinator)
- `validator` (proof search + block assembly)
- `wallet` (transaction submission + confirmation observer)

The system is designed so that:

- The blockchain is the source of truth for accepted blocks and confirmed transactions.
- Validators build candidate blocks only if selected as round winner.
- The pool is a buffer and serving layer; it does not decide final confirmation.
- Wallet confirmation is ledger-derived (via balance checks), with PUB/SUB used as a timing signal.

## 2) Process Roles

### Blockchain (`main_blockchain.c`)

- Maintains chain state and in-memory ledger.
- Validates and accepts/rejects candidate blocks.
- Applies transaction effects (balances + nonces).
- Publishes accepted block events and confirmed transaction hashes.

### Pool (`main_pool.c`)

- Accepts single/batch transaction submissions.
- Stores pending transactions and serves batches to winner validators.
- Subscribes to blockchain confirmation events and removes confirmed transactions.
- Forwards block events to wallet subscribers.

### Metronome (`main_metronome.c`)

- Broadcasts challenge rounds.
- Collects proof submissions.
- Selects and announces round winner.
- Waits for blockchain confirmation and advances rounds.
- Creates empty blocks only when no valid winner path completes.

### Validator (`main_validator.c`)

- Maintains local plot/proof search state.
- Responds to challenges with proofs.
- If winner: fetches pending TXs, verifies them, assembles and submits block.
- Uses deadline-aware fetch/verify behavior.

### Wallet (`main_wallet.c`)

- Creates deterministic wallets and signs transactions.
- Supports high-throughput OpenMP batch submission.
- Subscribes to block events and queries authoritative balance to compute confirmation progress.

## 3) Network Topology and Ports

Default bindings from the current code path:

| Component | Socket Type | Default | Purpose |
|---|---|---|---|
| Blockchain | REP | `tcp://*:5555` | Block ingest + state queries |
| Blockchain | PUB | configurable (commonly `:5559`) | `NEW_BLOCK`, `CONFIRM_BLOCK` |
| Metronome | REP | `tcp://*:5556` | Proof/control requests |
| Metronome | PUB | `tcp://*:5558` | `CHALLENGE`, `WINNER`, `NO_WINNER` |
| Metronome | PULL | `tcp://*:5560` | Blockchain `BLOCK_CONFIRMED` notifications |
| Pool | REP | `tcp://*:5557` | Submission + winner fetch |
| Pool | SUB | configurable (blockchain PUB) | `CONFIRM_BLOCK`, `NEW_BLOCK` intake |
| Pool | PUB | configurable (commonly `:5561`) | Forwarded `NEW_BLOCK`, `TX_CONFIRMED` |

## 4) Control Plane vs Data Plane

### Control Plane (small messages, orchestration)

- `CHALLENGE:*`
- `WINNER:*`
- `BLOCK_CONFIRMED:*`
- `GET_HEIGHT`, `GET_LAST_HASH`, `GET_NONCE`, `GET_BALANCE`

### Data Plane (bulk payloads)

- `SUBMIT_BATCH_PB:<protobuf TransactionBatch>`
- `GET_FOR_WINNER:max:height` → `TXPB<protobuf TransactionBatch>`
- `ADD_BLOCK_PB:<farmer+protobuf Block>`
- `CONFIRM_BLOCK:<height><count><hashes>` (binary)

## 5) Round Lifecycle (Normal Case)

1. Metronome emits `CHALLENGE`.
2. Validators search plots and submit proofs to metronome.
3. Metronome selects best proof and emits `WINNER`.
4. Winning validator:
   - gets last hash / height from blockchain,
   - requests pending TX batch from pool,
   - verifies signatures and nonce/balance constraints,
   - assembles block and submits `ADD_BLOCK_PB` to blockchain.
5. Blockchain accepts block, updates ledger, emits:
   - `NEW_BLOCK:*` (text event),
   - `CONFIRM_BLOCK:*` (binary tx-hash list),
   - `BLOCK_CONFIRMED:*` to metronome.
6. Pool consumes `CONFIRM_BLOCK`, evicts confirmed TXs, forwards notifications.
7. Wallet observers react to `NEW_BLOCK` and poll blockchain balance for ground-truth confirmations.

## 6) Transaction Lifecycle

1. Wallet signs transaction (scheme-specific signature + embedded pubkey/sig metadata).
2. Wallet submits transaction(s) to pool (`SUBMIT_BATCH_PB`).
3. Pool stores as pending.
4. Winner validator fetches pending transactions from pool.
5. Validator verifies:
   - address/public-key consistency,
   - signature validity (typed by `sig_type`),
   - nonce and balance admissibility.
6. Blockchain re-validates state transitions and commits.
7. Blockchain publishes confirmation hashes; pool removes confirmed entries.

## 7) Signature Architecture

Signature handling is compile-time selectable and runtime typed:

- `SIG_SCHEME=1`: Ed25519
- `SIG_SCHEME=2`: Falcon-512
- `SIG_SCHEME=3`: Hybrid
- `SIG_SCHEME=4`: ML-DSA-44

Transactions carry `sig_type`, `sig_len`, and `pubkey_len` so validators can verify mixed-scheme traffic safely when required.

## 8) Design Invariants

- **Blockchain authority:** only blockchain acceptance implies confirmation.
- **Eventual pool consistency:** pool state converges via `CONFIRM_BLOCK` events.
- **Replay resistance:** account nonce monotonicity enforced at state layer.
- **Safety under mixed key sizes:** universal maximum crypto buffers prevent decode overflow across schemes.
- **Deadline-aware block construction:** validator can send partial blocks rather than timing out.

## 9) Baseline vs Current

- `blockchain_base/` is the baseline/control path (Harsha baseline line in this repo).
- `blockchain/` is the active PQC-capable research path used for final measurements.

For delta details, see `docs/HARSHA_BASELINE_DIFF.md`.
