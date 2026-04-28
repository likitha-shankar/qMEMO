#!/usr/bin/env python3
"""
analyze_spotcheck.py

Reads the perf_spotcheck output directory and produces three diagnostic
plots plus a one-page text verdict on the three claims from Experiment 1.

Usage:
    python3 analyze_spotcheck.py results/perf_spotcheck_20260428_120000/

Outputs land in the same directory:
    cdf_bimodality.pdf        ML-DSA vs Falcon CDF at 1M iterations
    ed25519_stalls.pdf        stalls_l3_miss / cycles vs thread count
    cross_algo_stalls.pdf     stalls breakdown across algos at threads=96
    verdict.txt               human-readable summary of which claims hold
"""

import sys
import os
import csv
import re
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt

ALGO_COLORS = {
    "ed25519":    "#1f77b4",  # blue
    "falcon512":  "#ff7f0e",  # orange
    "dilithium2": "#2ca02c",  # green
}

# ---------------------------------------------------------------------
# perf -x , output parser
#
# perf stat -x , output looks like:
#   <count>,<unit>,<event>,<runtime>,<runtime_pct>,...
# We only need event name and count.
# ---------------------------------------------------------------------

def parse_perf_csv(path):
    """Return dict {event_name: count_int} from a perf -x , file."""
    counts = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            count_str = parts[0].strip()
            event = parts[2].strip()
            if count_str in ("<not counted>", "<not supported>", ""):
                continue
            try:
                count = int(count_str.replace(".", ""))  # locale safety
            except ValueError:
                try:
                    count = int(float(count_str))
                except ValueError:
                    continue
            counts[event] = count
    return counts


def load_latencies(path):
    """Load a bench_sign latencies.csv into a numpy array of nanoseconds.
    Schema:  algo,threads,cores_str,run_id,thread_id,iteration,latency_ns
    """
    lats = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                lats.append(int(row["latency_ns"]))
            except (KeyError, ValueError):
                continue
    return np.array(lats, dtype=np.int64)


# ---------------------------------------------------------------------
# Plot 1: bimodality check
# ---------------------------------------------------------------------

def plot_bimodality(out_dir):
    dilithium_path = out_dir / "dilithium2_bimodal_check_latencies.csv"
    falcon_path = out_dir / "falcon_tail_check_latencies.csv"

    if not dilithium_path.exists() or not falcon_path.exists():
        print(f"  [skip] bimodality plot: missing latency files")
        return None

    dlat = load_latencies(dilithium_path) / 1000.0  # ns -> us
    flat = load_latencies(falcon_path) / 1000.0

    fig, axes = plt.subplots(1, 2, figsize=(11, 4), sharey=True)

    for ax, lat, name, color in [
        (axes[0], dlat, "ML-DSA-44 (Dilithium2)", ALGO_COLORS["dilithium2"]),
        (axes[1], flat, "Falcon-512", ALGO_COLORS["falcon512"]),
    ]:
        if len(lat) == 0:
            ax.set_title(f"{name}: NO DATA")
            continue
        sorted_lat = np.sort(lat)
        cdf = np.arange(1, len(sorted_lat) + 1) / len(sorted_lat)
        ax.plot(sorted_lat, cdf, color=color, linewidth=1.5)
        ax.set_xscale("log")
        ax.set_xlabel("Latency (microseconds)")
        ax.set_title(f"{name}\n"
                     f"n={len(lat):,}  "
                     f"p50={np.percentile(lat, 50):.1f}us  "
                     f"p99={np.percentile(lat, 99):.1f}us  "
                     f"p99.9={np.percentile(lat, 99.9):.1f}us")
        ax.grid(True, alpha=0.3)

    axes[0].set_ylabel("Cumulative probability")
    fig.suptitle("Single-thread latency CDFs at 1M iterations / 10k warmup\n"
                 "(if ML-DSA shows two distinct steps, bimodality is real;"
                 " if it smooths out, the original was warmup/noise)",
                 fontsize=10)
    fig.tight_layout()

    out_path = out_dir / "cdf_bimodality.pdf"
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [ok]   {out_path.name}")

    # Compute a quantitative bimodality score so verdict.txt can reason
    # about it: ratio of p99 to p50, and gap between p25 and p75.
    return {
        "dilithium2_p50_us": float(np.percentile(dlat, 50)) if len(dlat) else None,
        "dilithium2_p99_us": float(np.percentile(dlat, 99)) if len(dlat) else None,
        "dilithium2_p99_p50_ratio": float(np.percentile(dlat, 99) / np.percentile(dlat, 50)) if len(dlat) else None,
        "falcon512_p50_us": float(np.percentile(flat, 50)) if len(flat) else None,
        "falcon512_p99_us": float(np.percentile(flat, 99)) if len(flat) else None,
        "falcon512_p99_p50_ratio": float(np.percentile(flat, 99) / np.percentile(flat, 50)) if len(flat) else None,
    }


