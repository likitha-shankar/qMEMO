#!/bin/bash
# setup_plots.sh - Generate persistent farmer plots for benchmarking.
#
# Generates plots for SIG_SCHEMEs in {1=Ed25519, 2=Falcon-512, 4=ML-DSA-44}
# using farmer wallets named farmer1..farmerN. Plots are saved to
# plots_persistent/{address_hex}_k{k}.plot
#
# Idempotent: re-running detects existing plot files and skips generation.
# Use --regenerate to force regeneration (deletes plots_persistent/ first).
#
# Usage:
#   ./setup_plots.sh                  # k=16, 1 farmer, all 3 schemes
#   ./setup_plots.sh --k 18 --farmers 4  # k=18, farmers 1-4, all 3 schemes
#   ./setup_plots.sh --regenerate
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLOTS_DIR="$SCRIPT_DIR/plots_persistent"
MANIFEST="$PLOTS_DIR/manifest.txt"

K_PARAM=16
NUM_FARMERS=1
SCHEMES="1 2 4"
REGEN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --k|--k-param) K_PARAM="$2"; shift 2 ;;
        --farmers)     NUM_FARMERS="$2"; shift 2 ;;
        --schemes)     SCHEMES="$2"; shift 2 ;;
        --regenerate)  REGEN=1; shift ;;
        -h|--help)
            sed -n "2,15p" "$0"; exit 0 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [ "$REGEN" = "1" ] && [ -d "$PLOTS_DIR" ]; then
    echo "[setup_plots] --regenerate: removing $PLOTS_DIR"
    rm -rf "$PLOTS_DIR"
fi

# Idempotent check: count expected plots
EXPECTED=$(( NUM_FARMERS * $(echo $SCHEMES | wc -w) ))
EXISTING=$(ls -1 "$PLOTS_DIR"/*.plot 2>/dev/null | wc -l || echo 0)

if [ "$EXISTING" -ge "$EXPECTED" ] && [ -f "$MANIFEST" ]; then
    echo "[setup_plots] Plots already present ($EXISTING files, expected $EXPECTED). Skipping."
    echo "[setup_plots] Manifest:"
    cat "$MANIFEST" | sed "s/^/  /"
    exit 0
fi

mkdir -p "$PLOTS_DIR"
cd "$SCRIPT_DIR"

declare -A SCHEME_NAME=([1]="ed25519" [2]="falcon512" [4]="mldsa44")

GEN_START=$(date +%s)
for SCHEME in $SCHEMES; do
    NAME="${SCHEME_NAME[$SCHEME]}"
    echo ""
    echo "[setup_plots] === Building SIG_SCHEME=$SCHEME ($NAME) ==="
    make clean >/dev/null 2>&1 || true
    if ! make SIG_SCHEME=$SCHEME 2>&1 | tail -3 | grep -qE "(error:)" ; then
        : # ok
    fi
    if [ ! -x ./build/validator ]; then
        echo "[setup_plots] BUILD FAILED for SIG_SCHEME=$SCHEME"
        exit 1
    fi

    for i in $(seq 1 $NUM_FARMERS); do
        FARMER="farmer$i"
        echo "[setup_plots] -- Generating plot for $FARMER (scheme=$NAME, k=$K_PARAM)"
        ./build/wallet create "$FARMER" >/dev/null 2>&1 || true
        # Run validator in --generate-plot-only mode (no sockets, no farming loop)
        ./build/validator -k $K_PARAM --generate-plot-only "$FARMER" 2>&1 | \
            grep -E "Plot loaded|Plot saved|Plot ready|generate-plot-only|ERROR" | head -5
    done
done
GEN_END=$(date +%s)
GEN_DUR=$((GEN_END - GEN_START))

# Build manifest
PLOT_COUNT=$(ls -1 "$PLOTS_DIR"/*.plot 2>/dev/null | wc -l)
PLOT_SIZE=$(ls -la "$PLOTS_DIR"/*.plot 2>/dev/null | awk "{print \$5}" | head -1)
HOSTNAME=$(hostname)
TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)

cat > "$MANIFEST" << EOF
hostname:        $HOSTNAME
generated_at:    $TS
generation_time: ${GEN_DUR}s
k_param:         $K_PARAM
num_farmers:     $NUM_FARMERS
schemes:         $SCHEMES
plot_count:      $PLOT_COUNT
plot_size_bytes: $PLOT_SIZE
EOF

echo ""
echo "[setup_plots] Done. Manifest:"
cat "$MANIFEST" | sed "s/^/  /"
echo ""
echo "[setup_plots] Plots ($PLOT_COUNT files):"
ls -la "$PLOTS_DIR"/*.plot
