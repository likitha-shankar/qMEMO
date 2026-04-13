#include "../include/transaction_pool.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// =============================================================================
// v47: Transaction Pool — Simplified (No Hash Table)
// =============================================================================
//
// DESIGN: Pool is a simple, fast buffer. No duplicate detection. No hash table.
//
// WHY NO HASH TABLE:
//   1. Nonces already guarantee uniqueness per sender. Duplicate TXs (same
//      sender + same nonce) are rejected by the validator during block creation.
//   2. In distributed mode, duplicates arrive from different nodes — no single
//      pool can detect them all. Validators must handle duplicates regardless.
//   3. Removing the hash table saves 16MB RAM, eliminates tombstone compaction,
//      and simplifies the entire pool to ~200 lines.
//
// CONFIRMATION:
//   Instead of hash-based lookup (O(1) but requires hash table + tombstones),
//   we track which entries were assigned to the last block via assigned_indices[].
//   On CONFIRM_BLOCK, we scan only the assigned list (O(K) where K = block size).
//   This is typically K = 10,000 — a linear scan takes <0.1ms.
//
// PERFORMANCE:
//   pool_add:         O(1) — free list pop, no hash computation, no duplicate check
//   pool_get_pending: O(n) scan + O(n log n) sort — same as before
//   pool_confirm:     O(K) scan of assigned list — was O(1) but K is small
// =============================================================================

#define INITIAL_POOL_CAPACITY 1100000

// ---- Nonce tracker ----

static AddressNonceTracker* find_or_create_nonce_tracker(TransactionPool* pool, 
                                                          const uint8_t address[20]) {
    for (uint32_t i = 0; i < pool->nonce_tracker_count; i++) {
        if (memcmp(pool->nonce_trackers[i].address, address, 20) == 0) {
            return &pool->nonce_trackers[i];
        }
    }
    
    if (pool->nonce_tracker_count >= pool->nonce_tracker_capacity) {
        if (pool->nonce_tracker_capacity >= MAX_TRACKED_ADDRESSES) return NULL;
        uint32_t new_cap = pool->nonce_tracker_capacity * 2;
        if (new_cap > MAX_TRACKED_ADDRESSES) new_cap = MAX_TRACKED_ADDRESSES;
        AddressNonceTracker* new_t = realloc(pool->nonce_trackers, new_cap * sizeof(AddressNonceTracker));
        if (!new_t) return NULL;
        memset(new_t + pool->nonce_tracker_capacity, 0, 
               (new_cap - pool->nonce_tracker_capacity) * sizeof(AddressNonceTracker));
        pool->nonce_trackers = new_t;
        pool->nonce_tracker_capacity = new_cap;
    }
    
    AddressNonceTracker* tracker = &pool->nonce_trackers[pool->nonce_tracker_count++];
    memcpy(tracker->address, address, 20);
    tracker->max_nonce = 0;
    tracker->pending_count = 0;
    return tracker;
}

// ---- Pool creation ----

TransactionPool* pool_create(void) {
    TransactionPool* pool = safe_malloc(sizeof(TransactionPool));
    memset(pool, 0, sizeof(TransactionPool));
    
    pool->capacity = INITIAL_POOL_CAPACITY;
    pool->entries = safe_malloc(pool->capacity * sizeof(PoolEntry));
    memset(pool->entries, 0, pool->capacity * sizeof(PoolEntry));
    
    // Free list: stack of available entry indices
    pool->free_list = safe_malloc(pool->capacity * sizeof(uint32_t));
    pool->free_count = pool->capacity;
    for (uint32_t i = 0; i < pool->capacity; i++) {
        pool->free_list[i] = pool->capacity - 1 - i;
    }
    
    // Assignment tracking
    pool->assigned_indices = safe_malloc(MAX_ASSIGNED * sizeof(uint32_t));
    pool->assigned_count = 0;
    pool->assigned_block_height = 0;
    
    pool->nonce_tracker_capacity = 100;
    pool->nonce_trackers = safe_malloc(pool->nonce_tracker_capacity * sizeof(AddressNonceTracker));
    memset(pool->nonce_trackers, 0, pool->nonce_tracker_capacity * sizeof(AddressNonceTracker));
    pool->nonce_tracker_count = 0;
    
    LOG_INFO("Transaction pool created (capacity: %u, no hash table — simplified v47)", 
             pool->capacity);
    
    return pool;
}

