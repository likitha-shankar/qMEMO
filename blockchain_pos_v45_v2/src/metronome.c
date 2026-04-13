/**
 * ============================================================================
 * METRONOME.C - Challenge Broadcaster & Winner Selector (v29)
 * ============================================================================
 * 
 * ARCHITECTURE (v29 - Proof-Based Winner Selection):
 * ==================================================
 * 
 * What metronome DOES:
 * - Broadcasts challenges every N seconds
 * - Receives LIGHTWEIGHT PROOFS (~100 bytes) from validators
 * - Selects winner (best proof quality)
 * - Announces winner via PUB socket
 * - Receives block confirmation DIRECTLY from blockchain via PULL socket (v29)
 * - Only broadcasts NEW challenge AFTER block is confirmed in chain
 * - Creates EMPTY blocks when NO validator submits valid proof
 * - Adjusts difficulty based on submissions
 * 
 * What metronome does NOT do:
 * - Does NOT fetch transactions from pool
 * - Does NOT create blocks with transactions
 * - Does NOT receive complete blocks from validators
 * - Does NOT wait for validator to relay block confirmation (v29 change)
 * 
 * ============================================================================
 */

#include "../include/metronome.h"
#include "../include/common.h"
#include "../include/transaction.h"
#include "../include/wallet.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>

// System address for empty blocks (no winner)
static const uint8_t SYSTEM_ADDRESS[20] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Buffer size for operations
#define METRONOME_BUFFER_SIZE (4 * 1024 * 1024)  // 4MB

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static int compare_quality(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 32);
}

// =============================================================================
// METRONOME CREATION
// =============================================================================

Metronome* metronome_create(uint32_t block_interval,
                            uint32_t difficulty_adjust_interval,
                            uint32_t k_param,
                            uint32_t initial_validator_count,
                            uint32_t initial_difficulty,
                            uint64_t base_mining_reward,
                            uint32_t halving_interval) {
    Metronome* m = safe_malloc(sizeof(Metronome));
    memset(m, 0, sizeof(Metronome));
    
    m->block_interval = block_interval > 0 ? block_interval : BLOCK_INTERVAL_DEFAULT;
    m->difficulty_adjust_interval = difficulty_adjust_interval > 0 ? 
                                    difficulty_adjust_interval : DIFFICULTY_ADJUST_INTERVAL;
    m->base_mining_reward = base_mining_reward > 0 ? base_mining_reward : BASE_MINING_REWARD;
    m->halving_interval = halving_interval > 0 ? halving_interval : HALVING_INTERVAL;
    m->k_param = k_param > 0 ? k_param : K_PARAM_DEFAULT;
    
    m->difficulty_state = difficulty_init(m->difficulty_adjust_interval, k_param, 
                                          initial_validator_count);
    if (initial_difficulty > 0) {
        difficulty_set(m->difficulty_state, initial_difficulty);
    }
    
    uint32_t actual_diff = difficulty_get_current(m->difficulty_state);
    
    m->challenge_counter = 0;
    m->best_submission_idx = -1;
    m->has_winner = false;
    m->block_confirmed = false;
    m->start_time = get_current_time_ms();
    
    // Initialize strict timing (v45.2)
    m->timing_idx = 0;
    m->timing_count = 0;
    m->t_create_avg_ms = TIMING_INITIAL_ESTIMATE_MS;
    m->empty_blocks_created = 0;
    
    // Compute initial proof window
    uint64_t block_time_ms = m->block_interval * 1000;
    m->t_proof_window_ms = block_time_ms - TIMING_INITIAL_ESTIMATE_MS - TIMING_MARGIN_MS;
    if (m->t_proof_window_ms < block_time_ms / 2) {
        m->t_proof_window_ms = block_time_ms / 2;  // never less than 50% of block time
    }
    
    LOG_INFO("📡 ════════════════════════════════════════════════════════════");
    LOG_INFO("📡 METRONOME CREATED (v45.2 - Strict Timing + Adaptive Window)");
    LOG_INFO("   ├─ Block interval:    %u seconds (STRICT - never exceeds)", m->block_interval);
    LOG_INFO("   ├─ Proof window:      %lu ms (adaptive, initial)", m->t_proof_window_ms);
    LOG_INFO("   ├─ Create budget:     %lu ms (adaptive, initial)", m->t_create_avg_ms);
    LOG_INFO("   ├─ Safety margin:     %u ms", TIMING_MARGIN_MS);
    LOG_INFO("   ├─ Difficulty:        %u (k=%u)", actual_diff, k_param);
    LOG_INFO("   ├─ Mining reward:     %lu (halves every %u blocks)",
             m->base_mining_reward, m->halving_interval);
    LOG_INFO("   └─ Architecture:      Fixed Clock → Proofs → Winner → Block → Confirm");
    LOG_INFO("📡 ════════════════════════════════════════════════════════════");
    
    return m;
}

bool metronome_init_sockets(Metronome* m,
                            const char* rep_addr,
                            const char* pub_addr,
                            const char* blockchain_addr,
                            const char* pool_addr,
                            const char* notify_pull_addr) {
    if (!m) return false;
    
    m->zmq_context = zmq_ctx_new();
    if (!m->zmq_context) {
        LOG_ERROR("Failed to create ZMQ context");
        return false;
    }
    
    // REP socket for receiving proofs and queries
    m->rep_socket = zmq_socket(m->zmq_context, ZMQ_REP);
    if (zmq_bind(m->rep_socket, rep_addr) != 0) {
        LOG_ERROR("Failed to bind REP socket to %s", rep_addr);
        return false;
    }
    LOG_INFO("🔌 Metronome REP bound to %s", rep_addr);
    
    // PUB socket for broadcasting challenges + winner announcements
    m->pub_socket = zmq_socket(m->zmq_context, ZMQ_PUB);
    if (zmq_bind(m->pub_socket, pub_addr) != 0) {
        LOG_ERROR("Failed to bind PUB socket to %s", pub_addr);
        return false;
    }
    LOG_INFO("🔌 Metronome PUB bound to %s", pub_addr);
    
    // REQ socket to blockchain (for empty blocks + last block queries)
    m->blockchain_req = zmq_socket(m->zmq_context, ZMQ_REQ);
    if (zmq_connect(m->blockchain_req, blockchain_addr) != 0) {
        LOG_ERROR("Failed to connect to blockchain at %s", blockchain_addr);
        return false;
    }
    LOG_INFO("🔌 Connected to blockchain at %s", blockchain_addr);
    
    // REQ socket to pool (for confirming TXs after block is added)
    m->pool_req = zmq_socket(m->zmq_context, ZMQ_REQ);
    if (zmq_connect(m->pool_req, pool_addr) != 0) {
        LOG_ERROR("Failed to connect to pool at %s", pool_addr);
        return false;
    }
    LOG_INFO("🔌 Connected to pool at %s", pool_addr);
    
    // PULL socket for block confirmations from blockchain (v29)
    if (notify_pull_addr) {
        m->notify_pull = zmq_socket(m->zmq_context, ZMQ_PULL);
        if (zmq_bind(m->notify_pull, notify_pull_addr) != 0) {
            LOG_ERROR("Failed to bind PULL socket to %s", notify_pull_addr);
            return false;
        }
        LOG_INFO("🔌 Metronome PULL bound to %s (blockchain notifications)", notify_pull_addr);
    } else {
        m->notify_pull = NULL;
        LOG_WARN("⚠️  No PULL socket configured - block confirmations via REP fallback");
    }
    
    return true;
}

