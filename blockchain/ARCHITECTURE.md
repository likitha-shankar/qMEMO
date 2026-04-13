# MEMO Blockchain — Architecture & System Documentation

## 1. System Overview

MEMO is a Proof-of-Space (PoS) blockchain inspired by Chia Network, implemented in C with ZeroMQ for inter-process communication. It uses a 5-process architecture designed for single-node operation today and distributed deployment in the future.

### Architecture Diagram

```
┌─────────────┐        ┌───────────────┐        ┌──────────────┐
│   WALLET     │──REQ──→│     POOL      │←──REQ──│  VALIDATOR   │
│  (CLI/API)   │        │  (TX Buffer)  │        │  (Farmer)    │
│              │        │               │        │              │
│ • Create TXs │        │ • Store TXs   │        │ • Find proofs│
│ • Sign Ed25519│       │ • Sort by     │        │ • Create     │
│ • Batch submit│       │   nonce       │        │   blocks     │
│ • Query bal  │        │ • Serve to    │        │ • Verify TXs │
│              │        │   validators  │        │              │
└──────┬───────┘        └───────┬───────┘        └──────┬───────┘
       │                        │  ↑ SUB                │
       │                        │  │ CONFIRM_BLOCK      │
       │                        │  │ (PUB/SUB async)    │
       │ REQ                    │  │                    │ REQ
       ↓                        │  │                    ↓
┌──────────────────────────────────────────────────────────────┐
│                      BLOCKCHAIN                               │
│                   (Ledger + Chain)                             │
│                                                               │
│  • Stores all blocks           • Validates blocks             │
│  • Maintains ledger            • Processes TXs (debit/credit) │
│  • Balance/nonce queries       • Publishes NEW_BLOCK          │
│  • Publishes CONFIRM_BLOCK     • PUSH → Metronome             │
│                                                               │
└──────────────────────────────┬───────────────────────────────┘
                               │ PUSH
                               ↓
                    ┌─────────────────────┐
                    │     METRONOME       │
                    │   (Coordinator)     │
                    │                     │
                    │ • Broadcast CHALLENGE│  ←──PUB──→  All Validators
                    │ • Collect proofs     │  ←──REP──→  Proof submissions
                    │ • Select winner      │
                    │ • Announce WINNER    │  ──PUB──→   Winning Validator
                    │ • Wait for confirm   │  ←──PULL──  From Blockchain
                    │ • Adjust difficulty   │
                    │ • Create empty blocks │
                    └─────────────────────┘
```

### Process Communication Map

| From → To | Socket Pattern | Purpose |
|-----------|---------------|---------|
| Wallet → Pool | REQ/REP | Submit TXs (SUBMIT_BATCH_PB) |
| Wallet → Blockchain | REQ/REP | Balance, nonce queries |
| Wallet → Blockchain PUB | SUB | Watch for NEW_BLOCK (wait_confirm) |
| Validator → Metronome | SUB | Receive CHALLENGE, WINNER announcements |
| Validator → Metronome | REQ/REP | Submit proofs (SUBMIT_PROOF) |
| Validator → Pool | REQ/REP | Fetch TXs (GET_FOR_WINNER) |
| Validator → Blockchain | REQ/REP | Submit block (ADD_BLOCK_PB), query balances |
| Metronome → Blockchain | REQ/REP | Query state, add empty blocks |
| Blockchain → Metronome | PUSH/PULL | BLOCK_CONFIRMED notification |
| Blockchain → Pool | PUB/SUB | CONFIRM_BLOCK (TX hashes for removal) |
| Blockchain → Wallets | PUB/SUB | NEW_BLOCK (height, TX count, hash) |

---

## 2. Block Lifecycle (One Complete Round)

