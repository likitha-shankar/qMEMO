/**
 * ============================================================================
 * MAIN_WALLET.C - High-Performance Wallet CLI with OpenMP Batch Submission
 * ============================================================================
 * v29 - OpenMP parallel batch submission replacing pthreads
 * 
 * OPTIMIZATIONS:
 * ==============
 * 1. OpenMP parallel for: distributes batch iterations across threads
 * 2. Batch submission: Send multiple TXs in single ZMQ message
 * 3. BLS signatures: 48-byte signatures (like Ethereum 2.0)
 * 4. Configurable batch_size (default 64, power of 2)
 * 
 * PARALLEL ARCHITECTURE:
 * ======================
 * OpenMP parallel loop (num_threads):
 *   For batch_index = 0 to (transactions_count / batch_size):
 *     Create batch of batch_size transactions
 *     Serialize and send as SUBMIT_BATCH
 *
 * Each thread has its own ZMQ context/socket/wallet (thread-local resources)
 * 
 * ============================================================================
 */

#include "../include/wallet.h"
#include "../include/transaction.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zmq.h>
#include <omp.h>
#include <sys/time.h>
#include <time.h>

// Default network addresses
#define DEFAULT_BLOCKCHAIN_ADDR "tcp://localhost:5555"
#define DEFAULT_POOL_ADDR "tcp://localhost:5557"
#define DEFAULT_BLOCKCHAIN_PUB_ADDR "tcp://localhost:5559"

// Batch optimization configuration (v29)
#define DEFAULT_BATCH_SIZE 64       // TXs per batch message (power of 2)
#define DEFAULT_NUM_THREADS 8       // Number of parallel threads
#define MAX_BATCH_SIZE 256          // Maximum TXs in one message
#define MAX_THREADS 64

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

void print_usage(const char* prog) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   WALLET CLI v29 - OpenMP Batch Submission + BLS            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Usage: %s <command> [args]\n", prog);
    printf("\n");
    printf("Basic Commands:\n");
    printf("  create <n>                 Create new wallet with name\n");
    printf("  info <file>                   Show wallet info from file\n");
    printf("  balance <name|address>        Get balance from blockchain\n");
    printf("  nonce <name|address>          Get transaction nonce\n");
    printf("  send <from> <to> <amount>     Send single transaction\n");
    printf("  address <n>                Get address for name\n");
    printf("\n");
    printf("High-Performance Batch Submission (OpenMP):\n");
    printf("  batch_send <from> <to> <amount> <count> [options]\n");
    printf("      Send multiple transactions with OpenMP + batching\n");
    printf("      Options:\n");
    printf("        --threads N          Number of threads (default: %d)\n", DEFAULT_NUM_THREADS);
    printf("        --batch N            TXs per batch message (default: %d, power of 2)\n", DEFAULT_BATCH_SIZE);
    printf("        --nonce N            Starting nonce (default: auto-fetch)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s create alice\n", prog);
    printf("  %s balance farmer1\n", prog);
    printf("  %s send farmer1 receiver 1\n", prog);
    printf("  %s batch_send farmer1 receiver 1 1024 --threads 8 --batch 64\n", prog);
    printf("\n");
}

// =============================================================================
// BASIC WALLET COMMANDS
// =============================================================================

int cmd_create(const char* name) {
    Wallet* wallet = wallet_create_named(name);
    if (!wallet) {
        fprintf(stderr, "Failed to create wallet\n");
        return 1;
    }
    
    char filename[128];
    snprintf(filename, sizeof(filename), "%s.dat", name);
    
    if (!wallet_save(wallet, filename)) {
        fprintf(stderr, "Failed to save wallet\n");
        wallet_destroy(wallet);
        return 1;
    }
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    WALLET CREATED (BLS)                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Name:        %-46s║\n", wallet->name);
    printf("║  Address:     %-46s║\n", wallet->address_hex);
    printf("║  Nonce:       %-46lu║\n", wallet->nonce);
    printf("║  Signature:   %-46s║\n", "BLS (48 bytes)");
    printf("║  File:        %-46s║\n", filename);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    wallet_destroy(wallet);
    return 0;
}

