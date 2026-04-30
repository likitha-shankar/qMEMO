#!/bin/bash

# Interactive Chameleon control panel for qMEMO benchmarks.
# Everything runs on Chameleon; this script orchestrates remotely via SSH.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SCRIPT_PATH="$SCRIPT_DIR/$(basename "${BASH_SOURCE[0]}")"
LOCAL_RESULTS_ROOT="$REPO_ROOT/benchmarks/results/chameleon_menu_runs"
mkdir -p "$LOCAL_RESULTS_ROOT"

DEFAULT_HOST="cc@129.114.108.20"
DEFAULT_SSH_KEY="$HOME/.ssh/chameleon_qmemo"
DEFAULT_REMOTE_DIR="~/qMEMO/blockchain"

HOST="${CHAMELEON_HOST:-$DEFAULT_HOST}"
SSH_KEY="${SSH_KEY:-$DEFAULT_SSH_KEY}"
REMOTE_DIR="${REMOTE_DIR:-$DEFAULT_REMOTE_DIR}"

LAST_SCHEME=2
LAST_TX=100000
LAST_BLOCK_INTERVAL=1
LAST_K=16
LAST_FARMERS=1
LAST_WARMUP=0
LAST_MAX_TXS=10000
LAST_BATCH=64
LAST_THREADS=8

LAST_RUN_ID=""

print_header() {
    echo ""
    echo "======================================================================"
    echo "  $1"
    echo "======================================================================"
}

confirm() {
    local prompt="$1"
    local ans
    read -r -p "$prompt [y/N]: " ans
    [[ "$ans" =~ ^[Yy]$ ]]
}

ssh_base_cmd() {
    if [ ! -f "$SSH_KEY" ]; then
        echo "ERROR: SSH key not found: $SSH_KEY"
        exit 1
    fi
    ssh -n -i "$SSH_KEY" -o ConnectTimeout=10 "$HOST" "$@"
}

remote_script() {
    # Usage: remote_script arg1 arg2 ... <<'REMOTE'
    #        ...remote bash...
    #        REMOTE
    ssh -T -i "$SSH_KEY" -o ConnectTimeout=10 "$HOST" bash -s -- "$@"
}

check_connection() {
    print_header "Connection Check"
    echo "Host      : $HOST"
    echo "SSH key   : $SSH_KEY"
    echo "Remote dir: $REMOTE_DIR"
    ssh_base_cmd "hostname && whoami && pwd"
}

ensure_remote_deps() {
    print_header "Ensuring Remote Dependencies"
    remote_script "$REMOTE_DIR" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"
eval "cd $REMOTE_DIR"

need_install=0
command -v gcc >/dev/null 2>&1 || need_install=1
command -v make >/dev/null 2>&1 || need_install=1
command -v protoc-c >/dev/null 2>&1 || need_install=1
command -v jq >/dev/null 2>&1 || need_install=1
command -v python3 >/dev/null 2>&1 || need_install=1

if [ "$need_install" -eq 1 ] || ! dpkg -s libprotobuf-c-dev >/dev/null 2>&1 || ! dpkg -s libzmq3-dev >/dev/null 2>&1; then
  echo "[remote] Installing missing apt dependencies..."
  sudo apt-get update -qq
  sudo apt-get install -y build-essential gcc make pkg-config libssl-dev protobuf-c-compiler libprotobuf-c-dev libzmq3-dev jq python3 rsync
fi

if [ ! -f "$HOME/qMEMO/liboqs_install/include/oqs/oqs.h" ] || [ ! -d "$HOME/qMEMO/liboqs_install/lib" ]; then
  echo "[remote] liboqs_install missing, running scripts/install_liboqs.sh..."
  cd "$HOME/qMEMO"
  bash scripts/install_liboqs.sh
  eval "cd $REMOTE_DIR"
fi

echo "[remote] Dependencies OK"
REMOTE
}

prompt_with_default() {
    local var_name="$1"
    local prompt="$2"
    local default="$3"
    local input
    read -r -p "$prompt [$default]: " input
    if [ -z "$input" ]; then
        printf -v "$var_name" "%s" "$default"
    else
        printf -v "$var_name" "%s" "$input"
    fi
}

