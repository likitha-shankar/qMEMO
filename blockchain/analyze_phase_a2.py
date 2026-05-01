#!/usr/bin/env python3
"""
Phase A2 analysis: decompose per-block pipeline timing from block_diag + tx_diag + pool_fetches CSVs.
Usage: python3 analyze_phase_a2.py [block_diag.csv] [tx_diag.csv] [pool_fetches.csv]
"""
import csv
import sys
import statistics
from collections import defaultdict

def load_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    print(f"  Loaded {len(rows)} rows from {path}")
    return rows

def ns_to_ms(ns):
    return int(ns) / 1e6

def pct(val, total):
    return 100.0 * val / total if total > 0 else 0.0

def p50(lst):
    return statistics.median(lst) if lst else 0.0

def p95(lst):
    if not lst:
        return 0.0
    s = sorted(lst)
    idx = max(0, int(len(s) * 0.95) - 1)
    return s[idx]

def main():
    block_csv_path = sys.argv[1] if len(sys.argv) > 1 else "block_diag_122261.csv"
    tx_csv_path    = sys.argv[2] if len(sys.argv) > 2 else "tx_diag_122261.csv"
    pool_csv_path  = sys.argv[3] if len(sys.argv) > 3 else "pool_fetches_122252.csv"

    print("Loading CSVs...")
    blocks = load_csv(block_csv_path)
    txs    = load_csv(tx_csv_path)
    pools  = load_csv(pool_csv_path)

    # -------------------------------------------------------------------------
    # POOL FETCH ANALYSIS
    # -------------------------------------------------------------------------
    print("\n=== POOL FETCH ANALYSIS ===")
    scan_ms_list = [ns_to_ms(r['scan_duration_ns']) for r in pools]
    pack_ms_list = [ns_to_ms(r['pack_duration_ns'])  for r in pools]
    print("Rows: " + str(len(pools)))
    print("scan_duration  p50=" + format(p50(scan_ms_list), '.2f') + "ms"
          "  p95=" + format(p95(scan_ms_list), '.2f') + "ms"
          "  max=" + format(max(scan_ms_list), '.2f') + "ms")
    print("pack_duration  p50=" + format(p50(pack_ms_list), '.2f') + "ms"
          "  p95=" + format(p95(pack_ms_list), '.2f') + "ms"
          "  max=" + format(max(pack_ms_list), '.2f') + "ms")

    # -------------------------------------------------------------------------
    # GROUP tx_diag BY t2_ns (each unique t2 = one pool fetch / one block)
    # -------------------------------------------------------------------------
    block_txs_by_t2 = defaultdict(list)
    for tx in txs:
        block_txs_by_t2[tx['t2_ns']].append(tx)

    t2_keys_sorted = sorted(block_txs_by_t2.keys(), key=lambda x: int(x))

    stage_data = []
    for t2_key in t2_keys_sorted:
        group = block_txs_by_t2[t2_key]
        if len(group) < 100:
            continue   # skip warmup fragments
        t2     = int(group[0]['t2_ns'])
        t2_5   = int(group[0]['t2_5_ns'])
        t2_75  = int(group[0]['t2_75_ns'])
        t2_875 = int(group[0]['t2_875_ns'])
        t3_vals = [int(tx['t3_ns']) for tx in group if int(tx['t3_ns']) > 0]
        if not t3_vals:
            continue
        t3_last = max(t3_vals)
        stage_data.append({
            't2':     t2,
            't2_5':   t2_5,
            't2_75':  t2_75,
            't2_875': t2_875,
            't3_last': t3_last,
            'n_tx':   len(group),
        })

    print("\n=== PER-BLOCK STAGE DATA SUMMARY ===")
    print("  Groups (blocks) found in tx_diag: " + str(len(stage_data)))
    if not stage_data:
        print("  ERROR: No stage data found — check CSV column names")
        return

    unpack_ms    = [ns_to_ms(s['t2_5']   - s['t2'])     for s in stage_data]
    bal_prep_ms  = [ns_to_ms(s['t2_75']  - s['t2_5'])   for s in stage_data]
    get_bal_ms   = [ns_to_ms(s['t2_875'] - s['t2_75'])  for s in stage_data]
    verify_ms    = [ns_to_ms(s['t3_last']- s['t2_875']) for s in stage_data]

    # Full blocks from block_diag
    full_blocks = [b for b in blocks if int(b['block_txs']) >= 1000]
    sub_dur_ms  = [ns_to_ms(b['submission_duration_ns']) for b in full_blocks]
    ack_dur_ms  = [ns_to_ms(b['ack_duration_ns'])        for b in full_blocks]

    print("\n=== STAGE DURATIONS (per-block, n=" + str(len(stage_data)) + " blocks) ===")
    hdr = "  " + "Stage".ljust(32) + "p50(ms)".rjust(9) + "p95(ms)".rjust(9) + "max(ms)".rjust(9)
    sep = "  " + "-" * 59
    print(hdr)
    print(sep)

    def row(label, lst):
        return ("  " + label.ljust(32)
                + format(p50(lst), '>9.1f')
                + format(p95(lst), '>9.1f')
                + format(max(lst) if lst else 0, '>9.1f'))

    print(row("T2->T2_5   protobuf unpack",  unpack_ms))
    print(row("T2_5->T2_75  balance prep",   bal_prep_ms))
    print(row("T2_75->T2_875 GET_BAL RTT",   get_bal_ms))
    print(row("T2_875->T3  verify+seqbal",   verify_ms))
    if sub_dur_ms:
        print(row("T3_last->T4  finaliz gap", sub_dur_ms))
        print(row("T4->T5  ADD_BLOCK ACK",    ack_dur_ms))

    # Dark zone: T5[block N] -> T2[block N+1]
    t5_vals  = sorted([int(b['t5_ns']) for b in full_blocks])
    t2_block = sorted([s['t2'] for s in stage_data])
    dark_ms  = []
    for t5 in t5_vals:
        next_t2 = [t2 for t2 in t2_block if t2 > t5]
        if next_t2:
            dark_ms.append(ns_to_ms(min(next_t2) - t5))
    if dark_ms:
        print(row("T5->T2[next]  dark zone",  dark_ms))

    # -------------------------------------------------------------------------
    # BUDGET RECONCILIATION
    # -------------------------------------------------------------------------
    block_interval_ms = 1000.0
    p50_unpack    = p50(unpack_ms)
    p50_balprep   = p50(bal_prep_ms)
    p50_getbal    = p50(get_bal_ms)
    p50_verify    = p50(verify_ms)
    p50_sub       = p50(sub_dur_ms)  if sub_dur_ms  else 0.0
    p50_ack       = p50(ack_dur_ms)  if ack_dur_ms  else 0.0
    total_measured = p50_unpack + p50_balprep + p50_getbal + p50_verify + p50_sub + p50_ack
    dark_zone      = max(0.0, block_interval_ms - total_measured)

    print("\n=== BUDGET RECONCILIATION (p50, ~" + str(int(block_interval_ms)) + "ms block interval) ===")
    def brow(label, val):
        return ("  " + label.ljust(30)
                + format(val, '>8.1f') + " ms"
                + "  (" + format(pct(val, block_interval_ms), '>5.1f') + "%)")

    print(brow("protobuf unpack",      p50_unpack))
    print(brow("balance prep",         p50_balprep))
    print(brow("GET_BAL ZMQ RTT",      p50_getbal))
    print(brow("verify + seq bal",     p50_verify))
    print(brow("finalization gap",     p50_sub))
    print(brow("ADD_BLOCK ACK",        p50_ack))
    print("  " + "-" * 48)
    print(brow("MEASURED TOTAL",       total_measured))
    print(brow("DARK ZONE (est.)",     dark_zone))

    # -------------------------------------------------------------------------
    # BOTTLENECK VERDICT
    # -------------------------------------------------------------------------
    stages_ranked = [
        ("verify+seqbal",    p50_verify),
        ("GET_BAL ZMQ RTT",  p50_getbal),
        ("ADD_BLOCK ACK",    p50_ack),
        ("protobuf unpack",  p50_unpack),
        ("balance prep",     p50_balprep),
        ("finalization gap", p50_sub),
        ("dark zone",        dark_zone),
    ]
    stages_ranked.sort(key=lambda x: -x[1])

    print("\n=== BOTTLENECK VERDICT (ranked) ===")
    for label, val in stages_ranked:
        bar = "#" * int(pct(val, block_interval_ms) / 2)
        print("  " + label.ljust(22) + format(val, '>7.1f') + "ms"
              + "  " + format(pct(val, block_interval_ms), '>5.1f') + "%  " + bar)

    winner = stages_ranked[0]
    print("\n  PRIMARY BOTTLENECK: " + winner[0]
          + " at " + format(winner[1], '.1f') + "ms"
          + " (" + format(pct(winner[1], block_interval_ms), '.1f') + "% of block interval)")

if __name__ == '__main__':
    main()