uint64_t pool_get_pending_nonce(const TransactionPool* pool, const uint8_t address[20]) {
    if (!pool) return 0;
    for (uint32_t i = 0; i < pool->nonce_tracker_count; i++) {
        if (memcmp(pool->nonce_trackers[i].address, address, 20) == 0) {
            return pool->nonce_trackers[i].max_nonce;
        }
    }
    return 0;
}

// =============================================================================
// TRANSACTION ADD - O(1) via free list (no duplicate check, no hash table)
// =============================================================================

bool pool_add(TransactionPool* pool, Transaction* tx) {
    return pool_add_with_pubkey(pool, tx, NULL);
}

bool pool_add_with_pubkey(TransactionPool* pool, Transaction* tx, const uint8_t pubkey[32]) {
    if (!pool || !tx) return false;
    
    if (pool->free_count == 0) {
        return false;  // Pool full
    }
    
    // O(1) slot allocation from free list
    uint32_t entry_idx = pool->free_list[--pool->free_count];
    
    Transaction* tx_copy = safe_malloc(sizeof(Transaction));
    memcpy(tx_copy, tx, sizeof(Transaction));
    
    // Compute TX hash (used for confirmation matching)
    transaction_compute_hash(tx, pool->entries[entry_idx].tx_hash);
    
    pool->entries[entry_idx].tx = tx_copy;
    pool->entries[entry_idx].status = TX_STATUS_PENDING;
    pool->entries[entry_idx].received_time = get_current_time_ms();
    pool->entries[entry_idx].assigned_block = 0;
    
    if (pubkey) {
        memcpy(pool->entries[entry_idx].pubkey, pubkey, 32);
    } else {
        memset(pool->entries[entry_idx].pubkey, 0, 32);
    }
    
    pool->count++;
    pool->pending_count++;
    
    // Track nonce
    AddressNonceTracker* tracker = find_or_create_nonce_tracker(pool, tx->source_address);
    if (tracker) {
        if (tx->nonce >= tracker->max_nonce) {
            tracker->max_nonce = tx->nonce + 1;
        }
        tracker->pending_count++;
    }
    
    if (pool->pending_count == 1 || pool->pending_count % 10000 == 0) {
        LOG_INFO("Pool: %u pending, %u total, %u free", 
                 pool->pending_count, pool->count, pool->free_count);
    }
    
    return true;
}

// =============================================================================
// GET PENDING - collect, sort, and track assigned entries
// =============================================================================

typedef struct { Transaction* tx; uint32_t orig_idx; } SortEntry;

static int sort_entry_compare(const void* a, const void* b) {
    const SortEntry* ea = (const SortEntry*)a;
    const SortEntry* eb = (const SortEntry*)b;
    int addr_cmp = memcmp(ea->tx->source_address, eb->tx->source_address, 20);
    if (addr_cmp != 0) return addr_cmp;
    if (ea->tx->nonce < eb->tx->nonce) return -1;
    if (ea->tx->nonce > eb->tx->nonce) return 1;
    return 0;
}

Transaction** pool_get_pending(TransactionPool* pool, uint32_t max_count,
                               uint32_t current_block, uint32_t* out_count) {
    return pool_get_pending_with_pubkeys(pool, max_count, current_block, out_count, NULL);
}