choose_scheme() {
    echo ""
    echo "Signature Scheme:"
    echo "  1) Ed25519 (classical)"
    echo "  2) Falcon-512 (PQC)"
    echo "  3) Hybrid (mixed signatures)"
    echo "  4) ML-DSA-44 (PQC)"
    prompt_with_default LAST_SCHEME "Choose scheme number" "$LAST_SCHEME"
}

choose_tx_profile() {
    echo ""
    echo "Transaction Count Profile:"
    echo "  1) 1,000 (quick smoke)"
    echo "  2) 100,000 (medium)"
    echo "  3) 1,000,000 (production)"
    echo "  4) custom"
    local p
    prompt_with_default p "Choose profile number" "2"
    case "$p" in
        1) LAST_TX=1000 ;;
        2) LAST_TX=100000 ;;
        3) LAST_TX=1000000 ;;
        4)
            prompt_with_default LAST_TX "Enter custom tx count (e.g., 50000)" "$LAST_TX"
            ;;
        *)
            echo "Invalid profile. Keeping previous tx count: $LAST_TX"
            ;;
    esac
}

choose_params() {
    echo ""
    echo "Enter benchmark parameters (press Enter to keep defaults):"
    echo "  Example: tx=1000000, block_interval=1, k=16, farmers=1, warmup=0, max_txs=10000, batch=64, threads=8"
    choose_tx_profile
    prompt_with_default LAST_BLOCK_INTERVAL "Block interval seconds" "$LAST_BLOCK_INTERVAL"
    prompt_with_default LAST_K "k parameter" "$LAST_K"
    prompt_with_default LAST_FARMERS "Number of farmers" "$LAST_FARMERS"
    prompt_with_default LAST_WARMUP "Warmup blocks" "$LAST_WARMUP"
    prompt_with_default LAST_MAX_TXS "Max TXs per block" "$LAST_MAX_TXS"
    prompt_with_default LAST_BATCH "Batch size" "$LAST_BATCH"
    prompt_with_default LAST_THREADS "Threads per farmer" "$LAST_THREADS"
}

show_plan() {
    echo ""
    echo "Run plan:"
    echo "  SIG_SCHEME      : $LAST_SCHEME"
    echo "  tx_count        : $LAST_TX"
    echo "  block_interval  : $LAST_BLOCK_INTERVAL"
    echo "  k               : $LAST_K"
    echo "  farmers         : $LAST_FARMERS"
    echo "  warmup          : $LAST_WARMUP"
    echo "  max_txs/block   : $LAST_MAX_TXS"
    echo "  batch_size      : $LAST_BATCH"
    echo "  threads         : $LAST_THREADS"
}