// =============================================================================
// CHALLENGE GENERATION
// =============================================================================

void metronome_generate_challenge(Metronome* m) {
    if (!m) return;
    
    char* buffer = safe_malloc(METRONOME_BUFFER_SIZE);
    
    // Get last block hash from blockchain (v45.2: use GET_LAST_HASH, not GET_LAST)
    // GET_LAST would return the FULL serialized block (2MB for 10K TXs!)
    // GET_LAST_HASH returns just the 64-char hex hash string (~1ms vs ~200ms)
    zmq_send(m->blockchain_req, "GET_LAST_HASH", 13, 0);
    int size = zmq_recv(m->blockchain_req, buffer, METRONOME_BUFFER_SIZE - 1, 0);
    
    uint8_t prev_hash[32] = {0};
    
    if (size > 0) {
        buffer[size] = '\0';
        if (strcmp(buffer, "NONE") != 0 && strlen(buffer) >= 64) {
            hex_to_bytes_buf(buffer, prev_hash, 32);
        }
    }
    
    // Get current height
    zmq_send(m->blockchain_req, "GET_HEIGHT", 10, 0);
    size = zmq_recv(m->blockchain_req, buffer, METRONOME_BUFFER_SIZE - 1, 0);
    uint32_t height = 0;
    if (size > 0) {
        buffer[size] = '\0';
        height = atoi(buffer);
    }
    
    free(buffer);
    
    // Generate challenge hash from previous block
    uint64_t timestamp = get_current_timestamp();
    uint8_t challenge_input[32 + 8 + 8];
    memcpy(challenge_input, prev_hash, 32);
    memcpy(challenge_input + 32, &m->challenge_counter, 8);
    memcpy(challenge_input + 40, &timestamp, 8);
    
    blake3_hash(challenge_input, sizeof(challenge_input), m->current_challenge.challenge_hash);
    memcpy(m->current_challenge.prev_block_hash, prev_hash, 32);
    m->current_challenge.challenge_id = m->challenge_counter++;
    m->current_challenge.target_block_height = height;
    m->current_challenge.issued_at = timestamp;
    m->current_challenge.current_difficulty = difficulty_get_current(m->difficulty_state);
    
    // Reset submissions for new round
    m->submission_count = 0;
    m->best_submission_idx = -1;
    m->has_winner = false;
    m->block_confirmed = false;
    memset(m->confirmed_block_hash, 0, 32);
    memset(m->current_winner_name, 0, sizeof(m->current_winner_name));
    
    char challenge_hex[65];
    bytes_to_hex_buf(m->current_challenge.challenge_hash, 32, challenge_hex);
    char prev_hex[65];
    bytes_to_hex_buf(prev_hash, 32, prev_hex);
    
    LOG_INFO("");
    LOG_INFO("📡 ════════════════════════════════════════════════════════════");
    LOG_INFO("📡 NEW CHALLENGE #%lu for Block #%u",
             m->current_challenge.challenge_id,
             m->current_challenge.target_block_height);
    LOG_INFO("   ├─ 🔗 Based on Block #%u: %.16s...", height > 0 ? height - 1 : 0, prev_hex);
    LOG_INFO("   ├─ 🔑 Challenge Hash: %.16s...", challenge_hex);
    LOG_INFO("   ├─ 🎯 Difficulty: %u", m->current_challenge.current_difficulty);
    LOG_INFO("   └─ 💰 Reward: %lu coins + fees", 
             metronome_get_mining_reward(m, m->current_challenge.target_block_height));
    LOG_INFO("📡 ════════════════════════════════════════════════════════════");
}

void metronome_broadcast_challenge(Metronome* m) {
    if (!m) return;
    
    char* serialized = challenge_serialize(&m->current_challenge);
    char message[512];
    snprintf(message, sizeof(message), "CHALLENGE:%s", serialized);
    
    zmq_send(m->pub_socket, message, strlen(message), 0);
    free(serialized);
}

// =============================================================================
// PROOF SUBMISSION HANDLING (FROM VALIDATORS)
// =============================================================================

/**
 * Handle a lightweight proof submission from a validator.
 * 
 * Validators submit only their proof (~100 bytes), NOT complete blocks.
 * Metronome tracks the best proof and announces the winner.
 */
const char* metronome_submit_proof(Metronome* m,
                                   const SpaceProof* proof,
                                   const uint8_t farmer_address[20],
                                   const char* farmer_name) {
    if (!m || !proof || !farmer_name) return "INVALID";
    
    m->total_proofs_received++;
    
    // Verify proof is for current challenge
    if (!proof_verify(proof, m->current_challenge.challenge_hash,
                      m->current_challenge.current_difficulty)) {
        LOG_WARN("⚠️  [%s] Invalid proof submitted", farmer_name);
        return "INVALID_PROOF";
    }
    
    // Verify proof quality meets difficulty
    uint32_t leading_zeros = count_leading_zeros(proof->quality, 32);
    if (leading_zeros < m->current_challenge.current_difficulty) {
        LOG_WARN("⚠️  [%s] Proof quality insufficient: %u zeros, need %u",
                 farmer_name, leading_zeros, m->current_challenge.current_difficulty);
        return "INSUFFICIENT_QUALITY";
    }
    
    // Check if we have room
    if (m->submission_count >= MAX_PROOF_SUBMISSIONS) {
        return "SUBMISSIONS_FULL";
    }
    
    // Store the proof submission
    uint32_t idx = m->submission_count++;
    memcpy(&m->submissions[idx].proof, proof, sizeof(SpaceProof));
    memcpy(m->submissions[idx].farmer_address, farmer_address, 20);
    safe_strcpy(m->submissions[idx].farmer_name, farmer_name, 64);
    memcpy(m->submissions[idx].quality, proof->quality, 32);
    m->submissions[idx].is_valid = true;
    m->submissions[idx].submission_time = get_current_time_ms();
    
    // Check if this is the best submission
    bool is_best = false;
    if (m->best_submission_idx < 0) {
        m->best_submission_idx = idx;
        is_best = true;
    } else {
        // Compare quality (lower = better)
        if (compare_quality(m->submissions[idx].quality, 
                           m->submissions[m->best_submission_idx].quality) < 0) {
            m->best_submission_idx = idx;
            is_best = true;
        }
    }
    
    char quality_hex[65];
    bytes_to_hex_buf(proof->quality, 32, quality_hex);
    
    LOG_INFO("⚡ [%s] Proof received! Quality: %.16s... (%u zeros) %s",
             farmer_name, quality_hex, leading_zeros,
             is_best ? "← NEW LEADER!" : "");
    
    return is_best ? "PROOF_ACCEPTED" : "NOT_BEST";
}

