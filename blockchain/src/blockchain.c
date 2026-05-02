/**
 * ============================================================================
 * BLOCKCHAIN.C - Immutable Ledger and State Management
 * ============================================================================
 * 
 * This file implements the blockchain data structure that stores all blocks
 * and maintains the ledger (account balances and nonces).
 * 
 * BLOCKCHAIN STRUCTURE:
 * =====================
 * 
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │  BLOCKCHAIN                                                              │
 * │  ├─ blocks[]           Array of block pointers                          │
 * │  │   ├─ blocks[0]      Genesis block (height=0)                         │
 * │  │   ├─ blocks[1]      First block after genesis                        │
 * │  │   └─ blocks[N-1]    Latest block                                     │
 * │  │                                                                       │
 * │  ├─ height             Number of blocks (N)                             │
 * │  ├─ last_hash[32]      Hash of latest block (for quick access)          │
 * │  │                                                                       │
 * │  └─ ledger[]           Account states (balance, nonce)                  │
 * │      ├─ ledger[0]      {address, balance, nonce}                        │
 * │      ├─ ledger[1]      {address, balance, nonce}                        │
 * │      └─ ...                                                              │
 * └──────────────────────────────────────────────────────────────────────────┘
 * 
 * LEDGER MANAGEMENT:
 * ==================
 * The ledger tracks per-address state:
 * - balance: How many coins the address owns
 * - nonce: Next expected transaction nonce (for replay protection)
 * 
 * When a block is processed:
 * 1. For coinbase tx: credit destination with (reward + fees)
 * 2. For regular tx:
 *    - Verify nonce matches expected
 *    - Debit source: balance -= (value + fee)
 *    - Credit dest: balance += value
 *    - Increment source's expected nonce
 * 
 * BLOCK VERIFICATION:
 * ===================
 * Before adding a block, we verify:
 * - Height is consecutive (prev_height + 1)
 * - Previous hash matches last block's hash
 * - Block hash is correctly computed
 * - All transactions are valid
 * 
 * ============================================================================
 */

#include "../include/blockchain.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef DIAG_OFF
uint64_t bc_diag_t_validate_ns = 0;
uint64_t bc_diag_t_commit_ns   = 0;
#endif

// =============================================================================
// BLOCKCHAIN CREATION
// =============================================================================

Blockchain* blockchain_create(void) {
    Blockchain* bc = safe_malloc(sizeof(Blockchain));
    memset(bc, 0, sizeof(Blockchain));
    
    bc->blocks = safe_malloc(MAX_BLOCKS * sizeof(Block*));
    memset(bc->blocks, 0, MAX_BLOCKS * sizeof(Block*));
    
    // Create genesis block
    Block* genesis = block_create_genesis();
    bc->blocks[0] = genesis;
    bc->height = 1;
    memcpy(bc->last_hash, genesis->header.hash, 32);
    
    LOG_INFO("🌅 Blockchain created with genesis block");
    
    return bc;
}

// =============================================================================
// BLOCK MANAGEMENT
// =============================================================================

bool blockchain_add_block(Blockchain* bc, Block* block) {
    if (!bc || !block) return false;
    
    if (bc->height >= MAX_BLOCKS) {
        LOG_ERROR("Blockchain full");
        return false;
    }
    
    // Verify block
    Block* prev_block = bc->height > 0 ? bc->blocks[bc->height - 1] : NULL;
    if (!block_verify(block, prev_block)) {
        LOG_ERROR("Block verification failed");
        return false;
    }
#ifndef DIAG_OFF
    bc_diag_t_validate_ns = get_current_time_ns();
#endif

    // Process transactions (update ledger)
    if (!blockchain_process_block(bc, block)) {
        LOG_ERROR("Block transaction processing failed");
        return false;
    }

    // Add to chain
    Block* block_copy = block_create();
    memcpy(&block_copy->header, &block->header, sizeof(BlockHeader));
    block_copy->total_fees = block->total_fees;

    for (uint32_t i = 0; i < block->header.transaction_count; i++) {
        if (block->transactions[i]) {
            block_add_transaction(block_copy, block->transactions[i]);
        }
    }

    bc->blocks[bc->height++] = block_copy;
    memcpy(bc->last_hash, block->header.hash, 32);
#ifndef DIAG_OFF
    bc_diag_t_commit_ns = get_current_time_ns();
#endif

    LOG_INFO("🔗 Block #%u added to chain", block->header.height);
    
    return true;
}

