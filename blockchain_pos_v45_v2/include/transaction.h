#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Forward declarations
typedef struct Wallet Wallet;

// =============================================================================
// COMPACT TRANSACTION STRUCTURE - 128 bytes total
// =============================================================================
// Layout (Ethereum-inspired with Ed25519 signatures):
//   nonce              = 8 bytes  - Unique nonce per sender (like Ethereum)
//   expiry_block       = 4 bytes  - Block height after which tx expires (0 = no expiry)
//   source_address[20] = 20 bytes - BLAKE3(Ed25519_pubkey)[0:20]
//   dest_address[20]   = 20 bytes - Recipient address
//   value              = 8 bytes  - Amount in smallest units (like satoshis)
//   fee                = 4 bytes  - Fee in smallest units (max ~4 billion)
//   signature[64]      = 64 bytes - Ed25519 signature
//   TOTAL              = 8 + 4 + 20 + 20 + 8 + 4 + 64 = 128 bytes
// =============================================================================
// WHY Ed25519 (not BLS, not ECDSA):
//   Ed25519 verify: ~0.06ms   BLS verify: ~1-3ms   ECDSA verify: ~0.1ms
//   Ed25519 sig:    64 bytes  BLS sig:    48 bytes  ECDSA sig:   64-72 bytes
//   Ed25519 gives us the fastest verification (17-50x faster than BLS)
//   with compact 64-byte signatures that fit our 128-byte TX goal.
//   For 10K TXs with 8 OpenMP threads: ~75ms total (vs 1.5s for BLS!)
//
// SIGNATURE SCHEME:
//   Private key:  32 bytes (Ed25519 seed, derived from wallet secret)
//   Public key:   32 bytes (Ed25519 pubkey, stored in wallet + pool)
//   Address:      hash160(pubkey) = 20 bytes (on-chain identifier)
//   Signature:    Ed25519(privkey, BLAKE3(tx_fields)) = 64 bytes
//   Verification: Ed25519_verify(pubkey, BLAKE3(tx_fields), sig)
//
// PUBLIC KEY STORAGE (SegWit-inspired separation):
//   On-chain TX:  128 bytes (compact, no pubkey - saves 32B per TX per node)
//   Network msg:  128 + 32 = 160 bytes (protobuf includes pubkey field)
//   Pool stores:  pubkey alongside each TX (for validator verification)
//   This is the same idea as Bitcoin SegWit: witness data (signatures/pubkeys)
//   are separated from the compact transaction for storage efficiency.
// =============================================================================
// Note: Transaction ID is computed on-the-fly when needed as:
//       tx_id = BLAKE3(nonce || expiry_block || source || dest || value || fee)
// =============================================================================

#pragma pack(push, 1)

typedef struct {
    uint64_t nonce;              // 8 bytes:  Unique nonce per sender
    uint32_t expiry_block;       // 4 bytes:  Block height for expiry (0 = no expiry)
    uint8_t source_address[20];  // 20 bytes: Sender address (binary)
    uint8_t dest_address[20];    // 20 bytes: Recipient address (binary)
    uint64_t value;              // 8 bytes:  Amount in smallest units
    uint32_t fee;                // 4 bytes:  Fee in smallest units
    uint8_t signature[64];       // 64 bytes: Ed25519 signature
} Transaction;                   // TOTAL: 128 bytes EXACTLY

#pragma pack(pop)

// Compile-time size check - CRITICAL!
_Static_assert(sizeof(Transaction) == 128, "Transaction must be exactly 128 bytes");

// =============================================================================
// CONSTANTS
// =============================================================================

#define TX_NONCE_LENGTH      8
#define TX_ADDRESS_LENGTH    20

// =============================================================================
// EXPIRY CONSTANTS
// =============================================================================
// Expiry is specified in blocks. With 6-second block time:
//   100 blocks  = ~10 minutes
//   300 blocks  = ~30 minutes
//   600 blocks  = ~1 hour
//   1000 blocks = ~1.6 hours

#define TX_NO_EXPIRY              0     // Never expires (use with caution!)
#define TX_DEFAULT_EXPIRY_BLOCKS  100   // Default: ~10 minutes (recommended)
#define TX_SHORT_EXPIRY_BLOCKS    50    // Short: ~5 minutes (time-sensitive)
#define TX_LONG_EXPIRY_BLOCKS     600   // Long: ~1 hour (congested network)
#define TX_MAX_EXPIRY_BLOCKS      10000 // Max: ~16 hours

// Helper macro to calculate expiry from current block
#define TX_EXPIRY_FROM_NOW(current_block, blocks) ((current_block) + (blocks))
#define TX_SIGNATURE_LENGTH  64
#define TX_TOTAL_SIZE        128
#define TX_HASH_SIZE         32    // Full BLAKE3 hash (256-bit security)

// Special coinbase marker (first 8 bytes = "COINBASE", rest = 0)
extern const uint8_t COINBASE_ADDRESS[20];

// =============================================================================
// MACROS
// =============================================================================

#define TX_IS_COINBASE(tx) (memcmp((tx)->source_address, COINBASE_ADDRESS, 20) == 0)
#define TX_NO_EXPIRY       0

// =============================================================================
// TRANSACTION FUNCTIONS
// =============================================================================

// Create transaction with nonce
Transaction* transaction_create(const Wallet* wallet, 
                                const uint8_t dest_address[20],
                                uint64_t value, 
                                uint32_t fee,
                                uint64_t nonce,
                                uint32_t expiry_block);

// Create coinbase (reward + all fees from block)
Transaction* transaction_create_coinbase(const uint8_t farmer_address[20], 
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height);

// Compute transaction hash (ID) - returns 28 bytes
// Hash = BLAKE3(nonce || expiry_block || source || dest || value || fee)[0:28]
void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]);

// Get transaction hash as hex string (caller must free)
char* transaction_get_hash_hex(const Transaction* tx);

// Verify transaction Ed25519 signature (needs sender's public key)
// pubkey can be NULL for backward compat (falls back to deterministic check)
bool transaction_verify(const Transaction* tx);
bool transaction_verify_ed25519(const Transaction* tx, const uint8_t pubkey[32]);

// Sign transaction
bool transaction_sign(Transaction* tx, const Wallet* wallet);

// Check if transaction is expired at given block height
bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height);

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

// Serialize transaction to protobuf binary (caller must free, returns size)
uint8_t* transaction_serialize_pb(const Transaction* tx, size_t* out_len);

// Deserialize transaction from protobuf binary
Transaction* transaction_deserialize_pb(const uint8_t* data, size_t len);

// Serialize to hex string (for legacy compatibility)
char* transaction_serialize(const Transaction* tx);

// Deserialize from hex string (for legacy compatibility)
Transaction* transaction_deserialize(const char* hex_data);

// Free transaction
void transaction_destroy(Transaction* tx);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

// Public key -> 20-byte address
void pubkey_to_address(const uint8_t* pubkey, size_t len, uint8_t address[20]);

// Address -> hex string (40 chars)
void address_to_hex(const uint8_t address[20], char hex[41]);

// Hex string -> address
bool hex_to_address(const char* hex, uint8_t address[20]);

// Transaction hash -> hex string (56 chars)
void txhash_to_hex(const uint8_t hash[TX_HASH_SIZE], char hex[TX_HASH_SIZE * 2 + 1]);

// Check if address is valid
bool address_is_valid(const uint8_t address[20]);

// Compare addresses
bool address_equals(const uint8_t a[20], const uint8_t b[20]);

// Convert wallet name to address (for demo: hash of name)
void wallet_name_to_address(const char* name, uint8_t address[20]);

#endif // TRANSACTION_H
