/**
 * ============================================================================
 * MAIN_BENCHMARK.C - Precise Internal Benchmarking Tool
 * ============================================================================
 * Measures:
 *   - Google Protocol Buffers serialization/deserialization times
 *   - ZeroMQ messaging latency
 *   - Proof search and plot generation times
 *   - BLAKE3 hashing performance
 * ============================================================================
 */

#include "../include/transaction.h"
#include "../include/block.h"
#include "../include/consensus.h"
#include "../include/wallet.h"
#include "../include/crypto_backend.h"
#include "../include/common.h"
#include "../include/blake3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zmq.h>

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct {
    uint64_t count;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    size_t total_bytes;
} BenchStats;

static void init_stats(BenchStats* stats) {
    stats->count = 0;
    stats->total_ns = 0;
    stats->min_ns = UINT64_MAX;
    stats->max_ns = 0;
    stats->total_bytes = 0;
}

static void record_stat(BenchStats* stats, uint64_t duration_ns, size_t bytes) {
    stats->count++;
    stats->total_ns += duration_ns;
    stats->total_bytes += bytes;
    if (duration_ns < stats->min_ns) stats->min_ns = duration_ns;
    if (duration_ns > stats->max_ns) stats->max_ns = duration_ns;
}

static void print_stats(const char* name, BenchStats* stats) {
    if (stats->count == 0) {
        printf("  %-35s: No data\n", name);
        return;
    }
    double avg_us = (double)stats->total_ns / stats->count / 1000.0;
    double min_us = (double)stats->min_ns / 1000.0;
    double max_us = (double)stats->max_ns / 1000.0;
    double throughput = stats->count * 1000000000.0 / stats->total_ns;
    double avg_bytes = (double)stats->total_bytes / stats->count;
    
    printf("  %-35s: avg %.2f µs, min %.2f µs, max %.2f µs\n",
           name, avg_us, min_us, max_us);
    printf("  %-35s  %.0f ops/sec, avg %.0f bytes\n", "", throughput, avg_bytes);
}

static void print_stats_csv(FILE* f, const char* category, const char* name, BenchStats* stats) {
    if (stats->count == 0) return;
    double avg_us = (double)stats->total_ns / stats->count / 1000.0;
    double min_us = (double)stats->min_ns / 1000.0;
    double max_us = (double)stats->max_ns / 1000.0;
    double throughput = stats->count * 1000000000.0 / stats->total_ns;
    double avg_bytes = (double)stats->total_bytes / stats->count;
    
    fprintf(f, "%s,%s,%lu,%.3f,%.3f,%.3f,%.0f,%.0f\n",
            category, name, stats->count, avg_us, min_us, max_us, throughput, avg_bytes);
}

/* ============================================================================
 * GPB (Google Protocol Buffers) BENCHMARKS
 * ============================================================================ */

static void benchmark_tx_serialization(int iterations, BenchStats* ser, BenchStats* deser) {
    printf("  Running transaction GPB benchmark (%d iterations)...\n", iterations);
    
    Wallet* wallet = wallet_create_named("bench_tx", SIG_SCHEME);
    if (!wallet) return;
    uint8_t dest_addr[20];
    memset(dest_addr, 0xAB, 20);
    
    for (int i = 0; i < iterations; i++) {
        Transaction* tx = transaction_create(wallet, dest_addr, 100, 1, i, 0);
        if (!tx) continue;
        
        // Measure GPB serialization
        uint64_t start = get_time_ns();
        char* hex = transaction_serialize(tx);
        uint64_t ser_time = get_time_ns() - start;
        
        if (hex) {
            size_t len = strlen(hex);
            record_stat(ser, ser_time, len / 2);  // bytes, not hex chars
            
            // Measure GPB deserialization
            start = get_time_ns();
            Transaction* tx2 = transaction_deserialize(hex);
            uint64_t deser_time = get_time_ns() - start;
            
            if (tx2) {
                record_stat(deser, deser_time, len / 2);
                transaction_destroy(tx2);
            }
            free(hex);
        }
        transaction_destroy(tx);
    }
    wallet_destroy(wallet);
}

