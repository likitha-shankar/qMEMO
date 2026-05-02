/**
 * ============================================================================
 * CONSENSUS.C - Proof of Space (PoS) Consensus Implementation
 * ============================================================================
 * 
 * This file implements the Proof of Space consensus mechanism, inspired by
 * Chia Network but simplified for educational purposes.
 * 
 * KEY CONCEPTS:
 * -------------
 * 
 * 1. PROOF OF SPACE vs PROOF OF WORK:
 *    - PoW: Miners compete by doing computation (wasteful energy)
 *    - PoS: Farmers compete by storing pre-computed data (storage-efficient)
 *    - "More storage = more wins" instead of "more CPU = more wins"
 * 
 * 2. PLOT GENERATION (one-time setup):
 *    - Farmer generates 2^k entries (k is typically 18-30)
 *    - Each entry: {nonce, hash} where hash = BLAKE3(plot_id || nonce)
 *    - Entries are sorted by hash for efficient lookup
 *    - This takes time but only done once
 * 
 * 3. CHALLENGE-RESPONSE (every block):
 *    - Metronome broadcasts challenge (32-byte hash)
 *    - Farmers find plot entries closest to challenge (binary search O(log n))
 *    - "Closeness" measured by XOR distance (fewer leading zeros = closer)
 *    - Only entries with quality meeting difficulty threshold are valid
 * 
 * 4. DIFFICULTY SYSTEM:
 *    - Difficulty N means proof quality must have N leading zero bits
 *    - Higher difficulty = fewer valid proofs = harder to win
 *    - Auto-adjusts to maintain healthy network participation
 *    - Default = k + log2(validators) - scales with plot size and competition
 * 
 * 5. WHY THIS IS SECURE:
 *    - Can't fake proof without storing the data
 *    - Plot regeneration takes too long (minutes/hours vs seconds for challenge)
 *    - Grinding attacks prevented by random challenges
 * 
 * ============================================================================
 */

#include "../include/consensus.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ============================================================================
// PLOT CREATION
// ============================================================================
// 
// A "plot" is the pre-computed data that farmers store to participate in
// consensus. Think of it like a lottery ticket database:
// - Each entry is a potential winning ticket
// - More entries = more chances to win
// - Plot ID uniquely identifies this farmer's plot
//
// PLOT STRUCTURE:
// ┌──────────────────────────────────────────────────────────────────┐
// │ Plot                                                              │
// │   ├─ plot_id[32]     = BLAKE3(farmer_address || timestamp || k)  │
// │   ├─ k_param         = Size parameter (2^k entries)              │
// │   ├─ entry_count     = 2^k                                        │
// │   └─ entries[]       = Array of {nonce, hash} pairs              │
// │         ├─ entry[0]  = {nonce: 0, hash: BLAKE3(plot_id||0)}      │
// │         ├─ entry[1]  = {nonce: 1, hash: BLAKE3(plot_id||1)}      │
// │         └─ ...                                                    │
// └──────────────────────────────────────────────────────────────────┘
// ============================================================================

Plot* plot_create(const uint8_t farmer_address[20], uint32_t k_param) {
    // Validate k parameter (too small = insecure, too large = impractical)
    if (k_param < K_PARAM_MIN || k_param > K_PARAM_MAX) {
        LOG_ERROR("❌ Invalid k parameter: %u (must be %d-%d)", k_param, K_PARAM_MIN, K_PARAM_MAX);
        return NULL;
    }
    
    Plot* plot = safe_malloc(sizeof(Plot));
    memset(plot, 0, sizeof(Plot));
    
    memcpy(plot->farmer_address, farmer_address, 20);
    plot->k_param = k_param;
    plot->entry_count = (uint64_t)1 << k_param;  // 2^k entries
    plot->is_sorted = false;
    
    // Generate unique plot_id: BLAKE3(farmer_address || timestamp || k_param)
    // This ensures each plot is unique even for same farmer
    uint64_t ts = get_current_timestamp();
    uint8_t buffer[20 + 8 + 4];
    memcpy(buffer, farmer_address, 20);
    memcpy(buffer + 20, &ts, 8);
    memcpy(buffer + 28, &k_param, 4);
    blake3_hash(buffer, sizeof(buffer), plot->plot_id);
    
    // Calculate memory requirements
    double mem_mb = (plot->entry_count * sizeof(PlotEntry)) / (1024.0 * 1024.0);
    
    LOG_INFO("📁 ════════════════════════════════════════════════════════════");
    LOG_INFO("📁 PLOT ALLOCATION");
    LOG_INFO("   ├─ k parameter:  %u", k_param);
    LOG_INFO("   ├─ Entry count:  %lu (2^%u)", plot->entry_count, k_param);
    LOG_INFO("   ├─ Memory:       %.2f MB", mem_mb);
    LOG_INFO("   └─ Entry size:   %lu bytes (4 nonce + 28 hash)", sizeof(PlotEntry));
    LOG_INFO("📁 ════════════════════════════════════════════════════════════");
    
    plot->entries = safe_malloc(plot->entry_count * sizeof(PlotEntry));
    
    return plot;
}