int cmd_info(const char* filepath) {
    Wallet* wallet = wallet_load(filepath);
    if (!wallet) {
        fprintf(stderr, "Failed to load wallet from %s\n", filepath);
        return 1;
    }
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    WALLET INFO                               ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Name:        %-46s║\n", wallet->name);
    printf("║  Address:     %-46s║\n", wallet->address_hex);
    printf("║  Nonce:       %-46lu║\n", wallet->nonce);
    printf("║  Private Key: %-46s║\n", 
           strlen(wallet->private_key_pem) > 0 ? "Present (can sign TXs)" : "MISSING (read-only)");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    wallet_destroy(wallet);
    return 0;
}

int cmd_balance(const char* name_or_addr, const char* blockchain_addr) {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_REQ);
    
    if (zmq_connect(socket, blockchain_addr) != 0) {
        fprintf(stderr, "Failed to connect to blockchain\n");
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return 1;
    }
    
    char request[256];
    snprintf(request, sizeof(request), "GET_BALANCE:%s", name_or_addr);
    
    zmq_send(socket, request, strlen(request), 0);
    
    char response[256];
    int size = zmq_recv(socket, response, sizeof(response) - 1, 0);
    
    if (size > 0) {
        response[size] = '\0';
        uint64_t balance = strtoull(response, NULL, 10);
        
        printf("\n");
        printf("💰 Balance for %s: %lu coins\n", name_or_addr, balance);
        printf("\n");
    } else {
        fprintf(stderr, "Failed to get balance\n");
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 0;
}

int cmd_nonce(const char* name_or_addr, const char* blockchain_addr) {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_REQ);
    
    if (zmq_connect(socket, blockchain_addr) != 0) {
        fprintf(stderr, "Failed to connect to blockchain\n");
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return 1;
    }
    
    char request[256];
    snprintf(request, sizeof(request), "GET_NONCE:%s", name_or_addr);
    
    zmq_send(socket, request, strlen(request), 0);
    
    char response[256];
    int size = zmq_recv(socket, response, sizeof(response) - 1, 0);
    
    if (size > 0) {
        response[size] = '\0';
        uint64_t nonce = strtoull(response, NULL, 10);
        
        printf("\n");
        printf("🔢 Nonce for %s: %lu\n", name_or_addr, nonce);
        printf("\n");
    } else {
        fprintf(stderr, "Failed to get nonce\n");
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 0;
}

int cmd_address(const char* name) {
    uint8_t addr[20];
    wallet_name_to_address(name, addr);
    
    char hex[41];
    address_to_hex(addr, hex);
    
    printf("\n");
    printf("📍 Address for '%s': %s\n", name, hex);
    printf("\n");
    
    return 0;
}

// =============================================================================
// SINGLE TRANSACTION SEND
// =============================================================================

