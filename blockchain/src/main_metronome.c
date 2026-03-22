/**
 * ============================================================================
 * MAIN_METRONOME.C - Block Coordinator Entry Point (v29)
 * ============================================================================
 * 
 * The Metronome is the "heartbeat" of the blockchain. It coordinates the
 * Proof of Space consensus by:
 * 
 * 1. Broadcasting challenges to all validators
 * 2. Collecting lightweight PROOF submissions from validators
 * 3. Selecting the winner (best quality proof)
 * 4. Announcing winner via PUB socket
 * 5. Waiting for BLOCKCHAIN to confirm block (via PULL socket, v29)
 * 6. Only then broadcasting the NEXT challenge (based on new block hash)
 * 7. Creating EMPTY blocks only when no validator submits valid proof
 * 
 * NETWORK SOCKETS:
 * ================
 * - PUB (5558): Broadcasts challenges + winner announcements
 * - REP (5556): Receives proof submissions + queries
 * - PULL (5560): Receives block confirmations from blockchain (v29)
 * - REQ -> Blockchain (5555): Queries chain state, adds empty blocks
 * - REQ -> Pool (5557): Reserved (winner handles TX confirmation)
 * 
 * ============================================================================
 */

#include "../include/metronome.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static Metronome* metronome = NULL;

void signal_handler(int sig) {
    (void)sig;
    if (metronome) {
        metronome_stop(metronome);
    }
    LOG_INFO("🛑 Shutdown signal received");
}

void print_usage(const char* prog) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║        METRONOME - Block Coordinator (v29)                       ║\n");
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Proof-based winner selection: validators submit proofs,         ║\n");
    printf("║  metronome selects winner, winner creates block & adds to chain  ║\n");
    printf("║  Blockchain notifies metronome directly (no validator relay)     ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Timing Options:\n");
    printf("  -i, --interval <sec>      Block interval in seconds (default: 6)\n");
    printf("\n");
    printf("Difficulty Options:\n");
    printf("  -d, --difficulty <1-256>  Initial difficulty (default: auto = k + log2(v))\n");
    printf("  -k, --k-param <16-30>     Plot k parameter (default: 20)\n");
    printf("  -v, --validators <n>      Initial validator count (default: 3)\n");
    printf("\n");
    printf("Reward Options:\n");
    printf("  -r, --reward <coins>      Base mining reward (default: 50)\n");
    printf("  --halving <blocks>        Halving interval (default: 100)\n");
    printf("\n");
    printf("Network Options:\n");
    printf("  --rep <addr>              REP socket address (default: tcp://*:5556)\n");
    printf("  --pub <addr>              PUB socket address (default: tcp://*:5558)\n");
    printf("  --notify <addr>           PULL socket for blockchain notifications (default: tcp://*:5560)\n");
    printf("  --blockchain <addr>       Blockchain address (default: tcp://localhost:5555)\n");
    printf("  --pool <addr>             Pool address (default: tcp://localhost:5557)\n");
    printf("\n");
    printf("Other:\n");
    printf("  --help                    Show this help\n");
    printf("\n");
}

int main(int argc, char* argv[]) {
    uint32_t block_interval = 6;
    uint32_t difficulty = 0;
    uint32_t k_param = 20;
    uint32_t validators = 3;
    uint64_t reward = 10000;
    uint32_t halving = 100;
    const char* rep_addr = "tcp://*:5556";
    const char* pub_addr = "tcp://*:5558";
    const char* notify_pull_addr = "tcp://*:5560";
    const char* blockchain_addr = "tcp://localhost:5555";
    const char* pool_addr = "tcp://localhost:5557";
    
    static struct option long_options[] = {
        {"interval", required_argument, 0, 'i'},
        {"difficulty", required_argument, 0, 'd'},
        {"k-param", required_argument, 0, 'k'},
        {"validators", required_argument, 0, 'v'},
        {"reward", required_argument, 0, 'r'},
        {"halving", required_argument, 0, 'h'},
        {"rep", required_argument, 0, 1},
        {"pub", required_argument, 0, 2},
        {"blockchain", required_argument, 0, 3},
        {"pool", required_argument, 0, 4},
        {"help", no_argument, 0, 5},
        {"notify", required_argument, 0, 6},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "i:d:k:v:r:h:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i': block_interval = atoi(optarg); break;
            case 'd': difficulty = atoi(optarg); break;
            case 'k': k_param = atoi(optarg); break;
            case 'v': validators = atoi(optarg); break;
            case 'r': reward = strtoull(optarg, NULL, 10); break;
            case 'h': halving = atoi(optarg); break;
            case 1: rep_addr = optarg; break;
            case 2: pub_addr = optarg; break;
            case 3: blockchain_addr = optarg; break;
            case 4: pool_addr = optarg; break;
            case 5:
                print_usage(argv[0]);
                return 0;
            case 6: notify_pull_addr = optarg; break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("⏱️  ════════════════════════════════════════════════════════════");
    LOG_INFO("⏱️  METRONOME - Block Coordinator (v29 - Direct Blockchain Notify)");
    LOG_INFO("⏱️  ════════════════════════════════════════════════════════════");
    LOG_INFO("   Network Configuration:");
    LOG_INFO("     PUB socket (challenges+winners):  %s", pub_addr);
    LOG_INFO("     REP socket (proofs+queries):      %s", rep_addr);
    LOG_INFO("     PULL socket (block notifications): %s", notify_pull_addr);
    LOG_INFO("     Blockchain server:                 %s", blockchain_addr);
    LOG_INFO("     Transaction pool:                  %s", pool_addr);
    LOG_INFO("   Architecture:");
    LOG_INFO("     Proofs → Winner → Block → Blockchain → Metronome (PULL)");
    LOG_INFO("     Block confirmation comes directly from blockchain!");
    LOG_INFO("⏱️  ════════════════════════════════════════════════════════════");
    
    metronome = metronome_create(block_interval, 5, k_param, validators,
                                 difficulty, reward, halving);
    if (!metronome) {
        LOG_ERROR("❌ Failed to create metronome");
        return 1;
    }
    
    if (!metronome_init_sockets(metronome, rep_addr, pub_addr, 
                                blockchain_addr, pool_addr, notify_pull_addr)) {
        LOG_ERROR("❌ Failed to initialize sockets");
        metronome_destroy(metronome);
        return 1;
    }
    
    LOG_INFO("🚀 Metronome starting - first challenge in %u seconds...", block_interval);
    
    /*
     * MAIN LOOP (v29):
     * 1. Generate new challenge from latest blockchain state
     * 2. Broadcast challenge to all validators
     * 3. Collect PROOF submissions (lightweight, ~100 bytes each)
     * 4. Select winner (best proof quality)
     * 5. Announce winner via PUB
     * 6. Wait for BLOCKCHAIN to notify block added (via PULL socket)
     * 7. If no winner/timeout: create empty block
     * 8. Repeat (new challenge uses confirmed block hash)
     */
    metronome_run(metronome);
    
    metronome_destroy(metronome);
    
    LOG_INFO("👋 Metronome stopped");
    return 0;
}
