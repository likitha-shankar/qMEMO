#!/usr/bin/env python3
"""analyze.py — Post-processing for Experiment 2 (Signature Verification).

Produces figures 1-12 from spec §14. Figure 12 is the cross-experiment
bottleneck plot that explains the 1M end-to-end convergence.

Usage:
    python3 analyze.py --input /path/to/exp2_sweep_DIR [options]

Options:
    --input DIR                   directory with bench_verify CSVs
    --out-dir DIR                 where to write figures [default: input/figures]
    --cross-experiment-input DIR  Experiment 1 signing results dir (for fig2, fig12)
    --e2e-tps FLOAT               end-to-end TPS ceiling for fig12 [default 8750]
    --fmt pdf|png                 [default pdf]
    --dpi N                       [default 150]
"""

import argparse
import glob
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

ALGO_COLOR = {"ed25519": "#1f77b4", "falcon512": "#ff7f0e", "dilithium2": "#2ca02c"}
ALGO_LABEL = {"ed25519": "Ed25519", "falcon512": "Falcon-512", "dilithium2": "ML-DSA-44"}
MIX_STYLE  = {"valid": "-", "invalid": "--", "alternating": ":"}
THREAD_TICKS = [1, 8, 24, 48, 96]


def savefig(fig, out_dir, name, fmt, dpi):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{name}.{fmt}")
    fig.savefig(path, format=fmt, dpi=dpi, bbox_inches="tight")
    print(f"  -> {path}")
    plt.close(fig)


def load_summaries(d):
    paths = glob.glob(os.path.join(d, "*_summary.csv"))
    if not paths:
        return pd.DataFrame()
    frames = []
    for p in paths:
        try:
            frames.append(pd.read_csv(p))
        except Exception as e:
            print(f"  WARN: {p}: {e}")
    if not frames:
        return pd.DataFrame()
    df = pd.concat(frames, ignore_index=True)
    df.columns = df.columns.str.strip()
    return df


