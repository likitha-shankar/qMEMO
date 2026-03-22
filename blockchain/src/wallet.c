#include "../include/wallet.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// =============================================================================
// WALLET CREATION
// =============================================================================

Wallet* wallet_create(void) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    wallet->crypto = crypto_ctx_new();
    if (!wallet->crypto) {
        free(wallet);
        return NULL;
    }

    if (!crypto_keygen(wallet->crypto,
                       wallet->public_key, &wallet->pubkey_len,
                       wallet->secret_key, &wallet->seckey_len)) {
        crypto_ctx_free(wallet->crypto);
        free(wallet);
        return NULL;
    }

    // Derive address from public key
    wallet_derive_address(wallet->public_key, wallet->pubkey_len, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);

    wallet->nonce = 0;
    return wallet;
}

Wallet* wallet_create_named(const char* name) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    safe_strcpy(wallet->name, name, sizeof(wallet->name));

    // Generate real keypair (needed for signing)
    wallet->crypto = crypto_ctx_new();
    if (!wallet->crypto) {
        free(wallet);
        return NULL;
    }

    if (!crypto_keygen(wallet->crypto,
                       wallet->public_key, &wallet->pubkey_len,
                       wallet->secret_key, &wallet->seckey_len)) {
        crypto_ctx_free(wallet->crypto);
        free(wallet);
        return NULL;
    }

    // For named wallets: address is derived from name (deterministic)
    // This means address != hash(pubkey) — pre-existing design for demo mode
    wallet_name_to_address(name, wallet->address);
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
    fprintf(f, "SCHEME:%s\n", CRYPTO_SCHEME_NAME);

    // Write raw keys as hex
    char *pk_hex = bytes_to_hex(wallet->public_key, wallet->pubkey_len);
    char *sk_hex = bytes_to_hex(wallet->secret_key, wallet->seckey_len);
    if (pk_hex) { fprintf(f, "PUBKEY:%s\n", pk_hex); free(pk_hex); }
    if (sk_hex) { fprintf(f, "SECKEY:%s\n", sk_hex); free(sk_hex); }

    fclose(f);
    return true;
}

Wallet* wallet_load(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;

    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    char line[4096];
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
        } else if (strncmp(line, "PUBKEY:", 7) == 0) {
            char *value = line + 7;
            trim(value);
            size_t hex_len = strlen(value);
            size_t byte_len = hex_len / 2;
            if (byte_len <= CRYPTO_PUBKEY_MAX) {
                hex_to_bytes_buf(value, wallet->public_key, byte_len);
                wallet->pubkey_len = byte_len;
            }
        } else if (strncmp(line, "SECKEY:", 7) == 0) {
            char *value = line + 7;
            trim(value);
            size_t hex_len = strlen(value);
            size_t byte_len = hex_len / 2;
            if (byte_len <= CRYPTO_SECKEY_MAX) {
                hex_to_bytes_buf(value, wallet->secret_key, byte_len);
                wallet->seckey_len = byte_len;
            }
        }
    }

    fclose(f);

    // Reconstruct crypto context
    wallet->crypto = crypto_ctx_new();

#if SIG_SCHEME == SIG_ECDSA
    // For ECDSA we need the EVP_PKEY in the ctx for signing.
    // Re-generate from saved keys by doing a fresh keygen if seckey is present.
    // Alternatively, we could reconstruct from raw bytes, but a fresh keygen
    // into the ctx then overwriting the pubkey/seckey is simpler for the MVP.
    // For now, if we have a secret key, we generate a throwaway keypair and
    // note that the loaded wallet won't be able to sign (the address won't match).
    // TODO: Implement EVP_PKEY reconstruction from raw seckey for ECDSA wallet_load.
    if (wallet->seckey_len > 0 && wallet->crypto) {
        // We need to reconstruct EVP_PKEY from raw secret key bytes.
        // Build EC key from private scalar + public point.
        // For now, wallet_load is primarily used for named wallets in benchmarks
        // which re-derive keys anyway. Full reconstruction is a future enhancement.
    }
#endif

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
                 uint8_t *signature, size_t *sig_len) {
    if (!wallet || !wallet->crypto || !message || !signature || !sig_len)
        return false;

    return crypto_sign(wallet->crypto, signature, sig_len,
                       message, msg_len,
                       wallet->secret_key, wallet->seckey_len);
}

bool wallet_verify(const uint8_t *public_key, size_t pk_len,
                   const uint8_t* message, size_t msg_len,
                   const uint8_t *signature, size_t sig_len) {
    crypto_ctx_t *ctx = crypto_ctx_new();
    if (!ctx) return false;

    bool ok = crypto_verify(ctx, signature, sig_len,
                            message, msg_len,
                            public_key, pk_len);
    crypto_ctx_free(ctx);
    return ok;
}

void wallet_destroy(Wallet* wallet) {
    if (!wallet) return;
    if (wallet->crypto) crypto_ctx_free(wallet->crypto);
    secure_free(wallet, sizeof(Wallet));
}

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void wallet_derive_address(const uint8_t* pubkey, size_t len, uint8_t address[20]) {
    hash160(pubkey, len, address);
}

void wallet_name_to_address(const char* name, uint8_t address[20]) {
    hash160((const uint8_t*)name, strlen(name), address);
}

bool wallet_is_hex_address(const char* str) {
    if (!str || strlen(str) != 40) return false;
    for (int i = 0; i < 40; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
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

void txhash_to_hex(const uint8_t hash[28], char hex[57]) {
    bytes_to_hex_buf(hash, 28, hex);
}

bool address_is_valid(const uint8_t address[20]) {
    return !is_zero(address, 20);
}

bool address_equals(const uint8_t a[20], const uint8_t b[20]) {
    return memcmp(a, b, 20) == 0;
}