```
TIME    METRONOME                VALIDATOR              POOL            BLOCKCHAIN
─────   ─────────                ─────────              ────            ──────────
 0ms    Generate CHALLENGE
        Broadcast via PUB ──────→ Receive CHALLENGE
                                  Search plot (O(log n))
                                  Find valid proof
                                  Submit proof via REQ ──→ Verify proof
                                                           Track best quality
~750ms  Proof window closes
        Select winner (best quality)
        Broadcast WINNER via PUB → Receive WINNER
                                  ┌──────────────────────────────────────────────┐
                                  │ BLOCK CREATION PIPELINE (~80-140ms)          │
                                  │                                              │
                                  │ Step 1: GET_LAST_HASH from blockchain (~1ms) │
                                  │ Step 2: GET_FOR_WINNER ──→ Fetch 10K TXs    │
                                  │         (sorted by addr,nonce)    (~14ms)    │
                                  │ Step 2.5: Sanity + balance check  (~5ms)     │
                                  │ Step 3-4: Create block + coinbase (~2ms)     │
                                  │ Step 5: Serialize protobuf + send (~20ms)    │
                                  │                        ─────────────────────→│
                                  └──────────────────────────────────────────────┘
                                                                       Verify block:
                                                                        • Height sequential?
                                                                        • Prev hash matches?
                                                                        • Block hash valid?
                                                                       Process TXs:
                                                                        • Coinbase → credit miner
                                                                        • Regular → check balance
                                                                        • Debit sender (value+fee)
                                                                        • Credit receiver (value)
                                                                        • Update nonces
                                                                       ←──PUSH── BLOCK_CONFIRMED
        Receive BLOCK_CONFIRMED ←─┘
                                                                       ──PUB──→ CONFIRM_BLOCK
                                                           Receive ←── (TX hashes)
                                                           Remove from pending
                                                                       ──PUB──→ NEW_BLOCK
~950ms  Hard deadline
        (if no confirm: empty block)
~1000ms Sleep until next tick
        ─── NEXT ROUND ───
```

---

## 3. Component Deep-Dives

### 3.1 Wallet (main_wallet.c, wallet.c)

**Purpose:** Creates transactions, signs them with Ed25519, submits to pool.

**Ed25519 Signing Flow:**
```
1. wallet_create_named("farmer1")
   → seed = SHA256("farmer1")                    // Deterministic from name
   → Ed25519 keypair from seed (OpenSSL EVP)     // 32-byte privkey, 32-byte pubkey
   → address = RIPEMD160(SHA256(pubkey))          // 20-byte address (like Bitcoin)

2. transaction_create(wallet, dest, value=1, fee=1, nonce, expiry)
   → tx_hash = BLAKE3(nonce || expiry || source || dest || value || fee)
   → signature = Ed25519_Sign(privkey, tx_hash)   // 64-byte signature
   → TX struct: 128 bytes total (compact, no pubkey on-chain)

3. batch_send via OpenMP:
   → 8 threads × batch_size=64 TXs per ZMQ message
   → Protobuf serialization (includes pubkey for pool storage)
   → SUBMIT_BATCH_PB: prefix + TransactionBatch protobuf
```

**Key Optimization — Parallel Batch Submission:**
- Each OpenMP thread has its own ZMQ context + socket + wallet copy
- Threads work on disjoint nonce ranges (no contention)
- Protobuf batching: 64 TXs per ZMQ message (vs 1 TX per message)
- Result: 162K TPS submission throughput (10 farmers × 8 threads)

**Needed for distributed:** Wallet→Pool communication would go over the network. Nonce management would need mempool awareness across nodes.

### 3.2 Transaction Pool (main_pool.c, transaction_pool.c)

**Purpose:** High-speed TX buffer between wallets and validators.

**Data Structure:**
```
TransactionPool
├── entries[1.1M]         // Pre-allocated PoolEntry array
│   └── PoolEntry
│       ├── tx*           // Transaction pointer (NULL = free slot)
│       ├── tx_hash[28]   // BLAKE3 hash for O(1) lookup
│       ├── pubkey[32]    // Ed25519 pubkey (for validator verification)
│       └── status        // PENDING, CONFIRMED, EXPIRED
├── hash_table[2M]        // Open-addressing hash table for O(1) contains/confirm
├── free_list[1.1M]       // Stack of free entry indices (O(1) allocation)
└── nonce_trackers[]      // Per-address max nonce (prevents duplicates)
```

**Key Optimizations:**

**1. Hash Table with Tombstone Compaction (O(1) add/lookup/confirm):**
- Problem: Open-addressing with HT_DELETED tombstones degrades over time.
  After 1M TXs churned through, nearly ALL slots become tombstones.
  Linear probing degrades from O(1) → O(n).
