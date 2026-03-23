/**
 * ============================================================================
 * VALIDATOR.C - Proof of Space Farmer/Validator (v29)
 * ============================================================================
 * 
 * ARCHITECTURE (v29 - Proof-Based Winner Selection):
 * ==================================================
 * 
 * 1. Receive challenge from metronome (SUB)
 * 2. Search plot for valid proof
 * 3. If valid proof found: submit LIGHTWEIGHT PROOF to metronome (REQ)
 * 4. Listen for WINNER announcement (SUB)
 * 5. If we won:
 *    a) Fetch pending transactions from pool (directly, no metronome)
 *    b) Create coinbase transaction (paying ourselves)
 *    c) Build complete block with transactions
 *    d) Send block directly to blockchain
 *       Blockchain directly notifies metronome on success.
 *    e) Confirm transactions with pool
 * 
 * KEY CHANGES FROM v28:
 * - Fixed proof submission delimiter bug (uses '#' for outer delimiter)
 * - Blockchain notifies metronome directly (no validator relay)
 * - Validator no longer sends BLOCK_CONFIRMED to metronome
 * 
 * ============================================================================
 */

#include "../include/validator.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include "../include/block.h"
#include "../include/transaction.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// Buffer sizes
#define VALIDATOR_BUFFER_SIZE (3 * 1024 * 1024)  // 3MB for large blocks
#define MAX_TXS_PER_BLOCK_DEFAULT 10000
static uint32_t max_txs_per_block = MAX_TXS_PER_BLOCK_DEFAULT;
#define BASE_MINING_REWARD 10000
#define HALVING_INTERVAL 10000000

// =============================================================================
// HELPER: Calculate mining reward for a given block height
// =============================================================================

static uint64_t calculate_mining_reward(uint32_t block_height) {
    uint64_t reward = BASE_MINING_REWARD;
    uint32_t halvings = block_height / HALVING_INTERVAL;
    
    for (uint32_t i = 0; i < halvings && reward > 1; i++) {
        reward /= 2;
    }
    
    return reward;
}

// =============================================================================
// VALIDATOR CREATION
// =============================================================================

Validator* validator_create(const char* name, uint32_t k_param) {
    Validator* v = safe_malloc(sizeof(Validator));
    memset(v, 0, sizeof(Validator));
    
    safe_strcpy(v->name, name, sizeof(v->name));
    v->k_param = k_param > 0 ? k_param : K_PARAM_DEFAULT;
    
    v->wallet = wallet_create_named(name, SIG_SCHEME);
    if (!v->wallet) {
        free(v);
        return NULL;
    }
    
    v->has_challenge = false;
    v->start_time = get_current_time_ms();
    v->running = false;
    
    LOG_INFO("🌾 ════════════════════════════════════════════════════════════");
    LOG_INFO("🌾 VALIDATOR CREATED: %s (v28 - Proof Submission)", v->name);
    LOG_INFO("   ├─ 📍 Address: %s", v->wallet->address_hex);
    LOG_INFO("   └─ 📊 k parameter: %u (2^%u = %lu entries)", 
             v->k_param, v->k_param, (uint64_t)1 << v->k_param);
    LOG_INFO("🌾 ════════════════════════════════════════════════════════════");
    
    return v;
}

