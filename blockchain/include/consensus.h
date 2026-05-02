#ifndef CONSENSUS_H
#define CONSENSUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// PROOF OF SPACE - BINARY SEARCH OPTIMIZED WITH BLAKE3
// =============================================================================
// Instead of brute-force scanning all entries, we:
// 1. Pre-sort all (nonce, hash) pairs by hash value
// 2. Use binary search to find entries matching target difficulty
// 3. Parameter k determines plot size: 2^k entries
// 4. Uses BLAKE3 for fast hash generation: hash = BLAKE3(plot_id || i)
//
// Entry Structure: 32 bytes each
//   nonce[4]  = 4 bytes  - Original index/nonce
//   hash[28]  = 28 bytes - Truncated BLAKE3 hash value
//   TOTAL     = 32 bytes
// =============================================================================

#pragma pack(push, 1)

// Single plot entry: 32 bytes
typedef struct {
    uint32_t nonce;      // 4 bytes: Original position/nonce
    uint8_t hash[28];    // 28 bytes: Truncated BLAKE3 hash
} PlotEntry;             // TOTAL: 32 bytes

#pragma pack(pop)

_Static_assert(sizeof(PlotEntry) == 32, "PlotEntry must be exactly 32 bytes");

// =============================================================================
// PLOT STRUCTURE
// =============================================================================

typedef struct {
    uint8_t plot_id[32];        // Unique plot identifier
    uint8_t farmer_address[20]; // Owner's address
    PlotEntry* entries;         // Sorted array of entries
    uint64_t entry_count;       // Number of entries (2^k)
    uint32_t k_param;           // k parameter
    bool is_sorted;             // Whether entries are sorted
} Plot;

// =============================================================================
// SPACE PROOF STRUCTURE
// =============================================================================

typedef struct {
    uint8_t plot_id[32];     // Which plot
    uint32_t nonce;          // Entry nonce
    uint8_t proof_hash[28];  // BLAKE3 hash at that entry
    uint8_t quality[32];     // hash(challenge || proof_hash) - LOWER = BETTER
} SpaceProof;

// =============================================================================
// NEW DIFFICULTY SYSTEM (1-256 range)
// =============================================================================
// Difficulty N = require N leading zero bits
// Difficulty 1 = 1 leading zero bit (easiest, ~50% pass)
// Difficulty 256 = 256 leading zero bits (impossible)
//
// Default difficulty = k + log2(num_validators)
// This ensures fair competition based on plot size and validator count
// =============================================================================

#define DIFFICULTY_MIN      1
#define DIFFICULTY_MAX      256
#define DIFFICULTY_DEFAULT  20      // Default starting difficulty

// Calculate default difficulty from k parameter and validator count
// NEW FORMULA: k - 2 + log2(validators) to ensure ~1-3 proofs per validator
static inline uint32_t calculate_default_difficulty(uint32_t k_param, uint32_t num_validators) {
    if (num_validators == 0) num_validators = 1;
    uint32_t log_validators = (uint32_t)floor(log2((double)num_validators));
    int32_t diff = (int32_t)k_param - 2 + (int32_t)log_validators;
    if (diff < (int32_t)DIFFICULTY_MIN) diff = DIFFICULTY_MIN;
    if (diff > (int32_t)DIFFICULTY_MAX) diff = DIFFICULTY_MAX;
    return (uint32_t)diff;
}

typedef struct {
    uint32_t current_difficulty;     // Current difficulty (1-256)
    uint32_t adjustment_interval;    // Adjust every N blocks
    uint32_t min_valid_proofs;       // Target min proofs per block
    uint32_t max_valid_proofs;       // Target max proofs per block
    uint32_t blocks_since_adjust;    // Counter
    uint32_t valid_proofs_count;     // Total proofs over interval
    uint32_t k_param;                // k parameter for default calc
    uint32_t validator_count;        // Current validator count
    uint32_t blocks_with_winner;     // Blocks that had a winner
    uint32_t consecutive_no_winner;  // Consecutive blocks without winner
    uint32_t consecutive_all_win;    // Consecutive blocks where all validators won
    uint32_t round_proofs;           // Proofs in current round
} DifficultyState;