# ---------------------------------------------------------------------
# Plot 2: Ed25519 memory-bandwidth claim
# ---------------------------------------------------------------------

def plot_ed25519_stalls(out_dir):
    thread_counts = [1, 24, 48, 96]
    stall_frac = []
    mem_loads = []

    for tc in thread_counts:
        a_path = out_dir / f"ed25519_t{tc}_groupA_perf.csv"
        b_path = out_dir / f"ed25519_t{tc}_groupB_perf.csv"

        if not a_path.exists():
            stall_frac.append(np.nan)
        else:
            counts = parse_perf_csv(a_path)
            cycles = counts.get("cycles", 0)
            stalls_l3 = counts.get("cycle_activity.stalls_l3_miss", 0)
            stall_frac.append(stalls_l3 / cycles if cycles > 0 else np.nan)

        if not b_path.exists():
            mem_loads.append(np.nan)
        else:
            counts = parse_perf_csv(b_path)
            mem_loads.append(counts.get("mem_inst_retired.all_loads", np.nan))

    fig, ax1 = plt.subplots(figsize=(7, 4.5))
    color1 = ALGO_COLORS["ed25519"]
    ax1.plot(thread_counts, [f * 100 if not np.isnan(f) else np.nan for f in stall_frac],
             marker="o", color=color1, linewidth=2, label="stalls_l3_miss / cycles")
    ax1.set_xlabel("Software thread count")
    ax1.set_ylabel("L3-miss stall fraction (% of cycles)", color=color1)
    ax1.tick_params(axis="y", labelcolor=color1)
    ax1.set_xscale("log", base=2)
    ax1.set_xticks(thread_counts)
    ax1.set_xticklabels([str(t) for t in thread_counts])
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    color2 = "#666666"
    ax2.plot(thread_counts, mem_loads,
             marker="s", linestyle="--", color=color2, linewidth=1.5,
             label="mem_inst_retired.all_loads")
    ax2.set_ylabel("Memory loads (count)", color=color2)
    ax2.tick_params(axis="y", labelcolor=color2)

    fig.suptitle("Ed25519: memory-bandwidth diagnosis\n"
                 "If stall fraction rises sharply with threads,"
                 " memory bandwidth is the bottleneck",
                 fontsize=10)
    fig.tight_layout()

    out_path = out_dir / "ed25519_stalls.pdf"
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [ok]   {out_path.name}")

    return {
        "ed25519_stall_l3_at_t1_pct":  stall_frac[0] * 100 if not np.isnan(stall_frac[0]) else None,
        "ed25519_stall_l3_at_t96_pct": stall_frac[3] * 100 if not np.isnan(stall_frac[3]) else None,
    }


# ---------------------------------------------------------------------
# Plot 3: cross-algorithm stalls at threads=96
# ---------------------------------------------------------------------

def plot_cross_algo_stalls(out_dir):
    algos = ["ed25519", "falcon512", "dilithium2"]
    breakdowns = {}

    for algo in algos:
        a_path = out_dir / f"{algo}_t96_groupA_perf.csv"
        if not a_path.exists():
            # ed25519 group A is named ed25519_t96_groupA, others were
            # added in phase 4. Skip if missing.
            continue
        counts = parse_perf_csv(a_path)
        cycles = counts.get("cycles", 0)
        if cycles == 0:
            continue
        breakdowns[algo] = {
            "stalls_l3_miss":  counts.get("cycle_activity.stalls_l3_miss", 0) / cycles,
            "stalls_mem_any":  counts.get("cycle_activity.stalls_mem_any", 0) / cycles,
            "stalls_total":    counts.get("cycle_activity.stalls_total", 0) / cycles,
        }

    if not breakdowns:
        print(f"  [skip] cross-algo stalls plot: no perf data found")
        return None

    fig, ax = plt.subplots(figsize=(7, 4.5))
    metrics = ["stalls_l3_miss", "stalls_mem_any", "stalls_total"]
    x = np.arange(len(metrics))
    width = 0.25

    for i, algo in enumerate(algos):
        if algo not in breakdowns:
            continue
        vals = [breakdowns[algo][m] * 100 for m in metrics]
        ax.bar(x + (i - 1) * width, vals, width,
               label=algo, color=ALGO_COLORS[algo])

    ax.set_xticks(x)
    ax.set_xticklabels(metrics, rotation=15)
    ax.set_ylabel("% of cycles")
    ax.set_title("Cycle stalls at threads=96 across algorithms\n"
                 "Differentially high stalls_l3_miss for one algo"
                 " = memory-bandwidth bound; uniform = different bottleneck",
                 fontsize=10)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()

    out_path = out_dir / "cross_algo_stalls.pdf"
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [ok]   {out_path.name}")

    return breakdowns