def load_latencies(d, max_rows=2_000_000):
    paths = glob.glob(os.path.join(d, "*_latencies.csv"))
    if not paths:
        return pd.DataFrame()
    total = sum(max(0, sum(1 for _ in open(p)) - 1) for p in paths)
    skip = max(1, total // max_rows)
    frames = []
    for p in paths:
        try:
            frames.append(pd.read_csv(p, skiprows=lambda i: i > 0 and i % skip != 0))
        except Exception:
            pass
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def agg_by(df, extra_group=()):
    """Average across runs for (algo, threads, pin_strategy [, signature_mix, ...])."""
    grp_cols = ["algo", "threads", "pin_strategy"] + list(extra_group)
    grp_cols = [c for c in grp_cols if c in df.columns]
    agg_map = {}
    for col in ["ops_per_sec_total", "mean_ns_valid", "mean_ns_invalid",
                "median_ns_valid", "median_ns_invalid",
                "p99_ns_valid", "p99_ns_invalid", "p99_9_ns_valid", "p99_9_ns_invalid"]:
        if col in df.columns:
            agg_map[col] = (col, "mean")
    if not agg_map:
        return df
    return df.groupby(grp_cols, as_index=False).agg(**agg_map)


# ── Fig 1 — Verify throughput vs threads (valid path, compact) ──────────

def fig1_throughput(df, out_dir, fmt, dpi):
    agg = agg_by(df[df.get("signature_mix", pd.Series(["valid"]*len(df))) == "valid"]
                 if "signature_mix" in df.columns else df)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for algo in ["ed25519", "falcon512", "dilithium2"]:
        sub = agg[(agg["algo"] == algo) &
                  (agg.get("pin_strategy", pd.Series(["compact"]*len(agg))) == "compact")
                 ].sort_values("threads")
        if sub.empty:
            continue
        ax.plot(sub["threads"], sub["ops_per_sec_total"] / 1e6,
                "o-", color=ALGO_COLOR[algo], label=ALGO_LABEL[algo], linewidth=1.8, markersize=5)
    ax.axvline(24, color="gray", lw=0.8, ls=":", alpha=0.7)
    ax.axvline(48, color="gray", lw=0.8, ls=":", alpha=0.7)
    ax.set_xscale("log", base=2)
    ax.set_xticks(THREAD_TICKS)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_xlabel("Software threads")
    ax.set_ylabel("Total verify throughput (Mops/s)")
    ax.set_title("Fig 1 — Verify throughput vs thread count (valid signatures, compact)")
    ax.legend(); ax.grid(True, which="both", alpha=0.3)
    savefig(fig, out_dir, "fig1_verify_throughput", fmt, dpi)


# ── Fig 2 — Sign vs Verify side by side ──────────────────────────────────

def fig2_sign_vs_verify(exp2_df, exp1_dir, out_dir, fmt, dpi):
    if not exp1_dir:
        print("  SKIP fig2: --cross-experiment-input not provided")
        return
    sign_df = load_summaries(exp1_dir)
    if sign_df.empty:
        print("  SKIP fig2: no Experiment 1 summaries found")
        return

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.5), sharey=False)
    titles = ["Signing (Experiment 1)", "Verification (Experiment 2)"]
    dfs    = [sign_df, exp2_df]
    ycols  = ["ops_per_sec_total", "ops_per_sec_total"]

    for ax, df, title, ycol in zip(axes, dfs, titles, ycols):
        sub = df.copy()
        if "signature_mix" in sub.columns:
            sub = sub[sub["signature_mix"] == "valid"]
        if "pin_strategy" in sub.columns:
            sub = sub[sub["pin_strategy"] == "compact"]
        agg = sub.groupby(["algo", "threads"], as_index=False)[ycol].mean()
        for algo in ["ed25519", "falcon512", "dilithium2"]:
            s = agg[agg["algo"] == algo].sort_values("threads")
            if s.empty:
                continue
            ax.plot(s["threads"], s[ycol] / 1e6, "o-",
                    color=ALGO_COLOR[algo], label=ALGO_LABEL[algo], linewidth=1.8, markersize=5)
        ax.set_xscale("log", base=2)
        ax.set_xticks(THREAD_TICKS)
        ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
        ax.set_xlabel("Software threads")
        ax.set_ylabel("Throughput (Mops/s)")
        ax.set_title(title)
        ax.legend(fontsize=8); ax.grid(True, which="both", alpha=0.3)

    fig.suptitle("Fig 2 — Sign vs Verify throughput (compact, valid path)")
    fig.tight_layout()
    savefig(fig, out_dir, "fig2_sign_vs_verify", fmt, dpi)


# ── Fig 3 — Parallel efficiency (verify, valid) ───────────────────────────

def fig3_efficiency(df, out_dir, fmt, dpi):
    sub = df.copy()
    if "signature_mix" in sub.columns:
        sub = sub[sub["signature_mix"] == "valid"]
    if "pin_strategy" in sub.columns:
        sub = sub[sub["pin_strategy"] == "compact"]
    agg = sub.groupby(["algo", "threads"], as_index=False)["ops_per_sec_total"].mean()

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.axhline(1.0, color="black", lw=0.8, ls="--", label="Ideal")
    for algo in ["ed25519", "falcon512", "dilithium2"]:
        s = agg[agg["algo"] == algo].sort_values("threads")
        if s.empty or len(s[s["threads"] == 1]) == 0:
            continue
        ref = float(s[s["threads"] == 1]["ops_per_sec_total"].iloc[0])
        s = s.copy()
        s["efficiency"] = s["ops_per_sec_total"] / (s["threads"] * ref)
        ax.plot(s["threads"], s["efficiency"], "o-",
                color=ALGO_COLOR[algo], label=ALGO_LABEL[algo], linewidth=1.8, markersize=5)
    ax.set_xscale("log", base=2)
    ax.set_xticks(THREAD_TICKS)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_ylim(0, 1.15)
    ax.set_xlabel("Software threads")
    ax.set_ylabel("Parallel efficiency")
    ax.set_title("Fig 3 — Parallel efficiency (verify, valid path, compact)")
    ax.legend(); ax.grid(True, which="both", alpha=0.3)
    savefig(fig, out_dir, "fig3_efficiency", fmt, dpi)


