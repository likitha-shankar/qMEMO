#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "transaction.h"
#include "consensus.h"

// =============================================================================
// BLOCK STRUCTURE
// =============================================================================

#define MAX_TRANSACTIONS_PER_BLOCK 70000  // Supports up to 65536 user TXs + coinbase

#pragma pack(push, 1)

typedef struct {
    uint8_t previous_hash[32];      // 32 bytes: Previous block hash
    uint8_t hash[32];               // 32 bytes: This block's hash
    uint32_t height;                // 4 bytes:  Block height
    uint64_t timestamp;             // 8 bytes:  Creation time
    uint8_t farmer_address[20];     // 20 bytes: Block creator
    uint32_t difficulty;            // 4 bytes:  Difficulty when mined (1-256)
    uint8_t challenge_hash[32];     // 32 bytes: Challenge this block answers
    uint8_t proof_hash[28];         // 28 bytes: Winning proof hash
    uint32_t proof_nonce;           // 4 bytes:  Winning proof nonce
    uint8_t quality[32];            // 32 bytes: Proof quality
    uint32_t transaction_count;     // 4 bytes:  Number of transactions
} BlockHeader;                      // TOTAL: 200 bytes (with pack=1)

#pragma pack(pop)

typedef struct {
    BlockHeader header;
    Transaction** transactions;     // Array of transaction pointers
    uint64_t total_fees;            // Sum of all transaction fees
} Block;

// =============================================================================
// BLOCK FUNCTIONS
// =============================================================================

// Create empty block
Block* block_create(void);

// Create genesis block
Block* block_create_genesis(void);

// Add transaction to block
bool block_add_transaction(Block* block, Transaction* tx);

// Calculate total fees in block
uint64_t block_calculate_fees(const Block* block);

// Set proof of space data
void block_set_proof(Block* block, const SpaceProof* proof, 
                     const uint8_t farmer_address[20]);

// Calculate block hash
void block_calculate_hash(Block* block);

// Verify block integrity
bool block_verify(const Block* block, const Block* prev_block);

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

// Serialize block to protobuf binary (caller must free, returns size)
uint8_t* block_serialize_pb(const Block* block, size_t* out_len);

// Deserialize block from protobuf binary
Block* block_deserialize_pb(const uint8_t* data, size_t len);

// Serialize block to hex string (legacy)
char* block_serialize(const Block* block);

// Deserialize block from hex string (legacy)
Block* block_deserialize(const char* hex_data);

// Free block memory
void block_destroy(Block* block);

// =============================================================================
// BLOCK UTILITIES
// =============================================================================

// Get coinbase transaction (first tx in block)
Transaction* block_get_coinbase(const Block* block);

// Check if block has valid proof of space
bool block_has_valid_proof(const Block* block);

// Print block info
void block_print(const Block* block);

#endif // BLOCK_H