// =============================================================================
// CONSTANTS
// =============================================================================

#define K_PARAM_DEFAULT     20       // 2^20 = ~1 million entries
#define K_PARAM_MIN         16       // 2^16 = 65,536 entries (for testing)
#define K_PARAM_MAX         30       // 2^30 = 1 billion entries

#define PLOT_ENTRY_SIZE     32
#define PROOF_HASH_SIZE     28
#define QUALITY_SIZE        32

// =============================================================================
// PLOT FUNCTIONS
// =============================================================================

// Create new plot with k parameter
Plot* plot_create(const uint8_t farmer_address[20], uint32_t k_param);

// Generate plot entries using BLAKE3(plot_id || i)
bool plot_generate(Plot* plot);

// Sort plot entries by hash (required before binary search)
void plot_sort(Plot* plot);

// Persist plot to disk (binary format with magic header).
// Returns true on success.
bool plot_save_to_file(const Plot* plot, const char* path);

// Load plot from disk. Returns NULL on missing/corrupt file.
// Caller owns the returned Plot* and must call plot_destroy().
Plot* plot_load_from_file(const char* path);

// Find best proof for a challenge using binary search
SpaceProof* plot_find_proof(const Plot* plot, 
                            const uint8_t challenge[32],
                            uint32_t difficulty);

// Verify a space proof
bool proof_verify(const SpaceProof* proof,
                  const uint8_t challenge[32],
                  uint32_t difficulty);

// Compare proof quality (returns <0 if a better, >0 if b better, 0 if equal)
int proof_compare_quality(const SpaceProof* a, const SpaceProof* b);

// Check if proof meets difficulty target
bool proof_meets_difficulty(const SpaceProof* proof, uint32_t difficulty);

// Free plot memory
void plot_destroy(Plot* plot);

// =============================================================================
// DIFFICULTY FUNCTIONS
// =============================================================================

// Initialize difficulty state with default difficulty based on k and validators
DifficultyState* difficulty_init(uint32_t interval, uint32_t k_param, uint32_t validator_count);

// Update min/max when validator count changes
void difficulty_update_validator_count(DifficultyState* state, uint32_t validator_count);

// Record a valid proof submission
void difficulty_record_proof(DifficultyState* state);

// Check and adjust difficulty after block
void difficulty_adjust(DifficultyState* state);

// Reset proof counter for new round
void difficulty_reset_round(DifficultyState* state);

// Get current difficulty
uint32_t difficulty_get_current(const DifficultyState* state);

// Set difficulty explicitly
void difficulty_set(DifficultyState* state, uint32_t difficulty);

// Free difficulty state
void difficulty_destroy(DifficultyState* state);

// Record whether current round had a winner
void difficulty_record_winner(DifficultyState* state, bool had_winner);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

// Comparison function for qsort (compare by hash)
int plot_entry_compare(const void* a, const void* b);

// Binary search for entries below target
PlotEntry* plot_binary_search(const Plot* plot, 
                              const uint8_t target[28],
                              uint64_t* found_count);

// Calculate quality hash
void calculate_quality(const uint8_t challenge[32],
                       const uint8_t proof_hash[28],
                       uint8_t quality[32]);

// Convert difficulty to target prefix bits
// In new system: difficulty N = require N leading zero bits
static inline uint32_t difficulty_to_target_bits(uint32_t difficulty) {
    // Direct mapping: difficulty 1 = 1 zero bit, difficulty 256 = 256 zero bits
    return difficulty;
}

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

// Serialize proof to protobuf binary (caller must free, returns size)
uint8_t* proof_serialize_pb(const SpaceProof* proof, size_t* out_len);

// Deserialize proof from protobuf binary
SpaceProof* proof_deserialize_pb(const uint8_t* data, size_t len);

// Serialize proof to hex string (legacy)
char* proof_serialize(const SpaceProof* proof);

// Deserialize proof from hex string (legacy)
SpaceProof* proof_deserialize(const char* hex_data);

#endif // CONSENSUS_H
