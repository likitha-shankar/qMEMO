# qMEMO Chameleon Menu Runner Guide

This guide explains how to use:

- `blockchain/start_chameleon_auto_tx.sh`

for interactive benchmark orchestration and automated self-tests on Chameleon.

---

## Script Location

From repo root:

- `./blockchain/start_chameleon_auto_tx.sh`

---

## Basic Syntax

```bash
# Interactive menu mode
./blockchain/start_chameleon_auto_tx.sh

# Show CLI help
./blockchain/start_chameleon_auto_tx.sh --help

# Automated self-test (quick)
./blockchain/start_chameleon_auto_tx.sh --self-test quick

# Automated self-test (full)
./blockchain/start_chameleon_auto_tx.sh --self-test full
```

---

## Connection Defaults

If you do not override anything, the script uses:

- Host: `cc@129.114.108.20`
- SSH key: `~/.ssh/chameleon_qmemo`
- Remote dir: `~/qMEMO/blockchain`

You can override via environment variables:

```bash
CHAMELEON_HOST="cc@<your-host>" \
SSH_KEY="$HOME/.ssh/<your-key>" \
REMOTE_DIR="~/qMEMO/blockchain" \
./blockchain/start_chameleon_auto_tx.sh
```

---

## Interactive Menu Options

When started in interactive mode, the script shows:

1. Check connection  
2. Ensure remote dependencies (auto-install)  
3. Build a signature scheme on Chameleon  
4. Run single benchmark scenario  
5. Run full matrix (all schemes, repeats)  
6. Run hybrid migration story (`s1 -> s2 -> s4 -> s3`)  
7. Validate latest run  
8. Stop all remote processes  
9. Sync latest run to local  
10. Show latest remote summary  
0. Exit

---

## Signature Scheme Mapping

- `1` = Ed25519
- `2` = Falcon-512
- `3` = Hybrid
- `4` = ML-DSA-44

---

## Parameter Meaning (Single Scenario)

When using menu option `4`, you are asked for:

- `tx_count`: number of transactions to submit
- `block_interval`: seconds between blocks
- `k`: PoS k-parameter
- `farmers`: number of farmers/validators
- `warmup`: warmup blocks before measurement
- `max_txs`: max tx per block
- `batch`: wallet batch size
- `threads`: worker threads

Defaults are shown in the menu before execution.

---

## Self-Test Modes

### Quick

`--self-test quick` runs a lightweight pre-demo validation:

- connection + deps
- build one scheme
- one single-run smoke
- one small matrix run
- one small hybrid run
- validate/summary/sync/stop controls

### Full

`--self-test full` runs heavier validation:

- includes matrix over `100K + 1M`
- includes hybrid run at `100K`

Both modes:

- write per-step logs under:
  - `benchmarks/results/chameleon_menu_runs/self_test_<timestamp>/`
- capture generated run IDs
- audit resulting CSV/log files for failures
- print final `result,pass` or `result,fail`

---

## Output Locations

Remote output:

- `~/qMEMO/blockchain/benchmark_results/<run_id>/`

Local synced output:

- `benchmarks/results/chameleon_menu_runs/<run_id>/`

Self-test logs:

- `benchmarks/results/chameleon_menu_runs/self_test_<timestamp>/`

---

## Typical Workflows

### A) Fast sanity before demo

```bash
./blockchain/start_chameleon_auto_tx.sh --self-test quick
```

### B) Full confidence run

```bash
./blockchain/start_chameleon_auto_tx.sh --self-test full
```

### C) Manual interactive run

```bash
./blockchain/start_chameleon_auto_tx.sh
```

Then usually:

1. Option `2` (deps)
2. Option `4` (single benchmark) or `5` (matrix)
3. Option `7` (validate)
4. Option `10` (summary)
5. Option `9` (sync)

---

## Troubleshooting

- **SSH key not found**  
  Set `SSH_KEY` or place key at `~/.ssh/chameleon_qmemo`.

- **Wrong host/remote path**  
  Override `CHAMELEON_HOST` and `REMOTE_DIR`.

- **Need to stop stuck remote jobs**  
  Use menu option `8`.

- **Need a clean confidence check**  
  Run `--self-test quick` or `--self-test full`.