- Solution: After each `confirm_batch` (once per block, ~1/sec), rebuild
  the hash table from live entries only. Cost: ~2ms for 120K entries.
  Eliminates ALL tombstones permanently.
- Why needed long-term: Yes, essential. TX pools in distributed systems
  process millions of TXs. Without compaction, lookup performance
  degrades linearly with pool churn.

**2. Free List (O(1) slot allocation):**
- Pre-allocated 1.1M entry array with a stack of free indices.
- `pool_add`: pop from free list. `pool_confirm`: push back.
- No malloc/free per TX → zero fragmentation.
- Why needed long-term: Yes. Avoids GC pauses and fragmentation
  in long-running pool processes.

**3. Nonce Ordering Sort (O(n log n) at fetch time):**
- TXs arrive in arbitrary order from parallel OpenMP submission.
- Pool sorts by (source_address, nonce) when validator requests them.
- This ensures correct execution order: TX with nonce=5 must execute
  before nonce=6 for the same sender.
- Uses SortEntry struct to carry both TX pointer and original index,
  enabling O(n) pubkey reordering after sort.
- Why needed long-term: Yes. In distributed pools, TXs arrive from
  multiple nodes in arbitrary order. Sorting guarantees deterministic
  execution across all validators.

**4. Protobuf Transport (SUBMIT_BATCH_PB / TXPB response):**
- Binary protobuf: ~2x smaller than hex, schema-safe, zero-copy pointers.
- Pool stores raw Transaction structs + pubkeys (SegWit-style separation).
- Why needed long-term: Essential for cross-language, cross-network
  compatibility. Protobuf handles schema evolution gracefully.

**5. PUB/SUB Confirmation (from blockchain, not validator):**
- Blockchain is the single source of truth for which TXs are confirmed.
- After accepting a block, blockchain publishes CONFIRM_BLOCK with TX hashes.
- Pool subscribes and removes confirmed TXs in batch (O(k) per block).
- Why needed long-term: Critical. In distributed systems, validators
  could crash or lie. Only the blockchain knows which blocks are canonical.

### 3.3 Validator (main_validator.c, validator.c, consensus.c)

**Purpose:** Find PoS proofs, win blocks, create blocks with TXs.

**Proof of Space Flow:**
```
1. PLOT GENERATION (one-time, ~2-5 seconds for k=16):
   → 2^16 = 65,536 entries
   → entry[i] = { nonce: i, hash: BLAKE3(plot_id || i)[0:28] }
   → Sort entries by hash (enables O(log n) binary search)
   → Store in memory (~2MB for k=16)

2. CHALLENGE RESPONSE (every block, ~0.1ms):
   → target = BLAKE3(challenge)[0:28]
   → Binary search plot for closest entry
   → quality = entry.hash XOR target
   → If leading_zeros(quality) >= difficulty: valid proof!

3. PROOF SUBMISSION (~1ms):
   → Serialize proof: plot_id | nonce | proof_hash | quality
   → Send to metronome via REQ: SUBMIT_PROOF:<proof>#<name>#<address>

4. BLOCK CREATION (when we win, ~80-140ms for 10K TXs):
   → Step 1: GET_LAST_HASH from blockchain (1ms)
   → Step 2: GET_FOR_WINNER from pool (14ms for 10K TXs)
   → Step 2.5: Sanity checks + balance verification (5ms)
   → Step 3: Create Block struct with header fields
   → Step 4: Create coinbase TX (mining_reward + total_fees)
   → Step 5: Protobuf serialize + send to blockchain (20ms)
```

**Why sanity checks instead of full Ed25519 verification:**
In Bitcoin, Chia, and Ethereum, signature verification happens during
block processing in chain nodes, NOT during block construction by miners.
Miners are time-constrained (must meet block deadline). Chain nodes
verify at their own pace and all nodes verify independently.

Ed25519 verification of 10K TXs = 75-150ms with 8 OpenMP threads.
With a 200ms validator budget, this leaves too little time for everything
else, causing deadline misses → empty blocks → 0 TPS.

For the distributed version, Ed25519 verification should be added
to blockchain_process_block() with pubkeys included in block protobuf
(SegWit-style witness data).

### 3.4 Metronome (main_metronome.c, metronome.c)

**Purpose:** Coordination heartbeat — broadcast challenges, select winners, enforce timing.

