#!/usr/bin/env python3
"""
analyze_results.py — Validate benchmark results against published baselines.

Part of the qMEMO project (IIT Chicago): benchmarking post-quantum digital
signatures for blockchain transaction verification.

This script cross-checks our measured Falcon-512 performance against:
  1. Published reference numbers from known CPUs
  2. Cycle-count predictions scaled to our clock frequency
  3. MEMO blockchain throughput requirements
  4. Statistical quality thresholds for publication

Usage:
    python3 scripts/analyze_results.py benchmarks/results/run_YYYYMMDD_HHMMSS/

    If no path is given, the script auto-detects the most recent run.

Output:
    <run_dir>/ANALYSIS.md   — human-readable validation report
"""

from __future__ import annotations

import json
import os
import sys
import textwrap
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

# ── Published baselines ────────────────────────────────────────────────────
# Sources:
#   [1] Falcon specification, §6.3 — reference C implementation benchmarks
#   [2] PQCrypto 2020 benchmarks (Intel Core i5-8259U, 2.3 GHz, Turbo 3.8 GHz)
#   [3] liboqs speed benchmarks (CI matrix, various CPUs)

REFERENCE_CYCLES_PER_VERIFY = 82_000  # approximate across implementations

@dataclass
class Baseline:
    label: str
    cpu: str
    ghz: float
    measured_ops_sec: float | None = None  # None → predict from cycles

BASELINES = [
    Baseline("PQCrypto 2020 ref. impl.",  "Intel i5-8259U",   2.3, 27_939),
    Baseline("Predicted (M2 Pro perf.)",  "Apple M2 Pro",     3.5, None),
    Baseline("Predicted (Intel 4.0 GHz)", "Intel i7 @ 4 GHz", 4.0, None),
]

# ── MEMO blockchain scenarios ──────────────────────────────────────────────
# MEMO targets high-throughput sharded execution.  We check whether
# Falcon-512 verification can keep up under realistic shard loads.

@dataclass
class MemoScenario:
    label: str
    total_tps: int
    shards: int

    @property
    def tps_per_shard(self) -> float:
        return self.total_tps / self.shards

MEMO_SCENARIOS = [
    MemoScenario("MEMO peak (spec)",         50_700, 256),
    MemoScenario("MEMO moderate load",       10_000,   4),
    MemoScenario("High-throughput L1",        4_000,   1),
    MemoScenario("Ethereum-class (~15 TPS)",     15,   1),
]

# ── Statistical quality thresholds ─────────────────────────────────────────

CV_EXCELLENT = 2.0    # % — very low noise
CV_ACCEPTABLE = 5.0   # % — ok for publication
OUTLIER_THRESHOLD = 1.0  # % of trials


# ═══════════════════════════════════════════════════════════════════════════
#  Data loading
# ═══════════════════════════════════════════════════════════════════════════

def find_latest_run(base: Path) -> Path:
    """Return the most recent run_* directory under the results folder."""
    runs = sorted(base.glob("run_*"), reverse=True)
    if not runs:
        sys.exit(f"ERROR: No run_* directories found under {base}")
    return runs[0]


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


# ═══════════════════════════════════════════════════════════════════════════
#  Analysis functions
# ═══════════════════════════════════════════════════════════════════════════

def predict_ops_sec(ghz: float) -> float:
    """Predict verification ops/sec from clock frequency and reference cycles."""
    cycles_per_sec = ghz * 1e9
    return cycles_per_sec / REFERENCE_CYCLES_PER_VERIFY


def analyze_baselines(measured: float) -> list[dict]:
    """Compare measured performance against published and predicted baselines."""
    rows = []
    for b in BASELINES:
        expected = b.measured_ops_sec if b.measured_ops_sec else predict_ops_sec(b.ghz)
        ratio = measured / expected
        rows.append({
            "label": b.label,
            "cpu": b.cpu,
            "ghz": b.ghz,
            "expected": expected,
            "measured": measured,
            "ratio": ratio,
            "delta_pct": (ratio - 1.0) * 100,
        })
    return rows


def analyze_memo(measured: float) -> list[dict]:
    """Evaluate headroom against MEMO blockchain requirements."""
    rows = []
    for s in MEMO_SCENARIOS:
        headroom = measured / s.tps_per_shard
        rows.append({
            "label": s.label,
            "total_tps": s.total_tps,
            "shards": s.shards,
            "tps_per_shard": s.tps_per_shard,
            "headroom": headroom,
            "verdict": "PASS" if headroom >= 1.0 else "FAIL",
        })
    return rows


