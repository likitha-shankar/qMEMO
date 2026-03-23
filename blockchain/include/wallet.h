#ifndef WALLET_H
#define WALLET_H

#include <stdint.h>
#include <stdbool.h>
#include "crypto_backend.h"

// =============================================================================
// WALLET STRUCTURE
// =============================================================================

typedef struct Wallet {
    char name[64];
    uint8_t address[20];
    char address_hex[41];
    uint8_t public_key[CRYPTO_PUBKEY_MAX];
    size_t  pubkey_len;
    uint8_t secret_key[CRYPTO_SECKEY_MAX];
    size_t  seckey_len;
    crypto_ctx_t *crypto;           // backend context (holds EVP_PKEY for ECDSA)
    uint8_t sig_type;               // SIG_ECDSA or SIG_FALCON512
    uint64_t nonce;
} Wallet;

// =============================================================================
// WALLET FUNCTIONS
// =============================================================================

Wallet* wallet_create(uint8_t sig_type);
Wallet* wallet_create_named(const char* name, uint8_t sig_type);
Wallet* wallet_load(const char* filepath);
bool wallet_save(const Wallet* wallet, const char* filepath);

const uint8_t* wallet_get_address(const Wallet* wallet);
const char* wallet_get_address_hex(const Wallet* wallet);

uint64_t wallet_get_next_nonce(Wallet* wallet);
uint64_t wallet_get_nonce(const Wallet* wallet);
void wallet_set_nonce(Wallet* wallet, uint64_t nonce);

bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t *signature, size_t *sig_len);

bool wallet_verify(const uint8_t *public_key, size_t pk_len,
                   const uint8_t* message, size_t msg_len,
                   const uint8_t *signature, size_t sig_len);

void wallet_destroy(Wallet* wallet);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void wallet_derive_address(const uint8_t* pubkey, size_t len, uint8_t address[20]);
void wallet_name_to_address(const char* name, uint8_t address[20]);
bool wallet_is_hex_address(const char* str);
bool wallet_parse_address(const char* str, uint8_t address[20]);
void address_to_hex(const uint8_t address[20], char hex[41]);
bool hex_to_address(const char* hex, uint8_t address[20]);
void txhash_to_hex(const uint8_t hash[28], char hex[57]);
bool address_is_valid(const uint8_t address[20]);
bool address_equals(const uint8_t a[20], const uint8_t b[20]);

#endif // WALLET_H
