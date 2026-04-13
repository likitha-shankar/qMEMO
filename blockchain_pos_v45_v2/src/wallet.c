#include "../include/wallet.h"
#include "../include/transaction.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

// =============================================================================
// WALLET CREATION
// =============================================================================

Wallet* wallet_create(void) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));
    
    // Generate random Ed25519 keypair
    uint8_t seed[32];
    RAND_bytes(seed, 32);
    
    // Create Ed25519 key from seed
    wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!wallet->evp_key) {
        free(wallet);
        return NULL;
    }
    
    // Store seed
    memcpy(wallet->ed25519_seed, seed, 32);
    
    // Extract public key
    size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
    
    // Derive address = hash160(ed25519_pubkey)
    wallet_derive_address(wallet->ed25519_pubkey, 32, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    
    wallet->public_key_len = 32;
    wallet->nonce = 0;
    
    return wallet;
}

Wallet* wallet_create_named(const char* name) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));
    
    safe_strcpy(wallet->name, name, sizeof(wallet->name));
    
    // Derive deterministic Ed25519 seed from name
    // seed = SHA256(name) → always same keypair for same name
    uint8_t seed[32];
    sha256((const uint8_t*)name, strlen(name), seed);
    
    // Create Ed25519 key from deterministic seed
    wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!wallet->evp_key) {
        // Fallback: use name-based address without real keys
        LOG_WARN("Ed25519 key creation failed for %s, using deterministic fallback", name);
        wallet_name_to_address(name, wallet->address);
        address_to_hex(wallet->address, wallet->address_hex);
        memcpy(wallet->ed25519_seed, seed, 32);
        // Derive pubkey deterministically without OpenSSL
        blake3_hash(seed, 32, wallet->ed25519_pubkey);
        wallet->public_key_len = 32;
        wallet->nonce = 0;
        return wallet;
    }
    
    // Store seed
    memcpy(wallet->ed25519_seed, seed, 32);
    
    // Extract public key
    size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
    
    // Address = hash160(ed25519_pubkey) — deterministic from name
    wallet_derive_address(wallet->ed25519_pubkey, 32, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    
    wallet->public_key_len = 32;
    wallet->nonce = 0;
    
    return wallet;
}

// =============================================================================
// WALLET PERSISTENCE
// =============================================================================

bool wallet_save(const Wallet* wallet, const char* filepath) {
    if (!wallet || !filepath) return false;
    
    FILE* f = fopen(filepath, "w");
    if (!f) return false;
    
    fprintf(f, "NAME:%s\n", wallet->name);
    fprintf(f, "ADDRESS:%s\n", wallet->address_hex);
    fprintf(f, "NONCE:%lu\n", wallet->nonce);
    
    // Store Ed25519 seed as hex (32 bytes → 64 hex chars)
    char seed_hex[65] = {0};
    bytes_to_hex_buf(wallet->ed25519_seed, 32, seed_hex);
    fprintf(f, "ED25519_SEED:%s\n", seed_hex);
    
    // Store Ed25519 pubkey as hex
    char pubkey_hex[65] = {0};
    bytes_to_hex_buf(wallet->ed25519_pubkey, 32, pubkey_hex);
    fprintf(f, "ED25519_PUBKEY:%s\n", pubkey_hex);
    
    fclose(f);
    return true;
}

