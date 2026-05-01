#ifndef METRONOME_H
#define METRONOME_H

#include <stdint.h>
#include <stdbool.h>
#include <zmq.h>
#include "consensus.h"
#include "block.h"

// =============================================================================
// METRONOME - CHALLENGE BROADCASTER & WINNER SELECTOR (v29)
// =============================================================================
// 
// ARCHITECTURE (v29 - Proof-Based Winner Selection):
// ==================================================
// 
// 1. Metronome broadcasts challenge
// 2. Validators find proofs and submit LIGHTWEIGHT PROOFS (~100 bytes)
// 3. Metronome collects proofs, selects winner (best quality)
// 4. Metronome announces winner via PUB
// 5. ONLY the winning validator:
//    a) Fetches transactions from pool (metronome NOT involved)
//    b) Creates coinbase transaction (paying itself)
//    c) Builds complete block with transactions
//    d) Sends block directly to blockchain
// 6. BLOCKCHAIN directly notifies metronome via PUSH/PULL (no validator relay)
// 7. Metronome broadcasts NEW challenge based on confirmed block hash
// 8. If NO valid proofs -> metronome creates EMPTY block (no TXs)
//
// =============================================================================

// =============================================================================
// CONFIGURATION DEFAULTS
// =============================================================================

#define BLOCK_INTERVAL_DEFAULT      1000    // Milliseconds between blocks
#define DIFFICULTY_ADJUST_INTERVAL  5       // Adjust every N blocks
#define BASE_MINING_REWARD          10000      // Base mining reward (before fees)
#define HALVING_INTERVAL            10000000     // Halve reward every N blocks (effectively disabled for benchmarks)
#define WINNER_TIMEOUT_MS           10000   // 10s timeout for winner to confirm block

// Strict timing parameters (v45.2)
#define TIMING_HISTORY_SIZE         10      // Moving average window
#define TIMING_INITIAL_ESTIMATE_MS  300     // Conservative initial estimate for block creation
#define TIMING_MARGIN_MS            50      // Safety margin for ZMQ latency + jitter
#define TIMING_MIN_BUDGET_MS        200     // Floor: never allocate less than 200ms for block creation

// =============================================================================
// PROOF SUBMISSION TRACKING
// =============================================================================

typedef struct {
    SpaceProof proof;
    uint8_t farmer_address[20];
    char farmer_name[64];
    uint8_t quality[32];
    bool is_valid;
    uint64_t submission_time;
} ProofSubmission;

#define MAX_PROOF_SUBMISSIONS 100

// =============================================================================
// CHALLENGE STATE
// =============================================================================

typedef struct {
    uint8_t challenge_hash[32];
    uint8_t prev_block_hash[32];
    uint64_t challenge_id;
    uint32_t target_block_height;
    uint64_t issued_at;
    uint32_t current_difficulty;
} Challenge;

// =============================================================================
// METRONOME STRUCTURE
// =============================================================================

typedef struct {
    // ZMQ sockets
    void* zmq_context;
    void* rep_socket;      // REP for API (proof submissions, queries)
    void* pub_socket;      // PUB for challenges + winner announcements
    void* blockchain_req;  // REQ to blockchain (for empty blocks + queries)
    void* pool_req;        // REQ to pool (for confirming TXs after block added)
    void* notify_pull;     // PULL for block confirmations from blockchain (v29)
    
    // Configuration
    uint32_t block_interval_ms;
    uint32_t difficulty_adjust_interval;
    uint64_t base_mining_reward;
    uint32_t halving_interval;
    uint32_t k_param;
    
    // State
    Challenge current_challenge;
    uint64_t challenge_counter;
    DifficultyState* difficulty_state;
    
    // Proof submission tracking for current round
    ProofSubmission submissions[MAX_PROOF_SUBMISSIONS];
    uint32_t submission_count;
    int32_t best_submission_idx;  // -1 if none
    
    // Winner state for current round
    char current_winner_name[64];
    bool has_winner;
    bool block_confirmed;
    uint8_t confirmed_block_hash[32];
    
    // Statistics
    uint64_t total_blocks;
    uint64_t total_proofs_received;
    uint64_t start_time;
    
    // Strict timing (v45.2) - adaptive proof window
    // The metronome measures how long block creation takes (from WINNER announce
    // to BLOCK_CONFIRMED) and adapts the proof collection window accordingly.
    // T_proof = BLOCK_TIME - T_create_avg - T_margin
    uint64_t create_times[TIMING_HISTORY_SIZE];  // ring buffer of measured create times
    uint32_t timing_idx;                          // next write position
    uint32_t timing_count;                        // how many measurements (up to HISTORY_SIZE)
    uint64_t t_create_avg_ms;                     // current moving average
    uint64_t t_proof_window_ms;                   // computed proof collection window
    uint64_t empty_blocks_created;                // count of deadline-missed empty blocks
    
    // Running flag
    bool running;
    bool paused;  // When true, don't create blocks (for 2-phase benchmarking)
} Metronome;

// =============================================================================
// METRONOME FUNCTIONS
// =============================================================================

Metronome* metronome_create(uint32_t block_interval_ms,
                            uint32_t difficulty_adjust_interval,
                            uint32_t k_param,
                            uint32_t initial_validator_count,
                            uint32_t initial_difficulty,
                            uint64_t base_mining_reward,
                            uint32_t halving_interval);

bool metronome_init_sockets(Metronome* m,
                            const char* rep_addr,
                            const char* pub_addr,
                            const char* blockchain_addr,
                            const char* pool_addr,
                            const char* notify_pull_addr);

void metronome_generate_challenge(Metronome* m);
void metronome_broadcast_challenge(Metronome* m);

const char* metronome_submit_proof(Metronome* m,
                                   const SpaceProof* proof,
                                   const uint8_t farmer_address[20],
                                   const char* farmer_name);

bool metronome_finalize_round(Metronome* m);
void metronome_handle_request(Metronome* m, const char* request, char* response, size_t resp_size);
void metronome_run(Metronome* m);
void metronome_stop(Metronome* m);
void metronome_get_stats(const Metronome* m, char* buffer, size_t size);
void metronome_destroy(Metronome* m);
uint64_t metronome_get_mining_reward(const Metronome* m, uint32_t block_height);

// =============================================================================
// CHALLENGE SERIALIZATION
// =============================================================================

uint8_t* challenge_serialize_pb(const Challenge* c, size_t* out_len);
bool challenge_deserialize_pb(const uint8_t* data, size_t len, Challenge* c);
char* challenge_serialize(const Challenge* c);
bool challenge_deserialize(const char* data, Challenge* c);

#endif // METRONOME_H