run_single_case() {
    ensure_remote_deps
    choose_scheme
    choose_params
    show_plan

    if [ "$LAST_TX" -ge 1000000 ]; then
        confirm "This is a heavy run (1M+ TX). Continue?" || return 0
    else
        confirm "Run this benchmark now?" || return 0
    fi

    print_header "Running Single Scenario on Chameleon"
    local run_id
    run_id="interactive_s${LAST_SCHEME}_tx${LAST_TX}_$(date +%Y%m%d_%H%M%S)"
    LAST_RUN_ID="$run_id"

    remote_script "$REMOTE_DIR" "$LAST_SCHEME" "$LAST_TX" "$LAST_BLOCK_INTERVAL" "$LAST_K" "$LAST_FARMERS" "$LAST_WARMUP" "$LAST_MAX_TXS" "$LAST_BATCH" "$LAST_THREADS" "$run_id" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"; SCHEME="$2"; TX="$3"; BI="$4"; K="$5"; F="$6"; W="$7"; MAXTX="$8"; BATCH="$9"; THREADS="${10}"; RUN_ID="${11}"
eval "cd $REMOTE_DIR"

OUT_DIR="benchmark_results/$RUN_ID"
mkdir -p "$OUT_DIR"

make clean >/dev/null
make SIG_SCHEME="$SCHEME" OQS_ROOT="$HOME/qMEMO/liboqs_install" -j4 >/dev/null
./benchmark.sh "$TX" "$BI" "$K" "$F" "$W" "$MAXTX" "$BATCH" "$THREADS" > "$OUT_DIR/run.log" 2>&1

LATEST_CSV="$(ls -1t benchmark_results/benchmark_*.csv | head -1)"
cp "$LATEST_CSV" "$OUT_DIR/result.csv"

python3 - "$OUT_DIR/result.csv" "$OUT_DIR/SUMMARY.md" <<'PY'
import csv,sys
csv_path, md_path = sys.argv[1], sys.argv[2]
vals={}
with open(csv_path) as f:
    r=csv.reader(f); next(r,None)
    for m,v,*_ in r: vals[m]=v
lines=[
    "# Single Run Summary",
    "",
    f"- tx_submitted: {vals.get('tx_submitted','')}",
    f"- tx_confirmed: {vals.get('tx_confirmed','')}",
    f"- tx_errors: {vals.get('tx_errors','')}",
    f"- confirm_rate: {vals.get('confirm_rate','')}%",
    f"- end_to_end_tps: {vals.get('end_to_end_tps','')}",
    f"- total_e2e_time: {vals.get('total_e2e_time','')} sec",
]
with open(md_path, "w") as f:
    f.write("\n".join(lines) + "\n")
print("RESULT_TABLE")
print("metric,value")
for k in ["tx_submitted","tx_confirmed","tx_errors","confirm_rate","end_to_end_tps","total_e2e_time","blocks_created"]:
    print(f"{k},{vals.get(k,'')}")
PY
REMOTE

    sync_run_to_local "$run_id"
    print_header "Single Scenario Completed"
    echo "Run ID: $run_id"
}

run_full_matrix() {
    ensure_remote_deps
    local repeats tx_choice tx_list tx_list_arg

    echo ""
    echo "Choose tx set:"
    echo "  1) 100K + 1M (recommended)"
    echo "  2) 1K + 100K + 1M"
    echo "  3) custom single tx"
    prompt_with_default tx_choice "Selection" "1"
    case "$tx_choice" in
        1) tx_list="100000 1000000" ;;
        2) tx_list="1000 100000 1000000" ;;
        3)
            prompt_with_default LAST_TX "Custom tx count" "$LAST_TX"
            tx_list="$LAST_TX"
            ;;
        *) tx_list="100000 1000000" ;;
    esac
    prompt_with_default repeats "Repeats per case" "3"

    echo ""
    echo "This will run schemes: 1 2 4 3"
    echo "tx set: $tx_list"
    echo "repeats: $repeats"
    confirm "Start full matrix run?" || return 0

    local run_id
    run_id="matrix_$(date +%Y%m%d_%H%M%S)"
    LAST_RUN_ID="$run_id"

    tx_list_arg="${tx_list// /,}"
    remote_script "$REMOTE_DIR" "$run_id" "$repeats" "$tx_list_arg" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"; RUN_ID="$2"; REPEATS="$3"; TX_LIST="$4"
eval "cd $REMOTE_DIR"
OUT_DIR="benchmark_results/$RUN_ID"
mkdir -p "$OUT_DIR"

