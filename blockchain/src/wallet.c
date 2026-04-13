#include "../include/wallet.h"
#include "../include/transaction.h"
#include "../include/crypto_backend.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#if SIG_SCHEME == SIG_FALCON512 || SIG_SCHEME == SIG_ML_DSA44 || SIG_SCHEME == SIG_HYBRID
#include <oqs/oqs.h>
#endif

// =============================================================================
// WALLET CREATION
// =============================================================================

Wallet* wallet_create(void) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    // Generate random Ed25519 keypair
    uint8_t seed[32];
    RAND_bytes(seed, 32);

    wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!wallet->evp_key) {
        free(wallet);
        return NULL;
    }

    memcpy(wallet->ed25519_seed, seed, 32);
    size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);

    memcpy(wallet->public_key, wallet->ed25519_pubkey, 32);
    wallet->pubkey_len = 32;
    wallet->sig_type = SIG_ED25519;

    wallet_derive_address(wallet->ed25519_pubkey, 32, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    wallet->nonce = 0;

    return wallet;
}

Wallet* wallet_create_named(const char* name, uint8_t sig_type) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    safe_strcpy(wallet->name, name, sizeof(wallet->name));
    wallet->sig_type = sig_type;

    // Derive deterministic seed from name (used for Ed25519 and as RNG seed for PQC)
    uint8_t seed[32];
    sha256((const uint8_t*)name, strlen(name), seed);

    if (sig_type == SIG_ED25519) {
        // Ed25519: deterministic from seed
        wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
        if (!wallet->evp_key) {
            LOG_WARN("Ed25519 key creation failed for %s, fallback", name);
            wallet_name_to_address(name, wallet->address);
            address_to_hex(wallet->address, wallet->address_hex);
            memcpy(wallet->ed25519_seed, seed, 32);
            blake3_hash(seed, 32, wallet->ed25519_pubkey);
            memcpy(wallet->public_key, wallet->ed25519_pubkey, 32);
            wallet->pubkey_len = 32;
            wallet->nonce = 0;
            return wallet;
        }
        memcpy(wallet->ed25519_seed, seed, 32);
        size_t pub_len = 32;
        EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
        memcpy(wallet->public_key, wallet->ed25519_pubkey, 32);
        wallet->pubkey_len = 32;
        wallet_derive_address(wallet->ed25519_pubkey, 32, wallet->address);

#if SIG_SCHEME == SIG_FALCON512 || SIG_SCHEME == SIG_HYBRID
    } else if (sig_type == SIG_FALCON512) {
        // Falcon-512: deterministic keygen seeded from name hash
        OQS_SIG* oqs = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!oqs) { free(wallet); return NULL; }

        // Seed the PRNG deterministically (liboqs uses OpenSSL RAND internally)
        RAND_seed(seed, 32);
        OQS_STATUS rc = OQS_SIG_keypair(oqs, wallet->public_key, wallet->oqs_seckey);
        OQS_SIG_free(oqs);
        if (rc != OQS_SUCCESS) { free(wallet); return NULL; }

        wallet->pubkey_len = OQS_SIG_falcon_512_length_public_key;
        wallet->oqs_seckey_len = OQS_SIG_falcon_512_length_secret_key;
        wallet_derive_address(wallet->public_key, wallet->pubkey_len, wallet->address);
#endif

#if SIG_SCHEME == SIG_ML_DSA44 || SIG_SCHEME == SIG_HYBRID
    } else if (sig_type == SIG_ML_DSA44) {
        // ML-DSA-44: deterministic keygen seeded from name hash
        OQS_SIG* oqs = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
        if (!oqs) { free(wallet); return NULL; }

        RAND_seed(seed, 32);
        OQS_STATUS rc = OQS_SIG_keypair(oqs, wallet->public_key, wallet->oqs_seckey);
        OQS_SIG_free(oqs);
        if (rc != OQS_SUCCESS) { free(wallet); return NULL; }

        wallet->pubkey_len = OQS_SIG_ml_dsa_44_length_public_key;
        wallet->oqs_seckey_len = OQS_SIG_ml_dsa_44_length_secret_key;
        wallet_derive_address(wallet->public_key, wallet->pubkey_len, wallet->address);
#endif

    } else {
        // Unknown scheme: fall back to Ed25519
        wallet->sig_type = SIG_ED25519;
        wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
        if (wallet->evp_key) {
            memcpy(wallet->ed25519_seed, seed, 32);
            size_t pub_len = 32;
            EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->ed25519_pubkey, &pub_len);
            memcpy(wallet->public_key, wallet->ed25519_pubkey, 32);
            wallet->pubkey_len = 32;
        }
        wallet_derive_address(wallet->public_key, wallet->pubkey_len, wallet->address);
    }

    address_to_hex(wallet->address, wallet->address_hex);
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
            memcpy(wallet->public_key, wallet->ed25519_pubkey, 32);
            wallet->pubkey_len = 32;
        }
        wallet->sig_type = SIG_ED25519;
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
        
        wallet->pubkey_len = 32;
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
                 uint8_t* sig_out, size_t* sig_len) {
    if (!wallet || !message || !sig_out || !sig_len) return false;

    if (wallet->sig_type == SIG_ED25519) {
        if (wallet->evp_key) {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) return false;

            if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, wallet->evp_key) != 1) {
                EVP_MD_CTX_free(ctx);
                return false;
            }

            *sig_len = 64;
            if (EVP_DigestSign(ctx, sig_out, sig_len, message, msg_len) != 1) {
                EVP_MD_CTX_free(ctx);
                return false;
            }

            EVP_MD_CTX_free(ctx);
            return true;
        }
        return false;
    }

#if SIG_SCHEME == SIG_FALCON512 || SIG_SCHEME == SIG_HYBRID
    if (wallet->sig_type == SIG_FALCON512) {
        OQS_SIG* oqs = OQS_SIG_new(OQS_SIG_alg_falcon_512);
        if (!oqs) return false;
        OQS_STATUS rc = OQS_SIG_sign(oqs, sig_out, sig_len,
                                     message, msg_len, wallet->oqs_seckey);
        OQS_SIG_free(oqs);
        return rc == OQS_SUCCESS;
    }
#endif

#if SIG_SCHEME == SIG_ML_DSA44 || SIG_SCHEME == SIG_HYBRID
    if (wallet->sig_type == SIG_ML_DSA44) {
        OQS_SIG* oqs = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
        if (!oqs) return false;
        OQS_STATUS rc = OQS_SIG_sign(oqs, sig_out, sig_len,
                                     message, msg_len, wallet->oqs_seckey);
        OQS_SIG_free(oqs);
        return rc == OQS_SUCCESS;
    }
#endif

    return false;
}

const uint8_t* wallet_get_pubkey(const Wallet* wallet) {
    return wallet ? wallet->public_key : NULL;
}

bool wallet_verify(const uint8_t* public_key, size_t pubkey_len,
                   const uint8_t* message, size_t msg_len,
                   const uint8_t* signature, size_t sig_len) {
    if (!public_key || !message || !signature) return false;
    if (sig_len == 0 || pubkey_len == 0) return false;

    // Dispatch to crypto_backend (handles Ed25519, Falcon, ML-DSA)
    // Default to Ed25519 if pubkey_len == 32, else use typed dispatch
    uint8_t sig_type = (pubkey_len == 32) ? SIG_ED25519 : SIG_FALCON512;
    return crypto_verify_typed(sig_type, signature, sig_len,
                               message, msg_len, public_key, pubkey_len);
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
