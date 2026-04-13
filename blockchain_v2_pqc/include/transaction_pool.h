#ifndef TRANSACTION_POOL_H
#define TRANSACTION_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include "transaction.h"

// =============================================================================
// TRANSACTION POOL - v47 (Simplified: No Hash Table)
// =============================================================================
//
// DESIGN PHILOSOPHY:
//   The pool is a SIMPLE, FAST BUFFER. It accepts all TXs without duplicate
//   checking. The VALIDATOR handles verification (signatures, balances, nonces)
//   during block creation. This matches the distributed architecture where:
//     - Multiple pools on different nodes receive different TXs
//     - No single pool can detect all duplicates
//     - Validators are the authority on TX validity
//
// REMOVED (vs v29.4):
//   - Hash table (8MB, tombstone compaction, O(1) lookup)
//   - Duplicate detection in pool_add (validator handles via nonce)
//   - Hash-based confirmation (now uses assigned-index tracking)
//
// KEPT:
//   - Pre-allocated entry array (cache-friendly, no fragmentation)
//   - Free list (O(1) slot allocation/deallocation)
//   - Nonce-ordered sorting for GET_FOR_WINNER
//   - SortEntry for O(n log n) pubkey reordering
//
// CONFIRMATION FLOW:
//   1. GET_FOR_WINNER → pool returns K TXs, saves their entry indices
//   2. Blockchain confirms block → sends CONFIRM_BLOCK with TX hashes
//   3. Pool scans ONLY the assigned indices (not entire array) for matches
//   4. Confirmed entries freed, unconfirmed returned to PENDING
// =============================================================================

#define MAX_POOL_SIZE       1200000
#define MAX_ASSIGNED        70000   // Max TXs assigned per block (matches block.h)
#define MAX_TRACKED_ADDRESSES 1000

typedef enum {
    TX_STATUS_PENDING,
    TX_STATUS_ASSIGNED,
    TX_STATUS_CONFIRMED,
    TX_STATUS_EXPIRED,
    TX_STATUS_REJECTED
} TxStatus;

typedef struct {
    Transaction* tx;
    uint8_t tx_hash[TX_HASH_SIZE];         // Computed once at add time, used for confirmation
    uint8_t pubkey[CRYPTO_PUBKEY_MAX];     // Signer public key (Ed25519=32B, Falcon=897B, ML-DSA=1312B)
    size_t  pubkey_len;                    // Actual pubkey bytes stored
    uint8_t sig_type;                      // SIG_ED25519 / SIG_FALCON512 / SIG_ML_DSA44
    TxStatus status;
    uint64_t received_time;
    uint32_t assigned_block;
} PoolEntry;

typedef struct {
    uint8_t address[20];
    uint64_t max_nonce;
    uint32_t pending_count;
} AddressNonceTracker;

typedef struct {
    PoolEntry* entries;
    uint32_t capacity;
    uint32_t count;
    uint32_t pending_count;
    uint32_t confirmed_count;
    
    // Free list: stack of available entry indices (O(1) alloc)
    uint32_t* free_list;
    uint32_t free_count;
    
    // Assignment tracking: entry indices of TXs sent to last winning validator
    // Used for O(K) confirmation instead of hash table lookup
    uint32_t* assigned_indices;
    uint32_t assigned_count;
    uint32_t assigned_block_height;
    
    AddressNonceTracker* nonce_trackers;
    uint32_t nonce_tracker_count;
    uint32_t nonce_tracker_capacity;
} TransactionPool;

// Pool functions
TransactionPool* pool_create(void);
bool pool_add(TransactionPool* pool, Transaction* tx);
bool pool_add_with_pubkey(TransactionPool* pool, Transaction* tx,
                          const uint8_t* pubkey, size_t pubkey_len, uint8_t sig_type);
uint64_t pool_get_pending_nonce(const TransactionPool* pool, const uint8_t address[20]);
Transaction** pool_get_pending(TransactionPool* pool, uint32_t max_count, 
                               uint32_t current_block, uint32_t* out_count);
Transaction** pool_get_pending_with_pubkeys(TransactionPool* pool, uint32_t max_count,
                                            uint32_t current_block, uint32_t* out_count,
                                            uint8_t** pubkeys_out);
bool pool_confirm(TransactionPool* pool, const uint8_t tx_hash[TX_HASH_SIZE]);
uint32_t pool_confirm_batch(TransactionPool* pool, const uint8_t* hashes, uint32_t hash_count);
void pool_return_assigned(TransactionPool* pool, uint32_t block_height);
uint32_t pool_cleanup_expired(TransactionPool* pool, uint32_t current_block);
void pool_get_stats(const TransactionPool* pool, uint32_t* pending, uint32_t* confirmed);
bool pool_contains(const TransactionPool* pool, const uint8_t tx_hash[TX_HASH_SIZE]);
void pool_destroy(TransactionPool* pool);
char* pool_serialize_status(const TransactionPool* pool);

#endif