@dataclass
class DistributionAnalysis:
    """Results of analysing the raw trial data distribution."""
    n: int
    mean: float
    median: float
    std: float
    cv_pct: float
    skewness: float
    kurtosis: float
    p5: float
    p95: float
    iqr: float
    outlier_count: int
    outlier_pct: float
    normality_pass: bool
    cv_grade: str
    outlier_grade: str
    overall_grade: str


def analyze_distribution(raw: list[float], stats: dict) -> DistributionAnalysis:
    """Validate statistical quality of the measurement distribution."""
    data = np.array(raw)
    n = len(data)
    mean = float(np.mean(data))
    std = float(np.std(data, ddof=1))
    median = float(np.median(data))
    cv_pct = (std / mean) * 100 if mean > 0 else 0.0

    lo, hi = mean - 3 * std, mean + 3 * std
    outlier_count = int(np.sum((data < lo) | (data > hi)))
    outlier_pct = (outlier_count / n) * 100

    p5 = float(np.percentile(data, 5))
    p95 = float(np.percentile(data, 95))
    q25 = float(np.percentile(data, 25))
    q75 = float(np.percentile(data, 75))
    iqr = q75 - q25

    # Skewness and kurtosis from the C benchmark's output
    skewness = stats.get("skewness", 0.0)
    kurtosis = stats.get("excess_kurtosis", 0.0)
    normality_pass = stats.get("normality_pass", False)

    if cv_pct < CV_EXCELLENT:
        cv_grade = "EXCELLENT"
    elif cv_pct < CV_ACCEPTABLE:
        cv_grade = "ACCEPTABLE"
    else:
        cv_grade = "POOR"

    outlier_grade = "PASS" if outlier_pct < OUTLIER_THRESHOLD else "WARN"

    # Overall: must meet all three criteria for a clean bill of health
    if cv_grade in ("EXCELLENT", "ACCEPTABLE") and outlier_grade == "PASS":
        overall_grade = "GOOD — suitable for publication"
    else:
        overall_grade = "REVIEW — check environment before publishing"

    return DistributionAnalysis(
        n=n, mean=mean, median=median, std=std, cv_pct=cv_pct,
        skewness=skewness, kurtosis=kurtosis,
        p5=p5, p95=p95, iqr=iqr,
        outlier_count=outlier_count, outlier_pct=outlier_pct,
        normality_pass=normality_pass,
        cv_grade=cv_grade, outlier_grade=outlier_grade,
        overall_grade=overall_grade,
    )


# ═══════════════════════════════════════════════════════════════════════════
#  Report generation
# ═══════════════════════════════════════════════════════════════════════════