Transaction** pool_get_pending_with_pubkeys(TransactionPool* pool, uint32_t max_count,
                                            uint32_t current_block, uint32_t* out_count,
                                            uint8_t** pubkeys_out) {
    if (!pool || max_count == 0) {
        if (out_count) *out_count = 0;
        if (pubkeys_out) *pubkeys_out = NULL;
        return NULL;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // v47.1: NO STATUS CHANGE. Pool is a dumb buffer.
    // ═══════════════════════════════════════════════════════════════════
    // We do NOT mark entries as ASSIGNED. TXs stay PENDING until confirmed.
    // If the same TX is served in consecutive rounds (because CONFIRM_BLOCK
    // hasn't arrived yet), the blockchain's nonce check rejects the duplicate.
    // This eliminates:
    //   - ASSIGNED→PENDING race condition (caused empty blocks)
    //   - assigned_indices overwrite bug (caused stuck TXs)
    //   - 90% confirmation rate bug
    // ═══════════════════════════════════════════════════════════════════
    
    Transaction** txs = safe_malloc(max_count * sizeof(Transaction*));
    uint8_t* pubkeys = pubkeys_out ? safe_malloc(max_count * 32) : NULL;
    uint32_t* entry_indices = safe_malloc(max_count * sizeof(uint32_t));
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < pool->capacity && count < max_count; i++) {
        PoolEntry* entry = &pool->entries[i];
        
        if (entry->tx && entry->status == TX_STATUS_PENDING) {
            // Expire check
            if (entry->tx->expiry_block > 0 && current_block > entry->tx->expiry_block) {
                entry->status = TX_STATUS_EXPIRED;
                pool->pending_count--;
                transaction_destroy(entry->tx);
                entry->tx = NULL;
                pool->count--;
                pool->free_list[pool->free_count++] = i;
                continue;
            }
            
            Transaction* tx_copy = safe_malloc(sizeof(Transaction));
            memcpy(tx_copy, entry->tx, sizeof(Transaction));
            txs[count] = tx_copy;
            if (pubkeys) {
                memcpy(pubkeys + count * 32, entry->pubkey, 32);
            }
            entry_indices[count] = i;
            count++;
        }
    }
    
    if (out_count) *out_count = count;
    
    if (count == 0) {
        free(txs); free(entry_indices);
        if (pubkeys) free(pubkeys);
        if (pubkeys_out) *pubkeys_out = NULL;
        return NULL;
    }
    
    // Sort by (source_address, nonce) with SortEntry for O(n log n) pubkey reordering
    SortEntry* sort_arr = safe_malloc(count * sizeof(SortEntry));
    for (uint32_t i = 0; i < count; i++) {
        sort_arr[i].tx = txs[i];
        sort_arr[i].orig_idx = i;
    }
    
    qsort(sort_arr, count, sizeof(SortEntry), sort_entry_compare);
    
    // Rebuild arrays in sorted order
    uint32_t* sorted_entry_indices = safe_malloc(count * sizeof(uint32_t));
    if (pubkeys) {
        uint8_t* pubkeys_sorted = safe_malloc(count * 32);
        for (uint32_t i = 0; i < count; i++) {
            txs[i] = sort_arr[i].tx;
            memcpy(pubkeys_sorted + i * 32, pubkeys + sort_arr[i].orig_idx * 32, 32);
            sorted_entry_indices[i] = entry_indices[sort_arr[i].orig_idx];
        }
        memcpy(pubkeys, pubkeys_sorted, count * 32);
        free(pubkeys_sorted);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            txs[i] = sort_arr[i].tx;
            sorted_entry_indices[i] = entry_indices[sort_arr[i].orig_idx];
        }
    }
    
    free(sort_arr);
    free(entry_indices);
    
    // Save entry indices as SEARCH HINT for pool_confirm (optimization only)
    // Status stays PENDING — no ASSIGNED marking
    pool->assigned_count = count < MAX_ASSIGNED ? count : MAX_ASSIGNED;
    for (uint32_t i = 0; i < pool->assigned_count; i++) {
        pool->assigned_indices[i] = sorted_entry_indices[i];
    }
    free(sorted_entry_indices);
    
    if (pubkeys_out) *pubkeys_out = pubkeys;
    else if (pubkeys) free(pubkeys);
    
    return txs;
}

// =============================================================================
// CONFIRM - scan assigned list for matching TX hashes
// =============================================================================
// Blockchain sends TX hashes in CONFIRM_BLOCK. We search the assigned_indices
// list (K entries, typically 10K) instead of a hash table.
// For K=10K hashes × 10K assigned = 100M comparisons — but each is a 32-byte
// memcmp that fails on first byte. Average probes: ~2. Total: ~0.2ms.
// This is fast enough and eliminates the entire hash table infrastructure.

