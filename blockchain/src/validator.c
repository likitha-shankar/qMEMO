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
#include "../include/crypto_backend.h"
#include "../proto/blockchain.pb-c.h"
#include "../include/block.h"
#include "../include/transaction.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <omp.h>

// Buffer sizes - scale with max TXs per block
// Each TX in protobuf ≈ 160 bytes. Need headroom for headers + batch wrapper.
// 10K TXs = 1.6MB, 32K = 5.1MB, 65K = 10.2MB
#define VALIDATOR_BASE_BUFFER_SIZE (4 * 1024 * 1024)  // 4MB minimum
#define MAX_TXS_PER_BLOCK_DEFAULT 10000

// Batch processing constants for deadline-aware block building
// The validator fetches ALL available TXs from pool in one call,
// then verifies and adds them in small batches. After each batch,
// it checks the deadline. If time is running out, it stops and sends
// a partially-filled block. This ensures blocks are NEVER late.
#define VERIFY_BATCH_SIZE    1000    // TXs per verification batch
#define SEND_RESERVE_MS      80     // Reserve time for serialize+send+confirm
#define MIN_BATCH_BUDGET_MS  5      // Minimum ms needed to process one more batch

static uint32_t max_txs_per_block = MAX_TXS_PER_BLOCK_DEFAULT;
static size_t validator_buffer_size = VALIDATOR_BASE_BUFFER_SIZE;
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