// =============================================================================
// MINING REWARD CALCULATION
// =============================================================================

uint64_t metronome_get_mining_reward(const Metronome* m, uint32_t block_height) {
    if (!m) return 0;
    
    uint64_t reward = m->base_mining_reward;
    uint32_t halvings = block_height / m->halving_interval;
    
    for (uint32_t i = 0; i < halvings && reward > 1; i++) {
        reward /= 2;
    }
    
    return reward;
}

// =============================================================================
// ROUND FINALIZATION
// =============================================================================

/**
 * Finalize the round:
 * 1. Select winner (best proof quality)
 * 2. If winner: announce via PUB, wait for block confirmation
 * 3. If no winner: create empty block and add to blockchain
 * 4. New challenge is broadcast ONLY after block is in the chain
 */
bool metronome_finalize_round(Metronome* m) {
    if (!m) return false;
    
    LOG_INFO("");
    LOG_INFO("⏱️  ════════════════════════════════════════════════════════════");
    LOG_INFO("⏱️  ROUND COMPLETE - Finalizing Block #%u", 
             m->current_challenge.target_block_height);
    LOG_INFO("   └─ 📊 Total proofs received: %u", m->submission_count);
    
    bool has_winner = (m->best_submission_idx >= 0);
    bool success = false;
    
    char* buffer = safe_malloc(METRONOME_BUFFER_SIZE);
    
    if (has_winner) {
        // =====================================================================
        // WINNER FOUND - Announce and wait for block confirmation
        // =====================================================================
        ProofSubmission* winner = &m->submissions[m->best_submission_idx];
        
        char quality_hex[65];
        bytes_to_hex_buf(winner->quality, 32, quality_hex);
        char addr_hex[41];
        bytes_to_hex_buf(winner->farmer_address, 20, addr_hex);
        
        LOG_INFO("");
        LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
        LOG_INFO("🏆 PROOF WINNER: %s", winner->farmer_name);
        LOG_INFO("   ├─ ⚡ Quality: %.16s... (%u leading zeros)", quality_hex,
                 count_leading_zeros(winner->quality, 32));
        LOG_INFO("   ├─ 📍 Address: %.16s...", addr_hex);
        LOG_INFO("   └─ 📊 Beat %u other submissions", m->submission_count - 1);
        
        // Store winner info
        m->has_winner = true;
        safe_strcpy(m->current_winner_name, winner->farmer_name, 64);
        
        // Broadcast WINNER announcement via PUB
        char winner_msg[256];
        snprintf(winner_msg, sizeof(winner_msg), "WINNER:%s|%lu|%u",
                 winner->farmer_name, 
                 m->current_challenge.challenge_id,
                 m->current_challenge.target_block_height);
        zmq_send(m->pub_socket, winner_msg, strlen(winner_msg), 0);
        
        LOG_INFO("📢 Winner announced: %s - waiting for block confirmation...", 
                 winner->farmer_name);
        
        // =====================================================================
        // WAIT for blockchain to confirm block was added (via PULL socket)
        // Blockchain sends: BLOCK_CONFIRMED:<block_hash_hex>|<farmer_name>
        // Also handle late REP requests (proof submissions, etc.)
        // =====================================================================
        uint64_t wait_start = get_current_time_ms();
        uint64_t wait_timeout = WINNER_TIMEOUT_MS;
        
        // Set up zmq_poll for both PULL (blockchain notify) and REP (late requests)
        int poll_count = m->notify_pull ? 2 : 1;
        zmq_pollitem_t poll_items[2];
        
        // Item 0: REP socket (handle late proof submissions, queries)
        poll_items[0].socket = m->rep_socket;
        poll_items[0].events = ZMQ_POLLIN;
        
        // Item 1: PULL socket (blockchain notifications) - if configured
        if (m->notify_pull) {
            poll_items[1].socket = m->notify_pull;
            poll_items[1].events = ZMQ_POLLIN;
        }
        
        while (get_current_time_ms() - wait_start < wait_timeout && m->running) {
            int rc = zmq_poll(poll_items, poll_count, 100);  // 100ms poll timeout
            if (rc < 0) continue;
            
            // Check PULL socket first (blockchain notifications - preferred path)
            if (m->notify_pull && (poll_items[1].revents & ZMQ_POLLIN)) {
                int sz = zmq_recv(m->notify_pull, buffer, METRONOME_BUFFER_SIZE - 1, 0);
                if (sz > 0) {
                    buffer[sz] = '\0';
                    
                    if (starts_with(buffer, "BLOCK_CONFIRMED:")) {
                        const char* data = buffer + 16;
                        const char* pipe = strchr(data, '|');
                        
                        if (pipe) {
                            char hash_hex[65] = {0};
                            size_t hash_len = pipe - data;
                            if (hash_len <= 64) {
                                memcpy(hash_hex, data, hash_len);
                                hash_hex[hash_len] = '\0';
                                
                                char confirmed_name[64];
                                safe_strcpy(confirmed_name, pipe + 1, sizeof(confirmed_name));
                                trim(confirmed_name);
                                
                                hex_to_bytes_buf(hash_hex, m->confirmed_block_hash, 32);
                                m->block_confirmed = true;
                                
                                LOG_INFO("✅ Block confirmed (via PULL from blockchain) by %s! Hash: %.16s...",
                                         confirmed_name, hash_hex);
                                
                                m->total_blocks++;
                                success = true;
                                break;
                            }
                        }
                    }
                }
            }
            
            // Check REP socket (handle late requests)
            if (poll_items[0].revents & ZMQ_POLLIN) {
                int sz = zmq_recv(m->rep_socket, buffer, METRONOME_BUFFER_SIZE - 1, 0);
                
                if (sz > 0) {
                    buffer[sz] = '\0';
                    
                    if (starts_with(buffer, "BLOCK_CONFIRMED:")) {
                        // Fallback: validator relay (legacy support)
                        const char* data = buffer + 16;
                        const char* pipe = strchr(data, '|');
                        
                        if (pipe) {
                            char hash_hex[65] = {0};
                            size_t hash_len = pipe - data;
                            if (hash_len <= 64) {
                                memcpy(hash_hex, data, hash_len);
                                hash_hex[hash_len] = '\0';
                                
                                char confirmed_name[64];
                                safe_strcpy(confirmed_name, pipe + 1, sizeof(confirmed_name));
                                trim(confirmed_name);
                                
                                hex_to_bytes_buf(hash_hex, m->confirmed_block_hash, 32);
                                m->block_confirmed = true;
                                
                                LOG_INFO("✅ Block confirmed (via REP fallback) by %s! Hash: %.16s...",
                                         confirmed_name, hash_hex);
                                
                                m->total_blocks++;
                                success = true;
                                
                                zmq_send(m->rep_socket, "ACK", 3, 0);
                                break;
                            }
                        }
                        zmq_send(m->rep_socket, "INVALID_FORMAT", 14, 0);
                        
                    } else if (starts_with(buffer, "BLOCK_FAILED:")) {
                        const char* reason = buffer + 13;
                        LOG_WARN("❌ Winner failed to add block: %s", reason);
                        zmq_send(m->rep_socket, "ACK", 3, 0);
                        break;
                        
                    } else if (starts_with(buffer, "SUBMIT_PROOF:")) {
                        // Late proof submission - reject during finalization
                        zmq_send(m->rep_socket, "ROUND_ENDED", 11, 0);
                        
                    } else {
                        // Handle other requests normally
                        char response[4096];
                        metronome_handle_request(m, buffer, response, sizeof(response));
                        zmq_send(m->rep_socket, response, strlen(response), 0);
                    }
                }
            }
        }
        
        if (!m->block_confirmed) {
            LOG_WARN("⏰ Winner timeout! %s did not confirm block in %u ms",
                     winner->farmer_name, WINNER_TIMEOUT_MS);
            LOG_WARN("   Creating empty block as fallback...");
            // Fall through to empty block creation below
            has_winner = false;
        }
        
        LOG_INFO("🏆 ════════════════════════════════════════════════════════════");
        
    }
    
    if (!has_winner || !success) {
        // =====================================================================
        // NO WINNER or winner failed - Create EMPTY block
        // =====================================================================
        LOG_INFO("");
        LOG_INFO("⏳ NO VALID WINNER - Creating EMPTY block");
        LOG_INFO("   (Metronome only creates empty blocks when no validator wins)");
        
        Block* empty_block = block_create();
        if (!empty_block) {
            LOG_ERROR("Failed to create empty block");
            free(buffer);
            return false;
        }
        
        // Get previous block hash
        zmq_send(m->blockchain_req, "GET_LAST", 8, 0);
        int size = zmq_recv(m->blockchain_req, buffer, METRONOME_BUFFER_SIZE - 1, 0);
        
        if (size > 0) {
            buffer[size] = '\0';
            if (strcmp(buffer, "NONE") != 0) {
                Block* last = block_deserialize(buffer);
                if (last) {
                    memcpy(empty_block->header.previous_hash, last->header.hash, 32);
                    block_destroy(last);
                }
            }
        }
        
        // Set block header (NO transactions, NO coinbase)
        empty_block->header.height = m->current_challenge.target_block_height;
        empty_block->header.timestamp = get_current_timestamp();
        empty_block->header.difficulty = m->current_challenge.current_difficulty;
        memcpy(empty_block->header.challenge_hash, m->current_challenge.challenge_hash, 32);
        memcpy(empty_block->header.farmer_address, SYSTEM_ADDRESS, 20);
        
        block_calculate_hash(empty_block);
        
        char* block_hex = block_serialize(empty_block);
        if (block_hex) {
            snprintf(buffer, METRONOME_BUFFER_SIZE, "ADD_BLOCK:%s", block_hex);
            
            zmq_send(m->blockchain_req, buffer, strlen(buffer), 0);
            size = zmq_recv(m->blockchain_req, buffer, METRONOME_BUFFER_SIZE - 1, 0);
            
            if (size > 0) {
                buffer[size] = '\0';
                success = (strcmp(buffer, "OK") == 0);
            }
            
            if (success) {
                char block_hash_hex[65];
                bytes_to_hex_buf(empty_block->header.hash, 32, block_hash_hex);
                
                LOG_INFO("⛏️  ✅ EMPTY BLOCK #%u ADDED! Hash: %.16s...",
                         empty_block->header.height, block_hash_hex);
                LOG_INFO("   └─ 📦 No transactions (no winner)");
                
                m->total_blocks++;
            }
            
            free(block_hex);
        }
        
        // Broadcast NO_WINNER so validators know this round is done
        char no_winner_msg[128];
        snprintf(no_winner_msg, sizeof(no_winner_msg), "NO_WINNER:%lu",
                 m->current_challenge.challenge_id);
        zmq_send(m->pub_socket, no_winner_msg, strlen(no_winner_msg), 0);
        
        block_destroy(empty_block);
    }
    
    free(buffer);
    
    // Difficulty adjustment
    bool round_had_winner = (m->best_submission_idx >= 0 && m->block_confirmed);
    difficulty_record_winner(m->difficulty_state, round_had_winner);
    uint32_t old_diff = difficulty_get_current(m->difficulty_state);
    difficulty_adjust(m->difficulty_state);
    uint32_t new_diff = difficulty_get_current(m->difficulty_state);
    
    if (new_diff != old_diff) {
        LOG_INFO("");
        if (new_diff > old_diff) {
            LOG_INFO("📈 DIFFICULTY: %u → %u", old_diff, new_diff);
        } else {
            LOG_INFO("📉 DIFFICULTY: %u → %u", old_diff, new_diff);
        }
    }
    
    return success;
}

