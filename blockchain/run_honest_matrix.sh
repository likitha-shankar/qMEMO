#!/bin/bash
# ============================================================
# run_honest_matrix.sh
# Non-interactive Chameleon runner for the corrected benchmark
# matrix. Fixes the max_txs_per_block cap issue identified in
# the April 2026 review: all schemes are now run with
# max_txs_per_block=100000 so the consensus config no longer
# masks signature-scheme differences.
#
# Runs: 4 schemes × {5K, 100K, 1M} TX × 2 repeats = 24 runs
# Estimated wall time: ~45 minutes on a single Chameleon node
# ============================================================
set -euo pipefail

SSH_KEY="${SSH_KEY:-$HOME/.ssh/chameleon_qmemo}"
HOST="${CHAMELEON_HOST:-cc@129.114.108.20}"
REMOTE_DIR="${REMOTE_DIR:-~/qMEMO/blockchain}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCAL_RESULTS="$REPO_ROOT/benchmarks/results/honest_matrix_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOCAL_RESULTS"

RUN_ID="honest_matrix_$(date +%Y%m%d_%H%M%S)"

echo "====================================================="
echo " qMEMO Honest Comparison Matrix"
echo " Host      : $HOST"
echo " Run ID    : $RUN_ID"
echo " Local out : $LOCAL_RESULTS"
echo " max_txs_per_block: 100000 (uncapped)"
echo "====================================================="
echo ""

# Push latest local code to remote before running
echo "[push] Syncing blockchain/ to Chameleon..."
rsync -az --exclude='build/' --exclude='benchmark_results/' \
    -e "ssh -i $SSH_KEY -o StrictHostKeyChecking=no" \
    "$SCRIPT_DIR/" "$HOST:$REMOTE_DIR/"
echo "[push] Sync complete."
echo ""

# Run the matrix remotely
ssh -T -i "$SSH_KEY" -o ConnectTimeout=30 -o StrictHostKeyChecking=no "$HOST" bash -s -- \
    "$REMOTE_DIR" "$RUN_ID" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"
RUN_ID="$2"
eval "cd $REMOTE_DIR"

OUT_DIR="benchmark_results/$RUN_ID"
mkdir -p "$OUT_DIR"

# TX counts to sweep: small (uncapped diff visible), medium, large
TX_LIST="5000 100000 1000000"
REPEATS=2
# max_txs_per_block=100000 — no consensus cap for any scheme or TX count
MAX_BLOCK=100000

echo ""
echo "Remote node: $(hostname), $(nproc) cores"
echo "Compiler:    $(gcc --version | head -1)"
echo "Output dir:  $OUT_DIR"
echo ""

for SCHEME in 1 2 4 3; do
  case $SCHEME in
    1) SNAME="Ed25519" ;;
    2) SNAME="Falcon-512" ;;
    4) SNAME="ML-DSA-44" ;;
    3) SNAME="Hybrid" ;;
  esac

  echo "============================================"
  echo "  Building SIG_SCHEME=$SCHEME ($SNAME)"
  echo "============================================"
  make clean >/dev/null 2>&1
  make SIG_SCHEME="$SCHEME" OQS_ROOT="$HOME/qMEMO/liboqs_install" -j8 2>&1 | tail -3
  echo ""

  for TX in $TX_LIST; do
    for REP in $(seq 1 "$REPEATS"); do
      TAG="s${SCHEME}_tx${TX}_r${REP}"
      echo "[$(date +%H:%M:%S)] scheme=$SNAME tx=$TX rep=$REP ..."
      # Args: NUM_TX BLOCK_INTERVAL K NUM_FARMERS WARMUP MAX_TXS_PER_BLOCK BATCH THREADS
      ./benchmark.sh "$TX" 1000 16 1 0 "$MAX_BLOCK" 64 8 \
        > "$OUT_DIR/${TAG}.log" 2>&1 || true

      LATEST_CSV="$(ls -1t benchmark_results/benchmark_*.csv 2>/dev/null | head -1 || true)"
      if [ -n "$LATEST_CSV" ] && [ -f "$LATEST_CSV" ]; then
        cp "$LATEST_CSV" "$OUT_DIR/${TAG}.csv"
        TPS=$(grep '^end_to_end_tps,' "$OUT_DIR/${TAG}.csv" | cut -d, -f2)
        CR=$(grep  '^confirm_rate,'    "$OUT_DIR/${TAG}.csv" | cut -d, -f2)
        echo "    -> e2e_tps=$TPS  confirm_rate=${CR}%"
      else
        echo "    -> WARNING: no CSV produced"
      fi
      echo ""
    done
  done
