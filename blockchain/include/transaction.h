#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "crypto_backend.h"

// Forward declarations
typedef struct Wallet Wallet;

// =============================================================================
// TRANSACTION STRUCTURE - variable size depending on SIG_SCHEME
// =============================================================================
// Core fields (fixed 64 bytes):
//   nonce              = 8 bytes
//   expiry_block       = 4 bytes
//   source_address[20] = 20 bytes
//   dest_address[20]   = 20 bytes
//   value              = 8 bytes
//   fee                = 4 bytes
// Crypto fields (variable):
//   signature          = up to CRYPTO_SIG_MAX bytes + length
//   public_key         = up to CRYPTO_PUBKEY_MAX bytes + length
// =============================================================================

typedef struct {
    uint64_t nonce;                              // 8 bytes
    uint32_t expiry_block;                       // 4 bytes
    uint8_t source_address[20];                  // 20 bytes
    uint8_t dest_address[20];                    // 20 bytes
    uint64_t value;                              // 8 bytes
    uint32_t fee;                                // 4 bytes
    uint8_t signature[CRYPTO_SIG_MAX];           // variable-length sig
    size_t  sig_len;                             // actual sig bytes
    uint8_t public_key[CRYPTO_PUBKEY_MAX];       // signer's pubkey
    size_t  pubkey_len;                          // actual pubkey bytes
    uint8_t sig_type;                            // SIG_ECDSA or SIG_FALCON512
} Transaction;

// =============================================================================
// CONSTANTS
// =============================================================================

#define TX_NONCE_LENGTH      8
#define TX_ADDRESS_LENGTH    20

// =============================================================================
// EXPIRY CONSTANTS
// =============================================================================

#define TX_NO_EXPIRY              0
#define TX_DEFAULT_EXPIRY_BLOCKS  100
#define TX_SHORT_EXPIRY_BLOCKS    50
#define TX_LONG_EXPIRY_BLOCKS     600
#define TX_MAX_EXPIRY_BLOCKS      10000

#define TX_EXPIRY_FROM_NOW(current_block, blocks) ((current_block) + (blocks))
#define TX_SIGNATURE_LENGTH  CRYPTO_SIG_MAX
#define TX_HASH_SIZE         28
#define TX_TOTAL_SIZE        sizeof(Transaction)

// Special coinbase marker (first 8 bytes = "COINBASE", rest = 0)
extern const uint8_t COINBASE_ADDRESS[20];

// =============================================================================
// MACROS
// =============================================================================

#define TX_IS_COINBASE(tx) (memcmp((tx)->source_address, COINBASE_ADDRESS, 20) == 0)

// =============================================================================
// TRANSACTION FUNCTIONS
// =============================================================================

Transaction* transaction_create(const Wallet* wallet,
                                const uint8_t dest_address[20],
                                uint64_t value,
                                uint32_t fee,
                                uint64_t nonce,
                                uint32_t expiry_block);

Transaction* transaction_create_coinbase(const uint8_t farmer_address[20],
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height);

void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]);

char* transaction_get_hash_hex(const Transaction* tx);

bool transaction_verify(const Transaction* tx);

bool transaction_sign(Transaction* tx, const Wallet* wallet);

bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height);

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

uint8_t* transaction_serialize_pb(const Transaction* tx, size_t* out_len);
Transaction* transaction_deserialize_pb(const uint8_t* data, size_t len);

char* transaction_serialize(const Transaction* tx);
Transaction* transaction_deserialize(const char* hex_data);

void transaction_destroy(Transaction* tx);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void pubkey_to_address(const uint8_t* pubkey, size_t len, uint8_t address[20]);
void address_to_hex(const uint8_t address[20], char hex[41]);
bool hex_to_address(const char* hex, uint8_t address[20]);
void txhash_to_hex(const uint8_t hash[TX_HASH_SIZE], char hex[TX_HASH_SIZE * 2 + 1]);
bool address_is_valid(const uint8_t address[20]);
bool address_equals(const uint8_t a[20], const uint8_t b[20]);
void wallet_name_to_address(const char* name, uint8_t address[20]);

#endif // TRANSACTION_H
