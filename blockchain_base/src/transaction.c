/**
 * ============================================================================
 * TRANSACTION.C - Ethereum 2.0 Style Transaction with BLS Signatures
 * ============================================================================
 * 
 * v26 - Now uses BLS signatures (48 bytes) like Ethereum 2.0
 * 
 * TRANSACTION STRUCTURE (128 bytes total):
 * ========================================
 * ┌────────────────────────────────────────────────────────────────────────┐
 * │ Field          │ Size    │ Description                                 │
 * ├────────────────┼─────────┼─────────────────────────────────────────────┤
 * │ nonce          │ 8 bytes │ Unique per sender (prevents replay attacks) │
 * │ expiry_block   │ 4 bytes │ Block height after which tx expires         │
 * │ source_address │ 20 bytes│ Sender's address (RIPEMD160(SHA256(pubkey)))│
 * │ dest_address   │ 20 bytes│ Recipient's address                         │
 * │ value          │ 8 bytes │ Amount to transfer                          │
 * │ fee            │ 4 bytes │ Transaction fee for miner                   │
 * │ signature      │ 48 bytes│ BLS signature (BLS12-381, like Eth2.0)      │
 * └────────────────┴─────────┴─────────────────────────────────────────────┘
 * 
 * BLS SIGNATURES (Boneh-Lynn-Shacham):
 * ====================================
 * - Uses BLS12-381 curve (same as Ethereum 2.0)
 * - Produces 48-byte compressed G1 point signatures
 * - Supports signature aggregation (multiple sigs → 1 sig)
 * - Verification: e(sig, g2) == e(H(m), pk)
 * 
 * Benefits over ECDSA:
 * - Signature aggregation reduces block size
 * - Constant verification time regardless of signers
 * - Simpler multi-signature schemes
 * 
 * ============================================================================
 */

#include "../include/transaction.h"
#include "../include/wallet.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// COINBASE ADDRESS CONSTANT
// =============================================================================

// "COINBASE" in first 8 bytes, rest zeros
const uint8_t COINBASE_ADDRESS[20] = {
    'C', 'O', 'I', 'N', 'B', 'A', 'S', 'E',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// =============================================================================
// TRANSACTION CREATION
// =============================================================================

Transaction* transaction_create(const Wallet* wallet, 
                                const uint8_t dest_address[20],
                                uint64_t value, 
                                uint32_t fee,
                                uint64_t nonce,
                                uint32_t expiry_block) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));
    
    // Set nonce and expiry
    tx->nonce = nonce;
    tx->expiry_block = expiry_block;
    
    // Copy addresses
    memcpy(tx->source_address, wallet->address, 20);
    memcpy(tx->dest_address, dest_address, 20);
    
    // Set values
    tx->value = value;
    tx->fee = fee;
    
    // Sign transaction
    transaction_sign(tx, wallet);
    
    return tx;
}

Transaction* transaction_create_coinbase(const uint8_t farmer_address[20], 
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));
    
    // Coinbase uses block height as nonce (deterministic)
    tx->nonce = (uint64_t)block_height;
    tx->expiry_block = 0;  // No expiry for coinbase
    
    // Coinbase source
    memcpy(tx->source_address, COINBASE_ADDRESS, 20);
    
    // Farmer receives reward + fees
    memcpy(tx->dest_address, farmer_address, 20);
    
    // Value = base reward + collected fees
    tx->value = base_reward + total_fees;
    tx->fee = 0;  // Coinbase has no fee
    
    // Signature left as zeros for coinbase
    
    return tx;
}

// =============================================================================
// TRANSACTION HASH COMPUTATION
// =============================================================================

void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]) {
    // Hash: nonce || expiry_block || source || dest || value || fee
    // (signature is NOT included - it commits to everything else)
    uint8_t buffer[8 + 4 + 20 + 20 + 8 + 4];  // 64 bytes
    size_t offset = 0;
    
    memcpy(buffer + offset, &tx->nonce, 8); offset += 8;
    memcpy(buffer + offset, &tx->expiry_block, 4); offset += 4;
    memcpy(buffer + offset, tx->source_address, 20); offset += 20;
    memcpy(buffer + offset, tx->dest_address, 20); offset += 20;
    memcpy(buffer + offset, &tx->value, 8); offset += 8;
    memcpy(buffer + offset, &tx->fee, 4); offset += 4;
    
    // Use BLAKE3 for transaction hash (truncated to 28 bytes)
    blake3_hash_truncated(buffer, sizeof(buffer), hash, TX_HASH_SIZE);
}

char* transaction_get_hash_hex(const Transaction* tx) {
    uint8_t hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, hash);
    return bytes_to_hex(hash, TX_HASH_SIZE);
}

// =============================================================================
// BLS-STYLE SIGNING AND VERIFICATION (48-byte signatures like Ethereum 2.0)
// =============================================================================
// Ed25519 TRANSACTION SIGNING
// =============================================================================
// 
// Ed25519 (Edwards-curve Digital Signature Algorithm):
//   Private key:  32 bytes (seed)
//   Public key:   32 bytes (curve point)
//   Signature:    64 bytes (R || S)
//   Verify time:  ~0.06ms per signature (17x faster than BLS, 1.6x faster than ECDSA)
//
// WHY Ed25519 over BLS:
//   BLS:    ~1-3ms per verify × 10K TXs = 10-30 seconds (even with 8 threads: 1.25-3.75s)
//   ECDSA:  ~0.1ms per verify × 10K TXs = 1s (with 8 threads: 125ms)  
//   Ed25519: ~0.06ms per verify × 10K TXs = 600ms (with 8 threads: 75ms)
//
// WHY Ed25519 over ECDSA:
//   - Deterministic: same input → same signature (no random nonce attacks)
//   - Faster: ~40% faster verification than secp256k1 ECDSA
//   - Simpler: no recovery ID needed (unlike Ethereum's v,r,s)
//   - Safe: no weak-nonce vulnerability (Sony PS3 attack impossible)
//   - 64-byte signatures fit perfectly in our 128-byte TX structure
//
// The message being signed is the TX hash (BLAKE3 of all fields except signature).
// The validator verifies by:
//   1. Recomputing TX hash from the fields
//   2. Checking Ed25519_verify(pubkey, tx_hash, signature) == true
//   3. Checking hash160(pubkey) == source_address (proves pubkey ownership)
// =============================================================================