// =============================================================================
// API HANDLING
// =============================================================================

void metronome_handle_request(Metronome* m, const char* request, char* response, size_t resp_size) {
    if (!m || !request || !response) {
        if (response && resp_size > 0) snprintf(response, resp_size, "ERROR");
        return;
    }
    
    if (starts_with(request, "SUBMIT_PROOF:")) {
        // =====================================================================
        // SUBMIT_PROOF:<proof_hex>#<farmer_name>#<farmer_address_hex>
        // Validators submit lightweight proofs, NOT complete blocks!
        // NOTE: Using '#' as outer delimiter because proof_serialize uses '|'
        // =====================================================================
        const char* data = request + 13;
        
        // Parse: proof_hex#farmer_name#farmer_address_hex
        char* fields[3] = {NULL, NULL, NULL};
        char* data_copy = safe_malloc(strlen(data) + 1);
        strcpy(data_copy, data);
        
        int field_count = 0;
        char* saveptr = NULL;
        char* token = strtok_r(data_copy, "#", &saveptr);
        while (token && field_count < 3) {
            fields[field_count++] = token;
            token = strtok_r(NULL, "#", &saveptr);
        }
        
        if (field_count != 3) {
            snprintf(response, resp_size, "INVALID_FORMAT");
            free(data_copy);
            return;
        }
        
        // Deserialize proof
        SpaceProof* proof = proof_deserialize(fields[0]);
        if (!proof) {
            snprintf(response, resp_size, "INVALID_PROOF");
            free(data_copy);
            return;
        }
        
        // Parse farmer name
        char farmer_name[64];
        safe_strcpy(farmer_name, fields[1], sizeof(farmer_name));
        trim(farmer_name);
        
        // Parse farmer address
        uint8_t farmer_address[20];
        hex_to_bytes_buf(fields[2], farmer_address, 20);
        
        const char* result = metronome_submit_proof(m, proof, farmer_address, farmer_name);
        snprintf(response, resp_size, "%s", result);
        
        free(proof);
        free(data_copy);
        
    } else if (starts_with(request, "BLOCK_CONFIRMED:")) {
        // This is handled in finalize_round wait loop, but handle here as fallback
        snprintf(response, resp_size, "ACK");
        
    } else if (strcmp(request, "GET_CHALLENGE") == 0) {
        char* serialized = challenge_serialize(&m->current_challenge);
        snprintf(response, resp_size, "%s", serialized);
        free(serialized);
        
    } else if (strcmp(request, "GET_DIFFICULTY") == 0) {
        snprintf(response, resp_size, "%u", difficulty_get_current(m->difficulty_state));
        
    } else if (starts_with(request, "SET_DIFFICULTY:")) {
        uint32_t new_diff = atoi(request + 15);  // "SET_DIFFICULTY:" = 15 chars
        if (new_diff >= 1 && new_diff <= 256) {
            difficulty_set(m->difficulty_state, new_diff);
            LOG_INFO("🎯 DIFFICULTY RESET to %u (was %u)", new_diff,
                     difficulty_get_current(m->difficulty_state));
            snprintf(response, resp_size, "DIFFICULTY_SET:%u", new_diff);
        } else {
            snprintf(response, resp_size, "INVALID_DIFFICULTY");
        }
        
    } else if (strcmp(request, "GET_STATS") == 0) {
        metronome_get_stats(m, response, resp_size);
        
    } else if (strcmp(request, "GET_REWARD") == 0) {
        uint64_t reward = metronome_get_mining_reward(m, m->current_challenge.target_block_height);
        snprintf(response, resp_size, "%lu", reward);
        
    } else if (strcmp(request, "PAUSE") == 0) {
        m->paused = true;
        LOG_INFO("⏸️  METRONOME PAUSED (no new blocks until RESUME)");
        snprintf(response, resp_size, "PAUSED");
        
    } else if (strcmp(request, "RESUME") == 0) {
        m->paused = false;
        LOG_INFO("▶️  METRONOME RESUMED (block creation active)");
        snprintf(response, resp_size, "RESUMED");
        
    } else {
        snprintf(response, resp_size, "UNKNOWN_COMMAND");
    }
}