// ============================================================================
// PLOT GENERATION (BLAKE3 Hashing)
// ============================================================================
//
// This is the "work" in Proof of Space - done ONCE during setup.
// For each nonce i from 0 to 2^k-1:
//   entry[i].nonce = i
//   entry[i].hash  = BLAKE3(plot_id || i)[0:28]  (truncated to 28 bytes)
//
// WHY BLAKE3?
// - Faster than SHA256 (designed for speed)
// - Cryptographically secure
// - Good distribution properties
//
// TIME COMPLEXITY: O(2^k) - linear in number of entries
// ============================================================================

bool plot_generate(Plot* plot) {
    if (!plot || !plot->entries) return false;
    
    LOG_INFO("⚙️  ════════════════════════════════════════════════════════════");
    LOG_INFO("⚙️  PLOT GENERATION STARTING (BLAKE3)");
    LOG_INFO("   ├─ Entries to generate: %lu", plot->entry_count);
    LOG_INFO("   └─ Hash function: BLAKE3 (truncated to 28 bytes)");
    LOG_INFO("⚙️  ════════════════════════════════════════════════════════════");
    
    uint64_t start_time = get_current_time_ms();
    uint64_t last_progress = 0;
    
    /*
     * PLOT ENTRY GENERATION ALGORITHM:
     * ================================
     * For i = 0 to 2^k - 1:
     *   1. Concatenate: buffer = plot_id (32 bytes) || i (4 bytes)
     *   2. Hash: full_hash = BLAKE3(buffer)
     *   3. Truncate: entry_hash = full_hash[0:28]
     *   4. Store: entries[i] = {nonce: i, hash: entry_hash}
     * 
     * The 28-byte truncation saves space while maintaining
     * enough entropy for collision resistance.
     */
    for (uint64_t i = 0; i < plot->entry_count; i++) {
        plot->entries[i].nonce = (uint32_t)i;
        
        // Construct input: plot_id (32 bytes) || nonce (4 bytes)
        uint8_t buffer[36];
        memcpy(buffer, plot->plot_id, 32);
        uint32_t nonce = (uint32_t)i;
        memcpy(buffer + 32, &nonce, 4);
        
        // BLAKE3 hash truncated to 28 bytes
        blake3_hash_truncated(buffer, sizeof(buffer), plot->entries[i].hash, 28);
        
        // Progress logging every 10%
        uint64_t progress = (i * 100) / plot->entry_count;
        if (progress >= last_progress + 10) {
            uint64_t elapsed = get_current_time_ms() - start_time;
            double rate = (double)i / (elapsed / 1000.0);
            LOG_INFO("   📊 Progress: %lu%% (%lu entries, %.0f entries/sec)", 
                     progress, i, rate);
            last_progress = progress;
        }
    }
    
    uint64_t gen_time = get_current_time_ms() - start_time;
    double rate = (double)plot->entry_count / (gen_time / 1000.0);
    
    LOG_INFO("⚙️  ════════════════════════════════════════════════════════════");
    LOG_INFO("⚙️  PLOT GENERATION COMPLETE");
    LOG_INFO("   ├─ Time taken:    %lu ms (%.1f sec)", gen_time, gen_time / 1000.0);
    LOG_INFO("   ├─ Hash rate:     %.0f hashes/sec", rate);
    LOG_INFO("   └─ Next step:     Sorting for binary search...");
    LOG_INFO("⚙️  ════════════════════════════════════════════════════════════");
    
    // Sort entries for efficient O(log n) lookup
    plot_sort(plot);
    
    return true;
}

// ============================================================================
// PLOT SORTING (Required for Binary Search)
// ============================================================================
//
// After generation, entries are sorted by hash value.
// This enables O(log n) binary search instead of O(n) linear scan.
//
// BEFORE SORT: entries in nonce order (0, 1, 2, ...)
// AFTER SORT:  entries in hash order (0x0000..., 0x0001..., ...)
// ============================================================================

int plot_entry_compare(const void* a, const void* b) {
    const PlotEntry* ea = (const PlotEntry*)a;
    const PlotEntry* eb = (const PlotEntry*)b;
    return memcmp(ea->hash, eb->hash, 28);
}

void plot_sort(Plot* plot) {
    if (!plot || !plot->entries || plot->is_sorted) return;
    
    LOG_INFO("🔀 Sorting %lu plot entries by hash...", plot->entry_count);
    uint64_t start_time = get_current_time_ms();
    
    // Standard quicksort - O(n log n) average case
    qsort(plot->entries, plot->entry_count, sizeof(PlotEntry), plot_entry_compare);
    
    plot->is_sorted = true;
    
    uint64_t sort_time = get_current_time_ms() - start_time;
    LOG_INFO("🔀 Sorting complete in %lu ms", sort_time);
    LOG_INFO("✅ Plot ready for challenges!");
}

// ============================================================================
// PLOT PERSISTENCE (binary format)
// ============================================================================
// File layout (little-endian):
//   [8]  magic = "QMEMPLOT"
//   [1]  version = 1
//   [4]  k_param
//   [8]  entry_count
//   [1]  is_sorted
//   [32] plot_id
//   [20] farmer_address
//   [entry_count * 32]  entries (PlotEntry array)
// ============================================================================