static void benchmark_block_serialization(int iterations, int txs_per_block, 
                                          BenchStats* ser, BenchStats* deser) {
    printf("  Running block GPB benchmark (%d iterations, %d tx/block)...\n", 
           iterations, txs_per_block);
    
    Wallet* wallet = wallet_create_named("bench_block", SIG_SCHEME);
    if (!wallet) return;
    uint8_t dest_addr[20];
    memset(dest_addr, 0xCD, 20);
    
    for (int i = 0; i < iterations; i++) {
        Block* block = block_create();
        block->header.height = i;
        block->header.timestamp = get_current_timestamp();
        
        for (int j = 0; j < txs_per_block; j++) {
            Transaction* tx = transaction_create(wallet, dest_addr, 10, 1, j, 0);
            if (tx) block_add_transaction(block, tx);
        }
        block_calculate_hash(block);
        
        // Measure GPB serialization
        uint64_t start = get_time_ns();
        char* hex = block_serialize(block);
        uint64_t ser_time = get_time_ns() - start;
        
        if (hex) {
            size_t len = strlen(hex);
            record_stat(ser, ser_time, len / 2);
            
            // Measure GPB deserialization
            start = get_time_ns();
            Block* block2 = block_deserialize(hex);
            uint64_t deser_time = get_time_ns() - start;
            
            if (block2) {
                record_stat(deser, deser_time, len / 2);
                block_destroy(block2);
            }
            free(hex);
        }
        block_destroy(block);
    }
    wallet_destroy(wallet);
}

/* ============================================================================
 * BLAKE3 HASHING BENCHMARKS
 * ============================================================================ */

static void benchmark_blake3(int iterations, BenchStats* hash_stats) {
    printf("  Running BLAKE3 benchmark (%d iterations)...\n", iterations);
    
    uint8_t data[256];
    uint8_t hash[32];
    
    // Fill with random data
    for (int i = 0; i < 256; i++) data[i] = rand() & 0xFF;
    
    for (int i = 0; i < iterations; i++) {
        data[0] = i & 0xFF;  // Vary input
        
        uint64_t start = get_time_ns();
        blake3_hash(data, 256, hash);
        uint64_t hash_time = get_time_ns() - start;
        
        record_stat(hash_stats, hash_time, 256);
    }
}

/* ============================================================================
 * PROOF OPERATIONS BENCHMARKS
 * ============================================================================ */

static void benchmark_proof_operations(int iterations, int k_param, 
                                       BenchStats* plot_stats, BenchStats* search_stats) {
    printf("  Generating plot (k=%d)...\n", k_param);
    
    uint8_t farmer_addr[20];
    memset(farmer_addr, 0xEF, 20);
    
    uint64_t start = get_time_ns();
    Plot* plot = plot_create(farmer_addr, k_param);
    uint64_t plot_time = get_time_ns() - start;
    
    if (!plot) return;
    record_stat(plot_stats, plot_time, (1ULL << k_param) * sizeof(PlotEntry));
    
    printf("  Running proof search benchmark (%d searches)...\n", iterations);
    
    for (int i = 0; i < iterations; i++) {
        uint8_t challenge[32];
        for (int j = 0; j < 32; j++) challenge[j] = rand() & 0xFF;
        
        start = get_time_ns();
        SpaceProof* proof = plot_find_proof(plot, challenge, 1);
        uint64_t search_time = get_time_ns() - start;
        
        record_stat(search_stats, search_time, sizeof(SpaceProof));
        if (proof) free(proof);
    }
    plot_destroy(plot);
}

/* ============================================================================
 * ZMQ MESSAGING BENCHMARKS
 * ============================================================================ */

static void benchmark_zmq_inproc(int iterations, BenchStats* rtt_stats) {
    printf("  Running ZMQ inproc benchmark (%d iterations)...\n", iterations);
    
    void* ctx = zmq_ctx_new();
    void* rep = zmq_socket(ctx, ZMQ_REP);
    void* req = zmq_socket(ctx, ZMQ_REQ);
    
    zmq_bind(rep, "inproc://benchmark");
    zmq_connect(req, "inproc://benchmark");
    
    // Test with various message sizes
    char msg[1024];
    char recv_buf[1024];
    memset(msg, 'X', sizeof(msg));
    
    for (int i = 0; i < iterations; i++) {
        size_t msg_size = 64;  // Typical transaction hex size
        
        uint64_t start = get_time_ns();
        zmq_send(req, msg, msg_size, 0);
        zmq_recv(rep, recv_buf, sizeof(recv_buf), 0);
        zmq_send(rep, "OK", 2, 0);
        zmq_recv(req, recv_buf, sizeof(recv_buf), 0);
        uint64_t rtt = get_time_ns() - start;
        
        record_stat(rtt_stats, rtt, msg_size + 2);
    }
    
    zmq_close(req);
    zmq_close(rep);
    zmq_ctx_destroy(ctx);
}