// =============================================================================
// STATISTICS
// =============================================================================

void metronome_get_stats(const Metronome* m, char* buffer, size_t size) {
    if (!m || !buffer) return;
    
    uint64_t uptime = (get_current_time_ms() - m->start_time) / 1000;
    
    snprintf(buffer, size,
             "BLOCKS:%lu|PROOFS_RECEIVED:%lu|UPTIME:%lu|DIFFICULTY:%u",
             m->total_blocks, m->total_proofs_received, uptime,
             difficulty_get_current(m->difficulty_state));
}

// =============================================================================
// ADAPTIVE TIMING (v45.2)
// =============================================================================

/**
 * Update moving average of block creation overhead.
 * Called after each successful block confirmation.
 * 
 * T_overhead = time from WINNER announcement to BLOCK_CONFIRMED receipt.
 * This includes: ZMQ delivery + validator block creation + blockchain processing.
 */
static void update_overhead(Metronome* m, uint64_t overhead_ms) {
    m->create_times[m->timing_idx] = overhead_ms;
    m->timing_idx = (m->timing_idx + 1) % TIMING_HISTORY_SIZE;
    if (m->timing_count < TIMING_HISTORY_SIZE) m->timing_count++;
    
    // Compute moving average + use worst case for safety
    uint64_t sum = 0, worst = 0;
    for (uint32_t i = 0; i < m->timing_count; i++) {
        sum += m->create_times[i];
        if (m->create_times[i] > worst) worst = m->create_times[i];
    }
    m->t_create_avg_ms = sum / m->timing_count;
    
    // Proof window = block_time - overhead_budget - margin
    // Budget = max(worst * 1.5, avg * 2, TIMING_MIN_BUDGET_MS)
    //
    // WHY conservative:
    //   If budget is too small → proof window too large → round overruns 1s
    //   → next proof window compressed → fewer proofs → empty blocks → wasted ticks
    //   A 650ms proof window still gives plenty of time for proof search.
    //   A 200ms minimum budget guarantees we NEVER overrun even in worst case.
    uint64_t block_time_ms = (uint64_t)m->block_interval * 1000;
    uint64_t budget = worst + worst / 2;  // worst * 1.5
    if (budget < m->t_create_avg_ms * 2) budget = m->t_create_avg_ms * 2;
    if (budget < TIMING_MIN_BUDGET_MS) budget = TIMING_MIN_BUDGET_MS;
    budget += TIMING_MARGIN_MS;
    
    if (budget < block_time_ms / 2) {
        m->t_proof_window_ms = block_time_ms - budget;
    } else {
        m->t_proof_window_ms = block_time_ms / 2;  // floor: 50% of block time
    }
    
    LOG_INFO("   📊 Adaptive: overhead=%lums, worst=%lums, avg=%lums → budget=%lums, window=%lums",
             overhead_ms, worst, m->t_create_avg_ms, budget, m->t_proof_window_ms);
}

/**
 * Create and submit an empty block to blockchain.
 * ONLY called when NO proofs were received (no winner to announce).
 */
static bool create_empty_block(Metronome* m) {
    char* buffer = safe_malloc(METRONOME_BUFFER_SIZE);
    
    Block* empty_block = block_create();
    if (!empty_block) {
        free(buffer);
        return false;
    }
    
    zmq_send(m->blockchain_req, "GET_LAST_HASH", 13, 0);
    int size = zmq_recv(m->blockchain_req, buffer, METRONOME_BUFFER_SIZE - 1, 0);
    if (size > 0) {
        buffer[size] = '\0';
        if (strcmp(buffer, "NONE") != 0 && strlen(buffer) >= 64) {
            hex_to_bytes_buf(buffer, empty_block->header.previous_hash, 32);
        }
    }
    
    empty_block->header.height = m->current_challenge.target_block_height;
    empty_block->header.timestamp = get_current_timestamp();
    empty_block->header.difficulty = m->current_challenge.current_difficulty;
    memcpy(empty_block->header.challenge_hash, m->current_challenge.challenge_hash, 32);
    memcpy(empty_block->header.farmer_address, SYSTEM_ADDRESS, 20);
    block_calculate_hash(empty_block);
    
    size_t pb_len = 0;
    uint8_t* pb_data = block_serialize_pb(empty_block, &pb_len);
    
    bool success = false;
    if (pb_data) {
        size_t msg_len = 13 + 64 + pb_len;
        uint8_t* msg = safe_malloc(msg_len);
        memcpy(msg, "ADD_BLOCK_PB:", 13);
        memset(msg + 13, 0, 64);
        strncpy((char*)(msg + 13), "METRONOME", 63);
        memcpy(msg + 77, pb_data, pb_len);
        
        zmq_send(m->blockchain_req, msg, msg_len, 0);
        size = zmq_recv(m->blockchain_req, buffer, 255, 0);
        
        if (size > 0) {
            buffer[size] = '\0';
            success = (strcmp(buffer, "OK") == 0);
        }
        free(msg);
        free(pb_data);
    }
    
    if (success) {
        m->total_blocks++;
        m->empty_blocks_created++;
        
        // CRITICAL: Drain the PUSH socket.
        // The blockchain sends BLOCK_CONFIRMED via PUSH for EVERY block,
        // including empty blocks we just submitted. If we don't drain it here,
        // the next round's Phase 3 will read this stale message and think
        // the winner's block was already confirmed → 28ms ghost block.
        if (m->notify_pull) {
            char drain[256];
            int drain_timeout = 50;  // Wait up to 50ms for the PUSH to arrive
            zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
            int drained = zmq_recv(m->notify_pull, drain, sizeof(drain) - 1, 0);
            if (drained > 0) {
                drain[drained] = '\0';
                LOG_INFO("🔄 Drained stale PUSH after empty block: %.40s...", drain);
            }
            // Restore default timeout
            int no_timeout = -1;
            zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &no_timeout, sizeof(no_timeout));
        }
    }
    
    char no_winner_msg[128];
    snprintf(no_winner_msg, sizeof(no_winner_msg), "NO_WINNER:%lu",
             m->current_challenge.challenge_id);
    zmq_send(m->pub_socket, no_winner_msg, strlen(no_winner_msg), 0);
    
    block_destroy(empty_block);
    free(buffer);
    return success;
}