int cmd_send(const char* from_name, const char* to_name, uint64_t amount,
             int64_t manual_nonce, uint32_t expiry_blocks,
             const char* blockchain_addr, const char* pool_addr) {
    
    char filename[128];
    snprintf(filename, sizeof(filename), "%s.dat", from_name);
    
    Wallet* from_wallet = wallet_load(filename);
    if (!from_wallet) {
        from_wallet = wallet_create_named(from_name);
        if (!from_wallet) {
            fprintf(stderr, "Failed to load/create wallet for %s\n", from_name);
            return 1;
        }
        wallet_save(from_wallet, filename);
    }
    
    uint8_t to_addr[20];
    wallet_parse_address(to_name, to_addr);
    
    void* context = zmq_ctx_new();
    void* bc_socket = zmq_socket(context, ZMQ_REQ);
    zmq_connect(bc_socket, blockchain_addr);
    
    char request[256];
    char response[256];
    int size;
    
    uint64_t nonce = 0;
    if (manual_nonce >= 0) {
        nonce = (uint64_t)manual_nonce;
    } else {
        uint64_t bc_nonce = 0;
        snprintf(request, sizeof(request), "GET_NONCE:%s", from_name);
        zmq_send(bc_socket, request, strlen(request), 0);
        size = zmq_recv(bc_socket, response, sizeof(response) - 1, 0);
        if (size > 0) {
            response[size] = '\0';
            bc_nonce = strtoull(response, NULL, 10);
        }
        
        void* pool_socket_nonce = zmq_socket(context, ZMQ_REQ);
        zmq_connect(pool_socket_nonce, pool_addr);
        
        uint64_t pool_nonce = 0;
        snprintf(request, sizeof(request), "GET_PENDING_NONCE:%s", from_wallet->address_hex);
        zmq_send(pool_socket_nonce, request, strlen(request), 0);
        size = zmq_recv(pool_socket_nonce, response, sizeof(response) - 1, 0);
        if (size > 0) {
            response[size] = '\0';
            pool_nonce = strtoull(response, NULL, 10);
        }
        zmq_close(pool_socket_nonce);
        
        nonce = (bc_nonce > pool_nonce) ? bc_nonce : pool_nonce;
    }
    
    zmq_send(bc_socket, "GET_HEIGHT", 10, 0);
    size = zmq_recv(bc_socket, response, sizeof(response) - 1, 0);
    uint32_t current_block = 0;
    if (size > 0) {
        response[size] = '\0';
        current_block = atoi(response);
    }
    zmq_close(bc_socket);
    
    uint32_t expiry_block = 0;
    if (expiry_blocks > 0) {
        expiry_block = current_block + expiry_blocks;
    }
    
    uint32_t fee = 1;
    Transaction* tx = transaction_create(from_wallet, to_addr, amount, fee, nonce, expiry_block);
    if (!tx) {
        fprintf(stderr, "Failed to create transaction\n");
        wallet_destroy(from_wallet);
        zmq_ctx_destroy(context);
        return 1;
    }
    
    void* pool_socket = zmq_socket(context, ZMQ_REQ);
    zmq_connect(pool_socket, pool_addr);
    
    // Clear response buffer to prevent stale data from earlier blockchain calls
    memset(response, 0, sizeof(response));
    
    // Submit via protobuf (includes Ed25519 pubkey for verification)
    Blockchain__TransactionBatch batch = BLOCKCHAIN__TRANSACTION_BATCH__INIT;
    Blockchain__Transaction pb_tx = BLOCKCHAIN__TRANSACTION__INIT;
    Blockchain__Transaction* pb_ptrs[1] = { &pb_tx };
    
    pb_tx.nonce = tx->nonce;
    pb_tx.expiry_block = tx->expiry_block;
    pb_tx.source_address.data = tx->source_address;
    pb_tx.source_address.len = 20;
    pb_tx.dest_address.data = tx->dest_address;
    pb_tx.dest_address.len = 20;
    pb_tx.value = tx->value;
    pb_tx.fee = tx->fee;
    if (!is_zero(tx->signature, 64)) {
        pb_tx.signature.data = tx->signature;
        pb_tx.signature.len = 64;
    }
    if (!is_zero(from_wallet->ed25519_pubkey, 32)) {
        pb_tx.public_key.data = from_wallet->ed25519_pubkey;
        pb_tx.public_key.len = 32;
    }
    
    batch.n_transactions = 1;
    batch.transactions = pb_ptrs;
    batch.count = 1;
    
    size_t pb_size = blockchain__transaction_batch__get_packed_size(&batch);
    size_t msg_size = 16 + pb_size;
    uint8_t* batch_msg = safe_malloc(msg_size);
    memcpy(batch_msg, "SUBMIT_BATCH_PB:", 16);
    blockchain__transaction_batch__pack(&batch, batch_msg + 16);
    
    zmq_send(pool_socket, batch_msg, msg_size, 0);
    
    size = zmq_recv(pool_socket, response, sizeof(response) - 1, 0);
    free(batch_msg);
    
    bool success = false;
    if (size > 0) {
        response[size] = '\0';
        success = (strncmp(response, "OK:", 3) == 0);
    }
    
    if (success) {
        uint8_t tx_hash[TX_HASH_SIZE];
        transaction_compute_hash(tx, tx_hash);
        char hash_hex[TX_HASH_SIZE * 2 + 1];
        txhash_to_hex(tx_hash, hash_hex);
        
        printf("\n");
        printf("╔══════════════════════════════════════════════════════════════╗\n");
        printf("║                 TRANSACTION SUBMITTED                        ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  From:   %-50s║\n", from_name);
        printf("║  To:     %-50s║\n", to_name);
        printf("║  Amount: %-50lu║\n", amount);
        printf("║  Nonce:  %-50lu║\n", nonce);
        printf("║  Hash:   %.50s...║\n", hash_hex);
        printf("╚══════════════════════════════════════════════════════════════╝\n");
    } else {
        fprintf(stderr, "FAILED: %s\n", response);
    }
    
    transaction_destroy(tx);
    wallet_destroy(from_wallet);
    zmq_close(pool_socket);
    zmq_ctx_destroy(context);
    
    return success ? 0 : 1;
}