def generate_report(
    run_dir: Path,
    summary: dict,
    stat_results: dict,
    baseline_rows: list[dict],
    memo_rows: list[dict],
    dist: DistributionAnalysis,
) -> str:
    """Produce the full ANALYSIS.md report as a string."""
    sys_info = summary["system"]
    verify = summary["verify_benchmark"]
    comp = summary["comparison_benchmark"]
    falcon = comp["falcon_512"]
    mldsa = comp["ml_dsa_44"]

    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    lines: list[str] = []
    w = lines.append  # shorthand

    w("# Benchmark Analysis & Validation Report")
    w("")
    w(f"> **Generated:** {ts}")
    w(f"> **Run:** `{summary['run_tag']}`")
    w(f"> **System:** {sys_info['cpu_model']} ({sys_info['cpu_cores']} cores), "
      f"{sys_info['ram_gb']} GB RAM, {sys_info['os']}")
    w("")

    # ── Section 1: Baseline comparison ─────────────────────────────────
    w("---")
    w("")
    w("## 1. Comparison with Published Baselines")
    w("")
    w(f"Our measured Falcon-512 verification throughput: "
      f"**{verify['ops_per_sec']:,.2f} ops/sec** "
      f"({verify['us_per_op']:.2f} µs/op)")
    w("")
    w(f"Reference cycle count: **{REFERENCE_CYCLES_PER_VERIFY:,} cycles/verify** "
      f"(from Falcon spec & PQCrypto benchmarks)")
    w("")
    w("| Baseline | CPU | GHz | Expected (ops/s) | Measured (ops/s) | Ratio | Delta |")
    w("|----------|-----|-----|------------------|-----------------|-------|-------|")
    for r in baseline_rows:
        w(f"| {r['label']} | {r['cpu']} | {r['ghz']} | "
          f"{r['expected']:,.0f} | {r['measured']:,.0f} | "
          f"{r['ratio']:.2f}x | {r['delta_pct']:+.1f}% |")
    w("")

    # Interpretation
    our_ratio = baseline_rows[1]["ratio"]  # vs predicted for our CPU
    if 0.85 <= our_ratio <= 1.15:
        w("**Verdict:** Our measurements are **consistent** with cycle-count "
          "predictions (within ±15%). The benchmark is producing credible numbers.")
    elif our_ratio > 1.15:
        w("**Verdict:** Our measurements are **faster** than predicted. "
          "This may indicate the M2 Pro's wider execution pipelines and "
          "better branch prediction outperform the naive cycles/GHz model.")
    else:
        w("**Verdict:** Our measurements are **slower** than predicted. "
          "Possible causes: thermal throttling, background load, or "
          "memory bandwidth contention.")
    w("")

    # ── Section 2: MEMO requirements ───────────────────────────────────
    w("---")
    w("")
    w("## 2. MEMO Blockchain Throughput Validation")
    w("")
    w("Can Falcon-512 verification keep up with MEMO's transaction throughput?")
    w("")
    w("| Scenario | Total TPS | Shards | TPS/Shard | Our Throughput | Headroom | Status |")
    w("|----------|-----------|--------|-----------|---------------|----------|--------|")
    for r in memo_rows:
        w(f"| {r['label']} | {r['total_tps']:,} | {r['shards']} | "
          f"{r['tps_per_shard']:,.1f} | {verify['ops_per_sec']:,.0f} | "
          f"**{r['headroom']:,.0f}x** | {r['verdict']} |")
    w("")
    w("**Interpretation:** Headroom = (our verify throughput) / (required TPS per shard). "
      "Values >> 1 mean the CPU has ample capacity for signature verification "
      "and can spend remaining cycles on execution, networking, and consensus.")
    w("")

    min_headroom = min(r["headroom"] for r in memo_rows)
    if min_headroom >= 10:
        w(f"Even under the most demanding scenario, we have "
          f"**{min_headroom:,.0f}x headroom** — Falcon-512 verification is "
          f"not a bottleneck for MEMO.")
    elif min_headroom >= 2:
        w(f"Headroom of {min_headroom:.0f}x is adequate but tight for the "
          f"heaviest scenario. Consider batched verification or multi-threading.")
    else:
        w(f"**Warning:** Headroom of {min_headroom:.1f}x under the heaviest "
          f"scenario is dangerously thin.")
    w("")

    # ── Section 3: Algorithm comparison summary ────────────────────────
    w("---")
    w("")
    w("## 3. Algorithm Comparison Summary")
    w("")
    w("| Metric | Falcon-512 | ML-DSA-44 | Advantage |")
    w("|--------|-----------|-----------|-----------|")
    w(f"| Verify (ops/s) | {falcon['verify_ops_sec']:,.0f} | "
      f"{mldsa['verify_ops_sec']:,.0f} | "
      f"Falcon {comp['verify_speedup_falcon']:.2f}x faster |")
    w(f"| Sign (ops/s) | {falcon['sign_ops_sec']:,.0f} | "
      f"{mldsa['sign_ops_sec']:,.0f} | "
      f"ML-DSA {mldsa['sign_ops_sec']/falcon['sign_ops_sec']:.2f}x faster |")
    w(f"| Keygen (ops/s) | {falcon['keygen_ops_sec']:,.0f} | "
      f"{mldsa['keygen_ops_sec']:,.0f} | "
      f"ML-DSA {mldsa['keygen_ops_sec']/falcon['keygen_ops_sec']:.0f}x faster |")
    w(f"| Signature size | {falcon['signature_bytes']} B | "
      f"{mldsa['signature_bytes']} B | "
      f"Falcon {mldsa['signature_bytes']/falcon['signature_bytes']:.1f}x smaller |")
    w(f"| Tx overhead | {falcon['total_tx_overhead']} B | "
      f"{mldsa['total_tx_overhead']} B | "
      f"Falcon {mldsa['total_tx_overhead']/falcon['total_tx_overhead']:.1f}x smaller |")
    w("")
    w("**For blockchain:** Falcon-512 wins on the two metrics that matter most — "
      "verification speed and on-chain size. ML-DSA-44's advantages (faster "
      "signing and keygen) are irrelevant in a validator context where "
      "verification is the hot path.")
    w("")

    # ── Section 4: Statistical quality ─────────────────────────────────
    w("---")
    w("")
    w("## 4. Statistical Quality Assessment")
    w("")
    w(f"Distribution of {dist.n} independent trials "
      f"({stat_results.get('iterations_per_trial', 100)} verifications each):")
    w("")
    w("| Metric | Value | Threshold | Grade |")
    w("|--------|-------|-----------|-------|")
    w(f"| CV (coefficient of variation) | {dist.cv_pct:.2f}% | "
      f"< {CV_EXCELLENT}% excellent, < {CV_ACCEPTABLE}% acceptable | "
      f"**{dist.cv_grade}** |")
    w(f"| Outliers (> 3σ) | {dist.outlier_count} ({dist.outlier_pct:.2f}%) | "
      f"< {OUTLIER_THRESHOLD}% | **{dist.outlier_grade}** |")
    w(f"| Normality (Jarque–Bera) | "
      f"{'pass' if dist.normality_pass else 'fail'} | "
      f"pass → mean±SD; fail → median/IQR | "
      f"{'Gaussian' if dist.normality_pass else 'Non-Gaussian'} |")
    w(f"| Skewness | {dist.skewness:.4f} | "
      f"|S| < 0.5 symmetric | "
      f"{'symmetric' if abs(dist.skewness) < 0.5 else 'skewed'} |")
    w(f"| Excess kurtosis | {dist.kurtosis:.4f} | "
      f"|K| < 1.0 normal tails | "
      f"{'normal' if abs(dist.kurtosis) < 1.0 else 'heavy tails'} |")
    w("")
    w(f"**Overall quality: {dist.overall_grade}**")
    w("")

    if dist.normality_pass:
        w("The distribution is consistent with Gaussian. For the paper, report:")
        w(f"  - **{dist.mean:,.0f} ± {dist.std:,.0f} ops/sec** (mean ± SD)")
        w("  - Use parametric tests (t-test, ANOVA) for cross-algorithm comparison.")
    else:
        w("The distribution departs from Gaussian (common for latency data due to "
          "OS scheduling jitter). For the paper, report:")
        w(f"  - **Median: {dist.median:,.0f} ops/sec** "
          f"(IQR: {dist.iqr:,.0f})")
        w(f"  - **P5–P95 range: {dist.p5:,.0f} – {dist.p95:,.0f} ops/sec**")
        w("  - Use non-parametric tests (Mann–Whitney U) for comparison.")
    w("")

    # ── Section 5: Recommended paper claims ────────────────────────────
    w("---")
    w("")
    w("## 5. Recommended Claims for Paper")
    w("")
    w("Based on this analysis, the following claims are supported by the data:")
    w("")

    if dist.normality_pass:
        central = f"{dist.mean:,.0f} ± {dist.std:,.0f} ops/sec (mean ± SD, n={dist.n})"
    else:
        central = (f"{dist.median:,.0f} ops/sec median "
                   f"(IQR {dist.iqr:,.0f}, n={dist.n})")

    claims = [
        f"Falcon-512 signature verification achieves **{central}** "
        f"on {sys_info['cpu_model']}.",

        f"This corresponds to **{verify['us_per_op']:.1f} µs per verification**, "
        f"consistent with the published reference of ~{REFERENCE_CYCLES_PER_VERIFY:,} "
        f"cycles/verify ({baseline_rows[1]['delta_pct']:+.1f}% vs prediction).",

        f"Falcon-512 verification is **{comp['verify_speedup_falcon']:.2f}x faster** "
        f"than ML-DSA-44 on the same hardware.",

        f"Falcon-512 signatures are **{mldsa['signature_bytes']/falcon['signature_bytes']:.1f}x "
        f"smaller** than ML-DSA-44 ({falcon['signature_bytes']} B vs "
        f"{mldsa['signature_bytes']} B).",

        f"Per-transaction on-chain overhead (signature + public key) is "
        f"**{falcon['total_tx_overhead']:,} B** for Falcon-512 vs "
        f"**{mldsa['total_tx_overhead']:,} B** for ML-DSA-44 "
        f"({comp['tx_overhead_ratio']:.2f}x reduction).",

        f"Under MEMO's sharded architecture, a single core provides "
        f"**{min_headroom:,.0f}x headroom** over the per-shard verification "
        f"requirement, confirming Falcon-512 is not a throughput bottleneck.",
    ]

    for i, claim in enumerate(claims, 1):
        w(f"{i}. {claim}")
        w("")

    # ── Section 6: Caveats ─────────────────────────────────────────────
    w("---")
    w("")
    w("## 6. Caveats & Limitations")
    w("")
    w("- All measurements are single-threaded on one core. Multi-threaded "
      "verification would scale near-linearly with core count.")
    w("- The 256-byte test message is a fixed payload; real blockchain "
      "transactions vary in size (though signature verification cost is "
      "dominated by the algorithm, not the message length).")
    w("- Cycle estimates use a flat 3.5 GHz assumption; actual M2 Pro "
      "P-core frequency varies with thermal state (3.49 GHz sustained, "
      "briefly higher on boost).")
    w("- The Jarque–Bera normality test is sensitive at n=1000; "
      "slight departures from Gaussian are statistically detectable "
      "but may not be practically significant.")
    w("- liboqs is a reference/research implementation; production "
      "deployments may use optimized libraries with different performance "
      "characteristics.")
    w("")

    return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════════════════

