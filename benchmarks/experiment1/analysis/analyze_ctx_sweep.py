#!/usr/bin/env python3
"""
analyze_ctx_sweep.py — compare CTX_FRESH vs CTX_REUSE Ed25519 scaling.

Usage:
    python3 analyze_ctx_sweep.py <ctx_sweep_results_dir>

Reads *_summary.csv files whose names match ed25519_{fresh,reuse}_t{N}_summary.csv,
plots a throughput-vs-threads comparison, and prints a ratio table.
"""

import sys
import re
import csv
import math
import pathlib
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MPL = True
except ImportError:
    HAS_MPL = False


def load_results(results_dir):
    """Returns dict: (mode, threads) -> list of ops_per_sec_total floats."""
    data = defaultdict(list)
    pattern = re.compile(r"ed25519_(fresh|reuse)_t(\d+)_summary\.csv$")
    p = pathlib.Path(results_dir)
    for f in sorted(p.glob("ed25519_*_summary.csv")):
        m = pattern.match(f.name)
        if not m:
            continue
        mode, t = m.group(1), int(m.group(2))
        with open(f) as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                try:
                    ops = float(row["ops_per_sec_total"])
                    data[(mode, t)].append(ops)
                except (KeyError, ValueError):
                    pass
    return data


def stats(vals):
    n = len(vals)
    if n == 0:
        return 0.0, 0.0
    mean = sum(vals) / n
    var = sum((v - mean) ** 2 for v in vals) / n if n > 1 else 0.0
    return mean, math.sqrt(var)


def print_table(data, thread_counts):
    print(f"\n{'Threads':>8} {'FRESH (ops/s)':>16} {'REUSE (ops/s)':>16} {'REUSE/FRESH':>12}")
    print("-" * 56)
    for t in thread_counts:
        fresh_vals = data.get(("fresh", t), [])
        reuse_vals = data.get(("reuse", t), [])
        f_mean, f_sd = stats(fresh_vals)
        r_mean, r_sd = stats(reuse_vals)
        ratio = r_mean / f_mean if f_mean > 0 else float("nan")
        print(f"{t:>8} {f_mean:>13,.0f}±{f_sd/f_mean*100 if f_mean>0 else 0:.0f}%"
              f" {r_mean:>13,.0f}±{r_sd/r_mean*100 if r_mean>0 else 0:.0f}%"
              f" {ratio:>11.2f}x")


def plot(data, thread_counts, out_path):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    styles = {"fresh": ("C0", "-", "o", "CTX_FRESH (wallet path)"),
              "reuse": ("C1", "--", "s", "CTX_REUSE (lower bound)")}

    for mode, (color, ls, marker, label) in styles.items():
        xs, ys, errs = [], [], []
        for t in thread_counts:
            vals = data.get((mode, t), [])
            if vals:
                mean, sd = stats(vals)
                xs.append(t)
                ys.append(mean / 1e3)
                errs.append(sd / 1e3)
        if xs:
            ax1.errorbar(xs, ys, yerr=errs, label=label,
                         color=color, linestyle=ls, marker=marker,
                         capsize=4, linewidth=2)

    ax1.set_xlabel("Threads")
    ax1.set_ylabel("Throughput (kops/s)")
    ax1.set_title("Ed25519 CTX_FRESH vs CTX_REUSE — Throughput")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # Ratio plot
    ratios_x, ratios_y = [], []
    for t in thread_counts:
        f_vals = data.get(("fresh", t), [])
        r_vals = data.get(("reuse", t), [])
        if f_vals and r_vals:
            f_mean, _ = stats(f_vals)
            r_mean, _ = stats(r_vals)
            if f_mean > 0:
                ratios_x.append(t)
                ratios_y.append(r_mean / f_mean)

    ax2.plot(ratios_x, ratios_y, color="C2", marker="D", linewidth=2)
    ax2.axhline(1.0, color="gray", linestyle=":", linewidth=1, label="parity")
    ax2.axhline(1.5, color="red", linestyle="--", linewidth=1, alpha=0.5,
                label="1.5× threshold")
    ax2.set_xlabel("Threads")
    ax2.set_ylabel("REUSE / FRESH throughput ratio")
    ax2.set_title("Allocator overhead ratio")
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    print(f"\nFigure saved: {out_path}")


def verdict(data, thread_counts):
    lines = []
    t96_fresh = data.get(("fresh", 96), [])
    t96_reuse = data.get(("reuse", 96), [])
    t1_fresh  = data.get(("fresh", 1), [])
    t1_reuse  = data.get(("reuse", 1), [])

    if not (t96_fresh and t96_reuse):
        return ["INCOMPLETE: missing t=96 data for one or both modes."]

    r96 = stats(t96_reuse)[0] / stats(t96_fresh)[0] if stats(t96_fresh)[0] > 0 else float("nan")
    r1  = stats(t1_reuse)[0]  / stats(t1_fresh)[0]  if t1_fresh and stats(t1_fresh)[0] > 0 else float("nan")

    lines.append(f"t=1  REUSE/FRESH = {r1:.2f}x")
    lines.append(f"t=96 REUSE/FRESH = {r96:.2f}x")

    if r96 >= 1.5:
        lines.append("VERDICT: ALLOCATOR CONFIRMED — EVP_MD_CTX_new/free per iteration")
        lines.append("  is the primary cause of Ed25519's scaling cliff.")
        lines.append("  RECOMMENDATION: add EVP_MD_CTX pooling to qMEMO wallet_sign().")
    elif r96 >= 1.2:
        lines.append("VERDICT: ALLOCATOR PARTIAL — moderate overhead at high concurrency.")
        lines.append("  Other factors (L1/L2 cache conflicts, NUMA) also contribute.")
    else:
        lines.append("VERDICT: ALLOCATOR REFUTED — context allocation is not the bottleneck.")
        lines.append("  Look elsewhere (NUMA, kernel scheduler, cache topology).")

    return lines


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <ctx_sweep_results_dir>")
        sys.exit(1)

    results_dir = sys.argv[1]
    data = load_results(results_dir)

    if not data:
        print(f"ERROR: no matching summary CSVs found in {results_dir}")
        sys.exit(1)

    thread_counts = sorted({t for (_, t) in data.keys()})
    print(f"Found data for modes: {sorted({m for (m, _) in data.keys()})}")
    print(f"Thread counts: {thread_counts}")

    print_table(data, thread_counts)

    v = verdict(data, thread_counts)
    print("\n=== VERDICT ===")
    for line in v:
        print(line)

    verdict_path = pathlib.Path(results_dir) / "ctx_verdict.txt"
    verdict_path.write_text("\n".join(v) + "\n")
    print(f"\nVerdict saved: {verdict_path}")

    if HAS_MPL:
        fig_path = pathlib.Path(results_dir) / "ctx_comparison.pdf"
        plot(data, thread_counts, str(fig_path))
    else:
        print("matplotlib not available — skipping plot")


if __name__ == "__main__":
    main()