#define PLOT_FILE_MAGIC "QMEMPLOT"
#define PLOT_FILE_VERSION 1

bool plot_save_to_file(const Plot* plot, const char* path) {
    if (!plot || !plot->entries || !path) return false;
    FILE* f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("plot_save_to_file: cannot open %s for write", path);
        return false;
    }
    bool ok = true;
    uint8_t version = PLOT_FILE_VERSION;
    uint8_t sorted = plot->is_sorted ? 1 : 0;
    ok = ok && fwrite(PLOT_FILE_MAGIC, 8, 1, f) == 1;
    ok = ok && fwrite(&version, 1, 1, f) == 1;
    ok = ok && fwrite(&plot->k_param, sizeof(plot->k_param), 1, f) == 1;
    ok = ok && fwrite(&plot->entry_count, sizeof(plot->entry_count), 1, f) == 1;
    ok = ok && fwrite(&sorted, 1, 1, f) == 1;
    ok = ok && fwrite(plot->plot_id, 32, 1, f) == 1;
    ok = ok && fwrite(plot->farmer_address, 20, 1, f) == 1;
    ok = ok && fwrite(plot->entries, sizeof(PlotEntry), plot->entry_count, f) == plot->entry_count;
    fclose(f);
    if (!ok) {
        LOG_ERROR("plot_save_to_file: short write to %s", path);
        return false;
    }
    return true;
}

Plot* plot_load_from_file(const char* path) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[8];
    uint8_t version, sorted;
    if (fread(magic, 8, 1, f) != 1 || memcmp(magic, PLOT_FILE_MAGIC, 8) != 0) {
        LOG_ERROR("plot_load_from_file: bad magic in %s", path);
        fclose(f); return NULL;
    }
    if (fread(&version, 1, 1, f) != 1 || version != PLOT_FILE_VERSION) {
        LOG_ERROR("plot_load_from_file: bad version in %s", path);
        fclose(f); return NULL;
    }
    Plot* plot = (Plot*)safe_malloc(sizeof(Plot));
    memset(plot, 0, sizeof(Plot));
    if (fread(&plot->k_param, sizeof(plot->k_param), 1, f) != 1) goto fail;
    if (fread(&plot->entry_count, sizeof(plot->entry_count), 1, f) != 1) goto fail;
    if (fread(&sorted, 1, 1, f) != 1) goto fail;
    plot->is_sorted = (sorted != 0);
    if (fread(plot->plot_id, 32, 1, f) != 1) goto fail;
    if (fread(plot->farmer_address, 20, 1, f) != 1) goto fail;
    if (plot->entry_count == 0 || plot->entry_count > ((uint64_t)1 << K_PARAM_MAX)) goto fail;
    plot->entries = (PlotEntry*)safe_malloc(sizeof(PlotEntry) * plot->entry_count);
    if (fread(plot->entries, sizeof(PlotEntry), plot->entry_count, f) != plot->entry_count) {
        free(plot->entries);
        goto fail;
    }
    fclose(f);
    LOG_INFO("✅ plot_load_from_file: loaded %lu entries (k=%u, sorted=%d) from %s",
             plot->entry_count, plot->k_param, plot->is_sorted, path);
    return plot;
fail:
    free(plot);
    fclose(f);
    LOG_ERROR("plot_load_from_file: short read from %s", path);
    return NULL;
}

// ============================================================================
// BINARY SEARCH - Find Entry Closest to Target
// ============================================================================
//
// Given a target hash, find the plot entry with the smallest XOR distance.
// Uses binary search: O(log n) instead of O(n) linear scan.
//
// ALGORITHM:
// 1. Binary search to find insertion point for target
// 2. Compare entry at that point and neighbors
// 3. Return entry with smallest XOR distance to target
//
// WHY XOR DISTANCE?
// - XOR of two hashes gives a "distance" metric
// - Leading zeros in XOR = how "close" the values are
// - More leading zeros = closer match = better quality
// ============================================================================

