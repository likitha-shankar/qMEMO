/**
 * ============================================================================
 * MAIN_BLOCKCHAIN.C - Blockchain Server (v45.2 - PUB/SUB Confirmation)
 * ============================================================================
 * 
 * v45.2 CHANGES:
 * - Blockchain is now the SINGLE SOURCE OF TRUTH for confirmations.
 * - After accepting a block, publishes CONFIRM_BLOCK with TX hashes on PUB.
 * - Pool and wallets subscribe to PUB for async confirmation notifications.
 * - Validators no longer confirm directly with pool (architectural fix).
 * 
 * PUB topics:
 *   NEW_BLOCK:<height>:<tx_count>:<hash>     - For wallets/benchmarks
 *   CONFIRM_BLOCK:<height><count><hashes>    - For pool TX removal (binary)
 *
 * COMMANDS: ADD_BLOCK_PB, ADD_BLOCK, GET_LAST, GET_LAST_HASH, GET_HEIGHT,
 *           GET_BALANCE, GET_NONCE, GET_SUMMARY
 * ============================================================================
 */

#include "../include/blockchain.h"
#include "../include/block.h"
#include "../include/transaction.h"
#include "../include/wallet.h"
#include "../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <zmq.h>

static volatile bool running = true;
static Blockchain* blockchain = NULL;
static uint64_t requests_handled = 0;

void signal_handler(int sig) {
    (void)sig;
    running = false;
    LOG_INFO("🛑 Shutdown signal received");
}