done

# Generate SUMMARY.md
python3 - "$OUT_DIR" <<'PY'
import csv, glob, os, statistics, sys

out = sys.argv[1]
rows = []
for f in sorted(glob.glob(out + "/*.csv")):
    name = os.path.basename(f).replace(".csv", "")
    parts = name.split("_")
    if len(parts) != 3:
        continue
    s, tx_part, rep = parts
    tx = tx_part.replace("tx", "")
    vals = {}
    with open(f) as fh:
        reader = csv.reader(fh)
        next(reader, None)
        for row in reader:
            if len(row) >= 2:
                vals[row[0]] = row[1]
    tps   = float(vals.get("end_to_end_tps", 0) or 0)
    conf  = float(vals.get("confirm_rate", 0) or 0)
    errs  = int(float(vals.get("tx_errors", 0) or 0))
    blocks = int(float(vals.get("blocks_created", 0) or 0))
    avg_tx_blk = float(vals.get("avg_tx_per_block", 0) or 0)
    rows.append((s, tx, tps, conf, errs, blocks, avg_tx_blk))

scheme_names = {"s1": "Ed25519", "s2": "Falcon-512", "s4": "ML-DSA-44", "s3": "Hybrid"}
tx_vals = sorted(set(r[1] for r in rows), key=lambda x: int(x))

md = [
    "# Honest Comparison Matrix — SUMMARY",
    "",
    "> Config: max_txs_per_block=100000, k=16, 1 farmer, 1s block, 8 threads, batch=64",
    "> All processes on single Chameleon node (localhost ZMQ — no inter-node latency)",
    "",
    "| Scheme | TX | n | mean TPS | std | min | max | confirm |",
    "|--------|---:|--:|---------:|----:|----:|----:|:-------:|",
]

for s in ["s1", "s2", "s4", "s3"]:
    for tx in tx_vals:
        cell = [(r[2], r[3], r[4]) for r in rows if r[0] == s and r[1] == tx]
        if not cell:
            continue
        tps_list = [c[0] for c in cell]
        conf_mean = statistics.mean(c[1] for c in cell)
        n = len(tps_list)
        mean = statistics.mean(tps_list)
        std  = statistics.pstdev(tps_list) if n > 1 else 0.0
        mn, mx = min(tps_list), max(tps_list)
        sname = scheme_names.get(s, s)
        md.append(f"| {sname} | {int(tx):,} | {n} | {mean:,.2f} | {std:.2f} | {mn:,.2f} | {mx:,.2f} | {conf_mean:.1f}% |")

bad = [r for r in rows if not (r[2] > 0 and r[3] == 100.0 and r[4] == 0)]
md += [
    "",
    f"- total_runs: {len(rows)}",
    f"- bad_runs: {len(bad)}",
]
if bad:
    md.append("- bad_run_details:")
    for b in bad:
        md.append(f"  - {b}")

with open(os.path.join(out, "SUMMARY.md"), "w") as f:
    f.write("\n".join(md) + "\n")

print("\n" + "\n".join(md))
PY

echo ""
echo "===== DONE. Results in $OUT_DIR ====="
REMOTE

# Sync results back to local
echo ""
echo "[sync] Pulling results from Chameleon..."
rsync -az -e "ssh -i $SSH_KEY -o StrictHostKeyChecking=no" \
    "$HOST:$REMOTE_DIR/benchmark_results/$RUN_ID/" \
    "$LOCAL_RESULTS/"
echo "[sync] Done. Results at: $LOCAL_RESULTS"
echo ""
cat "$LOCAL_RESULTS/SUMMARY.md"