Block* blockchain_get_block(const Blockchain* bc, uint64_t height) {
    if (!bc || height >= bc->height) return NULL;
    return bc->blocks[height];
}

Block* blockchain_get_last_block(const Blockchain* bc) {
    if (!bc || bc->height == 0) return NULL;
    return bc->blocks[bc->height - 1];
}

uint64_t blockchain_get_height(const Blockchain* bc) {
    return bc ? bc->height : 0;
}

// =============================================================================
// LEDGER MANAGEMENT
// =============================================================================

static int find_ledger_entry(const Blockchain* bc, const uint8_t address[20]) {
    for (uint32_t i = 0; i < bc->ledger_count; i++) {
        if (memcmp(bc->ledger[i].address, address, 20) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int create_ledger_entry(Blockchain* bc, const uint8_t address[20]) {
    if (bc->ledger_count >= 10000) return -1;
    
    int idx = bc->ledger_count++;
    memcpy(bc->ledger[idx].address, address, 20);
    bc->ledger[idx].balance = 0;
    bc->ledger[idx].nonce = 0;
    
    return idx;
}

uint64_t blockchain_get_balance(const Blockchain* bc, const uint8_t address[20]) {
    if (!bc) return 0;
    
    int idx = find_ledger_entry(bc, address);
    return idx >= 0 ? bc->ledger[idx].balance : 0;
}

uint64_t blockchain_get_nonce(const Blockchain* bc, const uint8_t address[20]) {
    if (!bc) return 0;
    
    int idx = find_ledger_entry(bc, address);
    return idx >= 0 ? bc->ledger[idx].nonce : 0;
}

void blockchain_update_balance(Blockchain* bc, const uint8_t address[20], int64_t delta) {
    if (!bc) return;
    
    int idx = find_ledger_entry(bc, address);
    if (idx < 0) {
        idx = create_ledger_entry(bc, address);
        if (idx < 0) return;
    }
    
    if (delta < 0 && bc->ledger[idx].balance < (uint64_t)(-delta)) {
        bc->ledger[idx].balance = 0;
    } else {
        bc->ledger[idx].balance += delta;
    }
}

void blockchain_update_nonce(Blockchain* bc, const uint8_t address[20], uint64_t nonce) {
    if (!bc) return;
    
    int idx = find_ledger_entry(bc, address);
    if (idx < 0) {
        idx = create_ledger_entry(bc, address);
        if (idx < 0) return;
    }
    
    if (nonce > bc->ledger[idx].nonce) {
        bc->ledger[idx].nonce = nonce;
    }
}

void blockchain_credit_address(Blockchain* bc, const uint8_t address[20], uint64_t amount) {
    if (!bc || amount == 0) return;
    blockchain_update_balance(bc, address, (int64_t)amount);
}

// =============================================================================
// BLOCK TRANSACTION PROCESSING
// =============================================================================

bool blockchain_process_block(Blockchain* bc, const Block* block) {
    if (!bc || !block) return false;
    
    uint64_t total_fees_collected = 0;
    uint64_t coinbase_amount = 0;
    char farmer_addr[41] = {0};
    uint32_t regular_tx_count = 0;
    uint32_t skipped_tx_count = 0;
    
    for (uint32_t i = 0; i < block->header.transaction_count; i++) {
        Transaction* tx = block->transactions[i];
        if (!tx) continue;
        
        if (TX_IS_COINBASE(tx)) {
            // Coinbase: credit destination with mining_reward + fees
            blockchain_update_balance(bc, tx->dest_address, tx->value);
            coinbase_amount = tx->value;
            bytes_to_hex_buf(tx->dest_address, 20, farmer_addr);
            
            LOG_INFO("💎 COINBASE: +%lu coins → %.16s...", tx->value, farmer_addr);
        } else {
            regular_tx_count++;
            
            // Regular transaction: verify nonce, CHECK BALANCE, debit source, credit dest
            uint64_t expected_nonce = blockchain_get_nonce(bc, tx->source_address);
            if (tx->nonce < expected_nonce) {
                LOG_WARN("Transaction nonce too low: %lu < %lu", tx->nonce, expected_nonce);
            }
            
            // BALANCE CHECK: reject if sender cannot afford value + fee
            // Without this check, coins are created from thin air when a sender
            // has 0 balance — the deduction silently underflows to 0 but the
            // receiver and miner are still credited.
            uint64_t sender_balance = blockchain_get_balance(bc, tx->source_address);
            uint64_t required = tx->value + tx->fee;
            if (sender_balance < required) {
                char addr_hex[41];
                bytes_to_hex_buf(tx->source_address, 20, addr_hex);
                LOG_WARN("⚠️  TX rejected: insufficient balance. %.16s... has %lu, needs %lu",
                         addr_hex, sender_balance, required);
                skipped_tx_count++;
                continue;  // Skip this TX entirely — don't debit, don't credit
            }
            
            // Update nonce
            blockchain_update_nonce(bc, tx->source_address, tx->nonce + 1);
            
            // Transfer value + fee from source
            blockchain_update_balance(bc, tx->source_address, -(int64_t)(tx->value + tx->fee));
            
            // Credit destination (only value, not fee)
            blockchain_update_balance(bc, tx->dest_address, tx->value);
            
            // Track fees for logging
            total_fees_collected += tx->fee;
        }
    }
    
    // Log comprehensive summary for blocks with transactions
    if (block->header.transaction_count > 0) {
        LOG_INFO("📊 Block #%u processed:", block->header.height);
        if (coinbase_amount > 0) {
            uint64_t base_reward = coinbase_amount > total_fees_collected ? 
                                   coinbase_amount - total_fees_collected : coinbase_amount;
            LOG_INFO("   ├─ 💰 Mining reward: %lu coins (base) + %lu (fees) = %lu total",
                     base_reward, total_fees_collected, coinbase_amount);
            LOG_INFO("   ├─ 👨‍🌾 Miner address: %.16s...", farmer_addr);
        }
        if (regular_tx_count > 0) {
            LOG_INFO("   ├─ 📦 Transactions: %u processed, %u skipped (insufficient funds)", 
                     regular_tx_count, skipped_tx_count);
            LOG_INFO("   └─ 💸 Total fees deducted: %lu coins", total_fees_collected);
        } else {
            LOG_INFO("   └─ 📦 No user transactions in block");
        }
    }
    
    return true;
}

// =============================================================================
// CHAIN VERIFICATION
// =============================================================================

bool blockchain_verify(const Blockchain* bc) {
    if (!bc || bc->height == 0) return false;
    
    for (uint64_t i = 1; i < bc->height; i++) {
        if (!block_verify(bc->blocks[i], bc->blocks[i-1])) {
            LOG_ERROR("Chain verification failed at block %lu", i);
            return false;
        }
    }
    
    return true;
}

// =============================================================================
// SERIALIZATION
// =============================================================================

bool blockchain_save_pb(const Blockchain* bc, const char* filepath) {
    if (!bc || !filepath) return false;
    
    FILE* f = fopen(filepath, "wb");
    if (!f) return false;
    
    // Write height
    fwrite(&bc->height, sizeof(uint64_t), 1, f);
    fwrite(bc->last_hash, 32, 1, f);
    
    // Write each block
    for (uint64_t i = 0; i < bc->height; i++) {
        size_t block_len;
        uint8_t* block_data = block_serialize_pb(bc->blocks[i], &block_len);
        if (block_data) {
            fwrite(&block_len, sizeof(size_t), 1, f);
            fwrite(block_data, 1, block_len, f);
            free(block_data);
        }
    }
    
    // Write ledger
    fwrite(&bc->ledger_count, sizeof(uint32_t), 1, f);
    for (uint32_t i = 0; i < bc->ledger_count; i++) {
        fwrite(bc->ledger[i].address, 20, 1, f);
        fwrite(&bc->ledger[i].balance, sizeof(uint64_t), 1, f);
        fwrite(&bc->ledger[i].nonce, sizeof(uint64_t), 1, f);
    }
    
    fclose(f);
    LOG_INFO("💾 Blockchain saved to %s", filepath);
    return true;
}

Blockchain* blockchain_load_pb(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    
    Blockchain* bc = safe_malloc(sizeof(Blockchain));
    memset(bc, 0, sizeof(Blockchain));
    bc->blocks = safe_malloc(MAX_BLOCKS * sizeof(Block*));
    memset(bc->blocks, 0, MAX_BLOCKS * sizeof(Block*));
    
    // Read height
    fread(&bc->height, sizeof(uint64_t), 1, f);
    fread(bc->last_hash, 32, 1, f);
    
    // Read blocks
    for (uint64_t i = 0; i < bc->height; i++) {
        size_t block_len;
        fread(&block_len, sizeof(size_t), 1, f);
        
        uint8_t* block_data = safe_malloc(block_len);
        fread(block_data, 1, block_len, f);
        
        bc->blocks[i] = block_deserialize_pb(block_data, block_len);
        free(block_data);
    }
    
    // Read ledger
    fread(&bc->ledger_count, sizeof(uint32_t), 1, f);
    for (uint32_t i = 0; i < bc->ledger_count; i++) {
        fread(bc->ledger[i].address, 20, 1, f);
        fread(&bc->ledger[i].balance, sizeof(uint64_t), 1, f);
        fread(&bc->ledger[i].nonce, sizeof(uint64_t), 1, f);
    }
    
    fclose(f);
    LOG_INFO("📂 Blockchain loaded from %s (%lu blocks)", filepath, bc->height);
    return bc;
}

void blockchain_destroy(Blockchain* bc) {
    if (!bc) return;
    
    if (bc->blocks) {
        for (uint64_t i = 0; i < bc->height; i++) {
            if (bc->blocks[i]) {
                block_destroy(bc->blocks[i]);
            }
        }
        free(bc->blocks);
    }
    
    free(bc);
}

// =============================================================================
// UTILITIES
// =============================================================================

void blockchain_print_summary(const Blockchain* bc) {
    if (!bc) return;
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                   BLOCKCHAIN SUMMARY                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Height:         %10lu blocks                            ║\n", bc->height);
    printf("║  Accounts:       %10u                                   ║\n", bc->ledger_count);
    
    char hash_hex[65];
    bytes_to_hex_buf(bc->last_hash, 32, hash_hex);
    printf("║  Last Hash:      %.32s...  ║\n", hash_hex);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

void blockchain_print_ledger(const Blockchain* bc) {
    if (!bc) return;
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                        LEDGER                                ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (uint32_t i = 0; i < bc->ledger_count && i < 20; i++) {
        char addr_hex[41];
        address_to_hex(bc->ledger[i].address, addr_hex);
        printf("║  %.16s... %12lu coins (nonce: %lu)       ║\n", 
               addr_hex, bc->ledger[i].balance, bc->ledger[i].nonce);
    }
    
    if (bc->ledger_count > 20) {
        printf("║  ... and %u more accounts                                   ║\n", 
               bc->ledger_count - 20);
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}
