#!/bin/bash
# Phase A: Pipeline diagnostic matrix
# Ed25519 at 100K / 250K / 500K / 1M TXs with MAX_BLOCK=10000 (baseline config).
# Builds once before the loop; uses a PATH-override no-op make so benchmark.sh
# skips its internal rebuild for each of the four runs.

set -euo pipefail
cd ~/qMEMO/blockchain

RUN_ID="phase_a_$(date +%Y%m%d_%H%M%S)"
OUT="benchmark_results/$RUN_ID"
mkdir -p "$OUT"

TX_LIST="100000 250000 500000 1000000"
MAX_BLOCK=10000     # match existing system baseline (April 18 numbers)
SCHEME=1            # Ed25519 only: fastest per-TX, sharpest bottleneck signal
K=16
FARMERS=1
BATCH=64
THREADS=8
BLOCK_INTERVAL=1

echo "================================================================"
echo " Phase A: Pipeline Diagnostic Matrix"
echo " TX counts : $TX_LIST"
echo " Scheme    : Ed25519 (1)"
echo " max_block : $MAX_BLOCK  (matches production baseline)"
echo " Output    : $OUT"
echo "================================================================"
echo ""

# ── Hard build ONCE before the loop ─────────────────────────────────
echo "[build] Clean build SIG_SCHEME=$SCHEME OQS_ROOT=~/qMEMO/liboqs_install ..."
make clean >/dev/null 2>&1
make SIG_SCHEME=$SCHEME OQS_ROOT=~/qMEMO/liboqs_install -j8 2>&1 | tail -5
echo ""

# Create a no-op make wrapper so benchmark.sh's internal make calls are skipped
TOOL_DIR=$(mktemp -d)
cat > "$TOOL_DIR/make" << 'MAKEEOF'
#!/bin/bash
# Phase A no-op make: binary already built before the loop
exit 0
MAKEEOF
chmod +x "$TOOL_DIR/make"
echo "[build] No-op make wrapper at $TOOL_DIR/make — benchmark.sh will skip rebuilds"
echo ""

cleanup() {
    rm -rf "$TOOL_DIR"
}
trap cleanup EXIT

for TX in $TX_LIST; do
    echo "════════════════════════════════════════════════"
    echo " Run: TX=$TX  max_block=$MAX_BLOCK"
    echo "════════════════════════════════════════════════"

    rm -f pool_fetches_*.csv tx_diag_*.csv

    START_TS=$(date +%s%3N)

    BENCH_OUT="$OUT/bench_tx${TX}.log"
    PATH="$TOOL_DIR:$PATH" timeout 900 \
        ./benchmark.sh $TX $BLOCK_INTERVAL $K $FARMERS 0 $MAX_BLOCK $BATCH $THREADS \
        2>&1 | tee "$BENCH_OUT"

    END_TS=$(date +%s%3N)
    ELAPSED_MS=$(( END_TS - START_TS ))

    # Archive diagnostic CSVs
    PF_SRC=$(ls -t pool_fetches_*.csv 2>/dev/null | head -1 || true)
    TD_SRC=$(ls -t tx_diag_*.csv      2>/dev/null | head -1 || true)

    if [ -n "$PF_SRC" ]; then
        cp "$PF_SRC" "$OUT/pool_fetches_tx${TX}.csv"
        echo "[csv] pool_fetches: $(wc -l < "$OUT/pool_fetches_tx${TX}.csv") rows"
    else
        echo "[csv] WARN: pool_fetches CSV missing"
    fi
    if [ -n "$TD_SRC" ]; then
        cp "$TD_SRC" "$OUT/tx_diag_tx${TX}.csv"
        echo "[csv] tx_diag: $(wc -l < "$OUT/tx_diag_tx${TX}.csv") rows"
    else
        echo "[csv] WARN: tx_diag CSV missing"
    fi

    E2E_TPS=$(grep  'End-to-end throughput'   "$BENCH_OUT" | grep -oP '[0-9]+\.[0-9]+' | head -1 || echo "N/A")
    CONF_RATE=$(grep 'Confirmation rate'       "$BENCH_OUT" | grep -oP '[0-9]+\.[0-9]+' | head -1 || echo "N/A")
    PROC_TIME=$(grep 'Processing time.*RESUME' "$BENCH_OUT" | grep -oP '[0-9]+\.[0-9]+' | head -1 || echo "N/A")
    echo "[result] e2e_tps=$E2E_TPS  confirm_rate=${CONF_RATE}%  proc_time=${PROC_TIME}s  wall=${ELAPSED_MS}ms"
    echo ""
done

echo ""
echo "================================================================"
echo " Phase A benchmarks complete — analysing timing CSVs"
echo "================================================================"
echo ""

python3 - "$OUT" << 'PYEOF'
import sys, os, csv, statistics, glob

OUT = sys.argv[1]
TX_COUNTS = [100000, 250000, 500000, 1000000]

print(f"Results dir: {OUT}\n")