static void benchmark_zmq_tcp(int iterations, BenchStats* rtt_stats) {
    printf("  Running ZMQ TCP benchmark (%d iterations)...\n", iterations);
    
    void* ctx = zmq_ctx_new();
    void* rep = zmq_socket(ctx, ZMQ_REP);
    void* req = zmq_socket(ctx, ZMQ_REQ);
    
    // Use TCP loopback for more realistic benchmark
    if (zmq_bind(rep, "tcp://127.0.0.1:15555") != 0) {
        printf("    (TCP benchmark skipped - port unavailable)\n");
        zmq_close(req);
        zmq_close(rep);
        zmq_ctx_destroy(ctx);
        return;
    }
    zmq_connect(req, "tcp://127.0.0.1:15555");
    
    char msg[1024];
    char recv_buf[1024];
    memset(msg, 'X', sizeof(msg));
    
    for (int i = 0; i < iterations; i++) {
        size_t msg_size = 256;  // Larger message for TCP
        
        uint64_t start = get_time_ns();
        zmq_send(req, msg, msg_size, 0);
        zmq_recv(rep, recv_buf, sizeof(recv_buf), 0);
        zmq_send(rep, "OK", 2, 0);
        zmq_recv(req, recv_buf, sizeof(recv_buf), 0);
        uint64_t rtt = get_time_ns() - start;
        
        record_stat(rtt_stats, rtt, msg_size + 2);
    }
    
    zmq_close(req);
    zmq_close(rep);
    zmq_ctx_destroy(ctx);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char* argv[]) {
    int iterations = 1000;
    int k_param = 16;
    const char* csv_file = NULL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_file = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            k_param = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            iterations = atoi(argv[i]);
            if (i + 1 < argc && argv[i+1][0] != '-') {
                k_param = atoi(argv[++i]);
            }
        }
    }
    
    srand(time(NULL));
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║               BLOCKCHAIN INTERNAL BENCHMARK                               ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Iterations: %-10d K parameter: %-2d (2^%-2d = %-7d entries)     ║\n", 
           iterations, k_param, k_param, 1 << k_param);
    printf("╚══════════════════════════════════════════════════════════════════════════╝\n\n");
    
    // Statistics arrays
    BenchStats tx_ser, tx_deser;
    BenchStats block_ser_1, block_deser_1;
    BenchStats block_ser_10, block_deser_10;
    BenchStats block_ser_100, block_deser_100;
    BenchStats blake3_stats;
    BenchStats plot_stats, search_stats;
    BenchStats zmq_inproc, zmq_tcp;
    
    init_stats(&tx_ser); init_stats(&tx_deser);
    init_stats(&block_ser_1); init_stats(&block_deser_1);
    init_stats(&block_ser_10); init_stats(&block_deser_10);
    init_stats(&block_ser_100); init_stats(&block_deser_100);
    init_stats(&blake3_stats);
    init_stats(&plot_stats); init_stats(&search_stats);
    init_stats(&zmq_inproc); init_stats(&zmq_tcp);
    
    /* ========== GPB (Protocol Buffers) Benchmarks ========== */
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  GPB (Google Protocol Buffers) SERIALIZATION\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    benchmark_tx_serialization(iterations, &tx_ser, &tx_deser);
    printf("\n  Transaction Serialization:\n");
    print_stats("Serialize (GPB encode)", &tx_ser);
    print_stats("Deserialize (GPB decode)", &tx_deser);
    
    benchmark_block_serialization(iterations/10, 1, &block_ser_1, &block_deser_1);
    printf("\n  Block Serialization (1 tx/block):\n");
    print_stats("Serialize (GPB encode)", &block_ser_1);
    print_stats("Deserialize (GPB decode)", &block_deser_1);
    
    benchmark_block_serialization(iterations/10, 10, &block_ser_10, &block_deser_10);
    printf("\n  Block Serialization (10 tx/block):\n");
    print_stats("Serialize (GPB encode)", &block_ser_10);
    print_stats("Deserialize (GPB decode)", &block_deser_10);
    
    benchmark_block_serialization(iterations/100 > 0 ? iterations/100 : 10, 100, &block_ser_100, &block_deser_100);
    printf("\n  Block Serialization (100 tx/block):\n");
    print_stats("Serialize (GPB encode)", &block_ser_100);
    print_stats("Deserialize (GPB decode)", &block_deser_100);
    
    /* ========== BLAKE3 Benchmarks ========== */
    printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    printf("  BLAKE3 HASHING\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    benchmark_blake3(iterations, &blake3_stats);
    printf("\n  BLAKE3 Hash (256 bytes input):\n");
    print_stats("Hash computation", &blake3_stats);
    
    /* ========== Proof Operations Benchmarks ========== */
    printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    printf("  PROOF OPERATIONS (k=%d)\n", k_param);
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    benchmark_proof_operations(iterations/10, k_param, &plot_stats, &search_stats);
    printf("\n  Plot and Search:\n");
    print_stats("Plot generation", &plot_stats);
    print_stats("Proof search (binary search)", &search_stats);
    
    /* ========== ZMQ Benchmarks ========== */
    printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    printf("  ZMQ (ZeroMQ) MESSAGING\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    benchmark_zmq_inproc(iterations, &zmq_inproc);
    printf("\n  ZMQ Inproc (in-process):\n");
    print_stats("Round-trip latency", &zmq_inproc);
    
    benchmark_zmq_tcp(iterations/10, &zmq_tcp);
    printf("\n  ZMQ TCP (loopback):\n");
    print_stats("Round-trip latency", &zmq_tcp);
    
    /* ========== Summary ========== */
    printf("\n╔══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                              SUMMARY                                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    
    double tx_ser_us = tx_ser.count > 0 ? (double)tx_ser.total_ns / tx_ser.count / 1000.0 : 0;
    double tx_deser_us = tx_deser.count > 0 ? (double)tx_deser.total_ns / tx_deser.count / 1000.0 : 0;
    double zmq_us = zmq_inproc.count > 0 ? (double)zmq_inproc.total_ns / zmq_inproc.count / 1000.0 : 0;
    double zmq_tcp_us = zmq_tcp.count > 0 ? (double)zmq_tcp.total_ns / zmq_tcp.count / 1000.0 : 0;
    double blake3_us = blake3_stats.count > 0 ? (double)blake3_stats.total_ns / blake3_stats.count / 1000.0 : 0;
    double search_us = search_stats.count > 0 ? (double)search_stats.total_ns / search_stats.count / 1000.0 : 0;
    double plot_ms = plot_stats.count > 0 ? (double)plot_stats.total_ns / plot_stats.count / 1000000.0 : 0;
    
    printf("║  GPB Transaction serialize:     %8.2f µs                              ║\n", tx_ser_us);
    printf("║  GPB Transaction deserialize:   %8.2f µs                              ║\n", tx_deser_us);
    printf("║  GPB Round-trip (ser+deser):    %8.2f µs                              ║\n", tx_ser_us + tx_deser_us);
    printf("║  BLAKE3 hash (256 bytes):       %8.2f µs                              ║\n", blake3_us);
    printf("║  ZMQ inproc round-trip:         %8.2f µs                              ║\n", zmq_us);
    printf("║  ZMQ TCP round-trip:            %8.2f µs                              ║\n", zmq_tcp_us);
    printf("║  Proof search (k=%d):           %8.2f µs                              ║\n", k_param, search_us);
    printf("║  Plot generation (k=%d):        %8.2f ms                              ║\n", k_param, plot_ms);
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    
    // Calculate theoretical maximums
    double tx_process_time = tx_ser_us + tx_deser_us + zmq_us;
    double theoretical_tps = 1000000.0 / tx_process_time;
    
    printf("║  THEORETICAL PERFORMANCE:                                                ║\n");
    printf("║    TX processing time:          %8.2f µs (GPB + ZMQ)                  ║\n", tx_process_time);
    printf("║    Max TPS (single thread):     %8.0f tx/sec                          ║\n", theoretical_tps);
    printf("║    Proof searches/sec:          %8.0f /sec                            ║\n", 1000000.0 / search_us);
    printf("╚══════════════════════════════════════════════════════════════════════════╝\n\n");
    
    /* ========== CSV Output ========== */
    if (csv_file) {
        FILE* f = fopen(csv_file, "w");
        if (f) {
            fprintf(f, "category,operation,count,avg_us,min_us,max_us,ops_per_sec,avg_bytes\n");
            print_stats_csv(f, "GPB", "tx_serialize", &tx_ser);
            print_stats_csv(f, "GPB", "tx_deserialize", &tx_deser);
            print_stats_csv(f, "GPB", "block_1tx_serialize", &block_ser_1);
            print_stats_csv(f, "GPB", "block_1tx_deserialize", &block_deser_1);
            print_stats_csv(f, "GPB", "block_10tx_serialize", &block_ser_10);
            print_stats_csv(f, "GPB", "block_10tx_deserialize", &block_deser_10);
            print_stats_csv(f, "GPB", "block_100tx_serialize", &block_ser_100);
            print_stats_csv(f, "GPB", "block_100tx_deserialize", &block_deser_100);
            print_stats_csv(f, "BLAKE3", "hash_256bytes", &blake3_stats);
            print_stats_csv(f, "Proof", "plot_generation", &plot_stats);
            print_stats_csv(f, "Proof", "proof_search", &search_stats);
            print_stats_csv(f, "ZMQ", "inproc_rtt", &zmq_inproc);
            print_stats_csv(f, "ZMQ", "tcp_rtt", &zmq_tcp);
            fclose(f);
            printf("Results saved to: %s\n\n", csv_file);
        }
    }
    
    return 0;
}