// =============================================================================
// OPENMP BATCH SEND COMMAND (v29)
// =============================================================================
// 
// Architecture:
//   Pre-allocate per-thread resources (ZMQ contexts, sockets, wallets)
//   OpenMP parallel for distributes batch iterations across threads:
//     For batch = 0 to num_batches:
//       Create batch_size transactions with correct nonces
//       Serialize into SUBMIT_BATCH message
//       Send to pool and collect response
//   Aggregate results with reduction
//
// =============================================================================

int cmd_batch_send(const char* from_name, const char* to_name, 
                   uint64_t amount, int total_count,
                   int num_threads, int batch_size,
                   int64_t manual_nonce,
                   const char* blockchain_addr, const char* pool_addr) {
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║        OPENMP BATCH SUBMISSION (v29)                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  From:        %-46s║\n", from_name);
    printf("║  To:          %-46s║\n", to_name);
    printf("║  Amount:      %-46lu║\n", amount);
    printf("║  Count:       %-46d║\n", total_count);
    printf("║  Threads:     %-46d║\n", num_threads);
    printf("║  Batch size:  %-46d║\n", batch_size);
    printf("║  Signature:   %-46s║\n", "BLS (48 bytes)");
    printf("║  Parallelism: %-46s║\n", "OpenMP");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    uint8_t to_addr[20];
    wallet_parse_address(to_name, to_addr);
    
    void* context = zmq_ctx_new();
    void* bc_socket = zmq_socket(context, ZMQ_REQ);
    zmq_connect(bc_socket, blockchain_addr);
    
    char request[256];
    char response[256];
    int size;
    
    // Get base nonce
    uint64_t base_nonce = 0;
    if (manual_nonce >= 0) {
        base_nonce = (uint64_t)manual_nonce;
    } else {
        snprintf(request, sizeof(request), "GET_NONCE:%s", from_name);
        zmq_send(bc_socket, request, strlen(request), 0);
        size = zmq_recv(bc_socket, response, sizeof(response) - 1, 0);
        if (size > 0) {
            response[size] = '\0';
            base_nonce = strtoull(response, NULL, 10);
        }
        
        void* pool_socket = zmq_socket(context, ZMQ_REQ);
        zmq_connect(pool_socket, pool_addr);
        
        Wallet* temp_wallet = wallet_create_named(from_name);
        if (temp_wallet) {
            snprintf(request, sizeof(request), "GET_PENDING_NONCE:%s", temp_wallet->address_hex);
            zmq_send(pool_socket, request, strlen(request), 0);
            size = zmq_recv(pool_socket, response, sizeof(response) - 1, 0);
            if (size > 0) {
                response[size] = '\0';
                uint64_t pool_nonce = strtoull(response, NULL, 10);
                if (pool_nonce > base_nonce) base_nonce = pool_nonce;
            }
            wallet_destroy(temp_wallet);
        }
        zmq_close(pool_socket);
    }
    
    printf("  Starting nonce: %lu\n", base_nonce);
    
    // Get current height
    zmq_send(bc_socket, "GET_HEIGHT", 10, 0);
    size = zmq_recv(bc_socket, response, sizeof(response) - 1, 0);
    uint32_t current_height = 0;
    if (size > 0) {
        response[size] = '\0';
        current_height = atoi(response);
    }
    zmq_close(bc_socket);
    zmq_ctx_destroy(context);
    
    uint32_t expiry_blocks = 1000;
    uint32_t fee = 1;
    
    // Calculate number of batch iterations
    int num_batches = (total_count + batch_size - 1) / batch_size;
    
    printf("  Current height: %u\n", current_height);
    printf("  Batch iterations: %d (ceil(%d / %d))\n", num_batches, total_count, batch_size);
    printf("\n  Submitting with %d OpenMP threads, batch size %d...\n", num_threads, batch_size);
    
    // =========================================================================
    // Pre-allocate per-thread resources
    // Each thread needs its own ZMQ context, socket, and wallet
    // (ZMQ sockets are NOT thread-safe)
    // =========================================================================
    void** thread_contexts = safe_malloc(num_threads * sizeof(void*));
    void** thread_sockets  = safe_malloc(num_threads * sizeof(void*));
    Wallet** thread_wallets = safe_malloc(num_threads * sizeof(Wallet*));
    
    for (int t = 0; t < num_threads; t++) {
        thread_contexts[t] = zmq_ctx_new();
        thread_sockets[t] = zmq_socket(thread_contexts[t], ZMQ_REQ);
        zmq_connect(thread_sockets[t], pool_addr);
        
        int timeout = 10000;
        zmq_setsockopt(thread_sockets[t], ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        
        thread_wallets[t] = wallet_create_named(from_name);
    }
    
    // Per-thread stats
    int* thread_submitted = safe_malloc(num_threads * sizeof(int));
    int* thread_errors    = safe_malloc(num_threads * sizeof(int));
    double* thread_elapsed = safe_malloc(num_threads * sizeof(double));
    memset(thread_submitted, 0, num_threads * sizeof(int));
    memset(thread_errors, 0, num_threads * sizeof(int));
    memset(thread_elapsed, 0, num_threads * sizeof(double));
    
    double start_time = get_time_ms();
    
    // =========================================================================
    // OpenMP parallel loop: distribute batch iterations across threads
    //
    // For batch_index = 0 to num_batches:
    //   Each iteration processes batch_size transactions (or fewer for last batch)
    //   Thread gets its own ZMQ socket and wallet via omp_get_thread_num()
    // =========================================================================
    #pragma omp parallel for num_threads(num_threads) schedule(dynamic)
    for (int b = 0; b < num_batches; b++) {
        int tid = omp_get_thread_num();
        double batch_start = get_time_ms();
        
        void* sock = thread_sockets[tid];
        Wallet* wallet = thread_wallets[tid];
        
        // Calculate transaction range for this batch
        int tx_start = b * batch_size;
        int tx_end   = tx_start + batch_size;
        if (tx_end > total_count) tx_end = total_count;
        int batch_count = tx_end - tx_start;
        
        // Build protobuf TransactionBatch
        // v45.2: ~2x smaller than hex, schema-safe, no string encoding overhead
        Transaction** raw_txs = safe_malloc(batch_count * sizeof(Transaction*));
        Blockchain__Transaction* pb_arr = safe_malloc(batch_count * sizeof(Blockchain__Transaction));
        Blockchain__Transaction** pb_ptrs = safe_malloc(batch_count * sizeof(Blockchain__Transaction*));
        int txs_in_batch = 0;
        
        for (int i = tx_start; i < tx_end; i++) {
            uint64_t nonce = base_nonce + i;
            uint32_t expiry = expiry_blocks > 0 ? current_height + expiry_blocks : 0;
            
            Transaction* tx = transaction_create(wallet, to_addr, 
                                                  amount, fee, nonce, expiry);
            if (tx) {
                raw_txs[txs_in_batch] = tx;
                
                // Zero-copy: point protobuf fields directly into Transaction struct
                Blockchain__Transaction* pt = &pb_arr[txs_in_batch];
                blockchain__transaction__init(pt);
                pt->nonce = tx->nonce;
                pt->expiry_block = tx->expiry_block;
                pt->source_address.data = tx->source_address;
                pt->source_address.len = 20;
                pt->dest_address.data = tx->dest_address;
                pt->dest_address.len = 20;
                pt->value = tx->value;
                pt->fee = tx->fee;
                if (!is_zero(tx->signature, 64)) {
                    pt->signature.data = tx->signature;
                    pt->signature.len = 64;
                }
                // Include Ed25519 pubkey for pool storage and validator verification
                if (!is_zero(wallet->ed25519_pubkey, 32)) {
                    pt->public_key.data = wallet->ed25519_pubkey;
                    pt->public_key.len = 32;
                }
                pb_ptrs[txs_in_batch] = pt;
                txs_in_batch++;
            }
        }
        
        // Pack and send protobuf batch
        if (txs_in_batch > 0) {
            Blockchain__TransactionBatch batch = BLOCKCHAIN__TRANSACTION_BATCH__INIT;
            batch.n_transactions = txs_in_batch;
            batch.transactions = pb_ptrs;
            batch.count = txs_in_batch;
            
            size_t pb_size = blockchain__transaction_batch__get_packed_size(&batch);
            size_t msg_size = 16 + pb_size;  // "SUBMIT_BATCH_PB:" = 16 bytes
            uint8_t* batch_msg = safe_malloc(msg_size);
            memcpy(batch_msg, "SUBMIT_BATCH_PB:", 16);
            blockchain__transaction_batch__pack(&batch, batch_msg + 16);
            
            zmq_send(sock, batch_msg, msg_size, 0);
            free(batch_msg);
            
            char resp[256];
            int sz = zmq_recv(sock, resp, sizeof(resp) - 1, 0);
            
            if (sz > 0) {
                resp[sz] = '\0';
                if (strncmp(resp, "OK:", 3) == 0) {
                    int accepted = 0, rejected = 0;
                    sscanf(resp + 3, "%d|%d", &accepted, &rejected);
                    thread_submitted[tid] += accepted;
                    thread_errors[tid] += rejected;
                } else if (strcmp(resp, "OK") == 0) {
                    thread_submitted[tid] += batch_count;
                } else {
                    thread_errors[tid] += batch_count;
                }
            } else {
                thread_errors[tid] += batch_count;
            }
        }
        
        // Cleanup (AFTER pack+send - zero-copy pointers must be valid during pack)
        for (int i = 0; i < txs_in_batch; i++) {
            transaction_destroy(raw_txs[i]);
        }
        free(raw_txs);
        free(pb_arr);
        free(pb_ptrs);
        thread_elapsed[tid] += get_time_ms() - batch_start;
        
        // Progress logging for Graph 1 (cumulative TXs over time)
        // Log every 4 batches to avoid excessive output while maintaining granularity
        if (b % 4 == 0 || b == num_batches - 1) {
            int total_so_far = 0;
            for (int t = 0; t < num_threads; t++) total_so_far += thread_submitted[t];
            fprintf(stderr, "SUBMIT_PROGRESS:%.3f:%d\n", 
                    get_time_ms() - start_time, total_so_far);
        }
    }
    
    double total_time = get_time_ms() - start_time;
    
    // Aggregate results
    int total_submitted = 0;
    int total_errors = 0;
    for (int t = 0; t < num_threads; t++) {
        total_submitted += thread_submitted[t];
        total_errors += thread_errors[t];
    }
    
    // Print per-thread stats
    printf("\n  Per-thread breakdown:\n");
    for (int t = 0; t < num_threads; t++) {
        if (thread_submitted[t] > 0 || thread_errors[t] > 0) {
            printf("    Thread %d: %d submitted, %d errors (%.1f ms)\n",
                   t, thread_submitted[t], thread_errors[t], thread_elapsed[t]);
        }
    }
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    SUBMISSION RESULTS                        ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Submitted:     %-44d║\n", total_submitted);
    printf("║  Errors:        %-44d║\n", total_errors);
    printf("║  Time:          %-42.2f ms║\n", total_time);
    printf("║  Throughput:    %-40.2f tx/sec║\n", 
           total_time > 0 ? (total_submitted * 1000.0 / total_time) : 0);
    printf("║  Parallelism:   OpenMP (%d threads, batch=%d)             ║\n",
           num_threads, batch_size);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    printf("\nBATCH_RESULT:%d\n", total_submitted);
    
    // Cleanup per-thread resources
    for (int t = 0; t < num_threads; t++) {
        if (thread_wallets[t]) wallet_destroy(thread_wallets[t]);
        if (thread_sockets[t]) zmq_close(thread_sockets[t]);
        if (thread_contexts[t]) zmq_ctx_destroy(thread_contexts[t]);
    }
    free(thread_contexts);
    free(thread_sockets);
    free(thread_wallets);
    free(thread_submitted);
    free(thread_errors);
    free(thread_elapsed);
    
    return total_errors > 0 ? 1 : 0;
}

// =============================================================================
// WAIT_CONFIRM - Subscribe to PUB for instant block notifications (v29.2)
// =============================================================================
// Subscribes to NEW_BLOCK events, checks balance after each block.
// Outputs to stdout: BLOCK_TIME:<height>:<tx_count>:<confirmed>:<interval_ms>:<elapsed_s>
// Final line: CONFIRMED:<count>:<height>:<elapsed_ms>

int cmd_wait_confirm(const char* receiver_name, int expected_count, uint64_t initial_balance,
                     int timeout_sec, const char* pub_addr, const char* blockchain_addr) {
    
    void* context = zmq_ctx_new();
    
    // SUB socket for NEW_BLOCK notifications
    void* sub_socket = zmq_socket(context, ZMQ_SUB);
    zmq_setsockopt(sub_socket, ZMQ_SUBSCRIBE, "NEW_BLOCK", 9);
    int linger = 0;
    zmq_setsockopt(sub_socket, ZMQ_LINGER, &linger, sizeof(linger));
    if (zmq_connect(sub_socket, pub_addr) != 0) {
        fprintf(stderr, "Failed to connect SUB to %s\n", pub_addr);
        zmq_close(sub_socket); zmq_ctx_destroy(context);
        return 1;
    }
    
    // REQ socket for balance queries
    void* req_socket = zmq_socket(context, ZMQ_REQ);
    zmq_setsockopt(req_socket, ZMQ_LINGER, &linger, sizeof(linger));
    int req_timeout = 5000;
    zmq_setsockopt(req_socket, ZMQ_RCVTIMEO, &req_timeout, sizeof(req_timeout));
    if (zmq_connect(req_socket, blockchain_addr) != 0) {
        fprintf(stderr, "Failed to connect REQ to %s\n", blockchain_addr);
        zmq_close(sub_socket); zmq_close(req_socket); zmq_ctx_destroy(context);
        return 1;
    }
    
    // Wait for SUB subscription to propagate
    struct timespec ts = {0, 100000000}; // 100ms
    nanosleep(&ts, NULL);
    
    int sub_timeout = 1000; // 1s poll
    zmq_setsockopt(sub_socket, ZMQ_RCVTIMEO, &sub_timeout, sizeof(sub_timeout));
    
    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    double start_ms = start_tv.tv_sec * 1000.0 + start_tv.tv_usec / 1000.0;
    
    int confirmed = 0;
    uint32_t last_height = 0;
    int blocks_received = 0;
    int stall_count = 0, last_confirmed = 0;
    double prev_block_ms = start_ms;
    
    char buffer[512], request[256], response[256];
    
    fprintf(stderr, "  📡 Subscribed to %s\n", pub_addr);
    fprintf(stderr, "  📊 Target: %d confirmed TXs for %s\n", expected_count, receiver_name);
    
    while (1) {
        struct timeval now_tv;
        gettimeofday(&now_tv, NULL);
        double now_ms = now_tv.tv_sec * 1000.0 + now_tv.tv_usec / 1000.0;
        double elapsed_sec = (now_ms - start_ms) / 1000.0;
        
        if (elapsed_sec >= timeout_sec) {
            fprintf(stderr, "\n  ⏰ Timeout after %ds\n", timeout_sec);
            break;
        }
        
        int size = zmq_recv(sub_socket, buffer, sizeof(buffer) - 1, 0);
        
        // Capture time AFTER recv returns — this is when the message
        // actually arrived, not when we started waiting for it.
        // Without this, GET_BALANCE delays from the previous iteration
        // inflate/deflate interval measurements (causing 1187ms + 28ms pairs).
        struct timeval recv_tv;
        gettimeofday(&recv_tv, NULL);
        double recv_ms = recv_tv.tv_sec * 1000.0 + recv_tv.tv_usec / 1000.0;
        double recv_elapsed_sec = (recv_ms - start_ms) / 1000.0;
        
        if (size > 0) {
            buffer[size] = '\0';
            
            if (strncmp(buffer, "NEW_BLOCK:", 10) == 0) {
                uint32_t height = 0, tx_count = 0;
                sscanf(buffer + 10, "%u:%u:", &height, &tx_count);
                
                double block_interval_ms = recv_ms - prev_block_ms;
                prev_block_ms = recv_ms;
                blocks_received++;
                last_height = height;
                
                // Query balance
                snprintf(request, sizeof(request), "GET_BALANCE:%s", receiver_name);
                zmq_send(req_socket, request, strlen(request), 0);
                int resp_size = zmq_recv(req_socket, response, sizeof(response) - 1, 0);
                if (resp_size > 0) {
                    response[resp_size] = '\0';
                    confirmed = (int)(strtoull(response, NULL, 10) - initial_balance);
                }
                
                // Per-block data line (stdout, parsed by benchmark.sh)
                printf("BLOCK_TIME:%u:%u:%d:%.0f:%.1f\n", height, tx_count, confirmed,
                       block_interval_ms, recv_elapsed_sec);
                fflush(stdout);
                
                fprintf(stderr, "\r  Block #%u (%u TXs) | Confirmed: %d/%d | Interval: %.0fms | %.1fs",
                        height, tx_count, confirmed, expected_count, block_interval_ms, recv_elapsed_sec);
                
                if (confirmed >= expected_count) {
                    fprintf(stderr, "\n  ✅ All %d transactions confirmed!\n", confirmed);
                    break;
                }
                
                if (confirmed == last_confirmed) {
                    stall_count++;
                    if (stall_count >= 10) {
                        fprintf(stderr, "\n  ⚠️  Stalled at %d (%d blocks with no new confirmations)\n", confirmed, stall_count);
                        break;
                    }
                } else {
                    stall_count = 0;
                    last_confirmed = confirmed;
                }
            }
        }
    }
    
    struct timeval end_tv;
    gettimeofday(&end_tv, NULL);
    double elapsed_ms = (end_tv.tv_sec * 1000.0 + end_tv.tv_usec / 1000.0) - start_ms;
    
    fprintf(stderr, "  📊 Blocks received: %d, Elapsed: %.1f ms\n", blocks_received, elapsed_ms);
    
    // Final output for script parsing
    printf("CONFIRMED:%d:%u:%.0f\n", confirmed, last_height, elapsed_ms);
    fflush(stdout);
    
    zmq_close(sub_socket);
    zmq_close(req_socket);
    zmq_ctx_destroy(context);
    
    return (confirmed >= expected_count) ? 0 : 1;
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* blockchain_addr = DEFAULT_BLOCKCHAIN_ADDR;
    const char* pool_addr = DEFAULT_POOL_ADDR;
    
    char* env_bc = getenv("BLOCKCHAIN_ADDR");
    char* env_pool = getenv("POOL_ADDR");
    if (env_bc) blockchain_addr = env_bc;
    if (env_pool) pool_addr = env_pool;
    
    const char* cmd = argv[1];
    
    if (strcmp(cmd, "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s create <n>\n", argv[0]);
            return 1;
        }
        return cmd_create(argv[2]);
        
    } else if (strcmp(cmd, "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s info <file>\n", argv[0]);
            return 1;
        }
        return cmd_info(argv[2]);
        
    } else if (strcmp(cmd, "balance") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s balance <name|address>\n", argv[0]);
            return 1;
        }
        return cmd_balance(argv[2], blockchain_addr);
        
    } else if (strcmp(cmd, "nonce") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s nonce <name|address>\n", argv[0]);
            return 1;
        }
        return cmd_nonce(argv[2], blockchain_addr);
        
    } else if (strcmp(cmd, "address") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s address <n>\n", argv[0]);
            return 1;
        }
        return cmd_address(argv[2]);
        
    } else if (strcmp(cmd, "send") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s send <from> <to> <amount> [nonce]\n", argv[0]);
            return 1;
        }
        int64_t nonce = -1;
        if (argc >= 6) nonce = atoll(argv[5]);
        return cmd_send(argv[2], argv[3], strtoull(argv[4], NULL, 10), 
                       nonce, 100, blockchain_addr, pool_addr);
        
    } else if (strcmp(cmd, "batch_send") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Usage: %s batch_send <from> <to> <amount> <count> [options]\n", argv[0]);
            fprintf(stderr, "  Options: --threads N --batch N --nonce N\n");
            return 1;
        }
        
        const char* from = argv[2];
        const char* to = argv[3];
        uint64_t amt = strtoull(argv[4], NULL, 10);
        int count = atoi(argv[5]);
        
        int threads = DEFAULT_NUM_THREADS;
        int batch = DEFAULT_BATCH_SIZE;
        int64_t nonce = -1;
        
        for (int i = 6; i < argc; i++) {
            if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
                threads = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
                batch = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--nonce") == 0 && i + 1 < argc) {
                nonce = atoll(argv[++i]);
            }
        }
        
        if (batch > MAX_BATCH_SIZE) batch = MAX_BATCH_SIZE;
        if (batch < 1) batch = 1;
        if (threads < 1) threads = 1;
        if (threads > MAX_THREADS) threads = MAX_THREADS;
        
        return cmd_batch_send(from, to, amt, count, threads, batch, 
                             nonce, blockchain_addr, pool_addr);
        
    } else if (strcmp(cmd, "wait_confirm") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s wait_confirm <receiver> <expected_count> <initial_balance> [options]\n", argv[0]);
            return 1;
        }
        const char* receiver = argv[2];
        int expected = atoi(argv[3]);
        uint64_t initial = strtoull(argv[4], NULL, 10);
        int timeout = 120;
        const char* pub_addr = DEFAULT_BLOCKCHAIN_PUB_ADDR;
        char* env_pub = getenv("BLOCKCHAIN_PUB_ADDR");
        if (env_pub) pub_addr = env_pub;
        for (int i = 5; i < argc; i++) {
            if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) timeout = atoi(argv[++i]);
            else if (strcmp(argv[i], "--pub-addr") == 0 && i + 1 < argc) pub_addr = argv[++i];
        }
        return cmd_wait_confirm(receiver, expected, initial, timeout, pub_addr, blockchain_addr);
        
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