# ── 1. Pool scan duration vs fill level ─────────────────────────────
print("=== POOL SCAN DURATION (ns) — filled fetches only ===")
print(f"{'TX count':>10}  {'n_fetches':>9}  {'scan_min_ms':>11}  {'scan_p50_ms':>11}  {'scan_p95_ms':>11}  {'scan_max_ms':>11}  {'pack_p50_ms':>11}")
print("-" * 95)
for tx in TX_COUNTS:
    pf = f"{OUT}/pool_fetches_tx{tx}.csv"
    if not os.path.exists(pf):
        print(f"{tx:>10}  MISSING"); continue
    scans, packs = [], []
    with open(pf) as f:
        for row in csv.DictReader(f):
            if int(row['pool_fill_count']) > 0:
                scans.append(int(row['scan_duration_ns']))
                packs.append(int(row['pack_duration_ns']))
    if not scans:
        print(f"{tx:>10}  no filled-pool rows"); continue
    scans.sort(); packs.sort(); n = len(scans)
    def ms(ns): return ns / 1e6
    p50 = scans[n // 2]; p95 = scans[int(n * 0.95)]
    pk50 = packs[n // 2]
    print(f"{tx:>10}  {n:>9}  {ms(min(scans)):>11.2f}  {ms(p50):>11.2f}  "
          f"{ms(p95):>11.2f}  {ms(max(scans)):>11.2f}  {ms(pk50):>11.2f}")

print()

# ── 2. Per-TX pipeline stage breakdown ──────────────────────────────
print("=== PER-TX PIPELINE BREAKDOWN (µs) ===")
print("  T2-T1   = pool-to-validator (ZMQ + batch build)")
print("  T2_5-T2 = protobuf unpack (batch-level, constant within batch)")
print("  T3-T2_5 = signature verify")
print("  T3-T1   = total validator-side latency")
print()
hdr = (f"{'TX count':>10}  {'rows':>8}  "
       f"{'T2-T1 p50µs':>12}  {'T2-T1 p95µs':>12}  "
       f"{'T2.5-T2 p50µs':>14}  "
       f"{'T3-T2.5 p50µs':>14}  {'T3-T2.5 p95µs':>14}  "
       f"{'T3-T1 p50µs':>13}  {'T3-T1 p95µs':>13}")
print(hdr)
print("-" * len(hdr))

for tx in TX_COUNTS:
    td = f"{OUT}/tx_diag_tx{tx}.csv"
    if not os.path.exists(td):
        print(f"{tx:>10}  MISSING"); continue
    dt21, dt25_2, dt3_25, dt3_1 = [], [], [], []
    with open(td) as f:
        for row in csv.DictReader(f):
            t1  = int(row['t1_ns'])
            t2  = int(row['t2_ns'])
            t25 = int(row['t2_5_ns'])
            t3  = int(row['t3_ns'])
            if t1 == 0 or t2 == 0 or t25 == 0 or t3 == 0:
                continue
            dt21.append(t2 - t1)
            dt25_2.append(t25 - t2)
            dt3_25.append(t3 - t25)
            dt3_1.append(t3 - t1)
    if not dt21:
        print(f"{tx:>10}  no valid rows"); continue
    def pct(arr, p):
        a = sorted(arr); return a[int(len(a) * p / 100)] / 1000  # ns→µs
    rows = len(dt21)
    print(f"{tx:>10}  {rows:>8}  "
          f"{pct(dt21,50):>12.1f}  {pct(dt21,95):>12.1f}  "
          f"{pct(dt25_2,50):>14.1f}  "
          f"{pct(dt3_25,50):>14.1f}  {pct(dt3_25,95):>14.1f}  "
          f"{pct(dt3_1,50):>13.1f}  {pct(dt3_1,95):>13.1f}")

print()

# ── 3. Stage share of total validator latency (p50) ─────────────────
print("=== STAGE SHARE OF T3-T1 TOTAL (at p50) ===")
print(f"{'TX count':>10}  {'T2-T1 %':>9}  {'T2.5-T2 %':>11}  {'T3-T2.5 %':>11}")
print("-" * 50)
for tx in TX_COUNTS:
    td = f"{OUT}/tx_diag_tx{tx}.csv"
    if not os.path.exists(td): continue
    dt21, dt25_2, dt3_25 = [], [], []
    with open(td) as f:
        for row in csv.DictReader(f):
            t1=int(row['t1_ns']); t2=int(row['t2_ns'])
            t25=int(row['t2_5_ns']); t3=int(row['t3_ns'])
            if t1==0 or t2==0 or t25==0 or t3==0: continue
            dt21.append(t2-t1); dt25_2.append(t25-t2); dt3_25.append(t3-t25)
    if not dt21: continue
    def p50(arr): a=sorted(arr); return a[len(a)//2]
    s1=p50(dt21); s2=p50(dt25_2); s3=p50(dt3_25); total=s1+s2+s3
    print(f"{tx:>10}  {100*s1/total:>9.1f}  {100*s2/total:>11.1f}  {100*s3/total:>11.1f}")

print()

# ── 4. Bottleneck verdict ────────────────────────────────────────────
print("=== BOTTLENECK VERDICT ===")
for tx in TX_COUNTS:
    pf = f"{OUT}/pool_fetches_tx{tx}.csv"
    td = f"{OUT}/tx_diag_tx{tx}.csv"
    if not os.path.exists(pf) or not os.path.exists(td): continue
    scans = []
    with open(pf) as f:
        for row in csv.DictReader(f):
            if int(row['pool_fill_count']) > 0:
                scans.append(int(row['scan_duration_ns']) / 1e6)
    verifies = []
    with open(td) as f:
        for row in csv.DictReader(f):
            t25=int(row['t2_5_ns']); t3=int(row['t3_ns'])
            if t25>0 and t3>0: verifies.append((t3-t25)/1e3)
    if scans and verifies:
        scans.sort(); verifies.sort()
        sm = statistics.median(scans); vm = statistics.median(verifies)
        ratio = sm * 1000 / max(vm, 0.001)
        dominant = "SCAN" if sm > 10 and ratio > 10 else "VERIFY" if vm > sm*1000 else "MIXED"
        print(f"  TX={tx:>8}: scan_p50={sm:.1f}ms  verify_p50={vm:.1f}µs/tx  "
              f"scan/verify={ratio:.0f}×  → {dominant}")

PYEOF

echo ""
echo "Phase A complete. Results: $OUT"