for SCHEME in 1 2 4 3; do
  make clean >/dev/null
  make SIG_SCHEME="$SCHEME" OQS_ROOT="$HOME/qMEMO/liboqs_install" -j4 >/dev/null
  for TX in ${TX_LIST//,/ }; do
    for REP in $(seq 1 "$REPEATS"); do
      echo "=== scheme=$SCHEME tx=$TX rep=$REP ==="
      ./benchmark.sh "$TX" 1 16 1 0 100000 64 8 > "$OUT_DIR/s${SCHEME}_tx${TX}_r${REP}.log" 2>&1
      LATEST_CSV="$(ls -1t benchmark_results/benchmark_*.csv | head -1)"
      cp "$LATEST_CSV" "$OUT_DIR/s${SCHEME}_tx${TX}_r${REP}.csv"
    done
  done
done

python3 - "$OUT_DIR" <<'PY'
import csv,glob,os,statistics,sys
out=sys.argv[1]
rows=[]
for f in sorted(glob.glob(out+"/*.csv")):
    n=os.path.basename(f).replace(".csv","")
    p=n.split("_")
    if len(p)!=3: continue
    s,tx,_=p
    vals={}
    with open(f) as fh:
        r=csv.reader(fh); next(r,None)
        for m,v,*_ in r: vals[m]=v
    rows.append((s,tx,float(vals.get("end_to_end_tps",0)),float(vals.get("confirm_rate",0)),int(float(vals.get("tx_errors",0)))))

txs=sorted(set(r[1] for r in rows))
md=["# Matrix Summary","","| Scheme | TX | n | mean TPS | std | min | max | mean confirm |","|---|---:|---:|---:|---:|---:|---:|---:|"]
print("RESULT_TABLE")
print("scheme,tx,n,mean_tps,std_tps,min_tps,max_tps,mean_confirm")
for s in ["s1","s2","s4","s3"]:
    for tx in txs:
        vals=[r[2] for r in rows if r[0]==s and r[1]==tx]
        conf=[r[3] for r in rows if r[0]==s and r[1]==tx]
        if not vals: continue
        mean=statistics.mean(vals)
        std=statistics.pstdev(vals) if len(vals)>1 else 0.0
        mn,mx=min(vals),max(vals)
        mc=statistics.mean(conf)
        print(f"{s},{tx},{len(vals)},{mean:.2f},{std:.2f},{mn:.2f},{mx:.2f},{mc:.1f}")
        md.append(f"| {s} | {tx.replace('tx','')} | {len(vals)} | {mean:.2f} | {std:.2f} | {mn:.2f} | {mx:.2f} | {mc:.1f}% |")

bad=[r for r in rows if not (r[2]>0 and r[3]==100.0 and r[4]==0)]
md += ["",f"- total_runs: {len(rows)}",f"- bad_runs: {len(bad)}"]
with open(os.path.join(out,"SUMMARY.md"),"w") as f:
    f.write("\n".join(md)+"\n")
PY
REMOTE

    sync_run_to_local "$run_id"
    print_header "Matrix Completed"
    echo "Run ID: $run_id"
}

run_hybrid_story() {
    ensure_remote_deps
    choose_tx_profile
    echo ""
    echo "Hybrid migration story run:"
    echo "  1) Ed25519  -> tx=$LAST_TX"
    echo "  2) Falcon   -> tx=$LAST_TX"
    echo "  3) ML-DSA   -> tx=$LAST_TX"
    echo "  4) Hybrid   -> tx=$LAST_TX"
    confirm "Run hybrid migration story now?" || return 0

    local run_id
    run_id="hybrid_story_tx${LAST_TX}_$(date +%Y%m%d_%H%M%S)"
    LAST_RUN_ID="$run_id"

    remote_script "$REMOTE_DIR" "$run_id" "$LAST_TX" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"; RUN_ID="$2"; TX="$3"
eval "cd $REMOTE_DIR"
OUT_DIR="benchmark_results/$RUN_ID"
mkdir -p "$OUT_DIR"

for SCHEME in 1 2 4 3; do
  make clean >/dev/null
  make SIG_SCHEME="$SCHEME" OQS_ROOT="$HOME/qMEMO/liboqs_install" -j4 >/dev/null
  ./benchmark.sh "$TX" 1 16 1 0 100000 64 8 > "$OUT_DIR/s${SCHEME}_tx${TX}.log" 2>&1
  LATEST_CSV="$(ls -1t benchmark_results/benchmark_*.csv | head -1)"
  cp "$LATEST_CSV" "$OUT_DIR/s${SCHEME}_tx${TX}.csv"
done

python3 - "$OUT_DIR" "$TX" <<'PY'
import csv,glob,os,sys
out,tx = sys.argv[1], sys.argv[2]
print("RESULT_TABLE")
print("scheme,tx,e2e_tps,confirm_rate,errors")
rows=[]
for s in ["s1","s2","s4","s3"]:
    f=os.path.join(out,f"{s}_tx{tx}.csv")
    vals={}
    with open(f) as fh:
        r=csv.reader(fh); next(r,None)
        for m,v,*_ in r: vals[m]=v
    rows.append((s, vals.get("end_to_end_tps",""), vals.get("confirm_rate",""), vals.get("tx_errors","")))
    print(f"{s},{tx},{vals.get('end_to_end_tps','')},{vals.get('confirm_rate','')},{vals.get('tx_errors','')}")
with open(os.path.join(out,"SUMMARY.md"),"w") as f:
    f.write("# Hybrid Story Summary\n\n")
    for r in rows:
        f.write(f"- {r[0]}: tps={r[1]}, confirm={r[2]}%, errors={r[3]}\n")
PY
REMOTE

    sync_run_to_local "$run_id"
    print_header "Hybrid Story Completed"
    echo "Run ID: $run_id"
}

