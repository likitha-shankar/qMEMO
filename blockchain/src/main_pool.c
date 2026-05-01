/**
 * ============================================================================
 * MAIN_POOL.C - Transaction Pool Server (v45.2 - Protobuf + PUB/SUB Confirm)
 * ============================================================================
 *
 * v45.2 ARCHITECTURE:
 * 
 *  ┌─────────────────────────────────────────────────────────────────────────┐
 *  │                    TRANSACTION LIFECYCLE                                 │
 *  │                                                                          │
 *  │  1. SUBMISSION (Wallet → Pool via REQ/REP)                              │
 *  │     └─ Wallet sends SUBMIT_BATCH_PB: protobuf TransactionBatch         │
 *  │     └─ Pool deserializes, validates, adds to pending queue              │
 *  │                                                                          │
 *  │  2. BLOCK CREATION (Validator fetches from Pool via REQ/REP)            │
 *  │     └─ Validator sends "GET_FOR_WINNER:max:height"                      │
 *  │     └─ Pool returns protobuf TransactionBatch ("TXPB:" prefix)         │
 *  │                                                                          │
 *  │  3. CONFIRMATION (Blockchain → Pool via PUB/SUB)                        │
 *  │     └─ Blockchain validates block, publishes CONFIRM_BLOCK on PUB      │
 *  │     └─ Pool subscribes, receives TX hashes, removes from pending       │
 *  │     └─ DECOUPLED from block creation (async, not in critical path!)    │
 *  │                                                                          │
 *  │  WHY BLOCKCHAIN CONFIRMS (not validator):                               │
 *  │     The blockchain is the SINGLE SOURCE OF TRUTH. In a distributed     │
 *  │     system, validators could lie or crash. Only the blockchain knows    │
 *  │     which blocks (and therefore which TXs) are truly confirmed.        │
 *  │     Wallets also subscribe to the same PUB channel.                    │
 *  └─────────────────────────────────────────────────────────────────────────┘
 *
 * SERIALIZATION: Google Protocol Buffers (protobuf) for all data paths.
 * TRANSPORT:     ZeroMQ (REP for requests, SUB for blockchain notifications)
 *
 * ============================================================================
 */

#include "../include/transaction_pool.h"
#include "../include/transaction.h"
#include "../include/crypto_backend.h"
#include "../include/common.h"
#include "../proto/blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <zmq.h>

static volatile bool running = true;
static TransactionPool* pool = NULL;
static uint64_t total_submitted = 0;
static uint64_t total_confirmed = 0;
static uint64_t total_rejected = 0;

void signal_handler(int sig) {
    (void)sig;
    running = false;
    LOG_INFO("🛑 Shutdown signal received");
}

/**
 * Helper: Convert protobuf Transaction to native Transaction struct.
 * Allocates a new Transaction. Caller must free with transaction_destroy().
 */
static Transaction* pb_to_transaction(const Blockchain__Transaction* pt) {
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

    return tx;
}

