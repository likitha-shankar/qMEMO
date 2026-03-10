#ifndef TRANSACTION_POOL_H
#define TRANSACTION_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include "transaction.h"

// =============================================================================
// TRANSACTION POOL - v29.3 with Hash Table Index for O(1) Lookups
// =============================================================================
//
// PERFORMANCE FIX: Previous O(n) linear scans caused:
//   pool_contains: 100K comparisons per TX -> submission 7.8K TPS (was 17.6K)
//   pool_confirm:  100K x 10K = 1B ops/block -> 1096ms overhead
//
// FIX: Open-addressing hash table maps tx_hash -> entry index
//   pool_contains: O(1) amortized
//   pool_confirm:  O(1) amortized  
//   pool_add slot: O(1) via free list
// =============================================================================

#define MAX_POOL_SIZE       1200000
#define HASH_TABLE_SIZE     (MAX_POOL_SIZE * 2)
#define MAX_TRACKED_ADDRESSES 1000

#define HT_EMPTY    UINT32_MAX
#define HT_DELETED  (UINT32_MAX - 1)

typedef enum {
    TX_STATUS_PENDING,
    TX_STATUS_ASSIGNED,
    TX_STATUS_CONFIRMED,
    TX_STATUS_EXPIRED,
    TX_STATUS_REJECTED
} TxStatus;

typedef struct {
    Transaction* tx;
    uint8_t tx_hash[28];
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
    
    // Hash table: tx_hash -> entry index (O(1) lookup)
    uint32_t* hash_table;
    uint32_t hash_table_size;
    
    // Free list: stack of available entry indices (O(1) alloc)
    uint32_t* free_list;
    uint32_t free_count;
    
    AddressNonceTracker* nonce_trackers;
    uint32_t nonce_tracker_count;
    uint32_t nonce_tracker_capacity;
} TransactionPool;

// Pool functions
TransactionPool* pool_create(void);
bool pool_add(TransactionPool* pool, Transaction* tx);
uint64_t pool_get_pending_nonce(const TransactionPool* pool, const uint8_t address[20]);
Transaction** pool_get_pending(TransactionPool* pool, uint32_t max_count, 
                               uint32_t current_block, uint32_t* out_count);
bool pool_confirm(TransactionPool* pool, const uint8_t tx_hash[28]);
uint32_t pool_confirm_batch(TransactionPool* pool, const uint8_t* hashes, uint32_t hash_count);
void pool_return_assigned(TransactionPool* pool, uint32_t block_height);
uint32_t pool_cleanup_expired(TransactionPool* pool, uint32_t current_block);
void pool_get_stats(const TransactionPool* pool, uint32_t* pending, uint32_t* confirmed);
bool pool_contains(const TransactionPool* pool, const uint8_t tx_hash[28]);
void pool_destroy(TransactionPool* pool);
char* pool_serialize_status(const TransactionPool* pool);

#endif