validate_latest() {
    print_header "Validate Latest Remote Run"
    remote_script "$REMOTE_DIR" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"
eval "cd $REMOTE_DIR"
LATEST_DIR="$(ls -dt benchmark_results/*/ 2>/dev/null | head -1 || true)"
if [ -z "$LATEST_DIR" ]; then
  echo "No run directories found under benchmark_results/"
  exit 0
fi
echo "Latest run dir: $LATEST_DIR"
python3 - "$LATEST_DIR" <<'PY'
import csv,glob,os,sys
base=sys.argv[1]
files=sorted(glob.glob(base+"/*.csv"))
print("csv_count",len(files))
bad=[]
for f in files:
    vals={}
    with open(f) as fh:
        r=csv.reader(fh); next(r,None)
        for m,v,*_ in r: vals[m]=v
    tps=float(vals.get("end_to_end_tps",0))
    conf=float(vals.get("confirm_rate",0))
    err=int(float(vals.get("tx_errors",0)))
    if not (tps>0 and conf==100.0 and err==0):
        bad.append((os.path.basename(f),tps,conf,err))
print("bad_runs",len(bad))
for b in bad[:10]:
    print("BAD",b)
PY
REMOTE
}

stop_all_remote() {
    print_header "Stopping Remote Benchmark/Blockchain Processes"
    remote_script "$REMOTE_DIR" <<'REMOTE'
set -euo pipefail
REMOTE_DIR="$1"
eval "cd $REMOTE_DIR"

# Use exact process names to avoid accidentally killing the control shell.
pkill -x blockchain >/dev/null 2>&1 || true
pkill -x metronome >/dev/null 2>&1 || true
pkill -x pool >/dev/null 2>&1 || true
pkill -x validator >/dev/null 2>&1 || true
pkill -x wallet >/dev/null 2>&1 || true
pkill -x benchmark >/dev/null 2>&1 || true
pkill -f '/benchmark.sh' >/dev/null 2>&1 || true

sleep 1
echo "Remote processes stopped (if any were running)."
REMOTE
}

sync_run_to_local() {
    local run_id="$1"
    if [ -z "$run_id" ]; then
        echo "No run id provided for sync."
        return 1
    fi
    local local_dir="$LOCAL_RESULTS_ROOT/$run_id"
    mkdir -p "$local_dir"
    print_header "Syncing Results to Local"
    echo "Remote: $HOST:$REMOTE_DIR/benchmark_results/$run_id/"
    echo "Local : $local_dir"
    rsync -az -e "ssh -i \"$SSH_KEY\"" "$HOST:$REMOTE_DIR/benchmark_results/$run_id/" "$local_dir/"
    echo "Sync complete."
}