// =============================================================================
// MAIN LOOP (v45.3 - Strict 1-Second Blocks with Hard Deadline)
// =============================================================================
//
// ALGORITHM:
//   block_time = 1000ms (HARD — every block is exactly 1 second apart)
//
//   LOOP:
//     t_tick = now (start of this round)
//     
//     Phase 1: Generate challenge, broadcast
//     Phase 2: Collect proofs until t_tick + proof_window (adaptive, ~650ms)
//     Phase 3:
//       IF winner → announce, wait for BLOCK_CONFIRMED until t_tick + block_time - MARGIN
//                   IF no confirm by deadline → create EMPTY block (validator was too slow)
//       ELSE     → create empty block (no proofs received)
//     Phase 4: ALWAYS sleep until t_tick + block_time (NEVER start early!)
//
// GUARANTEES:
//   - Every block is EXACTLY block_time apart (±1ms jitter)
//   - No block interval < 1 second (no rapid-fire catch-up)
//   - No block interval > 1 second (hard deadline → empty block)
//   - Validators that miss deadline lose their block (incentive to be fast)
//   - Self-correcting: slow validators → adaptive budget grows → window shrinks
//
// =============================================================================

void metronome_run(Metronome* m) {
    if (!m) return;
    
    m->running = true;
    uint64_t block_time_ms = (uint64_t)m->block_interval * 1000;
    
    LOG_INFO("");
    LOG_INFO("🚀 METRONOME STARTED (v45.3 - Strict 1-Second Blocks)");
    LOG_INFO("   Block time: %lu ms (STRICT — every block exactly this apart)", block_time_ms);
    LOG_INFO("   Proof window: %lu ms (adaptive)", m->t_proof_window_ms);
    LOG_INFO("   Hard deadline: %lu ms (empty block if validator misses)", 
             block_time_ms - TIMING_MARGIN_MS);
    LOG_INFO("   Empty blocks: when no proofs OR when validator misses deadline");
    
    int rep_timeout = 50;
    zmq_setsockopt(m->rep_socket, ZMQ_RCVTIMEO, &rep_timeout, sizeof(rep_timeout));
    
    while (m->running) {
        // ═════════════════════════════════════════════════════════════
        // t_tick = start of this round (set FRESH each iteration)
        // ═════════════════════════════════════════════════════════════
        uint64_t t_tick = get_current_time_ms();
        uint64_t t_deadline = t_tick + block_time_ms - TIMING_MARGIN_MS;
        uint64_t t_next = t_tick + block_time_ms;
        
        // Drain any stale PULL messages from previous rounds.
        // This prevents a leftover BLOCK_CONFIRMED (e.g. from an empty block
        // or overrun path) from being misread as a confirmation in THIS round.
        if (m->notify_pull) {
            int drain_timeout = 1;
            zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
            char drain[256];
            while (zmq_recv(m->notify_pull, drain, sizeof(drain), 0) > 0) {
                // discard stale messages
            }
        }
        
        // ═════════════════════════════════════════════════════════════
        // PAUSE CHECK
        // ═════════════════════════════════════════════════════════════
        if (m->paused) {
            char* request = safe_malloc(METRONOME_BUFFER_SIZE);
            int size = zmq_recv(m->rep_socket, request, METRONOME_BUFFER_SIZE - 1, 0);
            if (size > 0) {
                request[size] = '\0';
                char response[4096];
                metronome_handle_request(m, request, response, sizeof(response));
                zmq_send(m->rep_socket, response, strlen(response), 0);
            }
            free(request);
            usleep(50000);
            continue;
        }
        
        // ═════════════════════════════════════════════════════════════
        // PHASE 1: Generate and broadcast challenge
        // ═════════════════════════════════════════════════════════════
        
        // Safety: drain any stale BLOCK_CONFIRMED from previous rounds
        // This prevents ghost blocks from stale PUSH messages
        if (m->notify_pull) {
            char drain[256];
            int drain_timeout = 1;
            zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
            while (zmq_recv(m->notify_pull, drain, sizeof(drain) - 1, 0) > 0) {
                // Discard stale messages
            }
            int no_timeout = -1;
            zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &no_timeout, sizeof(no_timeout));
        }
        
        metronome_generate_challenge(m);
        metronome_broadcast_challenge(m);
        uint64_t t_after_challenge = get_current_time_ms();
        uint32_t round_height = m->current_challenge.target_block_height;
        
        // ═════════════════════════════════════════════════════════════
        // PHASE 2: Collect proofs for proof_window ms
        // ═════════════════════════════════════════════════════════════
        // FIX: Dynamic timeout to prevent overshoot.
        // OLD: Fixed 50ms zmq_recv timeout → loop could overshoot proof_end by 50ms
        //      → ate into validator's budget → deadline misses → empty blocks
        // NEW: Shrink timeout as proof_end approaches → exits within 5ms
        // ═════════════════════════════════════════════════════════════
        uint64_t proof_end = t_tick + m->t_proof_window_ms;
        
        while (get_current_time_ms() < proof_end && m->running) {
            // Dynamic timeout: remaining time or 50ms, whichever is smaller
            uint64_t now_loop = get_current_time_ms();
            int dynamic_timeout = (proof_end > now_loop) ? 
                (int)(proof_end - now_loop) : 0;
            if (dynamic_timeout > 50) dynamic_timeout = 50;
            if (dynamic_timeout < 1) dynamic_timeout = 1;
            zmq_setsockopt(m->rep_socket, ZMQ_RCVTIMEO, &dynamic_timeout, sizeof(dynamic_timeout));
            
            char* request = safe_malloc(METRONOME_BUFFER_SIZE);
            int size = zmq_recv(m->rep_socket, request, METRONOME_BUFFER_SIZE - 1, 0);
            if (size > 0) {
                request[size] = '\0';
                char response[4096];
                metronome_handle_request(m, request, response, sizeof(response));
                zmq_send(m->rep_socket, response, strlen(response), 0);
            }
            free(request);
        }
        // Restore standard timeout for Phase 3
        zmq_setsockopt(m->rep_socket, ZMQ_RCVTIMEO, &rep_timeout, sizeof(rep_timeout));
        
        // ═════════════════════════════════════════════════════════════
        // PHASE 3: Finalize - select winner and wait for block
        // ═════════════════════════════════════════════════════════════
        uint64_t t_after_proofs = get_current_time_ms();
        bool has_winner = (m->best_submission_idx >= 0);
        bool block_ok = false;
        
        if (has_winner) {
            // ─────────────────────────────────────────────────────────
            // WINNER FOUND: announce with BUDGET and wait until HARD DEADLINE
            // If validator doesn't deliver by t_deadline → EMPTY BLOCK
            // ─────────────────────────────────────────────────────────
            ProofSubmission* winner = &m->submissions[m->best_submission_idx];
            m->has_winner = true;
            safe_strcpy(m->current_winner_name, winner->farmer_name, 64);
            
            // Calculate budget: time from now until hard deadline
            uint64_t now_announce = get_current_time_ms();
            uint64_t budget_ms = (t_deadline > now_announce) ? (t_deadline - now_announce) : 0;
            
            char winner_msg[256];
            snprintf(winner_msg, sizeof(winner_msg), "WINNER:%s|%lu|%u|BUDGET:%lu",
                     winner->farmer_name, 
                     m->current_challenge.challenge_id,
                     m->current_challenge.target_block_height,
                     budget_ms);
            zmq_send(m->pub_socket, winner_msg, strlen(winner_msg), 0);
            
            uint64_t t_announce = get_current_time_ms();
            LOG_INFO("🏆 WINNER: %s (proofs: %u, budget: %lums) — deadline at +%lums",
                     winner->farmer_name, m->submission_count, budget_ms,
                     t_deadline - t_tick);
            
            // Wait for BLOCK_CONFIRMED until HARD DEADLINE
            char* buffer = safe_malloc(METRONOME_BUFFER_SIZE);
            
            int poll_count = m->notify_pull ? 2 : 1;
            zmq_pollitem_t poll_items[2];
            poll_items[0].socket = m->rep_socket;
            poll_items[0].fd = 0;
            poll_items[0].events = ZMQ_POLLIN;
            poll_items[0].revents = 0;
            if (m->notify_pull) {
                poll_items[1].socket = m->notify_pull;
                poll_items[1].fd = 0;
                poll_items[1].events = ZMQ_POLLIN;
                poll_items[1].revents = 0;
            }
            
            while (get_current_time_ms() < t_deadline && m->running && !block_ok) {
                uint64_t poll_timeout = t_deadline - get_current_time_ms();
                if (poll_timeout > 10) poll_timeout = 10;  // 10ms poll granularity
                
                int rc = zmq_poll(poll_items, poll_count, (long)poll_timeout);
                if (rc < 0) continue;
                
                // PULL socket (blockchain → metronome)
                if (m->notify_pull && (poll_items[1].revents & ZMQ_POLLIN)) {
                    int sz = zmq_recv(m->notify_pull, buffer, METRONOME_BUFFER_SIZE - 1, 0);
                    if (sz > 0) {
                        buffer[sz] = '\0';
                        if (starts_with(buffer, "BLOCK_CONFIRMED:")) {
                            const char* pipe = strchr(buffer + 16, '|');
                            if (pipe) {
                                char hash_hex[65] = {0};
                                size_t hlen = pipe - (buffer + 16);
                                if (hlen <= 64) {
                                    memcpy(hash_hex, buffer + 16, hlen);
                                    hex_to_bytes_buf(hash_hex, m->confirmed_block_hash, 32);
                                }
                                m->block_confirmed = true;
                                block_ok = true;
                                m->total_blocks++;
                                
                                uint64_t overhead_ms = get_current_time_ms() - t_announce;
                                update_overhead(m, overhead_ms);
                                
                                LOG_INFO("✅ Block confirmed in %lums (avg: %lums, window: %lums)",
                                         overhead_ms, m->t_create_avg_ms, m->t_proof_window_ms);
                            }
                        }
                    }
                }
                
                // REP socket (late proofs, queries, BLOCK_CONFIRMED fallback)
                if (poll_items[0].revents & ZMQ_POLLIN) {
                    int sz = zmq_recv(m->rep_socket, buffer, METRONOME_BUFFER_SIZE - 1, 0);
                    if (sz > 0) {
                        buffer[sz] = '\0';
                        
                        if (starts_with(buffer, "BLOCK_CONFIRMED:")) {
                            const char* pipe = strchr(buffer + 16, '|');
                            if (pipe) {
                                char hash_hex[65] = {0};
                                size_t hlen = pipe - (buffer + 16);
                                if (hlen <= 64) {
                                    memcpy(hash_hex, buffer + 16, hlen);
                                    hex_to_bytes_buf(hash_hex, m->confirmed_block_hash, 32);
                                }
                                m->block_confirmed = true;
                                block_ok = true;
                                m->total_blocks++;
                                
                                uint64_t overhead_ms = get_current_time_ms() - t_announce;
                                update_overhead(m, overhead_ms);
                                
                                zmq_send(m->rep_socket, "ACK", 3, 0);
                                LOG_INFO("✅ Block confirmed (REP) in %lums", overhead_ms);
                            } else {
                                zmq_send(m->rep_socket, "INVALID_FORMAT", 14, 0);
                            }
                        } else if (starts_with(buffer, "SUBMIT_PROOF:")) {
                            zmq_send(m->rep_socket, "ROUND_ENDED", 11, 0);
                        } else {
                            char response[4096];
                            metronome_handle_request(m, buffer, response, sizeof(response));
                            zmq_send(m->rep_socket, response, strlen(response), 0);
                        }
                    }
                }
            }
            
            free(buffer);
            
            if (!block_ok) {
                // ⏰ HARD DEADLINE MISSED — validator was too slow
                // Create empty block so the chain doesn't stall.
                // The validator's in-flight block will be rejected by blockchain
                // (wrong height after empty block advances the chain).
                LOG_WARN("⏰ DEADLINE MISS: %s failed to confirm by deadline (%lums). Empty block.",
                         winner->farmer_name, t_deadline - t_tick);
                create_empty_block(m);
                block_ok = true;
            }
            
        } else {
            // ─────────────────────────────────────────────────────────
            // NO WINNER: no proofs received. Create empty block.
            // ─────────────────────────────────────────────────────────
            LOG_INFO("⏳ No proofs received. Creating empty block.");
            create_empty_block(m);
            block_ok = true;
        }
        
        // ═════════════════════════════════════════════════════════════
        // ROUND TIMING LOG
        // ═════════════════════════════════════════════════════════════
        uint64_t t_after_block = get_current_time_ms();
        
        // ═════════════════════════════════════════════════════════════
        // DIFFICULTY ADJUSTMENT
        // ═════════════════════════════════════════════════════════════
        bool round_had_winner = (m->best_submission_idx >= 0 && m->block_confirmed);
        difficulty_record_winner(m->difficulty_state, round_had_winner);
        uint32_t old_diff = difficulty_get_current(m->difficulty_state);
        difficulty_adjust(m->difficulty_state);
        uint32_t new_diff = difficulty_get_current(m->difficulty_state);
        if (new_diff != old_diff) {
            LOG_INFO("%s DIFFICULTY: %u → %u", 
                     new_diff > old_diff ? "📈" : "📉", old_diff, new_diff);
        }
        
        // ═════════════════════════════════════════════════════════════
        // PHASE 4: ALWAYS wait until t_next (STRICT 1-second spacing)
        // ═════════════════════════════════════════════════════════════
        // NEVER start the next round early. Even if we finished in 800ms,
        // we WAIT the remaining 200ms. This guarantees:
        //   - Every block interval is exactly block_time_ms
        //   - No rapid-fire catch-up blocks (215ms, 19ms anomalies)
        //   - Consistent, predictable block cadence
        // ═════════════════════════════════════════════════════════════
        uint64_t now = get_current_time_ms();
        uint64_t sleep_ms_actual = (now < t_next) ? (t_next - now) : 0;
        
        // Machine-readable timing for Graph 4 (full metronome pipeline)
        // ROUND_TIMING:height:proofs:challenge_ms:proof_ms:block_ms:sleep_ms
        {
            uint64_t challenge_ms = t_after_challenge - t_tick;
            uint64_t proof_ms = t_after_proofs - t_after_challenge;
            uint64_t block_ms = t_after_block - t_after_proofs;
            LOG_INFO("ROUND_TIMING:%u:%u:%lu:%lu:%lu:%lu",
                     round_height, m->submission_count,
                     challenge_ms, proof_ms, block_ms, sleep_ms_actual);
        }
        if (now < t_next) {
            uint64_t sleep_ms = t_next - now;
            LOG_INFO("💤 Round complete. Sleeping %lums until next tick.", sleep_ms);
            
            // Drain any late messages during sleep (don't let ZMQ buffers fill)
            uint64_t sleep_end = t_next;
            while (get_current_time_ms() < sleep_end && m->running) {
                char drain_buf[4096];
                int drain_timeout = 10;
                zmq_setsockopt(m->rep_socket, ZMQ_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
                int sz = zmq_recv(m->rep_socket, drain_buf, sizeof(drain_buf) - 1, 0);
                if (sz > 0) {
                    drain_buf[sz] = '\0';
                    if (starts_with(drain_buf, "SUBMIT_PROOF:")) {
                        zmq_send(m->rep_socket, "ROUND_ENDED", 11, 0);
                    } else {
                        char response[4096];
                        metronome_handle_request(m, drain_buf, response, sizeof(response));
                        zmq_send(m->rep_socket, response, strlen(response), 0);
                    }
                }
                
                // Also drain PULL socket (late BLOCK_CONFIRMED from rejected blocks)
                if (m->notify_pull) {
                    int pull_timeout = 1;
                    zmq_setsockopt(m->notify_pull, ZMQ_RCVTIMEO, &pull_timeout, sizeof(pull_timeout));
                    sz = zmq_recv(m->notify_pull, drain_buf, sizeof(drain_buf) - 1, 0);
                    // Just discard - we already moved on
                }
            }
            
            // Restore timeout
            zmq_setsockopt(m->rep_socket, ZMQ_RCVTIMEO, &rep_timeout, sizeof(rep_timeout));
        } else {
            // We overran the block time. Log it but continue.
            // The next round will still start at "now" (fresh t_tick each iteration)
            // so we don't accumulate debt.
            int64_t overrun = (int64_t)(now - t_next);
            LOG_WARN("⚠️  Round overran by %ldms. Next round starts immediately.", overrun);
        }
    }
    
    LOG_INFO("🛑 Metronome stopped (blocks: %lu, empty: %lu, avg overhead: %lums)",
             m->total_blocks, m->empty_blocks_created, m->t_create_avg_ms);
}