bool transaction_sign(Transaction* tx, const Wallet* wallet) {
    if (!tx || !wallet) return false;
    
    // Compute transaction hash (what we're signing)
    uint8_t tx_hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, tx_hash);
    
    // Sign the tx_hash with wallet's Ed25519 key
    return wallet_sign(wallet, tx_hash, TX_HASH_SIZE, tx->signature);
}

bool transaction_verify(const Transaction* tx) {
    // Backward-compatible verify: basic sanity check only
    // For full Ed25519 verification, use transaction_verify_ed25519() with pubkey
    if (TX_IS_COINBASE(tx)) return true;
    if (is_zero(tx->signature, 64)) return false;
    return true;
}

bool transaction_verify_ed25519(const Transaction* tx, const uint8_t pubkey[32]) {
    if (!tx || !pubkey) return false;
    if (TX_IS_COINBASE(tx)) return true;
    if (is_zero(tx->signature, 64)) return false;
    
    // Step 1: Verify pubkey → address derivation
    uint8_t derived_address[20];
    hash160(pubkey, 32, derived_address);
    if (memcmp(derived_address, tx->source_address, 20) != 0) {
        return false;  // Pubkey doesn't match source address
    }
    
    // Step 2: Recompute TX hash from fields
    uint8_t tx_hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, tx_hash);
    
    // Step 3: Verify Ed25519 signature
    return wallet_verify(pubkey, tx_hash, TX_HASH_SIZE, tx->signature);
}

bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height) {
    if (tx->expiry_block == 0) return false;  // No expiry
    return current_block_height > tx->expiry_block;
}

// =============================================================================
// PROTOBUF SERIALIZATION
// =============================================================================

uint8_t* transaction_serialize_pb(const Transaction* tx, size_t* out_len) {
    if (!tx) return NULL;
    
    Blockchain__Transaction pb_tx = BLOCKCHAIN__TRANSACTION__INIT;
    
    pb_tx.nonce = tx->nonce;
    pb_tx.expiry_block = tx->expiry_block;
    
    pb_tx.source_address.len = 20;
    pb_tx.source_address.data = (uint8_t*)tx->source_address;
    
    pb_tx.dest_address.len = 20;
    pb_tx.dest_address.data = (uint8_t*)tx->dest_address;
    
    pb_tx.value = tx->value;
    pb_tx.fee = tx->fee;
    
    // Handle signature
    if (!is_zero(tx->signature, 64)) {
        pb_tx.signature.len = 64;
        pb_tx.signature.data = (uint8_t*)tx->signature;
    } else {
        pb_tx.signature.len = 0;
        pb_tx.signature.data = NULL;
    }
    
    // Calculate packed size
    size_t size = blockchain__transaction__get_packed_size(&pb_tx);
    uint8_t* buffer = safe_malloc(size);
    
    // Pack
    blockchain__transaction__pack(&pb_tx, buffer);
    
    if (out_len) *out_len = size;
    return buffer;
}

Transaction* transaction_deserialize_pb(const uint8_t* data, size_t len) {
    if (!data || len == 0) return NULL;
    
    Blockchain__Transaction* pb_tx = blockchain__transaction__unpack(NULL, len, data);
    if (!pb_tx) return NULL;
    
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));
    
    tx->nonce = pb_tx->nonce;
    tx->expiry_block = pb_tx->expiry_block;
    
    if (pb_tx->source_address.data && pb_tx->source_address.len >= 20) {
        memcpy(tx->source_address, pb_tx->source_address.data, 20);
    }
    
    if (pb_tx->dest_address.data && pb_tx->dest_address.len >= 20) {
        memcpy(tx->dest_address, pb_tx->dest_address.data, 20);
    }
    
    tx->value = pb_tx->value;
    tx->fee = pb_tx->fee;
    
    if (pb_tx->signature.data && pb_tx->signature.len > 0) {
        size_t sig_len = pb_tx->signature.len < 64 ? pb_tx->signature.len : 64;
        memcpy(tx->signature, pb_tx->signature.data, sig_len);
    }
    
    blockchain__transaction__free_unpacked(pb_tx, NULL);
    return tx;
}

// =============================================================================
// LEGACY SERIALIZATION (hex string for backward compatibility)
// =============================================================================

char* transaction_serialize(const Transaction* tx) {
    // Serialize entire 128-byte struct to hex
    return bytes_to_hex((const uint8_t*)tx, sizeof(Transaction));
}

Transaction* transaction_deserialize(const char* hex_data) {
    if (strlen(hex_data) != sizeof(Transaction) * 2) {  // 128 bytes * 2 = 256 hex chars
        return NULL;
    }
    
    Transaction* tx = safe_malloc(sizeof(Transaction));
    
    if (!hex_to_bytes_buf(hex_data, (uint8_t*)tx, sizeof(Transaction))) {
        free(tx);
        return NULL;
    }
    
    return tx;
}

void transaction_destroy(Transaction* tx) {
    if (tx) {
        secure_free(tx, sizeof(Transaction));
    }
}

// Address utilities moved to wallet.c
