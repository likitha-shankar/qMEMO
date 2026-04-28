#!/usr/bin/env python3
"""analyze.py — Post-processing for Experiment 1 (Signature Creation).

Consumes *_summary.csv, *_latencies.csv (sampled), *_metrics.csv, *_perf.csv
produced by bench_sign and generates all 8 figures + Table 1 from spec §14.

Usage:
    python3 analyze.py --results-dir /path/to/sweep_YYYYMMDD_HHMMSS [options]

Options:
    --results-dir DIR   directory containing bench_sign output files
    --out-dir DIR       where to write figures  [default: results-dir/figures]
    --fmt pdf|png       output format           [default: pdf]
    --dpi N             DPI for PNG output      [default: 150]
    --no-show           don't call plt.show()
"""

import argparse
import glob
import json
import os
import sys
import warnings
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

warnings.filterwarnings("ignore", category=FutureWarning)

# ── Plot style ────────────────────────────────────────────────────────────────
ALGO_COLOR  = {"ed25519": "#1f77b4", "falcon512": "#ff7f0e", "dilithium2": "#2ca02c"}
ALGO_LABEL  = {"ed25519": "Ed25519", "falcon512": "Falcon-512", "dilithium2": "ML-DSA-44"}
THREAD_TICKS = [1, 2, 4, 8, 12, 24, 48, 96]

plt.rcParams.update({
    "font.size": 10,
    "axes.labelsize": 11,
    "axes.titlesize": 11,
    "legend.fontsize": 9,
    "figure.dpi": 100,
})

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_summaries(results_dir: str) -> pd.DataFrame:
    paths = glob.glob(os.path.join(results_dir, "*_summary.csv"))
    if not paths:
        sys.exit(f"No *_summary.csv files found in {results_dir}")
    frames = []
    for p in paths:
        try:
            df = pd.read_csv(p)
            frames.append(df)
        except Exception as e:
            print(f"  WARN: could not parse {p}: {e}")
    if not frames:
        sys.exit("No parseable summary CSVs")
    df = pd.concat(frames, ignore_index=True)
    # Normalise column names (strip spaces)
    df.columns = df.columns.str.strip()
    return df


def aggregate_summary(df: pd.DataFrame) -> pd.DataFrame:
    """Average across runs for each (algo, threads, pin_strategy)."""
    grp = df.groupby(["algo", "threads", "pin_strategy"], as_index=False).agg(
        ops_mean=("ops_per_sec_total", "mean"),
        ops_std=("ops_per_sec_total", "std"),
        mean_ns_mean=("mean_ns", "mean"),
        p50_mean=("median_ns", "mean"),
        p95_mean=("p95_ns", "mean"),
        p99_mean=("p99_ns", "mean"),
        p999_mean=("p99_9_ns", "mean"),
        max_mean=("max_ns", "mean"),
        stddev_mean=("stddev_ns", "mean"),
        n_runs=("run_id", "count"),
    )
    return grp


def add_efficiency(agg: pd.DataFrame) -> pd.DataFrame:
    """Add speedup_vs_1t and efficiency columns."""
    rows = []
    for algo in agg["algo"].unique():
        for pin in agg["pin_strategy"].unique():
            sub = agg[(agg["algo"] == algo) & (agg["pin_strategy"] == pin)].copy()
            ref = sub[sub["threads"] == 1]
            if ref.empty:
                sub["speedup"] = float("nan")
                sub["efficiency"] = float("nan")
            else:
                ref_ops = float(ref["ops_mean"].iloc[0])
                sub = sub.copy()
                sub["speedup"] = sub["ops_mean"] / ref_ops
                sub["efficiency"] = sub["speedup"] / sub["threads"]
            rows.append(sub)
    return pd.concat(rows, ignore_index=True) if rows else agg