**Strict 1-Second Block Timing (v45.3):**
```
ROUND = exactly block_interval ms (e.g., 1000ms)
├── Phase 1: Challenge broadcast              (~3ms)
├── Phase 2: Proof collection window          (~750ms, adaptive)
├── Phase 3: Winner announce + wait confirm   (~63ms avg)
└── Phase 4: Sleep until next tick            (~184ms)

NEVER start next round early. NEVER exceed block_interval.
If validator misses deadline → empty block → chain advances.
```

**Adaptive Timing:**
- Tracks a moving average of block creation overhead (WINNER→CONFIRMED).
- Adjusts proof window: `window = block_time - budget - margin`.
- Budget = max(worst×1.5, avg×2, 200ms) + 50ms safety margin.
- Dynamic zmq_recv timeout in proof loop prevents overshoot.

**Difficulty Adjustment:**
- Every 5 blocks, checks winner rate and proof counts.
- Winner rate < 50% → decrease difficulty (too hard).
- Winner rate ≥ 90% AND many proofs → increase difficulty (too easy).
- Emergency: 3 consecutive blocks without winner → immediate decrease.
- Range: 1-256 (leading zero bits required in proof quality).

### 3.5 Blockchain (main_blockchain.c, blockchain.c)

**Purpose:** Immutable ledger, block validation, state management.

**Block Verification (blockchain_add_block → block_verify):**
```
1. HEIGHT CHECK: block.height == prev_block.height + 1
2. HASH CHAIN:  block.previous_hash == prev_block.hash
3. HASH VERIFY: Recompute block hash from fields + TX commitment
                SHA256(header_fields || BLAKE3(all_tx_hashes))
                Must match block.hash exactly.
```

**Transaction Processing (blockchain_process_block):**
```
For each TX in block:
  IF coinbase:
    → Credit dest with (base_reward + total_fees)
  ELSE (regular):
    → Check nonce ≥ expected (replay protection)
    → Check sender balance ≥ (value + fee)
    → Debit sender: balance -= (value + fee)
    → Credit receiver: balance += value
    → Track fees for miner
    → Update sender's expected nonce
```

**Ledger Structure:**
```
ledger[10000]  // Max 10K accounts
├── ledger[0] = { address[20], balance: 50000, nonce: 1024 }
├── ledger[1] = { address[20], balance: 30000, nonce: 512 }
└── ...
```

**Confirmation Publishing (after successful add):**
```
1. PUSH → Metronome:  "BLOCK_CONFIRMED:<hash>|<farmer_name>"
   (Metronome knows round is complete, can start next challenge)

2. PUB → Pool:        "CONFIRM_BLOCK:" + height(4B) + count(4B) + N×hash(28B)
   (Pool removes confirmed TXs from pending queue)

3. PUB → Wallets:     "NEW_BLOCK:<height>:<tx_count>:<hash>"
   (Benchmark monitor tracks progress)
```

---

## 4. Performance Analysis (from your benchmark)

### What the Logs Show

| Metric | Value | Notes |
|--------|-------|-------|
| Submission TPS | 162,660 tx/sec | 10 farmers × 8 threads, protobuf batching |
| Processing TPS | ~9,225 tx/sec | 102,400 TXs / 11.1s |
| Theoretical max | 10,000 tx/sec | 10K TXs/block ÷ 1s interval |
| Efficiency | 92.3% | Processing/theoretical |
| Block pipeline | ~141ms avg | Fetch 51ms + Serialize 61ms |
| Proof window | 728ms (72.8%) | Time spent collecting proofs |
| Block overhead | 63ms (6.3%) | Winner→Confirmed |
| Idle/sleep | 205ms (20.5%) | Waiting for next tick |

### Where Time Is Spent (Per Block)

```
Pool fetch (GET_FOR_WINNER):    51ms  ████████████████
  └─ Collect 10K entries:        7ms
  └─ Sort by (addr,nonce):       4ms
  └─ Protobuf serialize:         5ms
  └─ ZMQ transfer:              35ms

Validator verify + build:        28ms  █████████
  └─ Sanity checks:              1ms
  └─ Balance batch query:        5ms
  └─ Block struct creation:      2ms
  └─ Coinbase creation:          0ms
  └─ Block hash (BLAKE3 commit): 3ms

Serialize + blockchain send:     61ms  ████████████████████
  └─ Protobuf block pack:       10ms
  └─ ZMQ send (1.5MB):          20ms
  └─ Blockchain deserialize:    15ms
  └─ Process TXs (ledger):      10ms
  └─ Hash verify:                6ms

TOTAL PIPELINE:                ~140ms
BUDGET AVAILABLE:              ~200ms
HEADROOM:                       ~60ms (30%)
```

