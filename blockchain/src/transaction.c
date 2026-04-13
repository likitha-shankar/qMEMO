/**
 * transaction.c - Transaction creation, signing, verification, serialization
 *
 * Uses crypto_backend for real ECDSA or Falcon-512 signatures depending
 * on the SIG_SCHEME compile-time flag.
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

    tx->nonce = nonce;
    tx->expiry_block = expiry_block;

    memcpy(tx->source_address, wallet->address, 20);
    memcpy(tx->dest_address, dest_address, 20);

    tx->value = value;
    tx->fee = fee;

    transaction_sign(tx, wallet);

    return tx;
}

Transaction* transaction_create_coinbase(const uint8_t farmer_address[20],
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));

    tx->nonce = (uint64_t)block_height;
    tx->expiry_block = 0;

    memcpy(tx->source_address, COINBASE_ADDRESS, 20);
    memcpy(tx->dest_address, farmer_address, 20);

    tx->value = base_reward + total_fees;
    tx->fee = 0;

    // Coinbase: signature and pubkey left as zeros, sig_len/pubkey_len = 0

    return tx;
}

// =============================================================================
// TRANSACTION HASH COMPUTATION
// =============================================================================

void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]) {
    uint8_t buffer[8 + 4 + 20 + 20 + 8 + 4];  // 64 bytes
    size_t offset = 0;

    memcpy(buffer + offset, &tx->nonce, 8); offset += 8;
    memcpy(buffer + offset, &tx->expiry_block, 4); offset += 4;
    memcpy(buffer + offset, tx->source_address, 20); offset += 20;
    memcpy(buffer + offset, tx->dest_address, 20); offset += 20;
    memcpy(buffer + offset, &tx->value, 8); offset += 8;
    memcpy(buffer + offset, &tx->fee, 4); offset += 4;

    blake3_hash_truncated(buffer, sizeof(buffer), hash, TX_HASH_SIZE);
}

char* transaction_get_hash_hex(const Transaction* tx) {
    uint8_t hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, hash);
    return bytes_to_hex(hash, TX_HASH_SIZE);
}

// =============================================================================
// SIGNING AND VERIFICATION (via crypto_backend)
// =============================================================================

bool transaction_sign(Transaction* tx, const Wallet* wallet) {
    if (!tx || !wallet) return false;

    uint8_t tx_hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, tx_hash);

    if (!wallet_sign(wallet, tx_hash, TX_HASH_SIZE,
                     tx->signature, &tx->sig_len)) {
        return false;
    }

    // Embed signer's public key in the transaction
    memcpy(tx->public_key, wallet->public_key, wallet->pubkey_len);
    tx->pubkey_len = wallet->pubkey_len;
    tx->sig_type = wallet->sig_type;

    return true;
}

bool transaction_verify(const Transaction* tx) {
    if (TX_IS_COINBASE(tx)) return true;

    // Must have a signature and public key
    if (tx->sig_len == 0 || tx->pubkey_len == 0) return false;

    uint8_t tx_hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, tx_hash);

    return crypto_verify_typed(tx->sig_type, tx->signature, tx->sig_len,
                               tx_hash, TX_HASH_SIZE,
                               tx->public_key, tx->pubkey_len);
}

bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height) {
    if (tx->expiry_block == 0) return false;
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

    if (tx->sig_len > 0) {
        pb_tx.signature.len = tx->sig_len;
        pb_tx.signature.data = (uint8_t*)tx->signature;
    }

    if (tx->pubkey_len > 0) {
        pb_tx.public_key.len = tx->pubkey_len;
        pb_tx.public_key.data = (uint8_t*)tx->public_key;
    }

    pb_tx.sig_type = tx->sig_type;

    size_t size = blockchain__transaction__get_packed_size(&pb_tx);
    uint8_t* buffer = safe_malloc(size);
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

    if (pb_tx->source_address.data && pb_tx->source_address.len >= 20)
        memcpy(tx->source_address, pb_tx->source_address.data, 20);

    if (pb_tx->dest_address.data && pb_tx->dest_address.len >= 20)
        memcpy(tx->dest_address, pb_tx->dest_address.data, 20);

    tx->value = pb_tx->value;
    tx->fee = pb_tx->fee;

    if (pb_tx->signature.data && pb_tx->signature.len > 0) {
        size_t slen = pb_tx->signature.len;
        if (slen > CRYPTO_SIG_MAX) slen = CRYPTO_SIG_MAX;
        memcpy(tx->signature, pb_tx->signature.data, slen);
        tx->sig_len = slen;
    }

    if (pb_tx->public_key.data && pb_tx->public_key.len > 0) {
        size_t pklen = pb_tx->public_key.len;
        if (pklen > CRYPTO_PUBKEY_MAX) pklen = CRYPTO_PUBKEY_MAX;
        memcpy(tx->public_key, pb_tx->public_key.data, pklen);
        tx->pubkey_len = pklen;
    }

    tx->sig_type = pb_tx->sig_type ? pb_tx->sig_type : SIG_ED25519;

    blockchain__transaction__free_unpacked(pb_tx, NULL);
    return tx;
}

// =============================================================================
// LEGACY SERIALIZATION (hex string — best-effort with variable-size struct)
// =============================================================================

char* transaction_serialize(const Transaction* tx) {
    // Serialize via protobuf, then hex-encode
    size_t pb_len = 0;
    uint8_t *pb = transaction_serialize_pb(tx, &pb_len);
    if (!pb) return NULL;
    char *hex = bytes_to_hex(pb, pb_len);
    free(pb);
    return hex;
}

Transaction* transaction_deserialize(const char* hex_data) {
    if (!hex_data) return NULL;
    size_t hex_len = strlen(hex_data);
    if (hex_len < 2 || hex_len % 2 != 0) return NULL;

    size_t byte_len = hex_len / 2;
    uint8_t *buf = safe_malloc(byte_len);
    if (!hex_to_bytes_buf(hex_data, buf, byte_len)) {
        free(buf);
        return NULL;
    }

    Transaction *tx = transaction_deserialize_pb(buf, byte_len);
    free(buf);
    return tx;
}

void transaction_destroy(Transaction* tx) {
    if (tx) {
        secure_free(tx, sizeof(Transaction));
    }
}