Validator* validator_create(const char* name, uint32_t k_param, uint8_t sig_type) {
    Validator* v = safe_malloc(sizeof(Validator));
    memset(v, 0, sizeof(Validator));

    safe_strcpy(v->name, name, sizeof(v->name));
    v->k_param = k_param > 0 ? k_param : K_PARAM_DEFAULT;

    v->wallet = wallet_create_named(name, sig_type);
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
 * BATCH-PROCESSING ARCHITECTURE (v46 - Deadline-Aware Partial Blocks):
 * ====================================================================
 * 
 * 1. GET_LAST_HASH from blockchain                       (~1ms)
 * 2. Fetch ALL available TXs from pool in ONE call       (~15-100ms)
 * 3. Query ALL unique sender balances upfront             (~5ms)
 * 4. Create block structure + header + proof
 * 5. BATCH LOOP: verify + add TXs in batches of VERIFY_BATCH_SIZE
 *      - Sanity check + balance check per TX
 *      - Add valid TXs to block
 *      - CHECK DEADLINE after each batch → stop if time running out
 * 6. Create coinbase TX with accumulated fees
 * 7. Serialize (protobuf) + send to blockchain
 *
 * PARTIAL BLOCKS ARE OK: if we have 50K pending TXs but only time for 8K,
 * we send a block with 8K TXs. The remaining 42K stay in the pool for the
 * next block. A partial block is ALWAYS better than missing the deadline
 * (which results in an empty block from the metronome).
 *
 * WHY SINGLE POOL FETCH (not multiple small calls):
 *   Each ZMQ REQ/REP round-trip adds ~2ms overhead. For 50 batches of 1000,
 *   that's 100ms of pure round-trip overhead. A single fetch of 50K TXs
 *   takes ~50ms total. The batch processing happens locally after the fetch.
 *
 * WHY UPFRONT BALANCE QUERY:
 *   Balance queries go through the blockchain's single REP socket.
 *   Querying per-batch would serialize on that socket. One batch query
 *   for all unique senders (~5ms) vs 50 queries (~100ms).
 *
 * DISTRIBUTED-READY:
 *   When TXs come from thousands of wallets, the same pattern works.
 *   The pool has more diverse senders (more unique addresses), but the
 *   batch-verify-add-check-deadline loop is unchanged.
 */
bool validator_create_and_submit_block(Validator* v) {
    if (!v || !v->has_challenge) return false;
    
    uint64_t block_start_ms = get_current_time_ms();
    int64_t total_budget = (int64_t)(v->deadline_ms - block_start_ms);
    
    LOG_INFO("");
    LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
    LOG_INFO("🏆 [%s] WE WON! Creating block #%u (budget: %ld ms)...", 
             v->name, v->current_challenge.target_block_height, total_budget);
    
    if (total_budget < SEND_RESERVE_MS + 20) {
        LOG_WARN("❌ [%s] Budget too small (%ld ms), skipping block", v->name, total_budget);
        return false;
    }
    
    char* buffer = safe_malloc(validator_buffer_size);
    buffer[0] = '\0';
    
    // =========================================================================
    // STEP 1: Get previous block hash from blockchain (~1ms)
    // =========================================================================
    uint8_t prev_hash[32] = {0};
    uint64_t step1_start = get_current_time_ms();
    
    zmq_send(v->blockchain_req, "GET_LAST_HASH", 13, 0);
    int size = zmq_recv(v->blockchain_req, buffer, validator_buffer_size - 1, 0);
    if (size > 0) {
        buffer[size] = '\0';
        if (strcmp(buffer, "NONE") != 0 && size >= 64)
            hex_to_bytes_buf(buffer, prev_hash, 32);
    }
    uint64_t step1_ms = get_current_time_ms() - step1_start;
    LOG_INFO("   ├─ ⏱️  Step 1 (GET_LAST_HASH): %lu ms", step1_ms);
    
    // =========================================================================
    // STEP 2: Fetch pending transactions from pool (single call)
    // =========================================================================
    // v47.1: Dynamically cap fetch count based on remaining budget.
    // Pool fetch time scales with TX count:
    //   ~15ms base + (N/1000) × 5ms for sort+protobuf+ZMQ
    // Verify time: (N/1000) × 7.5ms per batch with OpenMP
    // We need to leave SEND_RESERVE_MS (80ms) for serialize+send.
    //
    // Without this cap: requesting 65K TXs takes 250ms pool fetch alone,
    // which exceeds the entire 200ms budget for 1s blocks → 0 TXs → empty block.
    // =========================================================================
    uint64_t step2_start = get_current_time_ms();
    int64_t remaining_budget = (int64_t)(v->deadline_ms - step2_start);
    
    // Calculate max TXs we can fetch AND process within remaining budget
    // Budget breakdown: fetch_time + verify_time + SEND_RESERVE_MS + margin
    // fetch_time ≈ 15 + (N/1000)*5 ms
    // verify_time ≈ (N/1000)*7.5 ms
    // So: remaining - 80(send) - 15(fetch_base) - 10(margin) = N * 0.0125
    // N = (remaining - 105) * 80
    uint32_t fetch_limit = max_txs_per_block;
    int64_t available_for_processing = remaining_budget - SEND_RESERVE_MS - 25;
    if (available_for_processing > 0) {
        uint32_t dynamic_limit = (uint32_t)(available_for_processing * 80);
        if (dynamic_limit < fetch_limit) {
            fetch_limit = dynamic_limit;
        }
    }
    // Minimum: at least 1000 TXs (one batch)
    if (fetch_limit < 1000) fetch_limit = 1000;
    // Cap at configured max
    if (fetch_limit > max_txs_per_block) fetch_limit = max_txs_per_block;
    
    Transaction** txs = safe_malloc(max_txs_per_block * sizeof(Transaction*));
    memset(txs, 0, max_txs_per_block * sizeof(Transaction*));
    uint32_t tx_count = 0;
    
    char request[256];
    snprintf(request, sizeof(request), "GET_FOR_WINNER:%u:%u", 
             fetch_limit, v->current_challenge.target_block_height);
    
    LOG_INFO("   ├─ Fetching up to %u TXs from pool (budget: %ld ms, cap from %u)...", 
             fetch_limit, remaining_budget, max_txs_per_block);
    zmq_send(v->pool_req, request, strlen(request), 0);
    size = zmq_recv(v->pool_req, buffer, validator_buffer_size - 1, 0);
    uint64_t t2_ns = get_current_time_ns();

    // Diagnostic timestamp arrays from TXTS sidecar.
    // Copied into owned heap arrays immediately — buffer is reused for step 3 balance query.
    uint64_t* diag_t0 = NULL;
    uint64_t* diag_t1 = NULL;
    uint32_t  diag_ts_count = 0;

    const uint8_t* pool_pb_data = NULL;
    size_t pool_pb_len = 0;

    if (size > 8 && memcmp(buffer, "TXTS", 4) == 0) {
        memcpy(&diag_ts_count, buffer + 4, 4);
        size_t ts_hdr = 8 + (size_t)diag_ts_count * 16;
        if ((size_t)size > ts_hdr && diag_ts_count > 0) {
            diag_t0 = safe_malloc(diag_ts_count * sizeof(uint64_t));
            diag_t1 = safe_malloc(diag_ts_count * sizeof(uint64_t));
            memcpy(diag_t0, buffer + 8,                      diag_ts_count * 8);
            memcpy(diag_t1, buffer + 8 + diag_ts_count * 8, diag_ts_count * 8);
            pool_pb_data = (uint8_t*)buffer + ts_hdr;
            pool_pb_len  = (size_t)size - ts_hdr;
        }
    } else if (size > 4 && memcmp(buffer, "TXPB", 4) == 0) {
        pool_pb_data = (uint8_t*)buffer + 4;
        pool_pb_len  = (size_t)size - 4;
    }

    if (pool_pb_data && pool_pb_len > 0) {
        Blockchain__TransactionBatch* batch =
            blockchain__transaction_batch__unpack(NULL, pool_pb_len, pool_pb_data);
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
                    size_t sig_len = pt->signature.len;
                    if (sig_len > CRYPTO_SIG_MAX) sig_len = CRYPTO_SIG_MAX;
                    memcpy(tx->signature, pt->signature.data, sig_len);
                    tx->sig_len = sig_len;
                }
                if (pt->public_key.data && pt->public_key.len > 0) {
                    size_t pk_len = pt->public_key.len;
                    if (pk_len > CRYPTO_PUBKEY_MAX) pk_len = CRYPTO_PUBKEY_MAX;
                    memcpy(tx->public_key, pt->public_key.data, pk_len);
                    tx->pubkey_len = pk_len;
                }
                tx->sig_type = pt->sig_type ? (uint8_t)pt->sig_type : SIG_ED25519;
                txs[tx_count++] = tx;
            }
            blockchain__transaction_batch__free_unpacked(batch, NULL);
        }
    }
    uint64_t t2_5_ns = get_current_time_ns();

    if (size > 8 && memcmp(buffer, "BIN:", 4) == 0) {
        uint32_t expected = 0;
        memcpy(&expected, buffer + 4, 4);
        uint8_t* tx_data = (uint8_t*)(buffer + 8);
        size_t available_bytes = size - 8;
        for (uint32_t i = 0; i < expected && i < max_txs_per_block; i++) {
            if ((i + 1) * sizeof(Transaction) > available_bytes) break;
            Transaction* tx = safe_malloc(sizeof(Transaction));
            memcpy(tx, tx_data + i * sizeof(Transaction), sizeof(Transaction));
            txs[tx_count++] = tx;
        }
    } else if (size <= 0) {
        LOG_WARN("   ├─ ⚠️  No response from pool");
    }
    
    uint64_t step2_ms = get_current_time_ms() - step2_start;
    LOG_INFO("   ├─ ⏱️  Step 2 (Pool fetch): %lu ms (%u TXs, %d bytes)", 
             step2_ms, tx_count, size);
    
    // =========================================================================
    // STEP 3: Query all unique sender balances upfront (one batch call, ~5ms)
    // =========================================================================
    uint64_t step3_start = get_current_time_ms();
    
    #define MAX_SENDERS 1024
    uint8_t unique_addrs[MAX_SENDERS][20];
    uint64_t cached_balances[MAX_SENDERS];
    bool sender_rejected[MAX_SENDERS];
    uint32_t unique_count = 0;
    memset(cached_balances, 0, sizeof(cached_balances));
    memset(sender_rejected, 0, sizeof(sender_rejected));
    
    // TXs are sorted by (source_address, nonce) → sequential scan finds uniques
    for (uint32_t i = 0; i < tx_count && unique_count < MAX_SENDERS; i++) {
        bool found = (unique_count > 0 && 
            memcmp(txs[i]->source_address, unique_addrs[unique_count - 1], 20) == 0);
        if (!found) {
            memcpy(unique_addrs[unique_count], txs[i]->source_address, 20);
            unique_count++;
        }
    }
    
    if (unique_count > 0 && tx_count > 0) {
        size_t req_size = 23 + (size_t)unique_count * 20;
        uint8_t* req = safe_malloc(req_size);
        memcpy(req, "GET_BALANCES_BATCH:", 19);
        memcpy(req + 19, &unique_count, 4);
        for (uint32_t i = 0; i < unique_count; i++)
            memcpy(req + 23 + i * 20, unique_addrs[i], 20);
        zmq_send(v->blockchain_req, req, req_size, 0);
        int bal_size = zmq_recv(v->blockchain_req, buffer, validator_buffer_size - 1, 0);
        if (bal_size >= 8 && memcmp(buffer, "BAL:", 4) == 0) {
            uint32_t bal_count = 0;
            memcpy(&bal_count, buffer + 4, 4);
            for (uint32_t i = 0; i < bal_count && i < unique_count; i++)
                memcpy(&cached_balances[i], buffer + 8 + i * 8, 8);
        }
        free(req);
    }
    
    uint64_t step3_ms = get_current_time_ms() - step3_start;
    LOG_INFO("   ├─ ⏱️  Step 3 (Balance query): %lu ms (%u senders)", step3_ms, unique_count);
    
    // =========================================================================
    // STEP 4: Create block + BATCH LOOP (deadline-aware partial blocks)
    // =========================================================================
    // Process TXs in batches of VERIFY_BATCH_SIZE. After each batch, check
    // the deadline. Stop when time_remaining < SEND_RESERVE_MS.
    // =========================================================================
    Block* block = block_create();
    if (!block) {
        LOG_ERROR("[%s] Failed to create block", v->name);
        if (diag_t0) free(diag_t0);
        if (diag_t1) free(diag_t1);
        free(buffer); free(txs);
        return false;
    }
    
    // Set block header
    memcpy(block->header.previous_hash, prev_hash, 32);
    block->header.height = v->current_challenge.target_block_height;
    block->header.timestamp = get_current_timestamp();
    block->header.difficulty = v->current_challenge.current_difficulty;
    memcpy(block->header.challenge_hash, v->current_challenge.challenge_hash, 32);
    memcpy(block->header.farmer_address, v->wallet->address, 20);
    
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
    // STEP 4a: Add PLACEHOLDER coinbase at index 0 (fees=0 for now)
    // =========================================================================
    // We need coinbase at index 0 before the batch loop so that
    // block_add_transaction puts user TXs at indices 1..N.
    // After the batch loop, we UPDATE the coinbase value with real fees.
    // This avoids the dangerous right-shift that caused buffer overflow:
    //   OLD: add 10000 user TXs → shift all right → write to index 10000
    //        (OUT OF BOUNDS on a 10000-slot array!)
    // =========================================================================
    uint64_t mining_reward = calculate_mining_reward(v->current_challenge.target_block_height);
    Transaction* coinbase_placeholder = transaction_create_coinbase(
        v->wallet->address, mining_reward, 0 /* fees updated later */,
        v->current_challenge.target_block_height);
    
    if (!coinbase_placeholder) {
        LOG_ERROR("[%s] Failed to create coinbase", v->name);
        if (diag_t0) free(diag_t0);
        if (diag_t1) free(diag_t1);
        block_destroy(block); free(buffer);
        for (uint32_t i = 0; i < tx_count; i++) if (txs[i]) transaction_destroy(txs[i]);
        free(txs);
        return false;
    }
    block_add_transaction(block, coinbase_placeholder);  // index 0
    transaction_destroy(coinbase_placeholder);
    
    // ─── STEP 4b: BATCH PROCESSING LOOP ─────────────────────────────────
    // For each batch of VERIFY_BATCH_SIZE TXs:
    //   Phase A: PARALLEL Ed25519 signature verification (OpenMP)
    //            Each TX independently verifiable — embarrassingly parallel.
    //            If pubkey available: full Ed25519 verify (recompute hash, check sig)
    //            If no pubkey: basic sanity check (non-zero sig)
    //   Phase B: SEQUENTIAL balance check (must track running balance per sender)
    //            Walk verified TXs, deduct value+fee from cached balances.
    //   Phase C: Add valid TXs to block
    //   Phase D: CHECK DEADLINE → stop if approaching, send partial block
    //
    // This matches production blockchain design (e.g., Bitcoin's ConnectBlock):
    //   - Signatures: embarrassingly parallel (no shared state)
    //   - Balances: sequential per sender (running balance dependency)
    //
    // For distributed: when TXs come from 1000s of wallets, the same pattern
    // works. More unique senders → more parallelism in sig verification,
    // and the sequential balance walk handles any sender count.
    // ─────────────────────────────────────────────────────────────────────
    uint64_t batch_loop_start = get_current_time_ms();
    uint32_t txs_added = 0;
    uint64_t total_fees = 0;
    uint32_t batches_processed = 0;
    uint32_t sig_failures = 0;
    uint32_t balance_failures = 0;
    uint64_t total_sig_ms = 0;
    bool deadline_stopped = false;
    bool block_full = false;
    
    // Pre-allocate per-batch validity array (reused across batches)
    uint8_t* batch_valid = safe_malloc(VERIFY_BATCH_SIZE);

    // tx_diag_<pid>.csv — per-TX pipeline timestamps
