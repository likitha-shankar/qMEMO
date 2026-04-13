#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <zmq.h>
#include "consensus.h"
#include "wallet.h"
#include "metronome.h"
#include "block.h"

// =============================================================================
// VALIDATOR (FARMER) - PROOF OF SPACE PARTICIPANT (v29)
// =============================================================================
//
// ARCHITECTURE (v29 - Proof-Based Winner Selection):
// ==================================================
// 1. Receive challenge from metronome (SUB)
// 2. Search plot for valid proof
// 3. If valid proof found: submit PROOF to metronome (REQ->REP)
// 4. Listen for WINNER announcement (SUB)
// 5. If we won:
//    a) Fetch transactions from pool (REQ->REP)
//    b) Create coinbase transaction (reward to self)
//    c) Build complete block with transactions
//    d) Send block directly to blockchain (REQ->REP)
//       Blockchain notifies metronome directly upon success.
//    e) Confirm transactions with pool
//
// The METRONOME only:
// - Issues challenges
// - Collects lightweight proofs and selects winner
// - Creates EMPTY blocks when no validator submits valid proof
// - Receives block confirmation from BLOCKCHAIN (not validator)
// - Waits for block confirmation before broadcasting next challenge
//
// =============================================================================

typedef struct {
    // Identity
    char name[64];
    Wallet* wallet;
    
    // Plot
    Plot* plot;
    uint32_t k_param;
    
    // ZMQ connections
    void* zmq_context;
    void* metronome_req;   // REQ to metronome for proof submissions + block confirmations
    void* metronome_sub;   // SUB for challenge broadcasts + winner announcements
    void* pool_req;        // REQ to pool for fetching transactions
    void* blockchain_req;  // REQ to blockchain for sending blocks + getting prev hash
    
    // State
    Challenge current_challenge;
    bool has_challenge;
    
    // Statistics
    uint64_t proofs_found;
    uint64_t proofs_submitted;
    uint64_t blocks_created;
    uint64_t blocks_accepted;
    uint64_t start_time;
    
    // Deadline-aware block creation (v45.2)
    uint64_t deadline_ms;          // absolute deadline (wall clock ms)
    
    // Running flag
    bool running;
} Validator;

// =============================================================================
// VALIDATOR FUNCTIONS
// =============================================================================

Validator* validator_create(const char* name, uint32_t k_param, uint8_t sig_type);

bool validator_init_sockets(Validator* v,
                            const char* metronome_req_addr,
                            const char* metronome_sub_addr,
                            const char* pool_addr,
                            const char* blockchain_addr);

bool validator_generate_plot(Validator* v);
void validator_handle_challenge(Validator* v, const Challenge* challenge);

// Find proof and submit to metronome (does NOT create block yet)
bool validator_find_and_submit_proof(Validator* v);

// Called when we win: fetch TXs, create block, send to blockchain, notify metronome
bool validator_create_and_submit_block(Validator* v);

void validator_run(Validator* v);
void validator_stop(Validator* v);
void validator_set_max_txs_per_block(uint32_t max_txs);
void validator_get_stats(const Validator* v, char* buffer, size_t size);
void validator_destroy(Validator* v);

#endif // VALIDATOR_H