void metronome_stop(Metronome* m) {
    if (m) m->running = false;
}

// =============================================================================
// SERIALIZATION
// =============================================================================

char* challenge_serialize(const Challenge* c) {
    if (!c) return NULL;
    
    char* result = safe_malloc(512);
    char hash_hex[65];
    char prev_hex[65];
    
    bytes_to_hex_buf(c->challenge_hash, 32, hash_hex);
    bytes_to_hex_buf(c->prev_block_hash, 32, prev_hex);
    
    snprintf(result, 512, "%s|%s|%lu|%u|%lu|%u",
             hash_hex, prev_hex, c->challenge_id, c->target_block_height,
             c->issued_at, c->current_difficulty);
    
    return result;
}

bool challenge_deserialize(const char* data, Challenge* c) {
    if (!data || !c) return false;
    
    char hash_hex[65];
    char prev_hex[65];
    
    if (sscanf(data, "%64[^|]|%64[^|]|%lu|%u|%lu|%u",
               hash_hex, prev_hex, &c->challenge_id, &c->target_block_height,
               &c->issued_at, &c->current_difficulty) != 6) {
        // Try old format
        memset(c->prev_block_hash, 0, 32);
        if (sscanf(data, "%64[^|]|%lu|%u|%lu|%u",
                   hash_hex, &c->challenge_id, &c->target_block_height,
                   &c->issued_at, &c->current_difficulty) != 5) {
            return false;
        }
        hex_to_bytes_buf(hash_hex, c->challenge_hash, 32);
        return true;
    }
    
    hex_to_bytes_buf(hash_hex, c->challenge_hash, 32);
    hex_to_bytes_buf(prev_hex, c->prev_block_hash, 32);
    return true;
}

// =============================================================================
// CLEANUP
// =============================================================================

void metronome_destroy(Metronome* m) {
    if (!m) return;
    
    if (m->rep_socket) zmq_close(m->rep_socket);
    if (m->pub_socket) zmq_close(m->pub_socket);
    if (m->blockchain_req) zmq_close(m->blockchain_req);
    if (m->pool_req) zmq_close(m->pool_req);
    if (m->notify_pull) zmq_close(m->notify_pull);
    if (m->zmq_context) zmq_ctx_destroy(m->zmq_context);
    if (m->difficulty_state) difficulty_destroy(m->difficulty_state);
    
    free(m);
}