sync_latest() {
    print_header "Sync Latest Remote Run"
    local latest
    latest="$(ssh_base_cmd "cd $REMOTE_DIR && ls -dt benchmark_results/*/ 2>/dev/null | head -1 | xargs -I{} basename {}" | tr -d '\r' | tail -n 1)"
    if [ -z "$latest" ]; then
        echo "No remote run directories found."
        return 0
    fi
    LAST_RUN_ID="$latest"
    sync_run_to_local "$latest"
}

show_latest_summary() {
    print_header "Latest Remote Summary"
    ssh_base_cmd "cd $REMOTE_DIR; latest=\$(ls -dt benchmark_results/*/ 2>/dev/null | head -1 || true); if [ -z \"\$latest\" ]; then echo 'No run directory found'; exit 0; fi; echo \"Run: \$latest\"; if [ -f \"\$latest/SUMMARY.md\" ]; then sed -n '1,120p' \"\$latest/SUMMARY.md\"; else echo 'No SUMMARY.md in latest run'; fi"
}

show_menu() {
    print_header "qMEMO Chameleon Interactive Runner"
    echo "Default host   : $HOST"
    echo "Default key    : $SSH_KEY"
    echo "Default remote : $REMOTE_DIR"
    echo ""
    echo "1) Check connection"
    echo "2) Ensure remote dependencies (auto-install)"
    echo "3) Build a signature scheme on Chameleon"
    echo "4) Run single benchmark scenario"
    echo "5) Run full matrix (all schemes, repeats)"
    echo "6) Run hybrid migration story (s1->s2->s4->s3)"
    echo "7) Validate latest run"
    echo "8) Stop all remote processes"
    echo "9) Sync latest run to local"
    echo "10) Show latest remote summary"
    echo "0) Exit"
}

self_test_run_step() {
    local step_name="$1"
    local step_input="$2"
    local step_log="$3"
    local run_id

    echo "[self-test] Running: $step_name"
    if printf "%b" "$step_input" | bash "$SCRIPT_PATH" >"$step_log" 2>&1; then
        echo "[self-test] PASS: $step_name"
    else
        echo "[self-test] FAIL: $step_name"
        echo "[self-test] See log: $step_log"
        return 1
    fi

    run_id="$(python3 - "$step_log" <<'PY'
import re,sys
text=open(sys.argv[1], errors='ignore').read()
ids=re.findall(r"Run ID:\s*([^\s]+)", text)
print(ids[-1] if ids else "")
PY
)"
    if [ -n "$run_id" ]; then
        SELF_TEST_RUN_IDS+=("$run_id")
    fi
}