# ── Fig 4 — Latency CDF, valid path ──────────────────────────────────────

def fig4_latency_cdf(lat, out_dir, fmt, dpi):
    if lat.empty:
        print("  SKIP fig4: no latency data")
        return
    lat.columns = lat.columns.str.strip()
    valid_lat = lat[lat.get("was_valid", pd.Series([1]*len(lat))) == 1] \
                if "was_valid" in lat.columns else lat
    thread_cuts = [1, 24, 96]
    algos = ["ed25519", "falcon512", "dilithium2"]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharey=True)
    for ax, algo in zip(axes, algos):
        sub = valid_lat[valid_lat["algo"] == algo]
        ax.set_title(ALGO_LABEL[algo])
        for tc in thread_cuts:
            rows = sub[sub["threads"] == tc]["latency_ns"] if "latency_ns" in sub.columns else pd.Series()
            if rows.empty:
                continue
            vals = np.sort(rows.values) / 1e3
            cdf  = np.arange(1, len(vals) + 1) / len(vals)
            ax.plot(vals, cdf, label=f"{tc} threads", linewidth=1.5)
        ax.set_xscale("log"); ax.set_xlabel("Latency (µs)")
        ax.grid(True, which="both", alpha=0.3); ax.legend(fontsize=8)
    axes[0].set_ylabel("CDF")
    fig.suptitle("Fig 4 — Verify latency CDF (valid signatures, threads ∈ {1, 24, 96})")
    fig.tight_layout()
    savefig(fig, out_dir, "fig4_latency_cdf_valid", fmt, dpi)


# ── Fig 5 — Valid vs invalid latency distribution ─────────────────────────

def fig5_valid_vs_invalid(lat, out_dir, fmt, dpi):
    if lat.empty or "was_valid" not in lat.columns:
        print("  SKIP fig5: no latency data with was_valid column")
        return
    lat.columns = lat.columns.str.strip()
    algos = ["ed25519", "falcon512", "dilithium2"]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharey=True)
    for ax, algo in zip(axes, algos):
        sub = lat[(lat["algo"] == algo) & (lat["threads"] == 24)]
        for valid_flag, label, color in [(1, "valid", "#1f77b4"), (0, "invalid", "#d62728")]:
            rows = sub[sub["was_valid"] == valid_flag]["latency_ns"] \
                   if "latency_ns" in sub.columns else pd.Series()
            if rows.empty:
                continue
            vals = np.sort(rows.values) / 1e3
            cdf  = np.arange(1, len(vals) + 1) / len(vals)
            ax.plot(vals, cdf, label=label, color=color, linewidth=1.5)
        ax.set_title(ALGO_LABEL[algo])
        ax.set_xscale("log"); ax.set_xlabel("Latency (µs)")
        ax.grid(True, which="both", alpha=0.3); ax.legend(fontsize=8)
    axes[0].set_ylabel("CDF")
    fig.suptitle("Fig 5 — Valid vs invalid path latency at threads=24\n"
                 "(diverging CDFs = fail-fast on invalid; overlapping = constant-time)")
    fig.tight_layout()
    savefig(fig, out_dir, "fig5_valid_vs_invalid", fmt, dpi)


# ── Fig 12 — THE BOTTLENECK PLOT ──────────────────────────────────────────

