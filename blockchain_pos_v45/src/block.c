#include "../include/block.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// BLOCK CREATION
// =============================================================================

Block* block_create(void) {
    Block* block = safe_malloc(sizeof(Block));
    memset(block, 0, sizeof(Block));
    
    block->transactions = safe_malloc(MAX_TRANSACTIONS_PER_BLOCK * sizeof(Transaction*));
    memset(block->transactions, 0, MAX_TRANSACTIONS_PER_BLOCK * sizeof(Transaction*));
    
    return block;
}

Block* block_create_genesis(void) {
    Block* block = block_create();
    
    memset(block->header.previous_hash, 0, 32);
    block->header.height = 0;
    block->header.timestamp = get_current_timestamp();
    block->header.difficulty = DIFFICULTY_DEFAULT;
    block->header.transaction_count = 0;
    
    block_calculate_hash(block);
    
    LOG_INFO("Genesis block created");
    
    return block;
}

// =============================================================================
// TRANSACTION MANAGEMENT
// =============================================================================

bool block_add_transaction(Block* block, Transaction* tx) {
    if (!block || !tx) return false;
    if (block->header.transaction_count >= MAX_TRANSACTIONS_PER_BLOCK) return false;
    
    Transaction* tx_copy = safe_malloc(sizeof(Transaction));
    memcpy(tx_copy, tx, sizeof(Transaction));
    
    block->transactions[block->header.transaction_count++] = tx_copy;
    
    return true;
}

uint64_t block_calculate_fees(const Block* block) {
    if (!block) return 0;
    
    uint64_t total_fees = 0;
    
    // Skip coinbase (index 0), sum fees from all other transactions
    for (uint32_t i = 1; i < block->header.transaction_count; i++) {
        if (block->transactions[i]) {
            total_fees += block->transactions[i]->fee;
        }
    }
    
    return total_fees;
}

// =============================================================================
// PROOF OF SPACE
// =============================================================================

void block_set_proof(Block* block, const SpaceProof* proof, 
                     const uint8_t farmer_address[20]) {
    if (!block || !proof) return;
    
    memcpy(block->header.farmer_address, farmer_address, 20);
    memcpy(block->header.proof_hash, proof->proof_hash, 28);
    memcpy(block->header.quality, proof->quality, 32);
    block->header.proof_nonce = proof->nonce;
}

// =============================================================================
// HASH CALCULATION
// =============================================================================

void block_calculate_hash(Block* block) {
    if (!block) return;
    
    uint8_t buffer[512];
    size_t offset = 0;
    
    memcpy(buffer + offset, block->header.previous_hash, 32); offset += 32;
    memcpy(buffer + offset, &block->header.height, 4); offset += 4;
    memcpy(buffer + offset, &block->header.timestamp, 8); offset += 8;
    memcpy(buffer + offset, block->header.farmer_address, 20); offset += 20;
    memcpy(buffer + offset, &block->header.difficulty, 4); offset += 4;
    memcpy(buffer + offset, block->header.challenge_hash, 32); offset += 32;
    memcpy(buffer + offset, block->header.proof_hash, 28); offset += 28;
    memcpy(buffer + offset, &block->header.proof_nonce, 4); offset += 4;
    memcpy(buffer + offset, block->header.quality, 32); offset += 32;
    memcpy(buffer + offset, &block->header.transaction_count, 4); offset += 4;
    
    // Include transaction hashes
    for (uint32_t i = 0; i < block->header.transaction_count && i < MAX_TRANSACTIONS_PER_BLOCK; i++) {
        if (block->transactions[i] && offset + TX_HASH_SIZE < sizeof(buffer)) {
            uint8_t tx_hash[TX_HASH_SIZE];
            transaction_compute_hash(block->transactions[i], tx_hash);
            memcpy(buffer + offset, tx_hash, TX_HASH_SIZE);
            offset += TX_HASH_SIZE;
        }
    }
    
    sha256(buffer, offset, block->header.hash);
}

// =============================================================================
// VERIFICATION
// =============================================================================