# ---------------------------------------------------------------------
# Hot-symbol parser (for verdict.txt)
# ---------------------------------------------------------------------

def parse_hot_symbols(path, top_n=15):
    """Pull the top symbols from a perf report --stdio output."""
    if not path.exists():
        return []
    syms = []
    in_table = False
    with open(path) as f:
        for line in f:
            if line.startswith("# Overhead"):
                in_table = True
                continue
            if not in_table:
                continue
            if line.startswith("#"):
                continue
            line = line.rstrip()
            if not line:
                continue
            # Format: "  12.34%  bench_sign       libcrypto.so   [.] EVP_MD_CTX_new"
            m = re.match(r"\s*(\d+\.\d+)%\s+\S+\s+(\S+)\s+\[\.\]\s+(.+)", line)
            if m:
                pct, dso, sym = m.groups()
                syms.append((float(pct), dso, sym))
                if len(syms) >= top_n:
                    break
    return syms


# ---------------------------------------------------------------------
# Verdict
# ---------------------------------------------------------------------

def write_verdict(out_dir, bimodal_stats, ed25519_stats, cross_stats):
    lines = []
    lines.append("=" * 70)
    lines.append("PERF SPOT-CHECK VERDICT")
    lines.append("=" * 70)
    lines.append("")

    # ---- Claim 2: bimodality ----
    lines.append("Claim 2: ML-DSA-44 shows bimodal latency at threads=1.")
    lines.append("-" * 70)
    if bimodal_stats and bimodal_stats.get("dilithium2_p99_p50_ratio") is not None:
        d_ratio = bimodal_stats["dilithium2_p99_p50_ratio"]
        f_ratio = bimodal_stats["falcon512_p99_p50_ratio"]
        lines.append(f"  ML-DSA-44 p99/p50 ratio: {d_ratio:.2f}")
        lines.append(f"  Falcon-512 p99/p50 ratio: {f_ratio:.2f}")
        lines.append("")
        if d_ratio < 1.3:
            lines.append("  VERDICT: REFUTED. With long iterations and warmup, ML-DSA-44")
            lines.append("           latency is tight (p99/p50 < 1.3). The original 'bimodality'")
            lines.append("           was sampler noise or insufficient warmup. Do NOT publish")
            lines.append("           this claim. ML-DSA-44's verify is constant-time-ish.")
        elif d_ratio > 1.5 and f_ratio < d_ratio:
            lines.append("  VERDICT: SUPPORTED. ML-DSA-44 shows a real tail wider than Falcon's.")
            lines.append("           This is unusual and worth investigating in the paper.")
            lines.append("           Inspect cdf_bimodality.pdf; if there are two distinct steps,")
            lines.append("           bimodality is real. If smooth, it's just a long tail.")
        else:
            lines.append("  VERDICT: AMBIGUOUS. ML-DSA-44 has p99/p50 = {:.2f}.".format(d_ratio))
            lines.append("           Inspect cdf_bimodality.pdf manually to decide.")
    else:
        lines.append("  VERDICT: NO DATA. Latency CSVs missing.")
    lines.append("")

    # ---- Claim 1: Ed25519 memory-bandwidth bottleneck ----
    lines.append("Claim 1: Ed25519's poor scaling is due to memory-bandwidth saturation.")
    lines.append("-" * 70)
    if ed25519_stats and ed25519_stats.get("ed25519_stall_l3_at_t96_pct") is not None:
        s1 = ed25519_stats.get("ed25519_stall_l3_at_t1_pct")
        s96 = ed25519_stats.get("ed25519_stall_l3_at_t96_pct")
        lines.append(f"  L3-miss stall fraction at threads=1:  {s1:.2f}%")
        lines.append(f"  L3-miss stall fraction at threads=96: {s96:.2f}%")
        lines.append("")
        if s96 > 30 and s96 / max(s1, 0.1) > 5:
            lines.append("  VERDICT: SUPPORTED. L3-miss stalls dominate at high thread count")
            lines.append("           and rise sharply from threads=1. Memory-bandwidth claim holds.")
        elif s96 < 10:
            lines.append("  VERDICT: REFUTED. L3-miss stalls remain low even at threads=96.")
            lines.append("           Ed25519 scaling cliff is NOT memory-bandwidth.")
            lines.append("           Most likely culprit: EVP_MD_CTX allocation in the hot loop.")
            lines.append("           Check ed25519_t1_symbols.txt for malloc/free dominance.")
            lines.append("           Add a context-reuse variant to bench_sign before publishing.")
        else:
            lines.append("  VERDICT: PARTIAL. L3-miss stalls grow but not dominantly.")
            lines.append("           Mixed bottleneck. Investigate the symbol profile and")
            lines.append("           also check stalls_mem_any in the perf CSVs.")
    else:
        lines.append("  VERDICT: NO DATA. Ed25519 perf CSVs missing or unparseable.")
    lines.append("")

    # ---- Hot symbols for Ed25519 ----
    sym_path = out_dir / "ed25519_t1_symbols.txt"
    syms = parse_hot_symbols(sym_path)
    lines.append("Top symbols, Ed25519 at threads=1:")
    lines.append("-" * 70)
    if syms:
        for pct, dso, sym in syms[:10]:
            lines.append(f"  {pct:5.2f}%  {dso:25}  {sym}")
        lines.append("")
        # Check whether allocator/context calls are in the top
        alloc_pct = sum(p for p, _, s in syms[:10]
                        if any(k in s for k in
                               ("MD_CTX_new", "MD_CTX_free", "MD_CTX_init",
                                "malloc", "free", "_int_malloc", "_int_free")))
        if alloc_pct > 10:
            lines.append(f"  >>> Allocator/context functions = {alloc_pct:.1f}% of cycles.")
            lines.append("      This is a SMOKING GUN for the EVP_MD_CTX hypothesis.")
            lines.append("      Add a context-reuse variant to bench_sign and re-run.")
        else:
            lines.append(f"  Allocator/context functions = {alloc_pct:.1f}% of cycles.")
            lines.append("  Allocator is NOT the dominant cost. Look elsewhere for")
            lines.append("  the Ed25519 scaling cliff.")
    else:
        lines.append("  (no symbol data; check that perf record completed)")
    lines.append("")

    # ---- Cross-algo at threads=96 ----
    lines.append("Cross-algorithm stalls at threads=96:")
    lines.append("-" * 70)
    if cross_stats:
        for algo, m in cross_stats.items():
            lines.append(f"  {algo:12}  l3-miss-stalls: {m['stalls_l3_miss']*100:5.2f}%   "
                         f"mem-any-stalls: {m['stalls_mem_any']*100:5.2f}%   "
                         f"total-stalls: {m['stalls_total']*100:5.2f}%")
        lines.append("")
        lines.append("  If Ed25519 stalls_l3_miss is SIGNIFICANTLY higher than Falcon/ML-DSA")
        lines.append("  at the same thread count, the differential supports the")
        lines.append("  'Ed25519-specific memory-bandwidth bottleneck' claim.")
    else:
        lines.append("  (no data)")
    lines.append("")

    # ---- Recommendation on Experiment 2 ----
    lines.append("=" * 70)
    lines.append("RECOMMENDATION FOR EXPERIMENT 2")
    lines.append("=" * 70)
    lines.append("")
    lines.append("Proceed to Experiment 2 only if:")
    lines.append("  1. Bimodality claim is either confirmed or removed from the paper.")
    lines.append("  2. Ed25519 scaling claim has a clear hardware-counter explanation.")
    lines.append("  3. If allocator was the smoking gun, bench_verify must use the same")
    lines.append("     context-reuse policy as bench_sign for fair comparison.")
    lines.append("")

    out_path = out_dir / "verdict.txt"
    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print(f"  [ok]   {out_path.name}")

    # Also dump to stdout
    print("")
    print("\n".join(lines))


# ---------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------

def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <perf_spotcheck_directory>")
        sys.exit(1)

    out_dir = Path(sys.argv[1]).resolve()
    if not out_dir.is_dir():
        print(f"ERROR: {out_dir} not a directory")
        sys.exit(1)

    print(f"analyzing {out_dir}")
    print("")

    bimodal = plot_bimodality(out_dir)
    ed25519 = plot_ed25519_stalls(out_dir)
    cross = plot_cross_algo_stalls(out_dir)

    write_verdict(out_dir, bimodal, ed25519, cross)


if __name__ == "__main__":
    main()