def fig12_bottleneck(df, exp1_dir, e2e_tps, out_dir, fmt, dpi):
    """Cross-experiment figure: verify ceiling vs end-to-end TPS ceiling."""
    fig, ax = plt.subplots(figsize=(8, 5))

    sub = df.copy()
    if "signature_mix" in sub.columns:
        sub = sub[sub["signature_mix"] == "valid"]
    if "pin_strategy" in sub.columns:
        sub = sub[sub["pin_strategy"] == "compact"]
    agg = sub.groupby(["algo", "threads"], as_index=False)["ops_per_sec_total"].mean()

    all_threads = sorted(agg["threads"].unique())
    max_verify = 0

    for algo in ["ed25519", "falcon512", "dilithium2"]:
        s = agg[agg["algo"] == algo].sort_values("threads")
        if s.empty:
            continue
        ax.plot(s["threads"], s["ops_per_sec_total"],
                "o-", color=ALGO_COLOR[algo], label=f"{ALGO_LABEL[algo]} (verify)",
                linewidth=2, markersize=6)
        max_verify = max(max_verify, s["ops_per_sec_total"].max())

    # End-to-end ceiling
    ax.axhline(e2e_tps, color="red", lw=2, ls="--",
               label=f"End-to-end TPS ceiling ({e2e_tps:,.0f} TPS, Apr 18 baseline)")

    # Annotate the gap
    if all_threads and max_verify > e2e_tps:
        t_last = max(all_threads)
        ax.annotate(
            f"Verify ceiling ≫ end-to-end ceiling\n"
            f"→ pipeline bottleneck is NOT verification",
            xy=(t_last, e2e_tps * 1.05),
            xytext=(t_last * 0.4, max_verify * 0.55),
            fontsize=9,
            arrowprops=dict(arrowstyle="->", color="red", lw=1.2),
            color="red"
        )

    ax.set_xscale("log", base=2)
    xticks = sorted(set(list(THREAD_TICKS) + list(all_threads)))
    ax.set_xticks(xticks)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_xlabel("Software threads")
    ax.set_ylabel("Operations per second")
    ax.set_title("Fig 12 — Verify throughput vs end-to-end pipeline ceiling\n"
                 "(Cascade Lake-R; turbo off; compact pinning)")
    ax.legend(loc="upper left"); ax.grid(True, which="both", alpha=0.3)
    ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: f"{x/1e6:.2f}M" if x >= 1e6 else f"{x:,.0f}"))

    savefig(fig, out_dir, "fig12_bottleneck", fmt, dpi)

    # Decision gate check
    if max_verify <= e2e_tps * 1.5:
        print("\n" + "!" * 70)
        print("  DECISION GATE: verify ceiling is CLOSE TO or BELOW the e2e ceiling!")
        print(f"  max verify = {max_verify:,.0f}  e2e ceiling = {e2e_tps:,.0f}")
        print("  The pipeline MAY be verify-bound. STOP and report to researcher.")
        print("!" * 70 + "\n")
        return False
    return True


# ── Main ─────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--input",                  required=True)
    p.add_argument("--out-dir",                default="")
    p.add_argument("--cross-experiment-input", default="",
                   help="Experiment 1 results dir for fig2, fig12")
    p.add_argument("--e2e-tps",               default=8750.0, type=float,
                   help="End-to-end TPS ceiling for fig12 [default 8750]")
    p.add_argument("--fmt",  default="pdf", choices=["pdf", "png"])
    p.add_argument("--dpi",  default=150,   type=int)
    args = p.parse_args()

    out_dir = args.out_dir or os.path.join(args.input, "figures")
    os.makedirs(out_dir, exist_ok=True)
    print(f"Input:   {args.input}")
    print(f"Figures: {out_dir}")

    print("\nLoading summary CSVs...")
    df = load_summaries(args.input)
    if df.empty:
        sys.exit("No summary CSVs found.")
    print(f"  {len(df)} rows")

    print("Loading latency CSVs...")
    lat = load_latencies(args.input)
    print(f"  {len(lat)} sampled rows")

    print("\nGenerating figures...")
    fig1_throughput(df, out_dir, args.fmt, args.dpi)
    fig2_sign_vs_verify(df, args.cross_experiment_input, out_dir, args.fmt, args.dpi)
    fig3_efficiency(df, out_dir, args.fmt, args.dpi)
    fig4_latency_cdf(lat, out_dir, args.fmt, args.dpi)
    fig5_valid_vs_invalid(lat, out_dir, args.fmt, args.dpi)

    gate_ok = fig12_bottleneck(df, args.cross_experiment_input,
                                args.e2e_tps, out_dir, args.fmt, args.dpi)

    n_figs = len(list(Path(out_dir).glob(f"*.{args.fmt}")))
    print(f"\nDone. {n_figs} figures in {out_dir}")
    if not gate_ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
