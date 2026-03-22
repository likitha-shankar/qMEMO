#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "block.h"
#include "transaction.h"

// =============================================================================
// BLOCKCHAIN STRUCTURE
// =============================================================================

#define MAX_BLOCKS 100000

typedef struct {
    Block** blocks;
    uint64_t height;
    uint8_t last_hash[32];
    
    // Ledger (address -> balance)
    struct {
        uint8_t address[20];
        uint64_t balance;
        uint64_t nonce;        // Transaction nonce for this address
    } ledger[10000];
    uint32_t ledger_count;
} Blockchain;

// =============================================================================
// BLOCKCHAIN FUNCTIONS
// =============================================================================

// Create new blockchain (with genesis block)
Blockchain* blockchain_create(void);

// Add block to chain
bool blockchain_add_block(Blockchain* bc, Block* block);

// Get block by height
Block* blockchain_get_block(const Blockchain* bc, uint64_t height);

// Get last block
Block* blockchain_get_last_block(const Blockchain* bc);

// Get current height
uint64_t blockchain_get_height(const Blockchain* bc);

// Get balance for address
uint64_t blockchain_get_balance(const Blockchain* bc, const uint8_t address[20]);

// Get nonce for address
uint64_t blockchain_get_nonce(const Blockchain* bc, const uint8_t address[20]);

// Update balance for address
void blockchain_update_balance(Blockchain* bc, const uint8_t address[20], 
                               int64_t delta);

// Update nonce for address
void blockchain_update_nonce(Blockchain* bc, const uint8_t address[20], 
                             uint64_t nonce);

// Process block transactions (update ledger)
bool blockchain_process_block(Blockchain* bc, const Block* block);

// Verify entire chain
bool blockchain_verify(const Blockchain* bc);

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

// Save blockchain state to protobuf file
bool blockchain_save_pb(const Blockchain* bc, const char* filepath);

// Load blockchain state from protobuf file
Blockchain* blockchain_load_pb(const char* filepath);

// Free blockchain memory
void blockchain_destroy(Blockchain* bc);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

// Print blockchain summary
void blockchain_print_summary(const Blockchain* bc);

// Print ledger
void blockchain_print_ledger(const Blockchain* bc);

#endif // BLOCKCHAIN_H