Wallet* wallet_load(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;
    
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));
    
    char line[2048];
    bool has_ed25519 = false;
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NAME:", 5) == 0) {
            char* value = line + 5;
            trim(value);
            safe_strcpy(wallet->name, value, sizeof(wallet->name));
        } else if (strncmp(line, "ADDRESS:", 8) == 0) {
            char* value = line + 8;
            trim(value);
            safe_strcpy(wallet->address_hex, value, sizeof(wallet->address_hex));
            hex_to_address(value, wallet->address);
        } else if (strncmp(line, "NONCE:", 6) == 0) {
            wallet->nonce = strtoull(line + 6, NULL, 10);
        } else if (strncmp(line, "ED25519_SEED:", 13) == 0) {
            char* value = line + 13;
            trim(value);
            if (strlen(value) >= 64) {
                hex_to_bytes_buf(value, wallet->ed25519_seed, 32);
                has_ed25519 = true;
            }
        } else if (strncmp(line, "ED25519_PUBKEY:", 15) == 0) {
            char* value = line + 15;
            trim(value);
            if (strlen(value) >= 64) {
                hex_to_bytes_buf(value, wallet->ed25519_pubkey, 32);
            }
        }
        // Skip PRIVATE_KEY: (legacy ECDSA format) — no longer used
    }
    
    fclose(f);
    
    // Reconstruct Ed25519 key from seed
    if (has_ed25519 && !is_zero(wallet->ed25519_seed, 32)) {
        wallet->evp_key = EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519, NULL, wallet->ed25519_seed, 32);
        if (wallet->evp_key) {
            size_t pub_len = 32;
            EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
        }
        wallet->public_key_len = 32;
    }
    
    // MIGRATION: If wallet was saved by old code (no Ed25519 data),
    // re-derive keys from name. The old .dat has wrong address format
    // (hash160(name) vs hash160(ed25519_pubkey)). We must regenerate
    // to get correct Ed25519 keys and matching address.
    if (!has_ed25519 && strlen(wallet->name) > 0) {
        LOG_INFO("🔄 Migrating wallet %s from legacy format to Ed25519...", wallet->name);
        
        uint64_t saved_nonce = wallet->nonce;
        char saved_name[64];
        safe_strcpy(saved_name, wallet->name, sizeof(saved_name));
        
        // Re-derive everything from name (deterministic)
        uint8_t seed[32];
        sha256((const uint8_t*)saved_name, strlen(saved_name), seed);
        memcpy(wallet->ed25519_seed, seed, 32);
        
        wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
        if (wallet->evp_key) {
            size_t pub_len = 32;
            EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
            wallet_derive_address(wallet->ed25519_pubkey, 32, wallet->address);
            address_to_hex(wallet->address, wallet->address_hex);
        } else {
            wallet_name_to_address(saved_name, wallet->address);
            address_to_hex(wallet->address, wallet->address_hex);
            blake3_hash(seed, 32, wallet->ed25519_pubkey);
        }
        
        wallet->public_key_len = 32;
        wallet->nonce = saved_nonce;
        safe_strcpy(wallet->name, saved_name, sizeof(wallet->name));
        
        // Save migrated wallet (overwrites old format)
        wallet_save(wallet, filepath);
        LOG_INFO("✅ Wallet %s migrated (address: %.16s...)", saved_name, wallet->address_hex);
    }
    
    return wallet;
}

// =============================================================================
// WALLET ACCESSORS
// =============================================================================

const uint8_t* wallet_get_address(const Wallet* wallet) {
    return wallet ? wallet->address : NULL;
}

const char* wallet_get_address_hex(const Wallet* wallet) {
    return wallet ? wallet->address_hex : NULL;
}

uint64_t wallet_get_next_nonce(Wallet* wallet) {
    if (!wallet) return 0;
    return wallet->nonce++;
}

uint64_t wallet_get_nonce(const Wallet* wallet) {
    return wallet ? wallet->nonce : 0;
}

void wallet_set_nonce(Wallet* wallet, uint64_t nonce) {
    if (wallet) wallet->nonce = nonce;
}

// =============================================================================
// SIGNING AND VERIFICATION
// =============================================================================

bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t signature[64]) {
    if (!wallet || !message || !signature) return false;
    memset(signature, 0, 64);
    
    if (wallet->evp_key) {
        // Real Ed25519 signing via OpenSSL
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return false;
        
        if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, wallet->evp_key) != 1) {
            EVP_MD_CTX_free(ctx);
            goto deterministic_fallback;
        }
        
        size_t sig_len = 64;
        if (EVP_DigestSign(ctx, signature, &sig_len, message, msg_len) != 1) {
            EVP_MD_CTX_free(ctx);
            goto deterministic_fallback;
        }
        
        EVP_MD_CTX_free(ctx);
        return true;
    }
    