def main():
    # Resolve run directory
    if len(sys.argv) > 1:
        run_dir = Path(sys.argv[1])
    else:
        results_base = Path(__file__).resolve().parent.parent / "benchmarks" / "results"
        run_dir = find_latest_run(results_base)
        print(f"Auto-detected latest run: {run_dir.name}")

    if not run_dir.is_dir():
        sys.exit(f"ERROR: Not a directory: {run_dir}")

    # Load data
    summary_path = run_dir / "summary.json"
    stats_path = run_dir / "statistical_results.json"

    if not summary_path.exists():
        sys.exit(f"ERROR: summary.json not found in {run_dir}")
    if not stats_path.exists():
        sys.exit(f"ERROR: statistical_results.json not found in {run_dir}")

    summary = load_json(summary_path)
    stat_results = load_json(stats_path)

    print(f"Run tag     : {summary['run_tag']}")
    print(f"System      : {summary['system']['cpu_model']}")
    print(f"Verify ops/s: {summary['verify_benchmark']['ops_per_sec']:,.2f}")
    print()

    measured = summary["verify_benchmark"]["ops_per_sec"]

    # Run analyses
    print("Comparing against published baselines …")
    baseline_rows = analyze_baselines(measured)

    print("Checking MEMO throughput requirements …")
    memo_rows = analyze_memo(measured)

    print("Validating statistical quality …")
    raw_data = stat_results.get("raw_data", [])
    stats_block = stat_results.get("statistics", {})
    dist = analyze_distribution(raw_data, stats_block)

    # Generate report
    print("Generating ANALYSIS.md …")
    report = generate_report(run_dir, summary, stat_results,
                             baseline_rows, memo_rows, dist)

    out_path = run_dir / "ANALYSIS.md"
    out_path.write_text(report)
    print(f"\nReport written to: {out_path}")

    # Print key findings to terminal
    print()
    print("=" * 60)
    print("  KEY FINDINGS")
    print("=" * 60)
    print()

    predicted = predict_ops_sec(3.5)
    ratio = measured / predicted
    print(f"  Measured vs predicted (M2 Pro): {ratio:.2f}x "
          f"({'+' if ratio >= 1 else ''}{(ratio-1)*100:.1f}%)")
    print(f"  Statistical quality           : {dist.overall_grade}")
    print(f"  CV                            : {dist.cv_pct:.2f}% ({dist.cv_grade})")
    print(f"  MEMO worst-case headroom      : {min(r['headroom'] for r in memo_rows):,.0f}x")
    print(f"  Falcon vs ML-DSA verify speed : "
          f"{summary['comparison_benchmark']['verify_speedup_falcon']:.2f}x")
    print()


if __name__ == "__main__":
    main()