int main(int argc, char* argv[]) {
    const char* bind_addr = "tcp://*:5555";
    const char* metronome_notify_addr = NULL;
    const char* pub_addr = NULL;
    
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--metronome-notify") == 0 || strcmp(argv[i], "-m") == 0) && i + 1 < argc) {
            metronome_notify_addr = argv[++i];
        } else if (strcmp(argv[i], "--pub") == 0 && i + 1 < argc) {
            pub_addr = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("\nBlockchain Server v29.2\n");
            printf("Usage: %s [bind_addr] [options]\n", argv[0]);
            printf("  bind_addr                    REP socket (default: tcp://*:5555)\n");
            printf("  -m, --metronome-notify ADDR  PUSH to metronome\n");
            printf("  --pub ADDR                   PUB socket for block notifications\n");
            printf("  -h, --help                   Show this help\n\n");
            return 0;
        } else if (argv[i][0] != '-') {
            bind_addr = argv[i];
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("🔗 ════════════════════════════════════════════════════════════");
    LOG_INFO("🔗 BLOCKCHAIN SERVER v29.2 (GET_LAST_HASH + PUB)");
    LOG_INFO("🔗 ════════════════════════════════════════════════════════════");
    LOG_INFO("   Commands: ADD_BLOCK, GET_LAST, GET_LAST_HASH, GET_HEIGHT,");
    LOG_INFO("             GET_BALANCE, GET_NONCE, GET_SUMMARY");
    if (metronome_notify_addr)
        LOG_INFO("   Metronome PUSH: %s", metronome_notify_addr);
    if (pub_addr)
        LOG_INFO("   Block PUB:      %s", pub_addr);
    LOG_INFO("🔗 ════════════════════════════════════════════════════════════");
    
    blockchain = blockchain_create();
    if (!blockchain) {
        LOG_ERROR("❌ Failed to create blockchain");
        return 1;
    }
    LOG_INFO("✅ Blockchain initialized (height: %lu)", blockchain->height);
    
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_REP);
    
    if (zmq_bind(socket, bind_addr) != 0) {
        LOG_ERROR("❌ Failed to bind to %s", bind_addr);
        blockchain_destroy(blockchain);
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return 1;
    }
    LOG_INFO("🔌 REP on %s", bind_addr);
    
    // PUSH → metronome
    void* metronome_push = NULL;
    if (metronome_notify_addr) {
        metronome_push = zmq_socket(context, ZMQ_PUSH);
        int linger = 1000;
        zmq_setsockopt(metronome_push, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_connect(metronome_push, metronome_notify_addr) != 0) {
            LOG_ERROR("❌ PUSH failed: %s", metronome_notify_addr);
            zmq_close(metronome_push);
            metronome_push = NULL;
        } else {
            LOG_INFO("🔌 PUSH → metronome at %s", metronome_notify_addr);
        }
    }
    
    // PUB → wallets/benchmarks for instant block notifications
    void* pub_socket = NULL;
    if (pub_addr) {
        pub_socket = zmq_socket(context, ZMQ_PUB);
        int linger = 1000;
        zmq_setsockopt(pub_socket, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_bind(pub_socket, pub_addr) != 0) {
            LOG_ERROR("❌ PUB bind failed: %s", pub_addr);
            zmq_close(pub_socket);
            pub_socket = NULL;
        } else {
            LOG_INFO("🔌 PUB on %s", pub_addr);
        }
    }
    
    LOG_INFO("🚀 Blockchain server ready!");
    
    int timeout = 1;  // 1ms poll: eliminates ~50ms avg wake-up latency at 100ms timeout
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 16MB buffer - enough for blocks with up to 65K transactions
    // (65536 TXs × ~160 bytes protobuf per TX = ~10MB)
    #define BLOCKCHAIN_RECV_BUFFER_SIZE (16 * 1024 * 1024)
    char* buffer = safe_malloc(BLOCKCHAIN_RECV_BUFFER_SIZE);
    
    while (running) {
        int size = zmq_recv(socket, buffer, BLOCKCHAIN_RECV_BUFFER_SIZE - 1, 0);
        
        if (size > 0) {
            buffer[size] = '\0';
            requests_handled++;
            
            // =============================================================
            // ADD_BLOCK_PB - Protobuf block transport (v45.2)
            // Format: "ADD_BLOCK_PB:" (13B) + farmer_name (64B) + protobuf_block
            // Uses Google Protocol Buffers for schema-safe deserialization.
            // ~10-15x faster than hex for 10K TXs:
            //   OLD: 2.24MB hex + O(N^2) strlen = 530ms
            //   NEW: ~1.1MB protobuf + O(N) unpack = ~30-50ms
            // =============================================================
            if (size > 77 && starts_with(buffer, "ADD_BLOCK_PB:")) {
                LOG_INFO("📥 ADD_BLOCK_PB (%d bytes)", size);
#ifndef DIAG_OFF
                uint64_t blkd_t_recv_ns = get_current_time_ns();
#endif

                char farmer_name[64];
                memcpy(farmer_name, buffer + 13, 64);
                farmer_name[63] = '\0';
                trim(farmer_name);

                uint8_t* block_data = (uint8_t*)(buffer + 77);  // 13 + 64
                size_t block_len = size - 77;
                Block* block = block_deserialize_pb(block_data, block_len);
#ifndef DIAG_OFF
                uint64_t blkd_t_unpack_ns = get_current_time_ns();
#endif

                if (block) {
                    if (blockchain_add_block(blockchain, block)) {
                        LOG_INFO("✅ Block #%u added (height: %lu, %u TXs)",
                                 block->header.height, blockchain->height,
                                 block->header.transaction_count);
#ifndef DIAG_OFF
                        uint64_t blkd_t_validate_ns = bc_diag_t_validate_ns;
                        uint64_t blkd_t_commit_ns   = bc_diag_t_commit_ns;
                        uint64_t blkd_t_send_ns     = get_current_time_ns();
#endif
                        zmq_send(socket, "OK", 2, 0);
#ifndef DIAG_OFF
                        {
                            static FILE* bc_csv = NULL;
                            if (!bc_csv) {
                                char csv_path[64];
                                snprintf(csv_path, sizeof(csv_path),
                                         "blockchain_diag_%d.csv", getpid());
                                bc_csv = fopen(csv_path, "w");
                                if (bc_csv) {
                                    fprintf(bc_csv,
                                        "block_height,block_txs,block_bytes,"
                                        "t_recv_ns,t_unpack_ns,t_validate_ns,"
                                        "t_commit_ns,t_send_ns,recv_to_send_ns\n");
                                    fflush(bc_csv);
                                }
                            }
                            if (bc_csv) {
                                fprintf(bc_csv, "%u,%u,%zu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                                    block->header.height,
                                    block->header.transaction_count,
                                    block_len,
                                    (unsigned long)blkd_t_recv_ns,
                                    (unsigned long)blkd_t_unpack_ns,
                                    (unsigned long)blkd_t_validate_ns,
                                    (unsigned long)blkd_t_commit_ns,
                                    (unsigned long)blkd_t_send_ns,
                                    (unsigned long)(blkd_t_send_ns - blkd_t_recv_ns));
                                fflush(bc_csv);
                            }
                        }
#endif
                        
                        char block_hash_hex[65];
                        bytes_to_hex_buf(block->header.hash, 32, block_hash_hex);
                        
                        // Notify metronome via PUSH
                        if (metronome_push && farmer_name[0] != '\0') {
                            char notify_msg[256];
                            snprintf(notify_msg, sizeof(notify_msg),
                                     "BLOCK_CONFIRMED:%s|%s", block_hash_hex, farmer_name);
                            zmq_send(metronome_push, notify_msg, strlen(notify_msg), ZMQ_DONTWAIT);
                        }
                        
                        if (pub_socket) {
                            // 1) NEW_BLOCK for wallets/benchmarks (text)
                            char pub_msg[256];
                            snprintf(pub_msg, sizeof(pub_msg), "NEW_BLOCK:%u:%u:%s",
                                     block->header.height, block->header.transaction_count,
                                     block_hash_hex);
                            zmq_send(pub_socket, pub_msg, strlen(pub_msg), ZMQ_DONTWAIT);
                            
                            // 2) CONFIRM_BLOCK for pool TX removal (binary)
                            // =====================================================
                            // THIS IS THE CORRECT CONFIRMATION PATH.
                            // The blockchain has validated and accepted the block,
                            // so these TXs are truly confirmed. Pool subscribes
                            // to this PUB topic and removes them from pending.
                            //
                            // Format: "CONFIRM_BLOCK:" (14B) + height (4B) +
                            //         hash_count (4B) + N x TX_HASH_SIZE hashes
                            //
                            // Skip index 0 (coinbase - not in pool).
                            // =====================================================
                            uint32_t user_tx_count = block->header.transaction_count > 1 
                                ? block->header.transaction_count - 1 : 0;
                            
                            if (user_tx_count > 0) {
                                size_t confirm_size = 22 + (size_t)user_tx_count * TX_HASH_SIZE;
                                uint8_t* confirm_msg = safe_malloc(confirm_size);
                                memcpy(confirm_msg, "CONFIRM_BLOCK:", 14);
                                
                                uint32_t height = block->header.height;
                                memcpy(confirm_msg + 14, &height, 4);
                                
                                uint32_t hash_count = 0;
                                uint8_t* hash_ptr = confirm_msg + 22;
                                for (uint32_t ti = 1; ti <= user_tx_count; ti++) {
                                    if (block->transactions[ti]) {
                                        transaction_compute_hash(block->transactions[ti], hash_ptr);
                                        hash_ptr += TX_HASH_SIZE;
                                        hash_count++;
                                    }
                                }
                                memcpy(confirm_msg + 18, &hash_count, 4);
                                
                                size_t actual_size = 22 + (size_t)hash_count * TX_HASH_SIZE;
                                zmq_send(pub_socket, confirm_msg, actual_size, ZMQ_DONTWAIT);
                                free(confirm_msg);
                                
                                LOG_INFO("📤 PUB CONFIRM_BLOCK #%u (%u hashes)", height, hash_count);
                            }
                        }
                    } else {
                        LOG_WARN("❌ Failed to add block #%u", block->header.height);
                        zmq_send(socket, "FAIL", 4, 0);
                    }
                    block_destroy(block);
                } else {
                    LOG_WARN("❌ Invalid protobuf block data (%zu bytes)", block_len);
                    zmq_send(socket, "INVALID", 7, 0);
                }
                
            }
            // =============================================================
            // ADD_BLOCK - Legacy hex block transport (kept for compatibility)
            // =============================================================
            else if (starts_with(buffer, "ADD_BLOCK:")) {
                LOG_INFO("📥 ADD_BLOCK (%d bytes)", size);
                
                const char* block_data = buffer + 10;
                char farmer_name[64] = {0};
                
                char* hash_sep = strrchr(block_data, '#');
                if (hash_sep) {
                    safe_strcpy(farmer_name, hash_sep + 1, sizeof(farmer_name));
                    trim(farmer_name);
                    *hash_sep = '\0';
                }
                
                Block* block = block_deserialize(block_data);
                if (block) {
                    if (blockchain_add_block(blockchain, block)) {
                        LOG_INFO("✅ Block #%u added (height: %lu, %u TXs)", 
                                 block->header.height, blockchain->height,
                                 block->header.transaction_count);
                        zmq_send(socket, "OK", 2, 0);
                        
                        char block_hash_hex[65];
                        bytes_to_hex_buf(block->header.hash, 32, block_hash_hex);
                        
                        if (metronome_push && farmer_name[0] != '\0') {
                            char notify_msg[256];
                            snprintf(notify_msg, sizeof(notify_msg),
                                     "BLOCK_CONFIRMED:%s|%s", block_hash_hex, farmer_name);
                            zmq_send(metronome_push, notify_msg, strlen(notify_msg), ZMQ_DONTWAIT);
                        }
                        
                        if (pub_socket) {
                            char pub_msg[256];
                            snprintf(pub_msg, sizeof(pub_msg), "NEW_BLOCK:%u:%u:%s",
                                     block->header.height, block->header.transaction_count,
                                     block_hash_hex);
                            zmq_send(pub_socket, pub_msg, strlen(pub_msg), ZMQ_DONTWAIT);
                            
                            // CONFIRM_BLOCK for pool (same as PB handler)
                            uint32_t user_tx_count = block->header.transaction_count > 1 
                                ? block->header.transaction_count - 1 : 0;
                            if (user_tx_count > 0) {
                                size_t confirm_size = 22 + (size_t)user_tx_count * TX_HASH_SIZE;
                                uint8_t* confirm_msg = safe_malloc(confirm_size);
                                memcpy(confirm_msg, "CONFIRM_BLOCK:", 14);
                                uint32_t height = block->header.height;
                                memcpy(confirm_msg + 14, &height, 4);
                                uint32_t hash_count = 0;
                                uint8_t* hash_ptr = confirm_msg + 22;
                                for (uint32_t ti = 1; ti <= user_tx_count; ti++) {
                                    if (block->transactions[ti]) {
                                        transaction_compute_hash(block->transactions[ti], hash_ptr);
                                        hash_ptr += TX_HASH_SIZE;
                                        hash_count++;
                                    }
                                }
                                memcpy(confirm_msg + 18, &hash_count, 4);
                                size_t actual_size = 22 + (size_t)hash_count * TX_HASH_SIZE;
                                zmq_send(pub_socket, confirm_msg, actual_size, ZMQ_DONTWAIT);
                                free(confirm_msg);
                            }
                        }
                    } else {
                        LOG_WARN("❌ Failed to add block #%u", block->header.height);
                        zmq_send(socket, "FAIL", 4, 0);
                    }
                    block_destroy(block);
                } else {
                    LOG_WARN("❌ Invalid block data");
                    zmq_send(socket, "INVALID", 7, 0);
                }
                
            } else if (starts_with(buffer, "GET_LAST_HASH")) {
                // ═══════════════════════════════════════════════════════
                // v29.2: Return ONLY the 64-char hash (not full block!)
                // OLD: serialize entire block with 10K TXs → 2.3MB hex!
                // NEW: 64 bytes. Saves ~200ms per block creation round.
                // ═══════════════════════════════════════════════════════
                Block* last = blockchain_get_last_block(blockchain);
                if (last) {
                    char hash_hex[65];
                    bytes_to_hex_buf(last->header.hash, 32, hash_hex);
                    zmq_send(socket, hash_hex, 64, 0);
                } else {
                    zmq_send(socket, "NONE", 4, 0);
                }
                
            } else if (starts_with(buffer, "GET_LAST")) {
                // Legacy: returns full serialized block
                Block* last = blockchain_get_last_block(blockchain);
                if (last) {
                    char* hex = block_serialize(last);
                    zmq_send(socket, hex, strlen(hex), 0);
                    free(hex);
                } else {
                    zmq_send(socket, "NONE", 4, 0);
                }
                
            } else if (starts_with(buffer, "GET_HEIGHT")) {
                char resp[32];
                snprintf(resp, sizeof(resp), "%lu", blockchain_get_height(blockchain));
                zmq_send(socket, resp, strlen(resp), 0);
                
            } else if (starts_with(buffer, "FUND_WALLET:")) {
                // ═══════════════════════════════════════════════════════
                // FUND_WALLET: Pre-fund a wallet address directly
                // Format: "FUND_WALLET:<40-char-hex-address>:<amount>"
                // Used by benchmark to skip warmup mining.
                // Directly credits the ledger — like a genesis allocation.
                //
                // The benchmark derives the address using the wallet binary:
                //   addr=$($BUILD_DIR/wallet address sender1)
                //   send "FUND_WALLET:$addr:50000"
                // ═══════════════════════════════════════════════════════
                char addr_hex[42] = {0};
                uint64_t amount = 0;
                char* p1 = buffer + 12;  // after "FUND_WALLET:"
                char* colon = strchr(p1, ':');
                if (colon && (colon - p1) == 40) {
                    memcpy(addr_hex, p1, 40);
                    addr_hex[40] = '\0';
                    amount = strtoull(colon + 1, NULL, 10);
                }
                
                if (addr_hex[0] && amount > 0) {
                    uint8_t addr[20];
                    hex_to_bytes_buf(addr_hex, addr, 20);
                    
                    blockchain_credit_address(blockchain, addr, amount);
                    
                    LOG_INFO("💰 FUND_WALLET: %s credited %lu coins", addr_hex, amount);
                    
                    char resp[128];
                    snprintf(resp, sizeof(resp), "FUNDED:%lu", amount);
                    zmq_send(socket, resp, strlen(resp), 0);
                } else {
                    LOG_WARN("FUND_WALLET: bad format (need 40-char hex address)");
                    zmq_send(socket, "FAIL:PARSE", 10, 0);
                }
                
            } else if (starts_with(buffer, "GET_BALANCE:")) {
                uint8_t addr[20];
                const char* addr_str = buffer + 12;
                if (wallet_parse_address(addr_str, addr)) {
                    uint64_t balance = blockchain_get_balance(blockchain, addr);
                    char resp[32];
                    snprintf(resp, sizeof(resp), "%lu", balance);
                    zmq_send(socket, resp, strlen(resp), 0);
                } else {
                    zmq_send(socket, "INVALID", 7, 0);
                }
                
            // GET_BALANCES_BATCH - batch balance query (v45.3)
            // Request:  "GET_BALANCES_BATCH:" (19B) + count (4B) + N×20B addresses
            // Response: "BAL:" (4B) + count (4B) + N×8B uint64_t balances
            } else if (size >= 23 && starts_with(buffer, "GET_BALANCES_BATCH:")) {
                uint32_t addr_count = 0;
                memcpy(&addr_count, buffer + 19, 4);
                
                size_t expected = 23 + (size_t)addr_count * 20;
                if ((size_t)size >= expected && addr_count <= 1000) {
                    size_t resp_size = 8 + (size_t)addr_count * 8;
                    uint8_t* resp = safe_malloc(resp_size);
                    memcpy(resp, "BAL:", 4);
                    memcpy(resp + 4, &addr_count, 4);
                    
                    uint8_t* addr_ptr = (uint8_t*)(buffer + 23);
                    for (uint32_t i = 0; i < addr_count; i++) {
                        uint64_t bal = blockchain_get_balance(blockchain, addr_ptr + i * 20);
                        memcpy(resp + 8 + i * 8, &bal, 8);
                    }
                    
                    zmq_send(socket, resp, resp_size, 0);
                    free(resp);
                } else {
                    zmq_send(socket, "INVALID", 7, 0);
                }
                
            } else if (starts_with(buffer, "GET_NONCE:")) {
                uint8_t addr[20];
                const char* addr_str = buffer + 10;
                if (wallet_parse_address(addr_str, addr)) {
                    uint64_t nonce = blockchain_get_nonce(blockchain, addr);
                    char resp[32];
                    snprintf(resp, sizeof(resp), "%lu", nonce);
                    zmq_send(socket, resp, strlen(resp), 0);
                } else {
                    zmq_send(socket, "INVALID", 7, 0);
                }
                
            } else if (starts_with(buffer, "GET_SUMMARY")) {
                char resp[512];
                snprintf(resp, sizeof(resp), "HEIGHT:%lu|ACCOUNTS:%u|REQUESTS:%lu",
                         blockchain->height, blockchain->ledger_count, requests_handled);
                zmq_send(socket, resp, strlen(resp), 0);
                
            } else {
                LOG_WARN("❓ Unknown: %.20s...", buffer);
                zmq_send(socket, "UNKNOWN", 7, 0);
            }
        }
    }
    
    free(buffer);
    LOG_INFO("💾 Saving blockchain...");
    blockchain_save_pb(blockchain, "blockchain.dat");
    LOG_INFO("📊 Final: %lu blocks, %u accounts, %lu requests",
             blockchain->height, blockchain->ledger_count, requests_handled);
    
    blockchain_destroy(blockchain);
    if (metronome_push) zmq_close(metronome_push);
    if (pub_socket) zmq_close(pub_socket);
    zmq_close(socket);
    zmq_ctx_destroy(context);
    
    LOG_INFO("👋 Blockchain server stopped");
    return 0;
}