#ifndef DIAG_OFF
    static FILE* tx_csv = NULL;
    if (!tx_csv) {
        char csv_path[64];
        snprintf(csv_path, sizeof(csv_path), "tx_diag_%d.csv", (int)getpid());
        tx_csv = fopen(csv_path, "w");
        if (tx_csv) {
            fprintf(tx_csv,
                "tx_nonce_hex,tx_size_bytes,t0_ns,t1_ns,t2_ns,t2_5_ns,t3_ns,src_addr_hex\n");
            fflush(tx_csv);
        }
    }
#endif

    for (uint32_t batch_start = 0; batch_start < tx_count && !block_full; batch_start += VERIFY_BATCH_SIZE) {
        // ── DEADLINE CHECK before each batch ──
        int64_t time_left = (int64_t)(v->deadline_ms - get_current_time_ms());
        if (time_left < SEND_RESERVE_MS + MIN_BATCH_BUDGET_MS) {
            deadline_stopped = true;
            LOG_INFO("   ├─ ⏰ Deadline in %ld ms → stopping at %u TXs (batch %u)",
                     time_left, txs_added, batches_processed);
            break;
        }
        
        uint32_t batch_end = batch_start + VERIFY_BATCH_SIZE;
        if (batch_end > tx_count) batch_end = tx_count;
        uint32_t batch_size = batch_end - batch_start;
        
        // ──────────────────────────────────────────────────────────────
        // PHASE A: PARALLEL PQC signature verification (OpenMP)
        // ──────────────────────────────────────────────────────────────
        // Each TX can be verified independently:
        //   1. TX carries its own pubkey + sig_type inline (no separate array)
        //   2. transaction_verify() dispatches by tx->sig_type via crypto_backend
        //   3. Recompute tx_hash = BLAKE3(nonce||expiry||src||dst||value||fee)
        //   4. crypto_verify_typed(sig_type, pubkey, tx_hash, signature)
        //
        // With 8 OpenMP threads and 1000 TXs per batch:
        //   125 TXs/thread × ~0.06ms/verify = ~7.5ms per batch (Ed25519)
        //   Falcon-512 and ML-DSA-44 are faster on modern hardware.
        // ──────────────────────────────────────────────────────────────
        memset(batch_valid, 1, batch_size);
        uint32_t batch_sig_fail = 0;
        uint64_t sig_start = get_current_time_ms();
        uint64_t* batch_t3 = safe_malloc(batch_size * sizeof(uint64_t));

        #pragma omp parallel for schedule(static) reduction(+:batch_sig_fail)
        for (uint32_t bi = 0; bi < batch_size; bi++) {
            uint32_t i = batch_start + bi;
            Transaction* tx = txs[i];
            if (!tx) { batch_valid[bi] = 0; batch_sig_fail++; batch_t3[bi] = 0; continue; }

            // TX carries its own pubkey inline — dispatch by sig_type
            if (!transaction_verify(tx)) {
                batch_valid[bi] = 0;
                batch_sig_fail++;
            }
            batch_t3[bi] = get_current_time_ns();
        }

        total_sig_ms += get_current_time_ms() - sig_start;
        sig_failures += batch_sig_fail;

        // Write per-TX diagnostics for this batch
#ifndef DIAG_OFF
        if (tx_csv) {
            char src_hex[41];
            for (uint32_t bi = 0; bi < batch_size; bi++) {
                uint32_t i = batch_start + bi;
                Transaction* tx = txs[i];
                if (!tx) continue;
                size_t tx_bytes = 64 + tx->sig_len + tx->pubkey_len;
                uint64_t t0 = (diag_t0 && i < diag_ts_count) ? diag_t0[i] : 0;
                uint64_t t1 = (diag_t1 && i < diag_ts_count) ? diag_t1[i] : 0;
                bytes_to_hex_buf(tx->source_address, 20, src_hex);
                fprintf(tx_csv, "%016lx,%zu,%lu,%lu,%lu,%lu,%lu,%s\n",
                        (unsigned long)tx->nonce, tx_bytes,
                        (unsigned long)t0, (unsigned long)t1,
                        (unsigned long)t2_ns, (unsigned long)t2_5_ns,
                        (unsigned long)batch_t3[bi], src_hex);
            }
            fflush(tx_csv);
        }
#endif
        free(batch_t3);
        
        // ──────────────────────────────────────────────────────────────
        // PHASE B: SEQUENTIAL balance check + add to block
        // ──────────────────────────────────────────────────────────────
        // Must be sequential: running balance per sender depends on
        // previous TX from same sender in this block.
        // ──────────────────────────────────────────────────────────────
        for (uint32_t bi = 0; bi < batch_size; bi++) {
            if (!batch_valid[bi]) continue;
            
            uint32_t i = batch_start + bi;
            Transaction* tx = txs[i];
            
            // Block full?
            if (block->header.transaction_count >= MAX_TRANSACTIONS_PER_BLOCK) {
                block_full = true;
                break;
            }
            
            // Find sender in cached balances
            int sender_idx = -1;
            for (uint32_t j = 0; j < unique_count; j++) {
                if (memcmp(tx->source_address, unique_addrs[j], 20) == 0) {
                    sender_idx = (int)j; break;
                }
            }
            if (sender_idx < 0 || sender_rejected[sender_idx]) {
                balance_failures++; continue;
            }
            
            // Balance check
            uint64_t required = tx->value + tx->fee;
            if (cached_balances[sender_idx] < required) {
                sender_rejected[sender_idx] = true;
                balance_failures++; continue;
            }
            cached_balances[sender_idx] -= required;
            
            // Add to block (block_add_transaction copies the TX)
            if (!block_add_transaction(block, tx)) {
                block_full = true;
                break;
            }
            total_fees += tx->fee;
            txs_added++;
        }
        batches_processed++;
    }
    
    free(batch_valid);
    
    uint64_t batch_ms = get_current_time_ms() - batch_loop_start;
    LOG_INFO("   ├─ ⏱️  Step 4 (Batch verify+add): %lu ms (sig: %lums, %u batches, %u/%u TXs%s%s)",
             batch_ms, total_sig_ms, batches_processed, txs_added, tx_count,
             deadline_stopped ? ", PARTIAL:deadline" : "",
             block_full ? ", PARTIAL:block_full" : "");
    if (sig_failures || balance_failures)
        LOG_INFO("   ├─ ⚠️  Rejected: %u sig, %u balance", sig_failures, balance_failures);
    
    // =========================================================================
    // STEP 5: UPDATE coinbase at index 0 with real accumulated fees
    // =========================================================================
    // The placeholder coinbase has value = mining_reward + 0.
    // Now that we know total_fees, update it to mining_reward + total_fees.
    // The coinbase is at block->transactions[0] (a copy made by block_add_transaction).
    if (block->transactions[0] && total_fees > 0) {
        block->transactions[0]->value = mining_reward + total_fees;
    }
    
    block->total_fees = total_fees;
    block_calculate_hash(block);
    
    char block_hash_hex[65];
    bytes_to_hex_buf(block->header.hash, 32, block_hash_hex);
    LOG_INFO("   ├─ 📦 Block: %u TXs (%u user + coinbase), reward %lu, hash %.16s...", 
             block->header.transaction_count, txs_added, mining_reward + total_fees, block_hash_hex);
    
    // =========================================================================
    // STEP 6: Serialize (protobuf) + send to blockchain
    // =========================================================================
    uint64_t step6_start = get_current_time_ms();
    
    size_t pb_len = 0;
    uint8_t* block_pb = block_serialize_pb(block, &pb_len);
    uint64_t ser_ms = get_current_time_ms() - step6_start;
    if (!block_pb) {
        LOG_ERROR("[%s] Failed to serialize block", v->name);
        if (diag_t0) free(diag_t0);
        if (diag_t1) free(diag_t1);
        block_destroy(block); free(buffer);
        for (uint32_t i = 0; i < tx_count; i++) if (txs[i]) transaction_destroy(txs[i]);
        free(txs);
        return false;
    }
    
    #define PB_BLOCK_HEADER_SIZE (13 + 64)
    size_t msg_len = PB_BLOCK_HEADER_SIZE + pb_len;
    uint8_t* msg = safe_malloc(msg_len);
    memcpy(msg, "ADD_BLOCK_PB:", 13);
    memset(msg + 13, 0, 64);
    strncpy((char*)(msg + 13), v->name, 63);
    memcpy(msg + PB_BLOCK_HEADER_SIZE, block_pb, pb_len);
    free(block_pb);
    
    zmq_send(v->blockchain_req, msg, msg_len, 0);
    size = zmq_recv(v->blockchain_req, buffer, validator_buffer_size - 1, 0);
    free(msg);
    
    bool blockchain_accepted = false;
    if (size > 0) {
        buffer[size] = '\0';
        blockchain_accepted = (strcmp(buffer, "OK") == 0);
    }
    uint64_t step6_ms = get_current_time_ms() - step6_start;
    
    LOG_INFO("   ├─ ⏱️  Step 6 (ser: %lums, total: %lums, %zu bytes)", ser_ms, step6_ms, pb_len);
    
    {
        int64_t delta = (int64_t)(get_current_time_ms() - v->deadline_ms);
        if (delta > 0) LOG_WARN("   ⏰ Block %ld ms PAST deadline", delta);
        else           LOG_INFO("   ✅ Block %ld ms BEFORE deadline", -delta);
    }
    
    if (blockchain_accepted) {
        LOG_INFO("✅ [%s] Block #%u ACCEPTED! %u/%u TXs (%.0f%%%s)",
                 v->name, v->current_challenge.target_block_height,
                 txs_added, tx_count,
                 tx_count > 0 ? (txs_added * 100.0 / tx_count) : 100.0,
                 deadline_stopped ? ", partial block" : "");
        v->blocks_accepted++;
    } else {
        LOG_ERROR("❌ [%s] Block REJECTED! Response: %s", v->name, buffer);
    }
    
    LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
    
    if (blockchain_accepted) {
        uint64_t total_block_ms = get_current_time_ms() - block_start_ms;
        LOG_INFO("BLOCK_TIMING:%u:%u:%lu:%lu:%lu:0:%lu",
                 v->current_challenge.target_block_height,
                 txs_added, step1_ms, step2_ms, step6_ms, total_block_ms);
    }
    
    // Cleanup
    if (diag_t0) free(diag_t0);
    if (diag_t1) free(diag_t1);
    for (uint32_t i = 0; i < tx_count; i++)
        if (txs[i]) transaction_destroy(txs[i]);
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
    // Scale buffer: 200 bytes per TX in protobuf + 16 bytes per TX for TXTS sidecar + 8 byte header
    size_t needed = (size_t)max_txs_per_block * 216 + 8;
    if (needed < VALIDATOR_BASE_BUFFER_SIZE) needed = VALIDATOR_BASE_BUFFER_SIZE;
    validator_buffer_size = needed;
    LOG_INFO("MAX_TXS_PER_BLOCK set to %u (buffer: %zu bytes)", max_txs_per_block, validator_buffer_size);
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
