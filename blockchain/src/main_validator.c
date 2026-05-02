/**
 * ============================================================================
 * MAIN_VALIDATOR.C - Farmer/Validator Entry Point (v28)
 * ============================================================================
 * 
 * ARCHITECTURE (v28 - Proof-Based Winner Selection):
 * ==================================================
 * 
 * When a validator finds a valid proof:
 * 1. Submits LIGHTWEIGHT PROOF to metronome (~100 bytes)
 * 2. Waits for WINNER announcement
 * 3. If we won:
 *    a) Fetches transactions from pool (directly)
 *    b) Creates coinbase transaction (paying itself)
 *    c) Builds complete block with transactions
 *    d) Sends block DIRECTLY to blockchain
 *    e) Confirms transactions with pool
 *    f) Notifies metronome: BLOCK_CONFIRMED
 * 
 * The metronome only:
 * - Broadcasts challenges + winner announcements
 * - Collects lightweight proofs and selects winner
 * - Creates EMPTY blocks when no validator submits valid proof
 * - Waits for block confirmation before issuing next challenge
 * 
 * ============================================================================
 */

#include "../include/validator.h"
#include "../include/crypto_backend.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static Validator* validator = NULL;

void signal_handler(int sig) {
    (void)sig;
    if (validator) {
        validator_stop(validator);
    }
    LOG_INFO("Shutdown signal received");
}

void print_usage(const char* prog) {
    printf("\n");
    printf("VALIDATOR/FARMER - Proof of Space (v28)\n");
    printf("========================================\n");
    printf("Submit proofs -> if winner -> create block -> send to blockchain\n");
    printf("\n");
    printf("Usage: %s <name> [options]\n", prog);
    printf("\n");
    printf("Arguments:\n");
    printf("  <name>                    Validator name (also used as wallet)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -k, --k-param <16-30>     Plot k parameter (default: 20)\n");
    printf("  --scheme ed25519|falcon|mldsa  Signature scheme (default: from build SIG_SCHEME)\n");
    printf("  --metronome-req <addr>    Metronome REQ (default: tcp://localhost:5556)\n");
    printf("  --metronome-sub <addr>    Metronome SUB (default: tcp://localhost:5558)\n");
    printf("  --pool <addr>             Pool address (default: tcp://localhost:5557)\n");
    printf("  --blockchain <addr>       Blockchain (default: tcp://localhost:5555)\n");
    printf("  --max-txs <N>             Max transactions per block (default: 10000)\n");
    printf("  -h, --help                Show this help\n");
    printf("\n");
}

int main(int argc, char* argv[]) {
    uint32_t k_param = 20;
    uint32_t max_txs = 0;  // 0 = use default
    const char* metronome_req = "tcp://localhost:5556";
    const char* metronome_sub = "tcp://localhost:5558";
    const char* pool_addr = "tcp://localhost:5557";
    const char* blockchain_addr = "tcp://localhost:5555";
    const char* name = NULL;
    uint8_t sig_type = SIG_SCHEME;  // default from compile-time flag

    static struct option long_options[] = {
        {"k-param", required_argument, 0, 'k'},
        {"scheme", required_argument, 0, 6},
        {"metronome-req", required_argument, 0, 1},
        {"metronome-sub", required_argument, 0, 2},
        {"pool", required_argument, 0, 3},
        {"blockchain", required_argument, 0, 4},
        {"max-txs", required_argument, 0, 5},
        {"generate-plot-only", no_argument, 0, 7},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    bool generate_plot_only = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "k:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k': k_param = atoi(optarg); break;
            case 6:
                if (strcmp(optarg, "ed25519") == 0) sig_type = SIG_ED25519;
                else if (strcmp(optarg, "falcon") == 0) sig_type = SIG_FALCON512;
                else if (strcmp(optarg, "mldsa") == 0) sig_type = SIG_ML_DSA44;
                else if (strcmp(optarg, "hybrid") == 0) sig_type = SIG_HYBRID;
                else { fprintf(stderr, "Unknown scheme: %s\n", optarg); return 1; }
                break;
            case 1: metronome_req = optarg; break;
            case 2: metronome_sub = optarg; break;
            case 3: pool_addr = optarg; break;
            case 4: blockchain_addr = optarg; break;
            case 5: max_txs = atoi(optarg); break;
            case 7: generate_plot_only = true; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (optind < argc) {
        name = argv[optind];
    }
    
    if (!name) {
        fprintf(stderr, "Error: Validator name required\n");
        print_usage(argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("VALIDATOR/FARMER: %s (v28 - Proof Submission)", name);
    LOG_INFO("  k parameter:      %u (2^%u = %lu entries)", 
             k_param, k_param, (uint64_t)1 << k_param);
    LOG_INFO("  Metronome REQ:    %s (for proofs + block confirms)", metronome_req);
    LOG_INFO("  Metronome SUB:    %s (for challenges + winner)", metronome_sub);
    LOG_INFO("  Pool:             %s (for TX fetching when winner)", pool_addr);
    LOG_INFO("  Blockchain:       %s (for block submission when winner)", blockchain_addr);
    LOG_INFO("  Architecture:     Proof → Winner? → Block → Blockchain → Notify");
    
    validator = validator_create(name, k_param, sig_type);
    if (!validator) {
        LOG_ERROR("Failed to create validator");
        return 1;
    }
    
    if (!generate_plot_only) {
        if (!validator_init_sockets(validator, metronome_req, metronome_sub,
                                    pool_addr, blockchain_addr)) {
            LOG_ERROR("Failed to initialize sockets");
            validator_destroy(validator);
            return 1;
        }
    }
    
    LOG_INFO("Generating plot...");
    if (!validator_generate_plot(validator)) {
        LOG_ERROR("Failed to generate plot");
        validator_destroy(validator);
        return 1;
    }
    
    if (generate_plot_only) {
        LOG_INFO("--generate-plot-only: plot generation complete, exiting.");
        validator_destroy(validator);
        return 0;
    }
    
    LOG_INFO("Plot ready! Starting farming loop...");
    LOG_INFO("Submit proofs -> if winner -> create block -> send to blockchain!");
    
    if (max_txs > 0) {
        validator_set_max_txs_per_block(max_txs);
    }
    
    validator_run(validator);
    
    validator_destroy(validator);
    LOG_INFO("Validator %s stopped", name);
    return 0;
}