int main(int argc, char* argv[]) {
    const char* bind_addr = "tcp://*:5557";
    const char* blockchain_pub_addr = NULL;  // SUB socket for confirmations
    const char* pool_pub_addr = NULL;        // PUB socket for wallet notifications
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sub") == 0 && i + 1 < argc) {
            blockchain_pub_addr = argv[++i];
        } else if (strcmp(argv[i], "--pub") == 0 && i + 1 < argc) {
            pool_pub_addr = argv[++i];
        } else if (argv[i][0] != '-') {
            bind_addr = argv[i];
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("🏊 ════════════════════════════════════════════════════════════");
    LOG_INFO("🏊 TRANSACTION POOL (v45.2 - Protobuf + PUB/SUB Confirm)");
    LOG_INFO("🏊 ════════════════════════════════════════════════════════════");
    LOG_INFO("   Data format:  Google Protocol Buffers (protobuf)");
    LOG_INFO("   Confirm via:  Blockchain PUB/SUB (async, not validator)");
    LOG_INFO("   REP commands:");
    LOG_INFO("     SUBMIT_BATCH_PB:<pb>  - Submit batch (protobuf)");
    LOG_INFO("     SUBMIT_BATCH:<hex>    - Submit batch (hex, legacy)");
    LOG_INFO("     GET_FOR_WINNER:n:h    - Get pending txs (protobuf response)");
    LOG_INFO("     GET_PENDING_NONCE:a   - Get next nonce for address");
    LOG_INFO("     GET_STATUS            - Get pool statistics");
    LOG_INFO("   SUB topics:");
    LOG_INFO("     CONFIRM_BLOCK:        - Blockchain-confirmed TX hashes");
    LOG_INFO("🏊 ════════════════════════════════════════════════════════════");
    
    // Create the transaction pool
    pool = pool_create();
    if (!pool) {
        LOG_ERROR("❌ Failed to create transaction pool");
        return 1;
    }
    LOG_INFO("✅ Transaction pool initialized (capacity: %d, max: %d)", 
             pool->capacity, MAX_POOL_SIZE);
    
    // ═══════════════════════════════════════════════════════════════════
    // ZMQ SOCKET SETUP
    // ═══════════════════════════════════════════════════════════════════
    void* context = zmq_ctx_new();
    
    // REP socket: handles requests from wallets and validators
    void* rep_socket = zmq_socket(context, ZMQ_REP);
    if (zmq_bind(rep_socket, bind_addr) != 0) {
        LOG_ERROR("❌ Failed to bind REP to %s", bind_addr);
        pool_destroy(pool);
        zmq_close(rep_socket);
        zmq_ctx_destroy(context);
        return 1;
    }
    LOG_INFO("🔌 REP listening on %s", bind_addr);
    
    // SUB socket: receives confirmations from blockchain PUB
    // The blockchain is the SINGLE SOURCE OF TRUTH for confirmations.
    // After blockchain_add_block() succeeds, it publishes TX hashes on PUB.
    // Pool subscribes and removes confirmed TXs from pending queue.
    void* sub_socket = NULL;
    if (blockchain_pub_addr) {
        sub_socket = zmq_socket(context, ZMQ_SUB);
        int linger = 0;
        zmq_setsockopt(sub_socket, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(sub_socket, blockchain_pub_addr);
        zmq_setsockopt(sub_socket, ZMQ_SUBSCRIBE, "CONFIRM_BLOCK:", 14);
        zmq_setsockopt(sub_socket, ZMQ_SUBSCRIBE, "NEW_BLOCK:", 10);
        LOG_INFO("🔌 SUB → blockchain PUB at %s (CONFIRM_BLOCK + NEW_BLOCK)", blockchain_pub_addr);
    } else {
        LOG_WARN("⚠️  No --sub address: pool won't receive blockchain confirmations");
        LOG_WARN("   Validators will need to send CONFIRM directly (legacy mode)");
    }
    
    // PUB socket: forwards block notifications to wallets
    // Wallets subscribe to pool PUB (not blockchain PUB) for confirmation flow:
    // Blockchain → Pool (CONFIRM_BLOCK) → Pool processes → Pool PUB → Wallets
    void* pub_socket = NULL;
    if (pool_pub_addr) {
        pub_socket = zmq_socket(context, ZMQ_PUB);
        int linger = 0;
        zmq_setsockopt(pub_socket, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_bind(pub_socket, pool_pub_addr) != 0) {
            LOG_ERROR("❌ Failed to bind PUB to %s", pool_pub_addr);
        } else {
            LOG_INFO("🔌 PUB bound to %s (wallet notifications)", pool_pub_addr);
        }
    }
    
    LOG_INFO("🚀 Transaction pool ready!");
    
    // ═══════════════════════════════════════════════════════════════════
    // MAIN LOOP (zmq_poll for REP + SUB)
    // ═══════════════════════════════════════════════════════════════════
    // 8MB buffer — handles up to 65K TX batches from wallet submissions
    // and large GET_FOR_WINNER responses
    #define RECV_BUFFER_SIZE (8 * 1024 * 1024)
    char* buffer = safe_malloc(RECV_BUFFER_SIZE);
    char* sub_buffer = safe_malloc(RECV_BUFFER_SIZE);
    
    zmq_pollitem_t items[2];
    items[0].socket = rep_socket;
    items[0].fd = 0;
    items[0].events = ZMQ_POLLIN;
    items[0].revents = 0;
    items[1].socket = sub_socket;
    items[1].fd = 0;
    items[1].events = ZMQ_POLLIN;
    items[1].revents = 0;
    int num_poll_items = sub_socket ? 2 : 1;
    
    while (running) {
        int rc = zmq_poll(items, num_poll_items, 100);
        if (rc < 0) {
            if (running) LOG_WARN("zmq_poll error: %s", zmq_strerror(zmq_errno()));
            continue;
        }
        
        // ═══════════════════════════════════════════════════════════
        // HANDLE REP REQUESTS (wallets, validators)
        // ═══════════════════════════════════════════════════════════
        if (items[0].revents & ZMQ_POLLIN) {
            int size = zmq_recv(rep_socket, buffer, RECV_BUFFER_SIZE - 1, 0);
            
            if (size > 0) {
                buffer[size] = '\0';
                
                // ==========================================================
                // SUBMIT_BATCH_PB - Protobuf batch submission (primary path)
                // ==========================================================
                // Format: "SUBMIT_BATCH_PB:" (16B) + protobuf TransactionBatch
                // 
                // ~2x smaller than hex, schema-safe, distributed-ready.
                // ==========================================================
                if (size > 16 && starts_with(buffer, "SUBMIT_BATCH_PB:")) {
                    Blockchain__TransactionBatch* batch = 
                        blockchain__transaction_batch__unpack(
                            NULL, size - 16, (uint8_t*)(buffer + 16));
                    
                    int accepted = 0, rejected = 0;
                    if (batch) {
                        for (size_t i = 0; i < batch->n_transactions; i++) {
                            Transaction* tx = pb_to_transaction(batch->transactions[i]);
                            if (pool_add(pool, tx)) {
                                accepted++;
                                total_submitted++;
                            } else {
                                rejected++;
                                total_rejected++;
                            }
                            transaction_destroy(tx);
                        }
                        blockchain__transaction_batch__free_unpacked(batch, NULL);
                    }
                    
                    if (accepted >= 100 || rejected > 0) {
                        LOG_INFO("BATCH_PB: +%d -%d (pending: %u)", 
                                 accepted, rejected, pool->pending_count);
                    }
                    
                    char resp[64];
                    snprintf(resp, sizeof(resp), "OK:%d|%d", accepted, rejected);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                // ==========================================================
                // SUBMIT_BATCH - Legacy hex batch submission (fallback)
                // ==========================================================
                else if (starts_with(buffer, "SUBMIT_BATCH:")) {
                    const char* ptr = buffer + 13;
                    int accepted = 0, rejected = 0;
                    
                    char tx_hex[512];
                    while (*ptr) {
                        int i = 0;
                        while (*ptr && *ptr != '|' && i < (int)sizeof(tx_hex) - 1) {
                            tx_hex[i++] = *ptr++;
                        }
                        tx_hex[i] = '\0';
                        
                        if (i > 0) {
                            Transaction* tx = transaction_deserialize(tx_hex);
                            if (tx) {
                                if (pool_add(pool, tx)) {
                                    accepted++;
                                    total_submitted++;
                                } else {
                                    rejected++;
                                    total_rejected++;
                                }
                                transaction_destroy(tx);
                            } else {
                                rejected++;
                                total_rejected++;
                            }
                        }
                        if (*ptr == '|') ptr++;
                    }
                    
                    if (accepted >= 100 || rejected > 0) {
                        LOG_INFO("BATCH: +%d -%d (pending: %u)", 
                                 accepted, rejected, pool->pending_count);
                    }
                    
                    char resp[64];
                    snprintf(resp, sizeof(resp), "OK:%d|%d", accepted, rejected);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                // ==========================================================
                // SUBMIT - Single transaction (legacy)
                // ==========================================================
                else if (starts_with(buffer, "SUBMIT:")) {
                    Transaction* tx = transaction_deserialize(buffer + 7);
                    if (tx) {
                        if (pool_add(pool, tx)) {
                            total_submitted++;
                            zmq_send(rep_socket, "OK", 2, 0);
                        } else {
                            total_rejected++;
                            zmq_send(rep_socket, "FAIL", 4, 0);
                        }
                        transaction_destroy(tx);
                    } else {
                        total_rejected++;
                        zmq_send(rep_socket, "INVALID", 7, 0);
                    }
                }
                // ==========================================================
                // GET_FOR_WINNER - Protobuf TransactionBatch response
                // ==========================================================
                // Format request:  "GET_FOR_WINNER:max_count:block_height"
                // Format response: "TXPB" (4B) + protobuf TransactionBatch
                //
                // Validator receives protobuf, unpacks TXs, builds block.
                // ==========================================================
                else if (starts_with(buffer, "GET_FOR_WINNER:")) {
                    uint32_t max_count = 100;
                    uint32_t block_height = 0;
                    sscanf(buffer + 15, "%u:%u", &max_count, &block_height);
                    
                    LOG_INFO("📤 GET_FOR_WINNER: requesting %u TXs for block #%u (pool pending: %u)",
                             max_count, block_height, pool->pending_count);

                    uint64_t ts_handler   = get_current_time_ns();
                    uint32_t fill_before  = pool->pending_count;

                    uint64_t scan_start   = get_current_time_ns();
                    uint32_t count = 0;
                    uint8_t* pubkeys = NULL;
                    uint64_t* t0_ns = NULL;
                    uint64_t* t1_ns = NULL;
                    Transaction** txs = pool_get_pending_with_pubkeys(
                        pool, max_count, block_height, &count, &pubkeys,
                        &t0_ns, &t1_ns);
                    uint64_t scan_duration_ns = get_current_time_ns() - scan_start;

                    if (scan_duration_ns > 100000000ULL)
                        LOG_WARN("⚠️  pool scan took %lu ms (>100ms) — scan is a bottleneck",
                                 scan_duration_ns / 1000000ULL);
                    
                    // Build protobuf TransactionBatch (zero-copy pointers)
                    Blockchain__TransactionBatch batch = BLOCKCHAIN__TRANSACTION_BATCH__INIT;
                    Blockchain__Transaction** pb_ptrs = NULL;
                    Blockchain__Transaction* pb_arr = NULL;
                    uint64_t total_fees = 0;
                    
                    if (count > 0) {
                        pb_ptrs = safe_malloc(count * sizeof(Blockchain__Transaction*));
                        pb_arr = safe_malloc(count * sizeof(Blockchain__Transaction));
                        batch.n_transactions = count;
                        batch.transactions = pb_ptrs;
                        batch.count = count;
                        
                        for (uint32_t i = 0; i < count; i++) {
                            Blockchain__Transaction* pt = &pb_arr[i];
                            blockchain__transaction__init(pt);
                            // Zero-copy: point into packed Transaction struct
                            pt->nonce = txs[i]->nonce;
                            pt->expiry_block = txs[i]->expiry_block;
                            pt->source_address.data = txs[i]->source_address;
                            pt->source_address.len = 20;
                            pt->dest_address.data = txs[i]->dest_address;
                            pt->dest_address.len = 20;
                            pt->value = txs[i]->value;
                            pt->fee = txs[i]->fee;
                            // Signature: use actual sig_len (supports PQC variable-length sigs)
                            if (txs[i]->sig_len > 0) {
                                pt->signature.data = txs[i]->signature;
                                pt->signature.len = txs[i]->sig_len;
                            } else if (!is_zero(txs[i]->signature, 64)) {
                                // Fallback: Ed25519 with no sig_len set
                                pt->signature.data = txs[i]->signature;
                                pt->signature.len = 64;
                            }
                            // Pubkey: TX carries it inline (v47 design, supports PQC variable-length)
                            if (txs[i]->pubkey_len > 0) {
                                pt->public_key.data = txs[i]->public_key;
                                pt->public_key.len = txs[i]->pubkey_len;
                            }
                            pt->sig_type = txs[i]->sig_type;
                            pb_ptrs[i] = pt;
                            total_fees += txs[i]->fee;
                        }
                    }
                    
                    // Pack protobuf and build TXTS response (sidecar + protobuf)
                    uint64_t pack_start = get_current_time_ns();
                    size_t pb_size = blockchain__transaction_batch__get_packed_size(&batch);
                    // TXTS format: "TXTS"(4) + count(4) + t0_ns[count](count*8) + t1_ns[count](count*8) + pb
                    size_t ts_hdr  = 4 + 4 + (size_t)count * 16;
                    size_t resp_size = ts_hdr + pb_size;
                    uint8_t* response = safe_malloc(resp_size);
                    memcpy(response, "TXTS", 4);
                    memcpy(response + 4, &count, 4);
                    if (count > 0 && t0_ns) memcpy(response + 8,             t0_ns, count * 8);
                    else if (count > 0)     memset(response + 8, 0, count * 8);
                    if (count > 0 && t1_ns) memcpy(response + 8 + count * 8, t1_ns, count * 8);
                    else if (count > 0)     memset(response + 8 + count * 8, 0, count * 8);
                    blockchain__transaction_batch__pack(&batch, response + ts_hdr);
                    uint64_t pack_duration_ns = get_current_time_ns() - pack_start;

                    // pool_fetches_<pid>.csv — one row per fetch
                    static FILE* pool_csv = NULL;
                    if (!pool_csv) {
                        char csv_path[64];
                        snprintf(csv_path, sizeof(csv_path), "pool_fetches_%d.csv", (int)getpid());
                        pool_csv = fopen(csv_path, "w");
                        if (pool_csv)
                            fprintf(pool_csv,
                                "timestamp_ns,pool_fill_count,txs_returned,"
                                "scan_duration_ns,pack_duration_ns\n");
                    }
                    if (pool_csv)
                        fprintf(pool_csv, "%lu,%u,%u,%lu,%lu\n",
                                (unsigned long)ts_handler, fill_before, count,
                                (unsigned long)scan_duration_ns,
                                (unsigned long)pack_duration_ns);

                    LOG_INFO("📤 ✅ Returned %u TXs (fees: %lu, %zu bytes, scan: %lums, pack: %lums)",
                             count, total_fees, resp_size,
                             (unsigned long)(scan_duration_ns / 1000000ULL),
                             (unsigned long)(pack_duration_ns / 1000000ULL));
                    zmq_send(rep_socket, response, resp_size, 0);
                    
                    // Cleanup (AFTER pack - zero-copy pointers must stay valid during pack)
                    for (uint32_t i = 0; i < count; i++) {
                        transaction_destroy(txs[i]);
                    }
                    if (txs) free(txs);
                    if (pubkeys) free(pubkeys);
                    if (t0_ns) free(t0_ns);
                    if (t1_ns) free(t1_ns);
                    if (pb_ptrs) free(pb_ptrs);
                    if (pb_arr) free(pb_arr);
                    free(response);
                }
                // ==========================================================
                // CONFIRM_BIN - Legacy binary confirm (backwards compat)
                // ==========================================================
                else if (size > 16 && starts_with(buffer, "CONFIRM_BIN:")) {
                    uint32_t hash_count = 0;
                    memcpy(&hash_count, buffer + 12, 4);
                    uint8_t* hashes = (uint8_t*)(buffer + 16);
                    
                    size_t expected_size = 16 + (size_t)hash_count * TX_HASH_SIZE;
                    if ((size_t)size < expected_size) {
                        hash_count = (size - 16) / TX_HASH_SIZE;
                    }
                    
                    uint32_t confirmed = pool_confirm_batch(pool, hashes, hash_count);
                    total_confirmed += confirmed;
                    
                    LOG_INFO("CONFIRMED_BIN(legacy) %u/%u (pending: %u)", 
                             confirmed, hash_count, pool->pending_count);
                    
                    char resp[32];
                    snprintf(resp, sizeof(resp), "CONFIRMED:%u", confirmed);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                // ==========================================================
                // CONFIRM - Legacy hex confirm (backwards compat)
                // ==========================================================
                else if (starts_with(buffer, "CONFIRM:")) {
                    const char* ptr = buffer + 8;
                    uint32_t hash_count = 0;
                    const char* scan = ptr;
                    while (*scan) {
                        int i = 0;
                        while (*scan && *scan != '|') { scan++; i++; }
                        if (i == TX_HASH_SIZE * 2) hash_count++;
                        if (*scan == '|') scan++;
                    }
                    
                    uint8_t* hashes = safe_malloc(hash_count * TX_HASH_SIZE);
                    uint32_t parsed = 0;
                    char hash_hex[TX_HASH_SIZE * 2 + 1];
                    
                    while (*ptr && parsed < hash_count) {
                        int i = 0;
                        while (*ptr && *ptr != '|' && i < TX_HASH_SIZE * 2) {
                            hash_hex[i++] = *ptr++;
                        }
                        hash_hex[i] = '\0';
                        if (i == TX_HASH_SIZE * 2) {
                            hex_to_bytes_buf(hash_hex, hashes + parsed * TX_HASH_SIZE, TX_HASH_SIZE);
                            parsed++;
                        }
                        if (*ptr == '|') ptr++;
                    }
                    
                    uint32_t confirmed = pool_confirm_batch(pool, hashes, parsed);
                    total_confirmed += confirmed;
                    free(hashes);
                    
                    LOG_INFO("CONFIRMED(legacy) %u/%u (pending: %u)", 
                             confirmed, parsed, pool->pending_count);
                    
                    char resp[32];
                    snprintf(resp, sizeof(resp), "CONFIRMED:%u", confirmed);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                // ==========================================================
                // GET_PENDING_NONCE
                // ==========================================================
                else if (starts_with(buffer, "GET_PENDING_NONCE:")) {
                    const char* addr_hex = buffer + 18;
                    uint8_t address[20];
                    hex_to_bytes_buf(addr_hex, address, 20);
                    
                    uint64_t pending_nonce = pool_get_pending_nonce(pool, address);
                    
                    char resp[32];
                    snprintf(resp, sizeof(resp), "%lu", pending_nonce);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                // ==========================================================
                // RETURN
                // ==========================================================
                else if (starts_with(buffer, "RETURN:")) {
                    LOG_INFO("↩️  Returning transactions to pending pool");
                    zmq_send(rep_socket, "OK", 2, 0);
                }
                // ==========================================================
                // GET_STATUS
                // ==========================================================
                else if (strcmp(buffer, "GET_STATUS") == 0) {
                    uint32_t pending, confirmed;
                    pool_get_stats(pool, &pending, &confirmed);
                    
                    char resp[256];
                    snprintf(resp, sizeof(resp), 
                             "PENDING:%u|CONFIRMED:%u|TOTAL:%u|CAPACITY:%u|SUBMITTED:%lu|REJECTED:%lu",
                             pending, confirmed, pool->count, pool->capacity, 
                             total_submitted, total_rejected);
                    zmq_send(rep_socket, resp, strlen(resp), 0);
                }
                else {
                    LOG_WARN("❓ Unknown command: %.20s...", buffer);
                    zmq_send(rep_socket, "UNKNOWN", 7, 0);
                }
            }
        }
        
        // ═══════════════════════════════════════════════════════════
        // HANDLE BLOCKCHAIN CONFIRMATIONS (PUB/SUB - async)
        // ═══════════════════════════════════════════════════════════
        // The blockchain publishes CONFIRM_BLOCK after validating+accepting
        // a block. This is the CORRECT confirmation path - blockchain is the
        // single source of truth, not individual validators.
        //
        // Format: "CONFIRM_BLOCK:" (14B) + block_height (4B) +
        //         hash_count (4B) + N x TX_HASH_SIZE (28B) hashes
        //
        // This is ASYNCHRONOUS - not in the block creation critical path.
        // Block creation: Steps 1-5 (GET_HASH, Fetch, Build, PB Send)
        // Confirmation:   Happens in background via PUB/SUB after block accepted
        // ═══════════════════════════════════════════════════════════
        if (num_poll_items > 1 && (items[1].revents & ZMQ_POLLIN)) {
            int size = zmq_recv(sub_socket, sub_buffer, RECV_BUFFER_SIZE - 1, 0);
            
            // Minimum: "CONFIRM_BLOCK:" (14) + height (4) + count (4) = 22 bytes
            if (size >= 22 && memcmp(sub_buffer, "CONFIRM_BLOCK:", 14) == 0) {
                uint32_t block_height = 0;
                uint32_t hash_count = 0;
                memcpy(&block_height, sub_buffer + 14, 4);
                memcpy(&hash_count, sub_buffer + 18, 4);
                
                size_t expected = 22 + (size_t)hash_count * TX_HASH_SIZE;
                if ((size_t)size < expected) {
                    LOG_WARN("CONFIRM_BLOCK: size mismatch (got %d, expected %zu)", 
                             size, expected);
                    hash_count = (size - 22) / TX_HASH_SIZE;
                }
                
                uint8_t* hashes = (uint8_t*)(sub_buffer + 22);
                uint32_t confirmed = pool_confirm_batch(pool, hashes, hash_count);
                total_confirmed += confirmed;
                
                LOG_INFO("📥 BLOCKCHAIN CONFIRMED block #%u: %u/%u txs (pending: %u)",
                         block_height, confirmed, hash_count, pool->pending_count);
                
                // Forward confirmation info to wallets via pool PUB
                // Wallets see: "TX_CONFIRMED:<block_height>:<count>"
                if (pub_socket) {
                    char fwd_msg[128];
                    snprintf(fwd_msg, sizeof(fwd_msg), "TX_CONFIRMED:%u:%u",
                             block_height, confirmed);
                    zmq_send(pub_socket, fwd_msg, strlen(fwd_msg), ZMQ_DONTWAIT);
                }
            }
            // Forward NEW_BLOCK from blockchain to wallets via pool PUB
            else if (size > 10 && memcmp(sub_buffer, "NEW_BLOCK:", 10) == 0) {
                sub_buffer[size] = '\0';
                // Forward as-is to wallets subscribed to pool PUB
                if (pub_socket) {
                    zmq_send(pub_socket, sub_buffer, size, ZMQ_DONTWAIT);
                }
            }
        }
    }
    
    LOG_INFO("📊 Final stats: submitted=%lu, confirmed=%lu, rejected=%lu",
             total_submitted, total_confirmed, total_rejected);
    
    free(buffer);
    free(sub_buffer);
    pool_destroy(pool);
    zmq_close(rep_socket);
    if (sub_socket) zmq_close(sub_socket);
    if (pub_socket) zmq_close(pub_socket);
    zmq_ctx_destroy(context);
    
    LOG_INFO("👋 Transaction pool stopped");
    return 0;
}