bool block_verify(const Block* block, const Block* prev_block) {
    if (!block) {
        LOG_ERROR("Block verification failed: NULL block");
        return false;
    }
    
    if (prev_block) {
        if (memcmp(block->header.previous_hash, prev_block->header.hash, 32) != 0) {
            char exp_hex[65], got_hex[65];
            bytes_to_hex_buf(prev_block->header.hash, 32, exp_hex);
            bytes_to_hex_buf(block->header.previous_hash, 32, got_hex);
            LOG_ERROR("Block #%u verification failed: previous hash mismatch", block->header.height);
            LOG_ERROR("   Expected: %.32s...", exp_hex);
            LOG_ERROR("   Got:      %.32s...", got_hex);
            return false;
        }
        
        if (block->header.height != prev_block->header.height + 1) {
            LOG_ERROR("Block verification failed: height mismatch");
            LOG_ERROR("   Expected: %u, Got: %u", prev_block->header.height + 1, block->header.height);
            return false;
        }
    } else {
        if (block->header.height != 0) {
            LOG_ERROR("Block verification failed: non-zero genesis height (got %u)", block->header.height);
            return false;
        }
    }
    
    // Verify block hash - create temp with proper initialization
    Block temp;
    memset(&temp, 0, sizeof(Block));
    memcpy(&temp.header, &block->header, sizeof(BlockHeader));
    temp.transactions = block->transactions;
    
    // Save original hash before recalculating
    uint8_t original_hash[32];
    memcpy(original_hash, block->header.hash, 32);
    
    block_calculate_hash(&temp);
    
    if (memcmp(temp.header.hash, original_hash, 32) != 0) {
        char exp_hex[65], got_hex[65];
        bytes_to_hex_buf(original_hash, 32, exp_hex);
        bytes_to_hex_buf(temp.header.hash, 32, got_hex);
        LOG_ERROR("Block #%u verification failed: hash mismatch", block->header.height);
        LOG_ERROR("   Original:     %.32s...", exp_hex);
        LOG_ERROR("   Recalculated: %.32s...", got_hex);
        LOG_ERROR("   TX count: %u", block->header.transaction_count);
        return false;
    }
    
    return true;
}

bool block_has_valid_proof(const Block* block) {
    if (!block) return false;
    return !is_zero(block->header.proof_hash, 28);
}

// =============================================================================
// PROTOBUF SERIALIZATION
// =============================================================================

uint8_t* block_serialize_pb(const Block* block, size_t* out_len) {
    if (!block) return NULL;
    
    Blockchain__Block pb_block = BLOCKCHAIN__BLOCK__INIT;
    Blockchain__BlockHeader pb_header = BLOCKCHAIN__BLOCK_HEADER__INIT;
    
    // Set header fields (zero-copy: point directly into block struct)
    pb_header.previous_hash.len = 32;
    pb_header.previous_hash.data = (uint8_t*)block->header.previous_hash;
    
    pb_header.hash.len = 32;
    pb_header.hash.data = (uint8_t*)block->header.hash;
    
    pb_header.height = block->header.height;
    pb_header.timestamp = block->header.timestamp;
    
    pb_header.farmer_address.len = 20;
    pb_header.farmer_address.data = (uint8_t*)block->header.farmer_address;
    
    pb_header.difficulty = block->header.difficulty;
    
    pb_header.challenge_hash.len = 32;
    pb_header.challenge_hash.data = (uint8_t*)block->header.challenge_hash;
    
    pb_header.proof_hash.len = 28;
    pb_header.proof_hash.data = (uint8_t*)block->header.proof_hash;
    
    pb_header.proof_nonce = block->header.proof_nonce;
    
    pb_header.quality.len = 32;
    pb_header.quality.data = (uint8_t*)block->header.quality;
    
    pb_header.transaction_count = block->header.transaction_count;
    
    pb_block.header = &pb_header;
    pb_block.total_fees = block->total_fees;
    
    // Serialize transactions using ZERO-COPY pointers
    // OLD: 30K small mallocs (20B addr × 2 + 48B sig) × 10K TXs = ~50ms overhead
    // NEW: point directly into Transaction struct memory = ~2ms
    uint32_t tc = block->header.transaction_count;
    Blockchain__Transaction** pb_txs = NULL;
    Blockchain__Transaction* pb_tx_array = NULL;  // single contiguous allocation
    
    if (tc > 0) {
        pb_txs = safe_malloc(tc * sizeof(Blockchain__Transaction*));
        pb_tx_array = safe_malloc(tc * sizeof(Blockchain__Transaction));
        pb_block.n_transactions = tc;
        pb_block.transactions = pb_txs;
        
        for (uint32_t i = 0; i < tc; i++) {
            Transaction* tx = block->transactions[i];
            Blockchain__Transaction* pb_tx = &pb_tx_array[i];
            blockchain__transaction__init(pb_tx);
            
            pb_tx->nonce = tx->nonce;
            pb_tx->expiry_block = tx->expiry_block;
            
            // Zero-copy: point directly into packed Transaction struct
            // Transaction is #pragma pack(push, 1) so fields are contiguous
            pb_tx->source_address.len = 20;
            pb_tx->source_address.data = tx->source_address;
            
            pb_tx->dest_address.len = 20;
            pb_tx->dest_address.data = tx->dest_address;
            
            pb_tx->value = tx->value;
            pb_tx->fee = tx->fee;
            
            // Only set signature if non-zero (coinbase has zero sig)
            if (!is_zero(tx->signature, 48)) {
                pb_tx->signature.len = 48;
                pb_tx->signature.data = tx->signature;
            }
            
            pb_txs[i] = pb_tx;
        }
    }
    
    size_t size = blockchain__block__get_packed_size(&pb_block);
    uint8_t* buffer = safe_malloc(size);
    blockchain__block__pack(&pb_block, buffer);
    
    // Cleanup (only 2 allocations total, no per-field frees needed)
    if (pb_txs) free(pb_txs);
    if (pb_tx_array) free(pb_tx_array);
    
    if (out_len) *out_len = size;
    return buffer;
}

