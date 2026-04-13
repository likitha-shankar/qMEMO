#ifndef WALLET_H
#define WALLET_H

#include <stdint.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include "crypto_backend.h"

// =============================================================================
// WALLET STRUCTURE
// =============================================================================
// Supports Ed25519 (scheme 1), Falcon-512 (scheme 2), ML-DSA-44 (scheme 4),
// and Hybrid (scheme 3). sig_type determines which keys are active.
// =============================================================================

typedef struct Wallet {
    char name[64];                          // Wallet name (e.g., "farmer1")
    uint8_t address[20];                    // 20-byte binary address = hash160(pubkey)
    char address_hex[41];                   // Hex representation of address

    // Ed25519 fields (sig_type == SIG_ED25519)
    uint8_t ed25519_pubkey[32];             // Ed25519 public key (32 bytes)
    uint8_t ed25519_seed[32];               // Ed25519 private seed (32 bytes)
    EVP_PKEY* evp_key;                      // OpenSSL Ed25519 key handle

    // PQC fields (sig_type == SIG_FALCON512 or SIG_ML_DSA44)
    uint8_t oqs_seckey[CRYPTO_SECKEY_MAX];  // Falcon/ML-DSA secret key
    size_t  oqs_seckey_len;

    // Common (active key, regardless of scheme)
    uint8_t public_key[CRYPTO_PUBKEY_MAX];  // Active public key bytes
    size_t  pubkey_len;                     // Length of active public key
    uint8_t sig_type;                       // SIG_ED25519 / SIG_FALCON512 / etc.

    uint64_t nonce;                         // Transaction nonce counter
} Wallet;

// =============================================================================
// WALLET FUNCTIONS
// =============================================================================

// Create new wallet with random keys (Ed25519 by default)
Wallet* wallet_create(void);

// Create wallet with specific name + signature scheme
// sig_type: SIG_ED25519=1, SIG_FALCON512=2, SIG_HYBRID=3, SIG_ML_DSA44=4
Wallet* wallet_create_named(const char* name, uint8_t sig_type);

// Load wallet from file
Wallet* wallet_load(const char* filepath);

// Save wallet to file
bool wallet_save(const Wallet* wallet, const char* filepath);

// Get wallet's 20-byte address
const uint8_t* wallet_get_address(const Wallet* wallet);

// Get wallet's address as hex string
const char* wallet_get_address_hex(const Wallet* wallet);

// Get and increment nonce
uint64_t wallet_get_next_nonce(Wallet* wallet);

// Get current nonce without incrementing
uint64_t wallet_get_nonce(const Wallet* wallet);

// Set nonce (for sync with blockchain state)
void wallet_set_nonce(Wallet* wallet, uint64_t nonce);

// Sign message — dispatches by wallet->sig_type, outputs variable-length sig
bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t* sig_out, size_t* sig_len);

// Get wallet's active public key
const uint8_t* wallet_get_pubkey(const Wallet* wallet);

// Verify signature (Ed25519 only, for legacy callers)
bool wallet_verify(const uint8_t* public_key, size_t pubkey_len,
                   const uint8_t* message, size_t msg_len,
                   const uint8_t* signature, size_t sig_len);

// Free wallet memory
void wallet_destroy(Wallet* wallet);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

// Derive 20-byte address from public key
void wallet_derive_address(const uint8_t* pubkey, size_t len, uint8_t address[20]);

// Convert name to deterministic address (for demo/testing)
void wallet_name_to_address(const char* name, uint8_t address[20]);

// Check if name looks like a hex address (40 chars)
bool wallet_is_hex_address(const char* str);

// Parse address from name or hex string
bool wallet_parse_address(const char* str, uint8_t address[20]);

// Convert address to hex string
void address_to_hex(const uint8_t address[20], char hex[41]);

// Convert hex string to address
bool hex_to_address(const char* hex, uint8_t address[20]);

// Convert transaction hash to hex string
void txhash_to_hex(const uint8_t hash[28], char hex[57]);

// Check if address is valid (non-zero)
bool address_is_valid(const uint8_t address[20]);

// Compare two addresses
bool address_equals(const uint8_t a[20], const uint8_t b[20]);

#endif // WALLET_H