bool validator_init_sockets(Validator* v,
                            const char* metronome_req_addr,
                            const char* metronome_sub_addr,
                            const char* pool_addr,
                            const char* blockchain_addr) {
    if (!v) return false;
    
    v->zmq_context = zmq_ctx_new();
    if (!v->zmq_context) {
        LOG_ERROR("Failed to create ZMQ context");
        return false;
    }
    
    // REQ socket for metronome (submitting proofs + block confirmations)
    v->metronome_req = zmq_socket(v->zmq_context, ZMQ_REQ);
    if (zmq_connect(v->metronome_req, metronome_req_addr) != 0) {
        LOG_ERROR("Failed to connect to metronome at %s", metronome_req_addr);
        return false;
    }
    LOG_INFO("🔌 [%s] Connected to metronome at %s", v->name, metronome_req_addr);
    
    // SUB socket for challenges + winner announcements
    v->metronome_sub = zmq_socket(v->zmq_context, ZMQ_SUB);
    if (zmq_connect(v->metronome_sub, metronome_sub_addr) != 0) {
        LOG_ERROR("Failed to connect to metronome SUB at %s", metronome_sub_addr);
        return false;
    }
    zmq_setsockopt(v->metronome_sub, ZMQ_SUBSCRIBE, "", 0);
    LOG_INFO("🔌 [%s] Subscribed to challenges+winners at %s", v->name, metronome_sub_addr);
    
    // REQ socket for pool (winner fetches TXs directly)
    v->pool_req = zmq_socket(v->zmq_context, ZMQ_REQ);
    if (zmq_connect(v->pool_req, pool_addr) != 0) {
        LOG_ERROR("Failed to connect to pool at %s", pool_addr);
        return false;
    }
    LOG_INFO("🔌 [%s] Connected to pool at %s (for TX fetching when winner)", v->name, pool_addr);
    
    // REQ socket for blockchain (winner sends block directly)
    v->blockchain_req = zmq_socket(v->zmq_context, ZMQ_REQ);
    if (zmq_connect(v->blockchain_req, blockchain_addr) != 0) {
        LOG_ERROR("Failed to connect to blockchain at %s", blockchain_addr);
        return false;
    }
    LOG_INFO("🔌 [%s] Connected to blockchain at %s (for block submission when winner)", v->name, blockchain_addr);
    
    return true;
}

// =============================================================================
// PLOT GENERATION
// =============================================================================

bool validator_generate_plot(Validator* v) {
    if (!v) return false;
    
    LOG_INFO("🌾 [%s] Generating plot with BLAKE3...", v->name);
    
    v->plot = plot_create(v->wallet->address, v->k_param);
    if (!v->plot) {
        LOG_ERROR("[%s] Failed to create plot", v->name);
        return false;
    }
    
    if (!plot_generate(v->plot)) {
        LOG_ERROR("[%s] Failed to generate plot", v->name);
        plot_destroy(v->plot);
        v->plot = NULL;
        return false;
    }
    
    char plot_id_hex[65];
    bytes_to_hex_buf(v->plot->plot_id, 32, plot_id_hex);
    
    LOG_INFO("🌾 [%s] Plot ready! ID: %.16s...", v->name, plot_id_hex);
    
    return true;
}

// =============================================================================
// CHALLENGE HANDLING
// =============================================================================

void validator_handle_challenge(Validator* v, const Challenge* challenge) {
    if (!v || !challenge) return;
    
    memcpy(&v->current_challenge, challenge, sizeof(Challenge));
    v->has_challenge = true;
    
    char hash_hex[65];
    bytes_to_hex_buf(challenge->challenge_hash, 32, hash_hex);
    
    LOG_INFO("📡 [%s] Received challenge #%lu for block #%u (difficulty: %u)", 
             v->name, challenge->challenge_id, challenge->target_block_height,
             challenge->current_difficulty);
}

// =============================================================================
// STEP 1: FIND PROOF AND SUBMIT TO METRONOME
// =============================================================================

/**
 * Search plot for valid proof and submit to metronome.
 * Does NOT create a block - just submits the proof.
 * Block creation only happens if we are announced as the winner.
 */
bool validator_find_and_submit_proof(Validator* v) {
    if (!v || !v->has_challenge || !v->plot) return false;
    
    // Search plot for valid proof
    SpaceProof* proof = plot_find_proof(v->plot, 
                                        v->current_challenge.challenge_hash,
                                        v->current_challenge.current_difficulty);
    
    if (!proof) {
        LOG_INFO("🔍 [%s] No valid proof for difficulty %u", 
                 v->name, v->current_challenge.current_difficulty);
        return false;
    }
    
    v->proofs_found++;
    
    char quality_hex[65];
    bytes_to_hex_buf(proof->quality, 32, quality_hex);
    LOG_INFO("🎯 [%s] VALID PROOF FOUND! Quality: %.16s... (%u leading zeros)",
             v->name, quality_hex, count_leading_zeros(proof->quality, 32));
    
    // Serialize proof
    char* proof_hex = proof_serialize(proof);
    if (!proof_hex) {
        LOG_ERROR("[%s] Failed to serialize proof", v->name);
        free(proof);
        return false;
    }
    
    // Format: SUBMIT_PROOF:<proof_hex>#<farmer_name>#<farmer_address_hex>
    // NOTE: Using '#' as outer delimiter because proof_serialize uses '|' internally
    char addr_hex[41];
    bytes_to_hex_buf(v->wallet->address, 20, addr_hex);
    
    size_t req_size = strlen("SUBMIT_PROOF:") + strlen(proof_hex) + 1 + strlen(v->name) + 1 + 40 + 1;
    char* submit_request = safe_malloc(req_size);
    snprintf(submit_request, req_size, "SUBMIT_PROOF:%s#%s#%s", proof_hex, v->name, addr_hex);
    
    LOG_INFO("📤 [%s] Submitting proof to metronome...", v->name);
    
    zmq_send(v->metronome_req, submit_request, strlen(submit_request), 0);
    
    char response[256];
    int size = zmq_recv(v->metronome_req, response, sizeof(response) - 1, 0);
    
    bool accepted = false;
    if (size > 0) {
        response[size] = '\0';
        v->proofs_submitted++;
        
        if (strcmp(response, "PROOF_ACCEPTED") == 0) {
            LOG_INFO("✅ [%s] Proof ACCEPTED! We are current leader!", v->name);
            accepted = true;
        } else if (strcmp(response, "NOT_BEST") == 0) {
            LOG_INFO("📊 [%s] Proof valid but another farmer has better quality", v->name);
            accepted = true;  // Still valid, just not the best
        } else {
            LOG_WARN("[%s] Proof response: %s", v->name, response);
        }
    }
    
    free(submit_request);
    free(proof_hex);
    free(proof);
    
    return accepted;
}