Block* block_deserialize_pb(const uint8_t* data, size_t len) {
    if (!data || len == 0) return NULL;
    
    Blockchain__Block* pb_block = blockchain__block__unpack(NULL, len, data);
    if (!pb_block) return NULL;
    
    Block* block = block_create();
    
    if (pb_block->header) {
        Blockchain__BlockHeader* h = pb_block->header;
        
        if (h->previous_hash.data && h->previous_hash.len >= 32)
            memcpy(block->header.previous_hash, h->previous_hash.data, 32);
        if (h->hash.data && h->hash.len >= 32)
            memcpy(block->header.hash, h->hash.data, 32);
        
        block->header.height = h->height;
        block->header.timestamp = h->timestamp;
        
        if (h->farmer_address.data && h->farmer_address.len >= 20)
            memcpy(block->header.farmer_address, h->farmer_address.data, 20);
        
        block->header.difficulty = h->difficulty;
        
        if (h->challenge_hash.data && h->challenge_hash.len >= 32)
            memcpy(block->header.challenge_hash, h->challenge_hash.data, 32);
        if (h->proof_hash.data && h->proof_hash.len >= 28)
            memcpy(block->header.proof_hash, h->proof_hash.data, 28);
        
        block->header.proof_nonce = h->proof_nonce;
        
        if (h->quality.data && h->quality.len >= 32)
            memcpy(block->header.quality, h->quality.data, 32);
        
        block->header.transaction_count = h->transaction_count;
    }
    
    block->total_fees = pb_block->total_fees;
    
    // Deserialize transactions
    for (size_t i = 0; i < pb_block->n_transactions && i < MAX_TRANSACTIONS_PER_BLOCK; i++) {
        Blockchain__Transaction* pb_tx = pb_block->transactions[i];
        
        Transaction* tx = safe_malloc(sizeof(Transaction));
        memset(tx, 0, sizeof(Transaction));
        
        tx->nonce = pb_tx->nonce;
        tx->expiry_block = pb_tx->expiry_block;
        
        if (pb_tx->source_address.data && pb_tx->source_address.len >= 20)
            memcpy(tx->source_address, pb_tx->source_address.data, 20);
        if (pb_tx->dest_address.data && pb_tx->dest_address.len >= 20)
            memcpy(tx->dest_address, pb_tx->dest_address.data, 20);
        
        tx->value = pb_tx->value;
        tx->fee = pb_tx->fee;
        
        if (pb_tx->signature.data && pb_tx->signature.len > 0) {
            size_t sig_len = pb_tx->signature.len < 48 ? pb_tx->signature.len : 48;
            memcpy(tx->signature, pb_tx->signature.data, sig_len);
        }
        
        block->transactions[i] = tx;
    }
    
    blockchain__block__free_unpacked(pb_block, NULL);
    return block;
}