static uint64_t binary_search_closest(const Plot* plot, const uint8_t target[28]) {
    if (plot->entry_count == 0) return 0;
    
    uint64_t left = 0;
    uint64_t right = plot->entry_count;
    
    // Standard binary search to find insertion point
    while (left < right) {
        uint64_t mid = left + (right - left) / 2;
        if (memcmp(plot->entries[mid].hash, target, 28) < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    // Handle edge cases
    if (left == 0) return 0;
    if (left >= plot->entry_count) return plot->entry_count - 1;
    
    // Compare distances to find which is actually closer
    // (the insertion point might not be the closest)
    uint64_t dist_left = 0, dist_prev = 0;
    for (int i = 0; i < 8 && i < 28; i++) {
        dist_left = (dist_left << 8) | (plot->entries[left].hash[i] ^ target[i]);
        dist_prev = (dist_prev << 8) | (plot->entries[left-1].hash[i] ^ target[i]);
    }
    
    return (dist_prev <= dist_left) ? (left - 1) : left;
}

// Calculate XOR distance between two hashes
// Result is stored in a 32-byte buffer (padded with zeros for 28-byte inputs)
static void calculate_distance(const uint8_t hash[28], const uint8_t target[28], uint8_t distance[32]) {
    memset(distance, 0, 32);
    for (int i = 0; i < 28; i++) {
        distance[i] = hash[i] ^ target[i];
    }
}

PlotEntry* plot_binary_search(const Plot* plot, 
                              const uint8_t target[28],
                              uint64_t* found_count) {
    if (!plot || !plot->is_sorted || !plot->entries) return NULL;
    
    uint64_t idx = binary_search_closest(plot, target);
    if (found_count) *found_count = 1;
    
    return &plot->entries[idx];
}

// ============================================================================
// PROOF FINDING - The Core of Proof of Space
// ============================================================================
//
// When a challenge arrives, farmer tries to find a "proof" from their plot.
//
// PROOF FINDING ALGORITHM:
// 1. Derive target from challenge: target = BLAKE3(challenge)[0:28]
// 2. Binary search plot for entry closest to target
// 3. Calculate quality = XOR(entry.hash, target)
// 4. Check if quality has enough leading zeros (meets difficulty)
// 5. If yes, this is a valid proof; return it
//
// QUALITY MEASUREMENT:
// - quality = entry_hash XOR target
// - More leading zeros = better quality = closer match
// - Difficulty N means: quality must have N leading zero bits
//
// EXAMPLE:
//   target     = 0x1234abcd...
//   entry.hash = 0x1234ab00...  (very close!)
//   quality    = 0x000000cd...  (3 zero bytes = 24 zero bits)
//   If difficulty <= 24, this is a valid proof
// ============================================================================

SpaceProof* plot_find_proof(const Plot* plot, 
                            const uint8_t challenge[32],
                            uint32_t difficulty) {
    if (!plot || !plot->is_sorted) return NULL;
    
    uint64_t start_time = get_current_time_ms();
    uint32_t target_zeros = difficulty_to_target_bits(difficulty);
    
    LOG_INFO("🔍 ────────────────────────────────────────────────────────────");
    LOG_INFO("🔍 SEARCHING FOR PROOF");
    LOG_INFO("   ├─ Challenge:  %.16s...", "");  // Would need hex conversion
    LOG_INFO("   ├─ Difficulty: %u (requires %u leading zero bits)", difficulty, target_zeros);
    LOG_INFO("   └─ Plot size:  %lu entries", plot->entry_count);
    
    /*
     * STEP 1: Derive target from challenge
     * =====================================
     * target = BLAKE3(challenge)[0:28]
     * 
     * This transforms the challenge into a 28-byte value
     * that we'll search for in our plot.
     */
    uint8_t full_target[32];
    blake3_hash(challenge, 32, full_target);
    uint8_t target[28];
    memcpy(target, full_target, 28);
    
    /*
     * STEP 2: Binary search for closest entry
     * =======================================
     * O(log n) search to find the plot entry
     * whose hash is closest to the target.
     */
    uint64_t closest_idx = binary_search_closest(plot, target);
    
    SpaceProof* best_proof = NULL;
    uint8_t best_quality[32];
    memset(best_quality, 0xFF, 32);  // Start with worst possible quality
    
    /*
     * STEP 3: Check closest entry and neighbors
     * =========================================
     * The binary search gives us the closest entry,
     * but due to XOR distance properties, neighbors
     * might actually be better. Check a small window.
     */
    int64_t start_idx = (int64_t)closest_idx - 2;
    int64_t end_idx = (int64_t)closest_idx + 3;
    
    if (start_idx < 0) start_idx = 0;
    if (end_idx > (int64_t)plot->entry_count) end_idx = plot->entry_count;
    
    uint32_t checked = 0;
    uint32_t met_difficulty = 0;
    
    for (int64_t i = start_idx; i < end_idx; i++) {
        PlotEntry* entry = &plot->entries[i];
        checked++;
        
        /*
         * STEP 4: Calculate quality as XOR distance
         * =========================================
         * quality = entry.hash XOR target
         * 
         * More leading zeros = better quality
         */
        uint8_t quality[32];
        calculate_distance(entry->hash, target, quality);
        
        /*
         * STEP 5: Check if meets difficulty threshold
         * ===========================================
         * Count leading zeros in quality.
         * Must have at least 'target_zeros' leading zeros.
         */
        uint32_t zeros = count_leading_zeros(quality, 32);
        if (zeros < target_zeros) {
            continue;  // Doesn't meet difficulty - skip
        }
        
        met_difficulty++;
        
        // Check if this is better than our current best
        if (memcmp(quality, best_quality, 32) < 0) {
            if (!best_proof) {
                best_proof = safe_malloc(sizeof(SpaceProof));
            }
            
            // Build the proof structure
            memcpy(best_proof->plot_id, plot->plot_id, 32);
            best_proof->nonce = entry->nonce;
            memcpy(best_proof->proof_hash, entry->hash, 28);
            memcpy(best_proof->quality, quality, 32);
            memcpy(best_quality, quality, 32);
        }
    }
    
    uint64_t search_time = get_current_time_ms() - start_time;
    benchmark_validator_work(search_time);
    
    if (best_proof) {
        char quality_hex[65];
        bytes_to_hex_buf(best_quality, 32, quality_hex);
        uint32_t zeros = count_leading_zeros(best_quality, 32);
        
        LOG_INFO("🔍 ────────────────────────────────────────────────────────────");
        LOG_INFO("🎯 PROOF FOUND!");
        LOG_INFO("   ├─ Quality:      %.16s... (%u leading zeros)", quality_hex, zeros);
        LOG_INFO("   ├─ Nonce:        %u", best_proof->nonce);
        LOG_INFO("   ├─ Checked:      %u entries (met difficulty: %u)", checked, met_difficulty);
        LOG_INFO("   └─ Search time:  %lu ms", search_time);
    } else {
        LOG_INFO("🔍 ────────────────────────────────────────────────────────────");
        LOG_INFO("❌ NO VALID PROOF FOUND");
        LOG_INFO("   ├─ Required:     %u leading zero bits", target_zeros);
        LOG_INFO("   ├─ Checked:      %u entries", checked);
        LOG_INFO("   └─ Search time:  %lu ms", search_time);
    }
    
    return best_proof;
}

// ============================================================================
// PROOF VERIFICATION
// ============================================================================
//
// Anyone can verify a proof without having the full plot.
// This is what makes Proof of Space work - verification is fast!
//
// VERIFICATION STEPS:
// 1. Recompute hash from (plot_id, nonce) - should match proof_hash
// 2. Recompute quality from (proof_hash, challenge) - should match claimed
// 3. Check that quality meets difficulty threshold
// ============================================================================

bool proof_verify(const SpaceProof* proof,
                  const uint8_t challenge[32],
                  uint32_t difficulty) {
    if (!proof) return false;
    
    LOG_INFO("🔐 Verifying proof (difficulty %u)...", difficulty);
    
    /*
     * STEP 1: Verify the hash was computed correctly
     * ==============================================
     * Recompute: expected_hash = BLAKE3(plot_id || nonce)[0:28]
     * Compare with proof_hash - must match exactly
     */
    uint8_t recomputed_hash[28];
    uint8_t hash_input[36];
    memcpy(hash_input, proof->plot_id, 32);
    memcpy(hash_input + 32, &proof->nonce, 4);
    blake3_hash_truncated(hash_input, 36, recomputed_hash, 28);
    
    if (memcmp(recomputed_hash, proof->proof_hash, 28) != 0) {
        LOG_WARN("❌ Proof verification failed: hash mismatch");
        LOG_WARN("   This could mean the proof was tampered with");
        return false;
    }
    
    /*
     * STEP 2: Verify the quality calculation
     * ======================================
     * Derive target from challenge
     * Calculate expected_quality = proof_hash XOR target
     * Compare with claimed quality
     */
    uint8_t full_target[32];
    blake3_hash(challenge, 32, full_target);
    uint8_t target[28];
    memcpy(target, full_target, 28);
    
    uint8_t expected_quality[32];
    calculate_distance(proof->proof_hash, target, expected_quality);
    
    if (memcmp(expected_quality, proof->quality, 32) != 0) {
        LOG_WARN("❌ Proof verification failed: quality mismatch");
        return false;
    }
    
    /*
     * STEP 3: Verify meets difficulty threshold
     * =========================================
     */
    bool meets = proof_meets_difficulty(proof, difficulty);
    
    if (meets) {
        LOG_INFO("✅ Proof verified successfully!");
    } else {
        LOG_WARN("❌ Proof does not meet difficulty %u", difficulty);
    }
    
    return meets;
}

bool proof_meets_difficulty(const SpaceProof* proof, uint32_t difficulty) {
    uint32_t target_zeros = difficulty_to_target_bits(difficulty);
    uint32_t actual_zeros = count_leading_zeros(proof->quality, 32);
    return actual_zeros >= target_zeros;
}

int proof_compare_quality(const SpaceProof* a, const SpaceProof* b) {
    // Lower quality value = better (more leading zeros)
    return memcmp(a->quality, b->quality, 32);
}

// ============================================================================
// QUALITY CALCULATION (Helper)
// ============================================================================

void calculate_quality(const uint8_t challenge[32], 
                       const uint8_t proof_hash[28],
                       uint8_t quality[32]) {
    uint8_t full_target[32];
    blake3_hash(challenge, 32, full_target);
    
    uint8_t target[28];
    memcpy(target, full_target, 28);
    
    calculate_distance(proof_hash, target, quality);
}

// ============================================================================
// DIFFICULTY SYSTEM (1-256 Range)
// ============================================================================
//
// NEW DIFFICULTY SYSTEM:
// - Difficulty ranges from 1 to 256
// - Difficulty N means proof quality must have N leading zero bits
// - Simple 1:1 mapping (no complex formulas)
//
// PROBABILITY ANALYSIS:
// - Difficulty 1:  1/2 chance of valid proof per entry
// - Difficulty 8:  1/256 chance per entry
// - Difficulty 16: 1/65536 chance per entry
// - Difficulty 20: 1/1048576 chance per entry
//
// DEFAULT DIFFICULTY FORMULA:
//   default = k + floor(log2(validators))
//
// RATIONALE:
// - k component: Larger plots (higher k) have more entries, so need
//   higher difficulty to balance
// - log2(validators) component: More validators = more competition,
//   need higher difficulty to avoid too many valid proofs
//
// EXAMPLE:
//   k=20, 3 validators → default = 20 + 1 = 21
//   k=18, 8 validators → default = 18 + 3 = 21
//
// NOTE: difficulty_to_target_bits() and calculate_default_difficulty()
//       are defined as inline functions in consensus.h
// ============================================================================

// Helper to calculate default difficulty with logging
static uint32_t calc_default_difficulty_logged(uint32_t k_param, uint32_t validator_count) {
    // Calculate log2 of validator count
    uint32_t log2_v = 0;
    if (validator_count > 1) {
        uint32_t v = validator_count;
        while (v > 1) {
            v >>= 1;
            log2_v++;
        }
    }
    
    // NEW FORMULA: Start with a base difficulty that ensures reasonable proof rates
    // For k entries, we want roughly 1-3 proofs per validator per round
    // Probability of valid proof per entry = 1/2^diff
    // Expected proofs = 2^k / 2^diff = 2^(k-diff)
    // For 1-3 proofs per validator: diff ≈ k - 1 to k - 2
    // With multiple validators: diff = k - 2 + log2_v (increase slightly for competition)
    
    int32_t default_diff = (int32_t)k_param - 2 + (int32_t)log2_v;
    
    // Ensure minimum difficulty for security
    if (default_diff < (int32_t)DIFFICULTY_MIN) default_diff = DIFFICULTY_MIN;
    if (default_diff > (int32_t)DIFFICULTY_MAX) default_diff = DIFFICULTY_MAX;
    
    LOG_INFO("📐 Default difficulty calculation (improved):");
    LOG_INFO("   ├─ k_param:        %u (plot has 2^%u = %lu entries)", k_param, k_param, 1UL << k_param);
    LOG_INFO("   ├─ validators:     %u (log2 = %u)", validator_count, log2_v);
    LOG_INFO("   ├─ formula:        k - 2 + log2(validators) = %d - 2 + %u = %d", k_param, log2_v, default_diff);
    LOG_INFO("   ├─ expected proofs: ~%.1f per validator per round", (double)(1UL << k_param) / (1UL << default_diff));
    LOG_INFO("   └─ final_diff:     %u", (uint32_t)default_diff);
    
    return (uint32_t)default_diff;
}

// ============================================================================
// DIFFICULTY STATE MANAGEMENT
// ============================================================================
//
// The difficulty auto-adjusts to maintain healthy network participation:
// - Too few valid proofs → decrease difficulty (make easier)
// - Too many valid proofs → increase difficulty (make harder)
//
// ADJUSTMENT ALGORITHM:
// 1. Track valid proofs over adjustment_interval blocks
// 2. Calculate average proofs per block
// 3. If avg < min_threshold → decrease difficulty by 1
// 4. If avg > max_threshold → increase difficulty by 1
// 5. Otherwise keep same
//
// THRESHOLDS:
// - min = 20% of validators (too few participating)
// - max = 80% of validators (too many winning)
// ============================================================================

static void calculate_min_max(uint32_t validator_count, uint32_t* min, uint32_t* max) {
    if (validator_count == 0) {
        *min = 1;
        *max = 1;
        return;
    }
    
    // min = ceil(count * 0.2) - at least 20% participation
    *min = (validator_count * 2 + 9) / 10;
    if (*min < 1) *min = 1;
    
    // max = ceil(count * 0.8) - at most 80% participation  
    *max = (validator_count * 8 + 9) / 10;
    if (*max < *min) *max = *min;
}

DifficultyState* difficulty_init(uint32_t interval, uint32_t k_param, uint32_t validator_count) {
    DifficultyState* state = safe_malloc(sizeof(DifficultyState));
    memset(state, 0, sizeof(DifficultyState));
    
    state->k_param = k_param;
    state->validator_count = validator_count;
    state->adjustment_interval = interval > 0 ? interval : 5;
    state->blocks_since_adjust = 0;
    state->valid_proofs_count = 0;
    state->blocks_with_winner = 0;
    state->consecutive_no_winner = 0;
    state->consecutive_all_win = 0;
    state->round_proofs = 0;
    
    // Calculate default difficulty using the logged helper
    state->current_difficulty = calc_default_difficulty_logged(k_param, validator_count);
    
    // Calculate adjustment thresholds
    calculate_min_max(validator_count, &state->min_valid_proofs, &state->max_valid_proofs);
    
    LOG_INFO("🎯 ════════════════════════════════════════════════════════════");
    LOG_INFO("🎯 DIFFICULTY SYSTEM INITIALIZED");
    LOG_INFO("   ├─ Current difficulty:    %u (%u leading zero bits required)", 
             state->current_difficulty, state->current_difficulty);
    LOG_INFO("   ├─ Adjustment interval:   %u blocks", interval);
    LOG_INFO("   ├─ Decrease threshold:    < %u proofs/block (20%% of %u)", 
             state->min_valid_proofs, validator_count);
    LOG_INFO("   └─ Increase threshold:    > %u proofs/block (80%% of %u)", 
             state->max_valid_proofs, validator_count);
    LOG_INFO("🎯 ════════════════════════════════════════════════════════════");
    
    return state;
}

void difficulty_update_validator_count(DifficultyState* state, uint32_t validator_count) {
    if (!state) return;
    state->validator_count = validator_count;
    calculate_min_max(validator_count, &state->min_valid_proofs, &state->max_valid_proofs);
    LOG_INFO("🎯 Updated thresholds for %u validators: min=%u, max=%u",
             validator_count, state->min_valid_proofs, state->max_valid_proofs);
}

void difficulty_record_proof(DifficultyState* state) {
    if (state) {
        state->valid_proofs_count++;
    }
}

void difficulty_adjust(DifficultyState* state) {
    if (!state) return;
    
    state->blocks_since_adjust++;
    
    // Only adjust after enough blocks have passed
    if (state->blocks_since_adjust < state->adjustment_interval) {
        return;
    }
    
    /*
     * IMPROVED DIFFICULTY ADJUSTMENT ALGORITHM
     * =========================================
     * 
     * Primary metric: Winner rate (blocks with at least one valid proof)
     * - Target: 70-100% winner rate
     * - If < 50%: difficulty too hard
     * - If 100% with high proofs: might be too easy
     * 
     * Secondary metric: Average proofs per block
     * - Target: 1-3 proofs per block
     * - Fine-tuning within acceptable winner rate
     */
    
    // Calculate metrics
    uint32_t total_blocks = state->blocks_since_adjust;
    double avg_proofs = (double)state->valid_proofs_count / total_blocks;
    double winner_rate = (double)state->blocks_with_winner / total_blocks * 100.0;
    
    LOG_INFO("📊 ════════════════════════════════════════════════════════════");
    LOG_INFO("📊 DIFFICULTY ADJUSTMENT CHECK (every %u blocks)", state->adjustment_interval);
    LOG_INFO("   ├─ Blocks in interval:   %u", total_blocks);
    LOG_INFO("   ├─ Blocks with winner:   %u (%.1f%%)", state->blocks_with_winner, winner_rate);
    LOG_INFO("   ├─ Total valid proofs:   %u", state->valid_proofs_count);
    LOG_INFO("   ├─ Avg proofs/block:     %.2f", avg_proofs);
    LOG_INFO("   ├─ Current difficulty:   %u", state->current_difficulty);
    LOG_INFO("   └─ Validators:           %u", state->validator_count);
    
    uint32_t old_difficulty = state->current_difficulty;
    int adjustment = 0;
    const char* reason = "";
    
    // Primary: Check winner rate first
    if (winner_rate < 50.0) {
        // Less than half the blocks have winners - too hard
        adjustment = -1;
        reason = "winner rate < 50%% (blocks often empty)";
        if (winner_rate < 30.0) {
            adjustment = -2;
            reason = "winner rate < 30%% (most blocks empty)";
        }
    } else if (winner_rate >= 90.0 && avg_proofs > 3.0) {
        // Almost all blocks have winners AND many proofs - too easy
        adjustment = +1;
        reason = "winner rate >= 90%% with high proof count";
    } else {
        // Secondary: Fine-tune based on proof count
        if (avg_proofs < 0.5) {
            adjustment = -1;
            reason = "very few proofs (avg < 0.5)";
        } else if (avg_proofs > 4.0) {
            adjustment = +1;
            reason = "many proofs (avg > 4.0)";
        } else {
            reason = "healthy network (winner rate and proof count in range)";
        }
    }
    
    // Apply adjustment with bounds checking
    if (adjustment < 0) {
        int new_diff = (int)state->current_difficulty + adjustment;
        state->current_difficulty = (new_diff >= DIFFICULTY_MIN) ? new_diff : DIFFICULTY_MIN;
    } else if (adjustment > 0) {
        int new_diff = (int)state->current_difficulty + adjustment;
        state->current_difficulty = (new_diff <= DIFFICULTY_MAX) ? new_diff : DIFFICULTY_MAX;
    }
    
    if (state->current_difficulty != old_difficulty) {
        if (state->current_difficulty > old_difficulty) {
            LOG_INFO("📈 DIFFICULTY INCREASED: %u → %u", old_difficulty, state->current_difficulty);
        } else {
            LOG_INFO("📉 DIFFICULTY DECREASED: %u → %u", old_difficulty, state->current_difficulty);
        }
        LOG_INFO("   Reason: %s", reason);
    } else {
        LOG_INFO("🎯 DIFFICULTY UNCHANGED at %u (%s)", state->current_difficulty, reason);
    }
    
    LOG_INFO("📊 ════════════════════════════════════════════════════════════");
    
    // Reset counters for next interval
    state->blocks_since_adjust = 0;
    state->valid_proofs_count = 0;
    state->blocks_with_winner = 0;
}

void difficulty_reset_round(DifficultyState* state) {
    // Reset per-round counter
    if (state) {
        state->round_proofs = 0;
    }
}

void difficulty_record_winner(DifficultyState* state, bool had_winner) {
    if (!state) return;
    
    if (had_winner) {
        state->blocks_with_winner++;
        state->consecutive_no_winner = 0;
        state->consecutive_all_win++;
    } else {
        state->consecutive_no_winner++;
        state->consecutive_all_win = 0;
        
        // Emergency: if 3+ consecutive blocks without winner, decrease immediately
        if (state->consecutive_no_winner >= 3 && state->current_difficulty > DIFFICULTY_MIN) {
            uint32_t old = state->current_difficulty;
            state->current_difficulty--;
            LOG_INFO("📉 EMERGENCY DECREASE: %u → %u (no winner for %u blocks)", 
                     old, state->current_difficulty, state->consecutive_no_winner);
            state->consecutive_no_winner = 0;
        }
    }
}

uint32_t difficulty_get_current(const DifficultyState* state) {
    return state ? state->current_difficulty : DIFFICULTY_DEFAULT;
}

void difficulty_set(DifficultyState* state, uint32_t difficulty) {
    if (state) {
        if (difficulty < DIFFICULTY_MIN) difficulty = DIFFICULTY_MIN;
        if (difficulty > DIFFICULTY_MAX) difficulty = DIFFICULTY_MAX;
        state->current_difficulty = difficulty;
        LOG_INFO("🎯 Difficulty manually set to %u", difficulty);
    }
}

void difficulty_destroy(DifficultyState* state) {
    if (state) free(state);
}

// ============================================================================
// PLOT CLEANUP
// ============================================================================

void plot_destroy(Plot* plot) {
    if (!plot) return;
    
    if (plot->entries) {
        LOG_INFO("🗑️  Freeing plot with %lu entries...", plot->entry_count);
        free(plot->entries);
    }
    free(plot);
}

// ============================================================================
// PROOF SERIALIZATION (Hex format for network transmission)
// ============================================================================
//
// Format: plot_id_hex | nonce | proof_hash_hex | quality_hex
// All separated by pipes for easy parsing
// ============================================================================

char* proof_serialize(const SpaceProof* proof) {
    if (!proof) return NULL;
    
    char* result = safe_malloc(256);
    char plot_id_hex[65], hash_hex[57], quality_hex[65];
    
    bytes_to_hex_buf(proof->plot_id, 32, plot_id_hex);
    bytes_to_hex_buf(proof->proof_hash, 28, hash_hex);
    bytes_to_hex_buf(proof->quality, 32, quality_hex);
    
    snprintf(result, 256, "%s|%u|%s|%s", 
             plot_id_hex, proof->nonce, hash_hex, quality_hex);
    
    return result;
}

SpaceProof* proof_deserialize(const char* data) {
    if (!data) return NULL;
    
    SpaceProof* proof = safe_malloc(sizeof(SpaceProof));
    
    char plot_id_hex[65], hash_hex[57], quality_hex[65];
    
    if (sscanf(data, "%64[^|]|%u|%56[^|]|%64s",
               plot_id_hex, &proof->nonce, hash_hex, quality_hex) != 4) {
        free(proof);
        return NULL;
    }
    
    hex_to_bytes_buf(plot_id_hex, proof->plot_id, 32);
    hex_to_bytes_buf(hash_hex, proof->proof_hash, 28);
    hex_to_bytes_buf(quality_hex, proof->quality, 32);
    
    return proof;
}

// ============================================================================
// PROTOBUF SERIALIZATION (Binary format - more compact)
// ============================================================================

uint8_t* proof_serialize_pb(const SpaceProof* proof, size_t* out_len) {
    if (!proof) return NULL;
    
    Blockchain__SpaceProof pb_proof = BLOCKCHAIN__SPACE_PROOF__INIT;
    
    pb_proof.plot_id.len = 32;
    pb_proof.plot_id.data = (uint8_t*)proof->plot_id;
    pb_proof.nonce = proof->nonce;
    pb_proof.proof_hash.len = 28;
    pb_proof.proof_hash.data = (uint8_t*)proof->proof_hash;
    pb_proof.quality.len = 32;
    pb_proof.quality.data = (uint8_t*)proof->quality;
    
    size_t size = blockchain__space_proof__get_packed_size(&pb_proof);
    uint8_t* buffer = safe_malloc(size);
    blockchain__space_proof__pack(&pb_proof, buffer);
    
    if (out_len) *out_len = size;
    return buffer;
}

SpaceProof* proof_deserialize_pb(const uint8_t* data, size_t len) {
    if (!data) return NULL;
    
    Blockchain__SpaceProof* pb_proof = blockchain__space_proof__unpack(NULL, len, data);
    if (!pb_proof) return NULL;
    
    SpaceProof* proof = safe_malloc(sizeof(SpaceProof));
    
    if (pb_proof->plot_id.data && pb_proof->plot_id.len >= 32)
        memcpy(proof->plot_id, pb_proof->plot_id.data, 32);
    
    proof->nonce = pb_proof->nonce;
    
    if (pb_proof->proof_hash.data && pb_proof->proof_hash.len >= 28)
        memcpy(proof->proof_hash, pb_proof->proof_hash.data, 28);
    
    if (pb_proof->quality.data && pb_proof->quality.len >= 32)
        memcpy(proof->quality, pb_proof->quality.data, 32);
    
    blockchain__space_proof__free_unpacked(pb_proof, NULL);
    return proof;
}