deterministic_fallback:
    // Fallback: deterministic signature from seed + message
    {
        uint8_t combined[32 + 64];  // seed + message_hash
        memcpy(combined, wallet->ed25519_seed, 32);
        
        uint8_t msg_hash[32];
        blake3_hash(message, msg_len, msg_hash);
        memcpy(combined + 32, msg_hash, 32);
        
        uint8_t hash1[32], hash2[32];
        blake3_hash(combined, 64, hash1);
        blake3_hash(hash1, 32, hash2);
        
        memcpy(signature, hash1, 32);
        memcpy(signature + 32, hash2, 32);
        return true;
    }
}

const uint8_t* wallet_get_pubkey(const Wallet* wallet) {
    return wallet ? wallet->ed25519_pubkey : NULL;
}

bool wallet_verify(const uint8_t public_key[32],
                   const uint8_t* message, size_t msg_len,
                   const uint8_t signature[64]) {
    if (!public_key || !message || !signature) return false;
    
    // Check signature is not all zeros
    if (is_zero(signature, 64)) return false;
    
    // Create Ed25519 public key from raw bytes
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, public_key, 32);
    if (!pkey) return false;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    
    int result = EVP_DigestVerify(ctx, signature, 64, message, msg_len);
    
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    
    return (result == 1);
}

void wallet_destroy(Wallet* wallet) {
    if (!wallet) return;
    
    if (wallet->evp_key) {
        EVP_PKEY_free(wallet->evp_key);
    }
    
    secure_free(wallet, sizeof(Wallet));
}

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void wallet_derive_address(const uint8_t* pubkey, size_t len, uint8_t address[20]) {
    hash160(pubkey, len, address);
}

void wallet_name_to_address(const char* name, uint8_t address[20]) {
    // MUST match wallet_create_named() derivation:
    //   seed = sha256(name) → Ed25519 keypair → address = hash160(pubkey)
    // This ensures "farmer1" resolves to the same address everywhere:
    //   wallet CLI, blockchain server, benchmark, validator.
    uint8_t seed[32];
    sha256((const uint8_t*)name, strlen(name), seed);
    
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (key) {
        uint8_t pubkey[32];
        size_t pub_len = 32;
        EVP_PKEY_get_raw_public_key(key, pubkey, &pub_len);
        EVP_PKEY_free(key);
        hash160(pubkey, 32, address);
    } else {
        // Fallback if Ed25519 fails: use BLAKE3(seed) as pseudo-pubkey
        uint8_t pseudo_pubkey[32];
        blake3_hash(seed, 32, pseudo_pubkey);
        hash160(pseudo_pubkey, 32, address);
    }
}

bool wallet_is_hex_address(const char* str) {
    if (!str || strlen(str) != 40) return false;
    
    for (int i = 0; i < 40; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool wallet_parse_address(const char* str, uint8_t address[20]) {
    if (!str || !address) return false;
    
    if (wallet_is_hex_address(str)) {
        return hex_to_address(str, address);
    } else {
        wallet_name_to_address(str, address);
        return true;
    }
}

void address_to_hex(const uint8_t address[20], char hex[41]) {
    bytes_to_hex_buf(address, 20, hex);
}

bool hex_to_address(const char* hex, uint8_t address[20]) {
    if (!hex || strlen(hex) != 40) return false;
    return hex_to_bytes_buf(hex, address, 20);
}

void txhash_to_hex(const uint8_t hash[TX_HASH_SIZE], char hex[TX_HASH_SIZE * 2 + 1]) {
    bytes_to_hex_buf(hash, TX_HASH_SIZE, hex);
}

bool address_is_valid(const uint8_t address[20]) {
    return !is_zero(address, 20);
}

bool address_equals(const uint8_t a[20], const uint8_t b[20]) {
    return memcmp(a, b, 20) == 0;
}