// =============================================================================
// LEGACY SERIALIZATION
// =============================================================================

char* block_serialize(const Block* block) {
    if (!block) return NULL;
    
    size_t header_hex_len = sizeof(BlockHeader) * 2;
    size_t tx_hex_len = block->header.transaction_count * TX_TOTAL_SIZE * 2;
    size_t total_len = header_hex_len + 1 + tx_hex_len + block->header.transaction_count + 10;
    
    char* result = safe_malloc(total_len);
    char* ptr = result;
    
    // Serialize header
    bytes_to_hex_buf((uint8_t*)&block->header, sizeof(BlockHeader), ptr);
    ptr += sizeof(BlockHeader) * 2;
    
    *ptr++ = ':';
    
    ptr += sprintf(ptr, "%u:", block->header.transaction_count);
    
    // Serialize transactions
    for (uint32_t i = 0; i < block->header.transaction_count; i++) {
        if (block->transactions[i]) {
            bytes_to_hex_buf((uint8_t*)block->transactions[i], TX_TOTAL_SIZE, ptr);
            ptr += TX_TOTAL_SIZE * 2;
            *ptr++ = '|';
        }
    }
    
    *ptr = '\0';
    return result;
}

Block* block_deserialize(const char* hex_data) {
    if (!hex_data) return NULL;
    
    Block* block = block_create();
    
    size_t header_hex_len = sizeof(BlockHeader) * 2;
    if (strlen(hex_data) < header_hex_len) {
        block_destroy(block);
        return NULL;
    }
    
    hex_to_bytes_buf(hex_data, (uint8_t*)&block->header, sizeof(BlockHeader));
    
    const char* ptr = hex_data + header_hex_len;
    if (*ptr != ':') {
        block_destroy(block);
        return NULL;
    }
    ptr++;
    
    uint32_t tx_count = 0;
    ptr += sscanf(ptr, "%u:", &tx_count);
    while (*ptr && *ptr != ':') ptr++;
    if (*ptr == ':') ptr++;
    
    // Deserialize transactions
    for (uint32_t i = 0; i < tx_count && *ptr; i++) {
        if (strlen(ptr) < TX_TOTAL_SIZE * 2) break;
        
        Transaction* tx = safe_malloc(sizeof(Transaction));
        hex_to_bytes_buf(ptr, (uint8_t*)tx, TX_TOTAL_SIZE);
        block->transactions[i] = tx;
        
        ptr += TX_TOTAL_SIZE * 2;
        if (*ptr == '|') ptr++;
    }
    
    return block;
}

// =============================================================================
// UTILITIES
// =============================================================================

Transaction* block_get_coinbase(const Block* block) {
    if (!block || block->header.transaction_count == 0) return NULL;
    return block->transactions[0];
}

void block_print(const Block* block) {
    if (!block) return;
    
    char hash_hex[65], prev_hex[65], farmer_hex[41];
    bytes_to_hex_buf(block->header.hash, 32, hash_hex);
    bytes_to_hex_buf(block->header.previous_hash, 32, prev_hex);
    address_to_hex(block->header.farmer_address, farmer_hex);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  Block #%-6u                                               ║\n", block->header.height);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Hash:       %.32s...  ║\n", hash_hex);
    printf("║  Prev Hash:  %.32s...  ║\n", prev_hex);
    printf("║  Farmer:     %.32s...  ║\n", farmer_hex);
    printf("║  Difficulty: %-10u (%u zero bits required)              ║\n", 
           block->header.difficulty, block->header.difficulty);
    printf("║  Tx Count:   %-10u                                       ║\n", block->header.transaction_count);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

// =============================================================================
// CLEANUP
// =============================================================================

void block_destroy(Block* block) {
    if (!block) return;
    
    if (block->transactions) {
        for (uint32_t i = 0; i < block->header.transaction_count; i++) {
            if (block->transactions[i]) {
                free(block->transactions[i]);
            }
        }
        free(block->transactions);
    }
    
    free(block);
}