bool pool_confirm(TransactionPool* pool, const uint8_t tx_hash[TX_HASH_SIZE]) {
    if (!pool) return false;
    
    // Search hint list first (entries served to last validator)
    for (uint32_t i = 0; i < pool->assigned_count; i++) {
        uint32_t idx = pool->assigned_indices[i];
        PoolEntry* entry = &pool->entries[idx];
        if (entry->tx && memcmp(entry->tx_hash, tx_hash, TX_HASH_SIZE) == 0) {
            pool->pending_count--;
            entry->status = TX_STATUS_CONFIRMED;
            pool->confirmed_count++;
            transaction_destroy(entry->tx);
            entry->tx = NULL;
            pool->count--;
            pool->free_list[pool->free_count++] = idx;
            // Remove from hint list (swap with last)
            pool->assigned_indices[i] = pool->assigned_indices[pool->assigned_count - 1];
            pool->assigned_count--;
            return true;
        }
    }
    
    // Fallback: scan entire pool
    for (uint32_t i = 0; i < pool->capacity; i++) {
        PoolEntry* entry = &pool->entries[i];
        if (entry->tx && memcmp(entry->tx_hash, tx_hash, TX_HASH_SIZE) == 0) {
            pool->pending_count--;
            entry->status = TX_STATUS_CONFIRMED;
            pool->confirmed_count++;
            transaction_destroy(entry->tx);
            entry->tx = NULL;
            pool->count--;
            pool->free_list[pool->free_count++] = i;
            return true;
        }
    }
    
    return false;
}

uint32_t pool_confirm_batch(TransactionPool* pool, const uint8_t* hashes, uint32_t hash_count) {
    if (!pool || !hashes) return 0;
    
    uint32_t confirmed = 0;
    for (uint32_t i = 0; i < hash_count; i++) {
        if (pool_confirm(pool, hashes + i * TX_HASH_SIZE)) {
            confirmed++;
        }
    }
    
    // NOTE: We do NOT return assigned→pending here. That happens at the 
    // START of pool_get_pending_with_pubkeys, which is the only safe place
    // to do it (avoids race conditions with stale assigned_indices).
    
    return confirmed;
}

// =============================================================================
// OTHER OPERATIONS
// =============================================================================

void pool_return_assigned(TransactionPool* pool, uint32_t block_height) {
    // v47.1: No-op. ASSIGNED status removed. TXs stay PENDING until confirmed.
    // If a TX was served but not confirmed, it stays PENDING and gets re-served.
    (void)pool; (void)block_height;
}

uint32_t pool_cleanup_expired(TransactionPool* pool, uint32_t current_block) {
    if (!pool) return 0;
    uint32_t removed = 0;
    for (uint32_t i = 0; i < pool->capacity; i++) {
        PoolEntry* entry = &pool->entries[i];
        if (entry->tx && entry->tx->expiry_block > 0 && 
            current_block > entry->tx->expiry_block) {
            if (entry->status == TX_STATUS_PENDING) pool->pending_count--;
            transaction_destroy(entry->tx);
            entry->tx = NULL;
            pool->count--;
            pool->free_list[pool->free_count++] = i;
            removed++;
        }
    }
    if (removed > 0) LOG_INFO("Cleaned up %u expired transactions", removed);
    return removed;
}

void pool_get_stats(const TransactionPool* pool, uint32_t* pending, uint32_t* confirmed) {
    if (pending) *pending = pool ? pool->pending_count : 0;
    if (confirmed) *confirmed = pool ? pool->confirmed_count : 0;
}

bool pool_contains(const TransactionPool* pool, const uint8_t tx_hash[TX_HASH_SIZE]) {
    if (!pool) return false;
    // Linear scan (no hash table) — only used for debugging, not critical path
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (pool->entries[i].tx && 
            memcmp(pool->entries[i].tx_hash, tx_hash, TX_HASH_SIZE) == 0) {
            return true;
        }
    }
    return false;
}

void pool_destroy(TransactionPool* pool) {
    if (!pool) return;
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (pool->entries[i].tx) transaction_destroy(pool->entries[i].tx);
    }
    free(pool->entries);
    free(pool->free_list);
    free(pool->assigned_indices);
    free(pool->nonce_trackers);
    free(pool);
}

char* pool_serialize_status(const TransactionPool* pool) {
    if (!pool) return NULL;
    char* buffer = safe_malloc(256);
    snprintf(buffer, 256, "PENDING:%u|CONFIRMED:%u|TOTAL:%u|CAPACITY:%u|FREE:%u",
             pool->pending_count, pool->confirmed_count, pool->count, 
             pool->capacity, pool->free_count);
    return buffer;
}