### Optimization Opportunities

**1. Increase block size (most impactful for TPS):**
With 1s blocks and 10K TXs, max = 10K TPS. With 32K TXs/block → 32K TPS theoretical. Pipeline would take ~300ms (still within budget if block interval is increased).

**2. ZMQ transfer optimization:**
The 1.5MB protobuf block takes ~20ms to transfer. IPC sockets (unix domain) instead of TCP would cut this to ~5ms for single-node.

**3. Parallel block processing in blockchain:**
The blockchain processes TXs sequentially. With pre-sorted TXs by sender, independent senders could be processed in parallel.

**4. Pipeline overlap:**
Currently: fetch → verify → build → send → confirm (sequential).
Could overlap: fetch TXs while computing proof, pre-build block header.

---

## 5. Comparison with Production Blockchains

| Feature | Bitcoin | Chia | Ethereum | MEMO |
|---------|---------|------|----------|------|
| Consensus | PoW (SHA256) | PoSpace + PoTime | PoS (validators) | PoSpace |
| Block time | 10 min | 18.75s | 12s | Configurable (1s) |
| TX verification | Script/ECDSA | CLVM/BLS | EVM/ECDSA | Ed25519 |
| Where sig verified | Block processing | Block processing | Block processing | Block processing* |
| Merkle root | SHA256 merkle tree | Merkle tree | Patricia trie | BLAKE3 commitment |
| TX ordering | Fee priority | Fee priority | Nonce sequential | Nonce sequential |
| Fee model | Fee market | Fee market | EIP-1559 | Fixed fee |
| State model | UTXO | Coin set | Account | Account (ledger) |
| Serialization | Custom binary | Streamable | RLP | Protobuf |

\* Sanity checks in validator; full verification planned for blockchain_process_block in distributed version.

---

## 6. Running Benchmarks

### Basic Usage
```bash
./benchmark.sh NUM_TX BLOCK_INTERVAL K_PARAM NUM_FARMERS WARMUP MAX_TXS BATCH THREADS
```

### Parameters
| # | Parameter | Default | Description |
|---|-----------|---------|-------------|
| 1 | NUM_TX | 1000 | Total transactions to send |
| 2 | BLOCK_INTERVAL | 1 | Seconds between blocks |
| 3 | K_PARAM | 16 | Plot size (2^k entries) |
| 4 | NUM_FARMERS | 10 | Number of validators |
| 5 | WARMUP | auto | Warmup blocks (auto-calculated) |
| 6 | MAX_TXS | 10000 | Max transactions per block |
| 7 | BATCH_SIZE | 64 | TXs per ZMQ batch message |
| 8 | NUM_THREADS | 8 | OpenMP threads per farmer |

### Examples
```bash
# Default: 102K TXs, 1s blocks, 10K TXs/block
./benchmark.sh 102400 1 16 10 auto 10000

# Larger blocks: 32K TXs/block, 2s interval
./benchmark.sh 200000 2 16 10 auto 32768

# Small blocks: 1K TXs/block, 1s interval
./benchmark.sh 50000 1 16 10 auto 1024

# Maximum throughput test: 64K TXs/block, 4s interval
./benchmark.sh 500000 4 16 10 auto 65536

# Minimal test: quick sanity check
./benchmark.sh 1000 1 16 5 auto 1000
```

### Parameter Sweep
```bash
chmod +x run_sweep.sh

# Full sweep: all block_interval × block_size combinations
./run_sweep.sh

# Quick sweep: subset of combinations
./run_sweep.sh --quick

# Single custom run
./run_sweep.sh --custom 2 32768

# Only vary block intervals (fixed 10K block size)
./run_sweep.sh --intervals-only

# Only vary block sizes (fixed 1s interval)
./run_sweep.sh --sizes-only
```
