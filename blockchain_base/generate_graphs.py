#!/usr/bin/env python3
"""
Generate benchmark graphs for MEMO blockchain PoS system.

Graph 1: Submission progress (time vs cumulative TXs per farmer)
Graph 2: Per-block pipeline timing (Gantt-style stacked bars)
Graph 3: Block size scaling (parametric: 10, 100, 1000, 10000 TXs/block)

Usage:
    python3 generate_graphs.py <benchmark_dir> [--graph3-dir <dir>]
"""

import sys
import os
import csv
import re

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    import numpy as np
except ImportError:
    print("ERROR: matplotlib not installed. Run: pip3 install matplotlib")
    sys.exit(1)


def strip_ansi(text):
    """Remove ANSI escape sequences from text."""
    return re.sub(r'\x1b\[[0-9;]*m', '', str(text))


def safe_int(val, default=0):
    """Safely convert to int, stripping any non-numeric chars."""
    try:
        cleaned = re.sub(r'[^\d\-]', '', strip_ansi(str(val)))
        return int(cleaned) if cleaned else default
    except (ValueError, TypeError):
        return default


def safe_float(val, default=0.0):
    """Safely convert to float, stripping ANSI codes."""
    try:
        cleaned = strip_ansi(str(val)).strip()
        return float(cleaned) if cleaned else default
    except (ValueError, TypeError):
        return default


def parse_csv(filepath):
    """Parse a CSV file into list of dicts, stripping ANSI codes."""
    rows = []
    if not os.path.exists(filepath):
        return rows
    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            cleaned = {k: strip_ansi(v) for k, v in row.items()}
            rows.append(cleaned)
    return rows