run_self_test() {
    local profile="${1:-quick}"
    local ts log_root
    ts="$(date +%Y%m%d_%H%M%S)"
    log_root="$LOCAL_RESULTS_ROOT/self_test_$ts"
    mkdir -p "$log_root"
    SELF_TEST_RUN_IDS=()

    print_header "qMEMO Self-Test ($profile)"
    echo "Logs: $log_root"

    self_test_run_step "connection + deps" $'1\n2\n0\n' "$log_root/01_connection_deps.log"
    self_test_run_step "build scheme 2" $'3\n2\ny\n0\n' "$log_root/02_build_s2.log"
    self_test_run_step "single run (s1, 1k)" $'4\n1\n1\n\n\n\n\n\n\n\ny\n0\n' "$log_root/03_single_s1_1k.log"

    if [ "$profile" = "full" ]; then
        self_test_run_step "matrix run (100k + 1m, r1)" $'5\n1\n1\ny\n0\n' "$log_root/04_matrix_full.log"
        self_test_run_step "hybrid run (100k)" $'6\n2\ny\n0\n' "$log_root/05_hybrid_100k.log"
    else
        self_test_run_step "matrix run (custom 1k, r1)" $'5\n3\n1000\n1\ny\n0\n' "$log_root/04_matrix_quick.log"
        self_test_run_step "hybrid run (1k)" $'6\n1\ny\n0\n' "$log_root/05_hybrid_1k.log"
    fi

    self_test_run_step "validate + summary + sync + stop" $'7\n10\n9\n8\ny\n0\n' "$log_root/06_controls.log"

    python3 - "$LOCAL_RESULTS_ROOT" "$log_root" "${SELF_TEST_RUN_IDS[@]}" <<'PY'
import csv,glob,os,re,sys
local_root=sys.argv[1]
log_root=sys.argv[2]
run_ids=sys.argv[3:]
run_ids=list(dict.fromkeys([r for r in run_ids if r]))  # de-dupe, preserve order
if not run_ids:
    print("SELF_TEST_AUDIT")
    print("result,fail,no_run_ids_captured")
    sys.exit(1)

fatal_patterns=[
    re.compile(r"segmentation fault",re.I),
    re.compile(r"core dumped",re.I),
    re.compile(r"make:\s*\*\*\*",re.I),
    re.compile(r"build failed",re.I),
    re.compile(r"command not found",re.I),
    re.compile(r"no such file or directory",re.I),
]
issues=[]
rows=[]
for rid in run_ids:
    d=os.path.join(local_root,rid)
    if not os.path.isdir(d):
        issues.append(f"missing_run_dir:{rid}")
        continue
    csvs=sorted(glob.glob(os.path.join(d,"*.csv")))
    logs=sorted(glob.glob(os.path.join(d,"*.log")))
    if not csvs: issues.append(f"no_csv:{rid}")
    if not logs: issues.append(f"no_log:{rid}")
    for c in csvs:
        vals={}
        with open(c) as fh:
            r=csv.reader(fh); next(r,None)
            for row in r:
                if len(row)>=2: vals[row[0]]=row[1]
        tps=float(vals.get("end_to_end_tps",0) or 0)
        conf=float(vals.get("confirm_rate",0) or 0)
        err=int(float(vals.get("tx_errors",0) or 0))
        if tps<=0: issues.append(f"bad_tps:{rid}:{os.path.basename(c)}:{tps}")
        if conf!=100.0: issues.append(f"bad_confirm:{rid}:{os.path.basename(c)}:{conf}")
        if err!=0: issues.append(f"tx_errors:{rid}:{os.path.basename(c)}:{err}")
    for lg in logs:
        txt=open(lg,errors='ignore').read()
        for p in fatal_patterns:
            if p.search(txt):
                issues.append(f"log_error:{rid}:{os.path.basename(lg)}:{p.pattern}")
                break
    rows.append((rid,len(csvs),len(logs)))

print("SELF_TEST_AUDIT")
print("run_id,csv_count,log_count")
for r in rows:
    print(",".join(map(str,r)))
print("log_dir",log_root)
if issues:
    print("result,fail")
    for i in issues[:50]:
        print("issue",i)
    sys.exit(1)
print("result,pass")
PY

    print_header "Self-Test Complete"
    echo "Profile : $profile"
    echo "Run IDs : ${SELF_TEST_RUN_IDS[*]}"
    echo "Logs    : $log_root"
}

build_scheme_only() {
    ensure_remote_deps
    choose_scheme
    confirm "Build SIG_SCHEME=$LAST_SCHEME on Chameleon now?" || return 0
    ssh_base_cmd "cd $REMOTE_DIR && make clean >/dev/null && make SIG_SCHEME=$LAST_SCHEME OQS_ROOT=~/qMEMO/liboqs_install -j4"
}

main_loop() {
    while true; do
        show_menu
        local choice
        if ! read -r -p "Choose an option: " choice; then
            echo "Input closed. Bye."
            break
        fi
        case "$choice" in
            1) check_connection ;;
            2) ensure_remote_deps ;;
            3) build_scheme_only ;;
            4) run_single_case ;;
            5) run_full_matrix ;;
            6) run_hybrid_story ;;
            7) validate_latest ;;
            8) confirm "Stop all remote benchmark/blockchain processes?" && stop_all_remote ;;
            9) sync_latest ;;
            10) show_latest_summary ;;
            0) echo "Bye."; break ;;
            *) echo "Invalid option." ;;
        esac
    done
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    cat <<'USAGE'
Usage:
  ./start_chameleon_auto_tx.sh
  ./start_chameleon_auto_tx.sh --self-test [quick|full]

Notes:
  - quick: lightweight pre-demo self-checks (default)
  - full : includes heavier matrix/hybrid scenarios
USAGE
    exit 0
fi

if [ "${1:-}" = "--self-test" ]; then
    run_self_test "${2:-quick}"
    exit $?
fi

main_loop