// =============================================================================
// STEP 2: CREATE AND SUBMIT BLOCK (ONLY WHEN WE WIN)
// =============================================================================

/**
 * Called ONLY when this validator is announced as the winner.
 * 
 * 1. Fetch transactions from pool (directly, no metronome)
 * 2. Get previous block hash from blockchain
 * 3. Create coinbase transaction (paying ourselves)
 * 4. Build complete block
 * 5. Send block directly to blockchain
 * 6. Confirm transactions with pool
 * 7. Notify metronome: BLOCK_CONFIRMED
 */
bool validator_create_and_submit_block(Validator* v) {
    if (!v || !v->has_challenge) return false;
    
    uint64_t now = get_current_time_ms();
    int64_t remaining = (int64_t)(v->deadline_ms - now);
    
    LOG_INFO("");
    LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
    LOG_INFO("🏆 [%s] WE WON! Creating block #%u (budget: %ld ms)...", 
             v->name, v->current_challenge.target_block_height, remaining);
    
    if (remaining < 20) {
        LOG_WARN("❌ [%s] Budget too small (%ld ms), skipping block creation", v->name, remaining);
        return false;
    }
    
    char* buffer = safe_malloc(VALIDATOR_BUFFER_SIZE);
    buffer[0] = '\0';
    
    // =========================================================================
    // STEP 1: Get previous block hash from blockchain
    // =========================================================================
    uint8_t prev_hash[32] = {0};
    uint64_t step1_start = get_current_time_ms();
    uint64_t block_start_ms = step1_start;
    uint64_t step6_total_ms = 0;
    
    zmq_send(v->blockchain_req, "GET_LAST_HASH", 13, 0);
    int size = zmq_recv(v->blockchain_req, buffer, VALIDATOR_BUFFER_SIZE - 1, 0);
    
    if (size > 0) {
        buffer[size] = '\0';
        if (strcmp(buffer, "NONE") != 0 && size >= 64) {
            hex_to_bytes_buf(buffer, prev_hash, 32);
        }
    }
    uint64_t step1_ms = get_current_time_ms() - step1_start;
    LOG_INFO("   ├─ ⏱️  Step 1 (GET_LAST_HASH): %lu ms", step1_ms);
    
    // =========================================================================
    // STEP 2: Fetch pending transactions from pool
    // =========================================================================
    // ALWAYS request max TXs. 10K TXs takes ~85ms total (fetch + serialize).
    // Budget is ~150ms. 10K ALWAYS fits. Don't over-engineer with rate estimates.
    //
    // If the pool has fewer TXs, we just get fewer. If after fetching we're
    // critically out of time, we truncate THEN (post-fetch, not pre-fetch).
    // =========================================================================
    Transaction** txs = safe_malloc(max_txs_per_block * sizeof(Transaction*));
    memset(txs, 0, max_txs_per_block * sizeof(Transaction*));
    uint32_t tx_count = 0;
    uint64_t total_fees = 0;
    
    char request[256];
    snprintf(request, sizeof(request), "GET_FOR_WINNER:%u:%u", 
             max_txs_per_block, v->current_challenge.target_block_height);
    
    uint64_t step2_start = get_current_time_ms();
    LOG_INFO("   Fetching TXs from pool...");
    zmq_send(v->pool_req, request, strlen(request), 0);
    size = zmq_recv(v->pool_req, buffer, VALIDATOR_BUFFER_SIZE - 1, 0);
    
    if (size > 0) {
        // v45.2: Protobuf TX response from pool
        // Format: "TXPB" (4B) + protobuf TransactionBatch
        // Pool serializes TXs using protobuf with zero-copy pointers.
        // Validator unpacks using protobuf-c for schema-safe deserialization.
        if (size > 4 && memcmp(buffer, "TXPB", 4) == 0) {
            Blockchain__TransactionBatch* batch = 
                blockchain__transaction_batch__unpack(
                    NULL, size - 4, (uint8_t*)(buffer + 4));
            
            if (batch) {
                for (size_t i = 0; i < batch->n_transactions && tx_count < max_txs_per_block; i++) {
                    Blockchain__Transaction* pt = batch->transactions[i];
                    Transaction* tx = safe_malloc(sizeof(Transaction));
                    memset(tx, 0, sizeof(Transaction));
                    
                    tx->nonce = pt->nonce;
                    tx->expiry_block = pt->expiry_block;
                    if (pt->source_address.data && pt->source_address.len >= 20)
                        memcpy(tx->source_address, pt->source_address.data, 20);
                    if (pt->dest_address.data && pt->dest_address.len >= 20)
                        memcpy(tx->dest_address, pt->dest_address.data, 20);
                    tx->value = pt->value;
                    tx->fee = pt->fee;
                    if (pt->signature.data && pt->signature.len > 0) {
                        size_t slen = pt->signature.len;
                        if (slen > CRYPTO_SIG_MAX) slen = CRYPTO_SIG_MAX;
                        memcpy(tx->signature, pt->signature.data, slen);
                        tx->sig_len = slen;
                    }
                    if (pt->public_key.data && pt->public_key.len > 0) {
                        size_t pklen = pt->public_key.len;
                        if (pklen > CRYPTO_PUBKEY_MAX) pklen = CRYPTO_PUBKEY_MAX;
                        memcpy(tx->public_key, pt->public_key.data, pklen);
                        tx->pubkey_len = pklen;
                    }
                    tx->sig_type = pt->sig_type ? pt->sig_type : SIG_ECDSA;

                    txs[tx_count++] = tx;
                    total_fees += tx->fee;
                }
                blockchain__transaction_batch__free_unpacked(batch, NULL);
            }
        }
        // Legacy binary response fallback (BIN: format)
        else if (size > 8 && memcmp(buffer, "BIN:", 4) == 0) {
            uint32_t expected = 0;
            memcpy(&expected, buffer + 4, 4);
            uint8_t* tx_data = (uint8_t*)(buffer + 8);
            size_t available_bytes = size - 8;
            
            for (uint32_t i = 0; i < expected && i < max_txs_per_block; i++) {
                if ((i + 1) * sizeof(Transaction) > available_bytes) break;
                Transaction* tx = safe_malloc(sizeof(Transaction));
                memcpy(tx, tx_data + i * sizeof(Transaction), sizeof(Transaction));
                txs[tx_count++] = tx;
                total_fees += tx->fee;
            }
        } else {
            // Legacy text response fallback
            buffer[size] = '\0';
            
            char* saveptr = NULL;
            char* line = strtok_r(buffer, "\n", &saveptr);
            if (line) {
                uint32_t expected = (uint32_t)atoi(line);
                
                while ((line = strtok_r(NULL, "\n", &saveptr)) != NULL && 
                       tx_count < expected && tx_count < max_txs_per_block) {
                    Transaction* tx = transaction_deserialize(line);
                    if (tx) {
                        txs[tx_count++] = tx;
                        total_fees += tx->fee;
                    } else {
                        LOG_WARN("   ├─ ⚠️  Failed to deserialize transaction");
                    }
                }
            }
        }
    } else {
        LOG_WARN("   ├─ ⚠️  No response from pool (timeout or error)");
    }
    
    uint64_t step2_ms = get_current_time_ms() - step2_start;
    LOG_INFO("   Step 2 (Pool→Validator): %lu ms (%u TXs, fees: %lu)", step2_ms, tx_count, total_fees);
    
    // =========================================================================
    // STEP 3: Create the block structure
    // =========================================================================
    Block* block = block_create();
    if (!block) {
        LOG_ERROR("[%s] Failed to create block", v->name);
        free(buffer);
        for (uint32_t i = 0; i < tx_count; i++) {
            if (txs[i]) transaction_destroy(txs[i]);
        }
        free(txs);
        return false;
    }
    
    // Set block header
    memcpy(block->header.previous_hash, prev_hash, 32);
    block->header.height = v->current_challenge.target_block_height;
    block->header.timestamp = get_current_timestamp();
    block->header.difficulty = v->current_challenge.current_difficulty;
    memcpy(block->header.challenge_hash, v->current_challenge.challenge_hash, 32);
    memcpy(block->header.farmer_address, v->wallet->address, 20);
    
    // Re-generate proof to set in block header (we know we have a valid one)
    SpaceProof* proof = plot_find_proof(v->plot, 
                                        v->current_challenge.challenge_hash,
                                        v->current_challenge.current_difficulty);
    if (proof) {
        memcpy(block->header.proof_hash, proof->proof_hash, 28);
        block->header.proof_nonce = proof->nonce;
        memcpy(block->header.quality, proof->quality, 32);
        free(proof);
    }
    
    // =========================================================================
    // STEP 4: Create coinbase transaction (PAYING OURSELVES!)
    // =========================================================================
    uint64_t mining_reward = calculate_mining_reward(v->current_challenge.target_block_height);
    uint64_t total_reward = mining_reward + total_fees;
    
    Transaction* coinbase = transaction_create_coinbase(
        v->wallet->address,
        mining_reward,
        total_fees,
        v->current_challenge.target_block_height
    );
    
    if (!coinbase) {
        LOG_ERROR("[%s] Failed to create coinbase", v->name);
        block_destroy(block);
        free(buffer);
        for (uint32_t i = 0; i < tx_count; i++) {
            if (txs[i]) transaction_destroy(txs[i]);
        }
        free(txs);
        return false;
    }
    
    // Add coinbase FIRST (always index 0)
    block_add_transaction(block, coinbase);
    
    // Add all user transactions
    for (uint32_t i = 0; i < tx_count; i++) {
        if (txs[i]) {
            block_add_transaction(block, txs[i]);
        }
    }
    
    block->total_fees = total_fees;
    
    // Calculate block hash
    block_calculate_hash(block);
    
    char block_hash_hex[65];
    bytes_to_hex_buf(block->header.hash, 32, block_hash_hex);
    
    LOG_INFO("   Block: %u TXs, reward %lu, hash %.16s...", block->header.transaction_count, total_reward, block_hash_hex);
    
    // =========================================================================
    // STEP 5: Send block DIRECTLY to blockchain (PROTOBUF transport - v45.2)
    // Uses Google Protocol Buffers for serialization over ZMQ binary transport.
    // Format: "ADD_BLOCK_PB:" + [64B farmer_name] + [protobuf binary block]
    //
    // WHY PROTOBUF OVER HEX:
    //   OLD (hex):     10K TXs → bytes_to_hex per TX → 2.24MB hex string
    //                  + block_deserialize with strlen() per TX = O(N²)
    //                  = ~530ms total
    //   NEW (protobuf): 10K TXs → block_serialize_pb (zero-copy) → ~1.1MB binary
    //                  + block_deserialize_pb (protobuf unpack) = O(N)
    //                  = ~30-50ms total (~10-15× faster)
    //
    // WHY PROTOBUF OVER RAW BINARY:
    //   Protobuf provides schema evolution and cross-language support,
    //   essential for the future distributed/decentralized architecture.
    //   The ~5ms overhead vs raw memcpy is negligible vs the 500ms we save.
    // =========================================================================
    uint64_t step5_start = get_current_time_ms();
    LOG_INFO("📤 [%s] Sending block to blockchain (protobuf)...", v->name);
    
    size_t pb_len = 0;
    uint8_t* block_pb = block_serialize_pb(block, &pb_len);
    uint64_t ser_ms = get_current_time_ms() - step5_start;
    if (!block_pb) {
        LOG_ERROR("[%s] Failed to serialize block (protobuf)", v->name);
        block_destroy(block);
        free(buffer);
        free(txs);
        return false;
    }
    
    // Build message: "ADD_BLOCK_PB:" (13B) + farmer_name (64B) + protobuf data
    #define PB_BLOCK_HEADER_SIZE (13 + 64)
    size_t msg_len = PB_BLOCK_HEADER_SIZE + pb_len;
    uint8_t* msg = safe_malloc(msg_len);
    memcpy(msg, "ADD_BLOCK_PB:", 13);
    memset(msg + 13, 0, 64);
    strncpy((char*)(msg + 13), v->name, 63);
    memcpy(msg + PB_BLOCK_HEADER_SIZE, block_pb, pb_len);
    free(block_pb);
    
    zmq_send(v->blockchain_req, msg, msg_len, 0);
    size = zmq_recv(v->blockchain_req, buffer, VALIDATOR_BUFFER_SIZE - 1, 0);
    free(msg);
    
    bool blockchain_accepted = false;
    if (size > 0) {
        buffer[size] = '\0';
        blockchain_accepted = (strcmp(buffer, "OK") == 0);
    }
    uint64_t step5_ms = get_current_time_ms() - step5_start;
    LOG_INFO("   ├─ ⏱️  Step 5 (protobuf serialize: %lu ms, total: %lu ms, %zu bytes pb)",
             ser_ms, step5_ms, pb_len);
    
    // Log deadline status
    {
        int64_t deadline_delta = (int64_t)(get_current_time_ms() - v->deadline_ms);
        if (deadline_delta > 0) {
            LOG_WARN("   ⏰ Block submitted %ld ms PAST deadline", deadline_delta);
        } else {
            LOG_INFO("   ✅ Block submitted %ld ms BEFORE deadline", -deadline_delta);
        }
    }
    
    if (blockchain_accepted) {
        LOG_INFO("✅ [%s] Block #%u ACCEPTED by blockchain! Hash: %.16s...",
                 v->name, v->current_challenge.target_block_height, block_hash_hex);
        LOG_INFO("   └─ Blockchain confirms to pool via PUB/SUB (async, not in critical path)");
        v->blocks_accepted++;
        
        // v45.2: Step 6 (CONFIRM to pool) REMOVED from validator.
        // =====================================================================
        // WHY: The blockchain is the single source of truth for confirmations.
        // After blockchain_add_block() succeeds, the blockchain node publishes
        // CONFIRM_BLOCK on its PUB socket. The pool subscribes and removes
        // confirmed TXs asynchronously. This is correct because:
        //
        //   1. Only the blockchain knows if a block was truly accepted
        //   2. Validators could crash/lie about confirmation
        //   3. In distributed mode, validators don't know other nodes' state
        //   4. Async PUB/SUB removes 44ms from the block critical path
        //
        // The confirm_ms field in BLOCK_TIMING is now 0 (async via PUB/SUB).
        // =====================================================================
        
    } else {
        LOG_ERROR("❌ [%s] Block REJECTED by blockchain! Response: %s", v->name, buffer);
    }
    
    LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
    
    // Machine-readable timing for graph generation (Graph 2)
    // BLOCK_TIMING:height:tx_count:get_hash_ms:fetch_tx_ms:serialize_send_ms:confirm_ms:total_ms
    if (blockchain_accepted) {
        uint64_t total_block_ms = get_current_time_ms() - block_start_ms;
        LOG_INFO("BLOCK_TIMING:%u:%u:%lu:%lu:%lu:%lu:%lu",
                 v->current_challenge.target_block_height,
                 tx_count, step1_ms, step2_ms, step5_ms, step6_total_ms, total_block_ms);
    }
    
    // Cleanup
    block_destroy(block);
    free(buffer);
    free(txs);
    
    return blockchain_accepted;
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void validator_run(Validator* v) {
    if (!v) return;
    
    v->running = true;
    
    LOG_INFO("🚀 [%s] Validator started (v45.2 - Deadline-Aware)", v->name);
    LOG_INFO("   [%s] Parses BUDGET from WINNER, adjusts TX count to meet deadline", v->name);
    
    // v45.2: Ultra-low latency. 5ms timeout = 2.5ms average ZMQ latency.
    // Old 100ms → 50ms → now 5ms. Saves 45ms on the critical path.
    // Tight loop is fine for a blockchain validator (CPU is cheap, latency is expensive).
    int timeout = 5;
    zmq_setsockopt(v->metronome_sub, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    uint64_t current_challenge_id = UINT64_MAX;
    
    while (v->running) {
        char buffer[2048];
        int size = zmq_recv(v->metronome_sub, buffer, sizeof(buffer) - 1, 0);
        
        if (size > 0) {
            buffer[size] = '\0';
            
            if (starts_with(buffer, "CHALLENGE:")) {
                Challenge challenge;
                if (challenge_deserialize(buffer + 10, &challenge)) {
                    validator_handle_challenge(v, &challenge);
                    current_challenge_id = challenge.challenge_id;
                    validator_find_and_submit_proof(v);
                }
                
            } else if (starts_with(buffer, "WINNER:")) {
                // v45.2: WINNER:<name>|<challenge_id>|<block_height>|BUDGET:<ms>
                // Parse BUDGET to set deadline for block creation
                char winner_name[64] = {0};
                uint64_t win_challenge_id = 0;
                uint32_t win_block_height = 0;
                uint64_t budget_ms = 200;  // default if not provided
                
                const char* data = buffer + 7;
                const char* first_pipe = strchr(data, '|');
                if (first_pipe) {
                    size_t name_len = first_pipe - data;
                    if (name_len < sizeof(winner_name)) {
                        memcpy(winner_name, data, name_len);
                        winner_name[name_len] = '\0';
                    }
                    sscanf(first_pipe + 1, "%lu|%u", &win_challenge_id, &win_block_height);
                    
                    // Parse BUDGET:XXX from the message
                    const char* budget_str = strstr(buffer, "BUDGET:");
                    if (budget_str) {
                        budget_ms = strtoull(budget_str + 7, NULL, 10);
                    }
                }
                
                if (strcmp(winner_name, v->name) == 0 && 
                    win_challenge_id == current_challenge_id) {
                    // Set hard deadline
                    v->deadline_ms = get_current_time_ms() + budget_ms;
                    
                    LOG_INFO("🎉 [%s] WE WON challenge #%lu! Budget: %lu ms, deadline: %lu",
                             v->name, win_challenge_id, budget_ms, v->deadline_ms);
                    
                    if (validator_create_and_submit_block(v)) {
                        v->blocks_created++;
                    }
                } else if (winner_name[0] != '\0') {
                    LOG_INFO("📢 [%s] Winner is %s for challenge #%lu (not us)",
                             v->name, winner_name, win_challenge_id);
                }
                
            } else if (starts_with(buffer, "NO_WINNER:")) {
                LOG_INFO("📢 [%s] No winner this round - empty block created", v->name);
            }
        }
        
        // v45.2: NO usleep here. ZMQ_RCVTIMEO (50ms) already provides non-blocking.
        // Old code had usleep(10000) which added 10ms latency to EVERY message.
    }
    
    LOG_INFO("🛑 [%s] Validator stopped", v->name);
}

void validator_stop(Validator* v) {
    if (v) v->running = false;
}

void validator_set_max_txs_per_block(uint32_t max_txs) {
    max_txs_per_block = max_txs > 0 ? max_txs : MAX_TXS_PER_BLOCK_DEFAULT;
    LOG_INFO("MAX_TXS_PER_BLOCK set to %u", max_txs_per_block);
}

// =============================================================================
// STATISTICS
// =============================================================================

void validator_get_stats(const Validator* v, char* buffer, size_t size) {
    if (!v || !buffer) return;
    
    uint64_t elapsed = (get_current_time_ms() - v->start_time) / 1000;
    
    snprintf(buffer, size,
             "NAME:%s|PROOFS_FOUND:%lu|PROOFS_SUBMITTED:%lu|BLOCKS_CREATED:%lu|BLOCKS_ACCEPTED:%lu|UPTIME:%lu",
             v->name, v->proofs_found, v->proofs_submitted, 
             v->blocks_created, v->blocks_accepted, elapsed);
}

// =============================================================================
// CLEANUP
// =============================================================================

void validator_destroy(Validator* v) {
    if (!v) return;
    
    if (v->metronome_req) zmq_close(v->metronome_req);
    if (v->metronome_sub) zmq_close(v->metronome_sub);
    if (v->pool_req) zmq_close(v->pool_req);
    if (v->blockchain_req) zmq_close(v->blockchain_req);
    if (v->zmq_context) zmq_ctx_destroy(v->zmq_context);
    if (v->wallet) wallet_destroy(v->wallet);
    if (v->plot) plot_destroy(v->plot);
    
    free(v);
}