def generate_graph1(benchmark_dir, output_path):
    csv_path = os.path.join(benchmark_dir, 'graph1_submit_progress.csv')
    rows = parse_csv(csv_path)
    if not rows:
        print(f"    Warning: No data in {csv_path}")
        return False

    farmers = {}
    for row in rows:
        farmer = row.get('farmer', 'unknown')
        if farmer not in farmers:
            farmers[farmer] = {'time': [], 'txs': []}
        farmers[farmer]['time'].append(safe_float(row.get('time_sec', 0)))
        farmers[farmer]['txs'].append(safe_int(row.get('cumulative_txs', 0)))

    fig, ax = plt.subplots(figsize=(12, 6))
    colors = plt.cm.Set3(np.linspace(0, 1, max(len(farmers), 1)))

    for idx, (farmer, data) in enumerate(sorted(farmers.items())):
        ax.plot(data['time'], data['txs'], '-',
                color=colors[idx % len(colors)], alpha=0.7, linewidth=1.5,
                label=farmer)

    all_times = [t for f in farmers.values() for t in f['time']]
    if all_times:
        max_time = max(all_times)
        total_txs = sum(max(f['txs']) for f in farmers.values() if f['txs'])
        if max_time > 0:
            rate = total_txs / max_time
            ax.annotate(f'Aggregate: {rate:,.0f} TX/s',
                        xy=(max_time * 0.6, total_txs * 0.85),
                        fontsize=12, fontweight='bold', color='#00d4ff')

    ax.set_xlabel('Time (seconds)', fontsize=12)
    ax.set_ylabel('Cumulative Transactions Submitted', fontsize=12)
    ax.set_title('Transaction Submission Progress (Phase A)', fontsize=14, fontweight='bold')
    ax.legend(loc='upper left', fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_facecolor('#1a1a2e')
    fig.patch.set_facecolor('#16213e')
    ax.tick_params(colors='white')
    ax.xaxis.label.set_color('white')
    ax.yaxis.label.set_color('white')
    ax.title.set_color('white')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
    plt.close()
    print(f"    ✓ Graph 1 saved: {output_path}")
    return True


def generate_graph2(benchmark_dir, output_path):
    csv_path = os.path.join(benchmark_dir, 'graph2_block_timing.csv')
    rows = parse_csv(csv_path)
    if not rows:
        print(f"    Warning: No data in {csv_path}")
        return False

    blocks = []
    for row in rows:
        tx_count = safe_int(row.get('tx_count', 0))
        if tx_count > 0:
            blocks.append({
                'height': safe_int(row.get('block_height', 0)),
                'tx_count': tx_count,
                'get_hash': safe_int(row.get('get_hash_ms', 0)),
                'fetch_tx': safe_int(row.get('fetch_tx_ms', 0)),
                'serialize_send': safe_int(row.get('serialize_send_ms', 0)),
                'confirm': safe_int(row.get('confirm_ms', 0)),
                'total': safe_int(row.get('total_ms', 0)),
            })

    if not blocks:
        print("    Warning: No blocks with transactions found")
        return False

    # Deduplicate by height (keep winner = highest tx_count)
    seen = {}
    for b in blocks:
        h = b['height']
        if h not in seen or b['tx_count'] > seen[h]['tx_count']:
            seen[h] = b
    blocks = sorted(seen.values(), key=lambda b: b['height'])

    # Limit to last 30 blocks
    if len(blocks) > 30:
        blocks = blocks[-30:]

    fig, ax = plt.subplots(figsize=(14, max(6, len(blocks) * 0.35)))

    step_colors = {
        'get_hash': '#00ff88', 'fetch_tx': '#ff4444',
        'serialize_send': '#00d4ff', 'confirm': '#ffaa00',
    }
    step_labels = {
        'get_hash': 'GET_HASH', 'fetch_tx': 'Fetch TXs',
        'serialize_send': 'Serialize+Send', 'confirm': 'CONFIRM',
    }

    y_labels = [f"#{b['height']} ({b['tx_count']}tx)" for b in blocks]

    for idx, block in enumerate(blocks):
        left = 0
        for step in ['get_hash', 'fetch_tx', 'serialize_send', 'confirm']:
            w = block[step]
            if w > 0:
                ax.barh(idx, w, left=left, height=0.6,
                        color=step_colors[step], alpha=0.85)
            left += w

    patches = [mpatches.Patch(color=step_colors[k], label=step_labels[k]) for k in step_colors]
    ax.legend(handles=patches, loc='upper right', fontsize=9)

    if blocks:
        avg_t = np.mean([b['total'] for b in blocks])
        avg_f = np.mean([b['fetch_tx'] for b in blocks])
        avg_s = np.mean([b['serialize_send'] for b in blocks])
        avg_c = np.mean([b['confirm'] for b in blocks])
        tot_f = sum(b['fetch_tx'] for b in blocks)
        tot_s = sum(b['serialize_send'] for b in blocks)
        tot_c = sum(b['confirm'] for b in blocks)
        tot_t = tot_f + tot_s + tot_c + sum(b['get_hash'] for b in blocks)
        n = len(blocks)
        ax.annotate(
            f'Avg: {avg_t:.0f}ms total | Fetch: {avg_f:.0f}ms | Ser: {avg_s:.0f}ms | Conf: {avg_c:.0f}ms\n'
            f'Total ({n} blocks): {tot_t:.0f}ms | Fetch: {tot_f:.0f}ms | Ser: {tot_s:.0f}ms | Conf: {tot_c:.0f}ms',
            xy=(0.98, 0.02), xycoords='axes fraction',
            fontsize=9, color='white', ha='right', va='bottom',
            bbox=dict(boxstyle='round', facecolor='#333', alpha=0.8))

    ax.set_yticks(range(len(blocks)))
    ax.set_yticklabels(y_labels, fontsize=8)
    ax.set_xlabel('Time (ms)', fontsize=12)
    ax.set_title('Per-Block Pipeline Timing (Phase B)', fontsize=14, fontweight='bold')
    ax.invert_yaxis()
    ax.grid(True, axis='x', alpha=0.3)
    ax.set_facecolor('#1a1a2e')
    fig.patch.set_facecolor('#16213e')
    ax.tick_params(colors='white')
    ax.xaxis.label.set_color('white')
    ax.title.set_color('white')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
    plt.close()
    print(f"    ✓ Graph 2 saved: {output_path}")
    return True


def generate_graph3(data_dir, output_path):
    csv_path = os.path.join(data_dir, 'graph3_scaling.csv')
    rows = parse_csv(csv_path)
    if not rows:
        print(f"    Warning: No data in {csv_path}")
        return False

    configs = []
    for row in rows:
        configs.append({
            'max_txs': safe_int(row.get('max_txs_per_block', 0)),
            'get_hash': safe_float(row.get('avg_get_hash_ms', 0)),
            'fetch_tx': safe_float(row.get('avg_fetch_tx_ms', 0)),
            'serialize_send': safe_float(row.get('avg_serialize_send_ms', 0)),
            'confirm': safe_float(row.get('avg_confirm_ms', 0)),
            'total': safe_float(row.get('avg_total_ms', 0)),
            'process_tps': safe_float(row.get('process_tps', 0)),
        })
    configs.sort(key=lambda c: c['max_txs'])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))
    x = np.arange(len(configs))
    x_labels = [str(c['max_txs']) for c in configs]
    width = 0.6

    bottoms = np.zeros(len(configs))
    steps = ['get_hash', 'fetch_tx', 'serialize_send', 'confirm']
    colors = ['#00ff88', '#ff4444', '#00d4ff', '#ffaa00']
    labels = ['GET_HASH', 'Fetch TXs', 'Serialize+Send', 'CONFIRM']

    for step, color, label in zip(steps, colors, labels):
        vals = [c[step] for c in configs]
        ax1.bar(x, vals, width, bottom=bottoms, color=color, label=label, alpha=0.85)
        bottoms += np.array(vals)

    ax1.set_xlabel('Max TXs per Block', fontsize=12)
    ax1.set_ylabel('Average Time per Block (ms)', fontsize=12)
    ax1.set_title('Block Processing Time by Component', fontsize=13, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(x_labels)
    ax1.legend(fontsize=9)
    ax1.grid(True, axis='y', alpha=0.3)

    total_times = [c['total'] for c in configs]
    process_tps = [c['process_tps'] for c in configs]

    ax2.bar(x, total_times, width, color='#7c3aed', alpha=0.85, label='Total Block Time (ms)')
    ax2.set_xlabel('Max TXs per Block', fontsize=12)
    ax2.set_ylabel('Total Block Time (ms)', fontsize=12, color='#7c3aed')
    ax2.set_xticks(x)
    ax2.set_xticklabels(x_labels)

    ax3 = ax2.twinx()
    ax3.plot(x, process_tps, 'o-', color='#00ff88', linewidth=2, markersize=8, label='Processing TPS')
    ax3.set_ylabel('Processing TPS', fontsize=12, color='#00ff88')
    ax2.set_title('Scaling: Block Time vs Throughput', fontsize=13, fontweight='bold')

    for axis in [ax1, ax2]:
        axis.set_facecolor('#1a1a2e')
        axis.tick_params(colors='white')
        axis.xaxis.label.set_color('white')
        axis.yaxis.label.set_color('white')
        axis.title.set_color('white')
    ax3.tick_params(colors='#00ff88')
    ax3.yaxis.label.set_color('#00ff88')
    fig.patch.set_facecolor('#16213e')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
    plt.close()
    print(f"    ✓ Graph 3 saved: {output_path}")
    return True


def generate_graph4(benchmark_dir, output_path):
    """Graph 4: Full Metronome Round Pipeline (1-second breakdown)"""
    csv_path = os.path.join(benchmark_dir, 'graph4_round_timing.csv')
    rows = parse_csv(csv_path)
    if not rows:
        print(f"    Warning: No data in {csv_path}")
        return False

    rounds = []
    for row in rows:
        rounds.append({
            'height': safe_int(row.get('block_height', 0)),
            'proofs': safe_int(row.get('proof_count', 0)),
            'challenge': safe_int(row.get('challenge_ms', 0)),
            'proof': safe_int(row.get('proof_ms', 0)),
            'block': safe_int(row.get('block_ms', 0)),
            'sleep': safe_int(row.get('sleep_ms', 0)),
        })

    if not rounds:
        print("    Warning: No round timing data found")
        return False

    # Deduplicate by height (keep first occurrence)
    seen = {}
    for r in rounds:
        h = r['height']
        if h not in seen:
            seen[h] = r
    rounds = sorted(seen.values(), key=lambda r: r['height'])

    # Limit to last 30 rounds
    if len(rounds) > 30:
        rounds = rounds[-30:]

    fig, ax = plt.subplots(figsize=(16, max(6, len(rounds) * 0.35)))

    step_colors = {
        'challenge': '#00ff88',   # Green - challenge broadcast
        'proof': '#ff4444',       # Red - proof collection window
        'block': '#00d4ff',       # Blue - winner + block creation + confirm
        'sleep': '#7c3aed',       # Purple - idle sleep until next tick
    }
    step_labels = {
        'challenge': 'Challenge Broadcast',
        'proof': 'Proof Collection',
        'block': 'Winner→Block→Confirm',
        'sleep': 'Sleep (idle)',
    }

    y_labels = []
    for r in rounds:
        proofs_str = f"{r['proofs']}p" if r['proofs'] > 0 else "0p"
        y_labels.append(f"#{r['height']} ({proofs_str})")

    for idx, r in enumerate(rounds):
        left = 0
        for step in ['challenge', 'proof', 'block', 'sleep']:
            w = r[step]
            if w > 0:
                ax.barh(idx, w, left=left, height=0.6,
                        color=step_colors[step], alpha=0.85)
            left += w

    patches = [mpatches.Patch(color=step_colors[k], label=step_labels[k]) for k in step_colors]
    ax.legend(handles=patches, loc='upper right', fontsize=9)

    # Add 1000ms reference line
    ax.axvline(x=1000, color='#ffaa00', linestyle='--', alpha=0.7, linewidth=1.5)
    ax.text(1005, -0.5, '1000ms target', color='#ffaa00', fontsize=8, va='top')

    if rounds:
        avg_c = np.mean([r['challenge'] for r in rounds])
        avg_p = np.mean([r['proof'] for r in rounds])
        avg_b = np.mean([r['block'] for r in rounds])
        avg_s = np.mean([r['sleep'] for r in rounds])
        avg_total = avg_c + avg_p + avg_b + avg_s
        ax.annotate(
            f'Avg round: {avg_total:.0f}ms = {avg_c:.0f}ms challenge + {avg_p:.0f}ms proof + '
            f'{avg_b:.0f}ms block + {avg_s:.0f}ms sleep\n'
            f'Proof window: {avg_p:.0f}ms ({avg_p/10:.1f}%) | '
            f'Block overhead: {avg_b:.0f}ms ({avg_b/10:.1f}%) | '
            f'Idle: {avg_s:.0f}ms ({avg_s/10:.1f}%)',
            xy=(0.98, 0.02), xycoords='axes fraction',
            fontsize=9, color='white', ha='right', va='bottom',
            bbox=dict(boxstyle='round', facecolor='#333', alpha=0.8))

    ax.set_yticks(range(len(rounds)))
    ax.set_yticklabels(y_labels, fontsize=8)
    ax.set_xlabel('Time (ms)', fontsize=12)
    ax.set_title('Full Metronome Round Pipeline (1-second breakdown)', fontsize=14, fontweight='bold')
    ax.invert_yaxis()
    ax.grid(True, axis='x', alpha=0.3)
    ax.set_facecolor('#1a1a2e')
    fig.patch.set_facecolor('#16213e')
    ax.tick_params(colors='white')
    ax.xaxis.label.set_color('white')
    ax.title.set_color('white')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
    plt.close()
    print(f"    ✓ Graph 4 saved: {output_path}")
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 generate_graphs.py <benchmark_dir> [--graph3-dir <dir>]")
        sys.exit(1)

    benchmark_dir = sys.argv[1]
    graph3_dir = None
    for i, arg in enumerate(sys.argv):
        if arg == '--graph3-dir' and i + 1 < len(sys.argv):
            graph3_dir = sys.argv[i + 1]

    print(f"Generating graphs from: {benchmark_dir}")
    print()

    # Only generate graph1/graph2/graph4 if data files exist AND not in graph3-only mode
    if not graph3_dir:
        g1_csv = os.path.join(benchmark_dir, 'graph1_submit_progress.csv')
        g2_csv = os.path.join(benchmark_dir, 'graph2_block_timing.csv')
        g4_csv = os.path.join(benchmark_dir, 'graph4_round_timing.csv')
        
        if os.path.exists(g1_csv) and os.path.getsize(g1_csv) > 0:
            generate_graph1(benchmark_dir, os.path.join(benchmark_dir, 'graph1_submission_progress.png'))
        
        if os.path.exists(g2_csv) and os.path.getsize(g2_csv) > 0:
            generate_graph2(benchmark_dir, os.path.join(benchmark_dir, 'graph2_block_pipeline.png'))
        
        if os.path.exists(g4_csv) and os.path.getsize(g4_csv) > 0:
            generate_graph4(benchmark_dir, os.path.join(benchmark_dir, 'graph4_metronome_pipeline.png'))

    target = graph3_dir or benchmark_dir
    if os.path.exists(os.path.join(target, 'graph3_scaling.csv')):
        generate_graph3(target, os.path.join(target, 'graph3_block_scaling.png'))


if __name__ == '__main__':
    main()