def load_latencies_sampled(results_dir: str, max_rows: int = 2_000_000) -> pd.DataFrame:
    """Load a random sample of latency rows to keep memory manageable."""
    paths = glob.glob(os.path.join(results_dir, "*_latencies.csv"))
    if not paths:
        return pd.DataFrame()
    # Count total rows to decide skip rate
    total = 0
    for p in paths:
        try:
            total += sum(1 for _ in open(p)) - 1  # subtract header
        except Exception:
            pass
    skip = max(1, total // max_rows)
    frames = []
    for p in paths:
        try:
            # skiprows: keep header (row 0) then every skip-th row
            df = pd.read_csv(p, skiprows=lambda i: i > 0 and i % skip != 0)
            frames.append(df)
        except Exception as e:
            print(f"  WARN: {p}: {e}")
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def load_metrics(results_dir: str) -> pd.DataFrame:
    paths = glob.glob(os.path.join(results_dir, "*_metrics.csv"))
    if not paths:
        return pd.DataFrame()
    frames = [pd.read_csv(p) for p in paths]
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def load_perf(results_dir: str) -> pd.DataFrame:
    paths = glob.glob(os.path.join(results_dir, "*_perf.csv"))
    if not paths:
        return pd.DataFrame()
    frames = [pd.read_csv(p) for p in paths]
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def savefig(fig, out_dir: str, name: str, fmt: str, dpi: int):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{name}.{fmt}")
    fig.savefig(path, format=fmt, dpi=dpi, bbox_inches="tight")
    print(f"  → {path}")


# ═════════════════════════════════════════════════════════════════════════════
# Figure 1 — Throughput vs threads
# ═════════════════════════════════════════════════════════════════════════════

def fig1_throughput(agg: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    fig, ax = plt.subplots(figsize=(7, 4.5))

    for algo in ["ed25519", "falcon512", "dilithium2"]:
        color = ALGO_COLOR[algo]
        label = ALGO_LABEL[algo]

        for pin, ls in [("compact", "-"), ("spread", "--")]:
            sub = agg[(agg["algo"] == algo) & (agg["pin_strategy"] == pin)].sort_values("threads")
            if sub.empty:
                continue
            lbl = f"{label} ({pin})" if pin == "compact" else None  # avoid legend clutter for spread
            ax.errorbar(sub["threads"], sub["ops_mean"] / 1e6,
                        yerr=sub["ops_std"] / 1e6,
                        fmt=f"o{ls}", color=color, label=lbl,
                        linewidth=1.8, markersize=5, capsize=3, elinewidth=1)

    ax.axvline(24, color="gray", lw=0.8, ls=":", alpha=0.7)
    ax.axvline(48, color="gray", lw=0.8, ls=":", alpha=0.7)
    ax.text(24.5, ax.get_ylim()[1] * 0.95 if ax.get_ylim()[1] > 0 else 1,
            "socket sat.", fontsize=7, color="gray", va="top")
    ax.text(48.5, ax.get_ylim()[1] * 0.85 if ax.get_ylim()[1] > 0 else 1,
            "SMT begins", fontsize=7, color="gray", va="top")

    ax.set_xscale("log", base=2)
    ax.set_xticks(THREAD_TICKS)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_xlabel("Software threads")
    ax.set_ylabel("Total throughput (Mops/s)")
    ax.set_title("Fig 1 — Signing throughput vs thread count (solid=compact, dashed=spread)")
    ax.legend(loc="upper left", framealpha=0.85)
    ax.grid(True, which="both", alpha=0.3)

    savefig(fig, out_dir, "fig1_throughput", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 2 — Parallel efficiency
# ═════════════════════════════════════════════════════════════════════════════

def fig2_efficiency(agg: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.axhline(1.0, color="black", lw=0.8, ls="--", label="Ideal (100%)")

    for algo in ["ed25519", "falcon512", "dilithium2"]:
        sub = agg[(agg["algo"] == algo) & (agg["pin_strategy"] == "compact")].sort_values("threads")
        if sub.empty or "efficiency" not in sub.columns:
            continue
        ax.plot(sub["threads"], sub["efficiency"],
                "o-", color=ALGO_COLOR[algo], label=ALGO_LABEL[algo],
                linewidth=1.8, markersize=5)

    ax.axvline(24, color="gray", lw=0.8, ls=":")
    ax.axvline(48, color="gray", lw=0.8, ls=":")
    ax.set_xscale("log", base=2)
    ax.set_xticks(THREAD_TICKS)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_ylim(0, 1.15)
    ax.set_xlabel("Software threads")
    ax.set_ylabel("Parallel efficiency  (N-thread / (N × 1-thread))")
    ax.set_title("Fig 2 — Parallel efficiency (compact pinning)")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)

    savefig(fig, out_dir, "fig2_efficiency", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 3 — Latency CDF (one panel per algo)
# ═════════════════════════════════════════════════════════════════════════════

def fig3_latency_cdf(lat: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    if lat.empty:
        print("  SKIP fig3: no latency data")
        return

    lat.columns = lat.columns.str.strip()
    thread_cuts = [1, 24, 96]
    algos = ["ed25519", "falcon512", "dilithium2"]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharey=True)

    for ax, algo in zip(axes, algos):
        sub = lat[lat["algo"] == algo]
        ax.set_title(ALGO_LABEL[algo])
        for tc in thread_cuts:
            rows = sub[sub["threads"] == tc]["latency_ns"]
            if rows.empty:
                continue
            vals = np.sort(rows.values) / 1e3  # → µs
            cdf  = np.arange(1, len(vals) + 1) / len(vals)
            ax.plot(vals, cdf, label=f"{tc} threads", linewidth=1.5)
        ax.set_xscale("log")
        ax.set_xlabel("Latency (µs)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8)

    axes[0].set_ylabel("CDF")
    fig.suptitle("Fig 3 — Per-operation latency CDF at threads ∈ {1, 24, 96}")
    fig.tight_layout()

    savefig(fig, out_dir, "fig3_latency_cdf", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 4 — Stalled cycles breakdown  (requires perf data)
# ═════════════════════════════════════════════════════════════════════════════

def fig4_stall_breakdown(perf: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    if perf.empty:
        print("  SKIP fig4: no perf data")
        return

    perf.columns = perf.columns.str.strip()
    algos = ["ed25519", "falcon512", "dilithium2"]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharey=True)

    stall_events = [
        ("stalls_l1d_miss",    "L1d miss",  "#d62728"),
        ("stalls_l2_miss",     "L2 miss",   "#ff7f0e"),
        ("stalls_l3_miss",     "L3 miss",   "#9467bd"),
        ("stalls_mem_any",     "mem-any",   "#8c564b"),
    ]

    for ax, algo in zip(axes, algos):
        sub = perf[perf["algo"] == algo]
        threads = sorted(sub["threads"].unique())
        if not threads:
            continue

        cycles_map = {}
        for _, row in sub[sub["event"].str.contains("cycles", na=False)].iterrows():
            t = row.get("threads", 0)
            cycles_map[t] = float(row["value"])

        bottoms = np.zeros(len(threads))
        t_idx   = {t: i for i, t in enumerate(threads)}

        for evt_frag, label, color in stall_events:
            vals = np.zeros(len(threads))
            for _, row in sub[sub["event"].str.contains(evt_frag, na=False)].iterrows():
                t = row.get("threads", 0)
                cy = cycles_map.get(t, 1)
                if t in t_idx:
                    vals[t_idx[t]] = float(row["value"]) / max(cy, 1)
            ax.bar(range(len(threads)), vals, bottom=bottoms,
                   color=color, label=label, width=0.6)
            bottoms += vals

        ax.set_xticks(range(len(threads)))
        ax.set_xticklabels([str(t) for t in threads], rotation=45, ha="right", fontsize=8)
        ax.set_title(ALGO_LABEL[algo])
        ax.set_xlabel("Threads")

    axes[0].set_ylabel("Fraction of cycles stalled")
    axes[0].legend(loc="upper left", fontsize=8)
    fig.suptitle("Fig 4 — Stalled cycles breakdown (stalls_l1d / l2 / l3 / mem_any)")
    fig.tight_layout()

    savefig(fig, out_dir, "fig4_stall_breakdown", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 5 — Top-down breakdown  (requires perf data)
# ═════════════════════════════════════════════════════════════════════════════

def fig5_topdown(perf: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    if perf.empty:
        print("  SKIP fig5: no perf data")
        return

    perf.columns = perf.columns.str.strip()
    slots_events = [
        ("topdown-retiring",  "Retiring",  "#2ca02c"),
        ("topdown-bad-spec",  "Bad spec",  "#d62728"),
        ("topdown-fe-bound",  "FE bound",  "#ff7f0e"),
        ("topdown-be-bound",  "BE bound",  "#9467bd"),
    ]

    algos = ["ed25519", "falcon512", "dilithium2"]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharey=True)

    for ax, algo in zip(axes, algos):
        sub = perf[perf["algo"] == algo]
        threads = sorted(sub["threads"].unique())
        if not threads:
            continue
        bottoms = np.zeros(len(threads))
        t_idx   = {t: i for i, t in enumerate(threads)}

        for evt_frag, label, color in slots_events:
            vals = np.zeros(len(threads))
            for _, row in sub[sub["event"].str.contains(evt_frag, na=False)].iterrows():
                t = row.get("threads", 0)
                if t in t_idx:
                    try:
                        vals[t_idx[t]] = float(row["value"]) / 100.0
                    except (ValueError, TypeError):
                        pass
            ax.bar(range(len(threads)), vals, bottom=bottoms,
                   color=color, label=label, width=0.6)
            bottoms += vals

        ax.set_xticks(range(len(threads)))
        ax.set_xticklabels([str(t) for t in threads], rotation=45, ha="right", fontsize=8)
        ax.set_ylim(0, 1.05)
        ax.set_title(ALGO_LABEL[algo])
        ax.set_xlabel("Threads")

    axes[0].set_ylabel("Fraction of pipeline slots")
    axes[0].legend(loc="upper left", fontsize=8)
    fig.suptitle("Fig 5 — Top-down pipeline breakdown")
    fig.tight_layout()

    savefig(fig, out_dir, "fig5_topdown", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 6 — AVX-512 vs AVX2  (requires separate avx2 results tagged "avx2")
# ═════════════════════════════════════════════════════════════════════════════

def fig6_avx(agg: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    avx512 = agg[~agg.get("tag", pd.Series(dtype=str)).str.contains("avx2", na=False)]
    avx2   = agg[agg.get("tag",  pd.Series(dtype=str)).str.contains("avx2", na=False)]

    if avx2.empty:
        print("  SKIP fig6: no AVX2 results (run bench_sign with --tag avx2 using avx2 build)")
        return

    thread_cuts = [1, 24, 48]
    fig, ax = plt.subplots(figsize=(8, 4.5))

    x = np.arange(len(thread_cuts))
    w = 0.35
    algos = ["dilithium2"]  # AVX-512 throttling most pronounced for ML-DSA

    for i, algo in enumerate(algos):
        for j, tc in enumerate(thread_cuts):
            r512 = avx512[(avx512["algo"] == algo) & (avx512["threads"] == tc)]["ops_mean"]
            r2   = avx2[  (avx2["algo"]   == algo) & (avx2["threads"]   == tc)]["ops_mean"]
            v512 = float(r512.iloc[0]) / 1e6 if not r512.empty else 0
            v2   = float(r2.iloc[0])   / 1e6 if not r2.empty   else 0
            bar_x = x[j] + i * 2 * w
            ax.bar(bar_x - w/2, v512, w, label=f"{ALGO_LABEL[algo]} AVX-512" if j == 0 else "",
                   color=ALGO_COLOR[algo], alpha=0.9)
            ax.bar(bar_x + w/2, v2,   w, label=f"{ALGO_LABEL[algo]} AVX2" if j == 0 else "",
                   color=ALGO_COLOR[algo], alpha=0.4)

    ax.set_xticks(x)
    ax.set_xticklabels([f"threads={t}" for t in thread_cuts])
    ax.set_ylabel("Throughput (Mops/s)")
    ax.set_title("Fig 6 — AVX-512 vs AVX2 on Cascade Lake (ML-DSA-44)")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)

    savefig(fig, out_dir, "fig6_avx", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 7 — Compact vs spread pinning
# ═════════════════════════════════════════════════════════════════════════════

def fig7_pinning(agg: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    thread_cuts = [24, 48]
    algos = ["ed25519", "falcon512", "dilithium2"]
    n_algos = len(algos)
    n_tc    = len(thread_cuts)

    fig, axes = plt.subplots(1, n_tc, figsize=(10, 4.5), sharey=False)
    if n_tc == 1:
        axes = [axes]

    for ax, tc in zip(axes, thread_cuts):
        x  = np.arange(n_algos)
        w  = 0.35
        compact_vals, spread_vals = [], []
        for algo in algos:
            rc = agg[(agg["algo"] == algo) & (agg["threads"] == tc) & (agg["pin_strategy"] == "compact")]["ops_mean"]
            rs = agg[(agg["algo"] == algo) & (agg["threads"] == tc) & (agg["pin_strategy"] == "spread")]["ops_mean"]
            compact_vals.append(float(rc.iloc[0]) / 1e6 if not rc.empty else 0)
            spread_vals.append(float(rs.iloc[0])  / 1e6 if not rs.empty else 0)

        bars_c = ax.bar(x - w/2, compact_vals, w, label="compact",
                        color=[ALGO_COLOR[a] for a in algos], alpha=0.9)
        bars_s = ax.bar(x + w/2, spread_vals,  w, label="spread",
                        color=[ALGO_COLOR[a] for a in algos], alpha=0.45)
        ax.set_xticks(x)
        ax.set_xticklabels([ALGO_LABEL[a] for a in algos], rotation=15, ha="right")
        ax.set_title(f"threads = {tc}")
        ax.set_ylabel("Throughput (Mops/s)")
        ax.legend(handles=[bars_c[0], bars_s[0]], labels=["compact", "spread"])
        ax.grid(axis="y", alpha=0.3)

    fig.suptitle("Fig 7 — Compact vs spread pinning at key thread counts")
    fig.tight_layout()

    savefig(fig, out_dir, "fig7_pinning", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Figure 8 — Frequency over time  (from metrics CSV)
# ═════════════════════════════════════════════════════════════════════════════

def fig8_freq_time(metrics: pd.DataFrame, agg: pd.DataFrame, out_dir: str, fmt: str, dpi: int):
    if metrics.empty:
        print("  SKIP fig8: no metrics data")
        return

    metrics.columns = metrics.columns.str.strip()
    if "cpu0_freq_khz" not in metrics.columns:
        print("  SKIP fig8: cpu0_freq_khz column not found")
        return

    # Use dilithium2 compact runs as the showcase
    fig, ax = plt.subplots(figsize=(9, 4.5))
    # Metrics file doesn't have algo/threads labels currently — placeholder note
    ax.plot(metrics.index, metrics["cpu0_freq_khz"] / 1e3,
            color="steelblue", linewidth=1, label="CPU0 freq (GHz)")
    ax.set_xlabel("Sample index (100 ms intervals)")
    ax.set_ylabel("Frequency (GHz)")
    ax.set_title("Fig 8 — CPU frequency over time during benchmark")
    ax.grid(alpha=0.3)
    ax.legend()
    # Annotate nominal frequency if readable
    nominal = metrics["cpu0_freq_khz"].median() / 1e3
    ax.axhline(nominal, color="red", ls="--", lw=0.8, label=f"median {nominal:.2f} GHz")
    ax.legend()

    savefig(fig, out_dir, "fig8_freq_time", fmt, dpi)
    plt.close(fig)


# ═════════════════════════════════════════════════════════════════════════════
# Table 1 — Full grid
# ═════════════════════════════════════════════════════════════════════════════

def table1(agg: pd.DataFrame, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "table1_full_grid.csv")

    cols = ["algo", "threads", "pin_strategy",
            "ops_mean", "ops_std",
            "p50_mean", "p95_mean", "p99_mean", "p999_mean",
            "max_mean", "stddev_mean", "n_runs"]
    sub = agg[[c for c in cols if c in agg.columns]].copy()

    # Round to sensible precision
    for c in ["ops_mean", "ops_std"]:
        if c in sub.columns: sub[c] = sub[c].round(0)
    for c in ["p50_mean", "p95_mean", "p99_mean", "p999_mean", "max_mean", "stddev_mean"]:
        if c in sub.columns: sub[c] = sub[c].round(1)

    sub = sub.sort_values(["algo", "pin_strategy", "threads"])
    sub.to_csv(path, index=False)
    print(f"  → {path}")

    # Also print a summary to stdout
    print("\nTable 1 — Throughput summary (Mops/s):")
    print(f"{'algo':<14} {'pin':<10} {'threads':>8}  {'Mops/s':>10}  {'±':>8}  "
          f"{'p50µs':>8} {'p99µs':>8} {'p99.9µs':>9}")
    print("-" * 80)
    for _, row in sub.iterrows():
        print(f"{row.get('algo',''):<14} "
              f"{row.get('pin_strategy',''):<10} "
              f"{int(row.get('threads', 0)):>8}  "
              f"{row.get('ops_mean', 0)/1e6:>10.3f}  "
              f"{row.get('ops_std', 0)/1e6:>8.3f}  "
              f"{row.get('p50_mean', 0)/1e3:>8.1f} "
              f"{row.get('p99_mean', 0)/1e3:>8.1f} "
              f"{row.get('p999_mean', 0)/1e3:>9.1f}")


# ═════════════════════════════════════════════════════════════════════════════
# Main
# ═════════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results-dir", required=True, help="Directory with bench_sign CSVs")
    parser.add_argument("--out-dir",     default="",    help="Output directory for figures")
    parser.add_argument("--fmt",         default="pdf", choices=["pdf", "png"])
    parser.add_argument("--dpi",         default=150,   type=int)
    parser.add_argument("--no-show",     action="store_true")
    args = parser.parse_args()

    out_dir = args.out_dir or os.path.join(args.results_dir, "figures")
    os.makedirs(out_dir, exist_ok=True)
    print(f"Results dir: {args.results_dir}")
    print(f"Output dir:  {out_dir}")
    print(f"Format:      {args.fmt}")

    # ── Load data ────────────────────────────────────────────────────────────
    print("\nLoading summary CSVs...")
    raw = load_summaries(args.results_dir)
    print(f"  {len(raw)} rows across {raw['algo'].nunique() if 'algo' in raw else 0} algos")

    agg = aggregate_summary(raw)
    agg = add_efficiency(agg)

    print("Loading latency CSVs (sampled to 2M rows)...")
    lat = load_latencies_sampled(args.results_dir)
    print(f"  {len(lat)} sampled latency rows")

    print("Loading metrics CSVs...")
    metrics = load_metrics(args.results_dir)
    print(f"  {len(metrics)} metric samples")

    print("Loading perf CSVs...")
    perf = load_perf(args.results_dir)
    print(f"  {len(perf)} perf rows")

    # ── Generate figures ─────────────────────────────────────────────────────
    print("\nGenerating figures...")
    fig1_throughput(agg, out_dir, args.fmt, args.dpi)
    fig2_efficiency(agg, out_dir, args.fmt, args.dpi)
    fig3_latency_cdf(lat, out_dir, args.fmt, args.dpi)
    fig4_stall_breakdown(perf, out_dir, args.fmt, args.dpi)
    fig5_topdown(perf, out_dir, args.fmt, args.dpi)
    fig6_avx(agg, out_dir, args.fmt, args.dpi)
    fig7_pinning(agg, out_dir, args.fmt, args.dpi)
    fig8_freq_time(metrics, agg, out_dir, args.fmt, args.dpi)
    table1(agg, out_dir)

    print(f"\nDone. {len(list(Path(out_dir).glob(f'*.{args.fmt}')))} figures written to {out_dir}")


if __name__ == "__main__":
    main()
