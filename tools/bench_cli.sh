#!/usr/bin/env bash
# bench_cli.sh — CLI benchmark: neospice vs ngspice
#
# Measures end-to-end wall-clock time for both simulators on the same netlist.
# Uses date +%s.%N for nanosecond-resolution timing. Reports median, min, max.
#
# Usage:
#   ./tools/bench_cli.sh [circuit.cir] [warmup] [runs]
#
# Defaults:
#   circuit  = tests/circuits/ths4131_diff_amp.cir
#   warmup   = 5
#   runs     = 50

set -euo pipefail

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CIRCUIT="${1:-$REPO_DIR/tests/circuits/ths4131_diff_amp.cir}"
WARMUP="${2:-5}"
RUNS="${3:-50}"

NEOSPICE="$REPO_DIR/build-release/neospice"
NGSPICE="$(which ngspice 2>/dev/null || echo "")"

# Temp dir for output files (cleaned up on exit)
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# --- Helpers ---
now_ns() { date +%s.%N; }

median_of() {
    # Read values from array variable name, compute median
    local -n arr=$1
    local sorted
    IFS=$'\n' sorted=($(printf '%s\n' "${arr[@]}" | sort -g))
    local n=${#sorted[@]}
    if (( n == 0 )); then echo "0"; return; fi
    if (( n % 2 == 1 )); then
        echo "${sorted[$((n/2))]}"
    else
        python3 -c "print((${sorted[$((n/2-1))]} + ${sorted[$((n/2))]}) / 2.0)"
    fi
}

stats_of() {
    local -n arr=$1
    local n=${#arr[@]}
    if (( n == 0 )); then
        echo "no data"
        return
    fi
    local sorted
    IFS=$'\n' sorted=($(printf '%s\n' "${arr[@]}" | sort -g))
    local med
    if (( n % 2 == 1 )); then
        med="${sorted[$((n/2))]}"
    else
        med=$(python3 -c "print((${sorted[$((n/2-1))]} + ${sorted[$((n/2))]}) / 2.0)")
    fi
    local min_v="${sorted[0]}"
    local max_v="${sorted[$((n-1))]}"
    local sum
    sum=$(python3 -c "print(sum([${arr[*]/%/,}]))")
    local mean
    mean=$(python3 -c "print($sum / $n)")
    echo "$med $min_v $max_v $mean $n"
}

format_time() {
    # Input: time in seconds. Output: human-readable.
    python3 -c "
t = $1
if t < 0.001:
    print(f'{t*1e6:.0f} us')
elif t < 1.0:
    print(f'{t*1e3:.2f} ms')
else:
    print(f'{t:.3f} s')
"
}

# --- Validation ---
if [[ ! -f "$CIRCUIT" ]]; then
    echo "Error: circuit file not found: $CIRCUIT" >&2
    exit 1
fi

if [[ ! -x "$NEOSPICE" ]]; then
    echo "Error: neospice binary not found at $NEOSPICE" >&2
    echo "  Build with: cmake --build build-release -j\$(nproc)" >&2
    exit 1
fi

if [[ -z "$NGSPICE" ]]; then
    echo "Warning: ngspice not found in PATH, skipping ngspice benchmarks" >&2
fi

# --- Header ---
echo "================================================================="
echo "  CLI Benchmark: neospice vs ngspice"
echo "================================================================="
echo ""
echo "  Circuit:  $(basename "$CIRCUIT")"
echo "  Warmup:   $WARMUP iterations"
echo "  Measured: $RUNS iterations"
echo "  neospice: $NEOSPICE"
if [[ -n "$NGSPICE" ]]; then
    echo "  ngspice:  $NGSPICE ($(ngspice --version 2>&1 | head -1 | sed 's/\*//g' | xargs))"
fi
echo ""

# --- Verify both produce output ---
echo "Verifying simulators produce output..."
"$NEOSPICE" "$CIRCUIT" -o "$TMPDIR/neo_verify.raw" > /dev/null 2>&1
neo_out_files=$(ls "$TMPDIR"/neo_verify*.raw 2>/dev/null)
if [[ -z "$neo_out_files" ]]; then
    echo "Error: neospice did not produce output" >&2
    exit 1
fi
neo_bytes=$(cat $neo_out_files | wc -c)
echo "  neospice: OK ($neo_bytes bytes across $(echo "$neo_out_files" | wc -l) file(s))"

if [[ -n "$NGSPICE" ]]; then
    "$NGSPICE" -b -r "$TMPDIR/ng_verify.raw" "$CIRCUIT" > /dev/null 2>&1
    if [[ ! -f "$TMPDIR/ng_verify.raw" ]]; then
        echo "  ngspice:  FAILED (skipping)" >&2
        NGSPICE=""
    else
        echo "  ngspice:  OK ($(wc -c < "$TMPDIR/ng_verify.raw") bytes)"
    fi
fi
echo ""

# --- Benchmark neospice ---
echo "Benchmarking neospice..."
echo -n "  Warmup: "
for (( i=0; i<WARMUP; i++ )); do
    "$NEOSPICE" "$CIRCUIT" -o "$TMPDIR/neo_warmup.raw" > /dev/null 2>&1
    echo -n "."
done
echo " done"

neo_times=()
echo -n "  Measuring ($RUNS runs): "
for (( i=0; i<RUNS; i++ )); do
    t0=$(now_ns)
    "$NEOSPICE" "$CIRCUIT" -o "$TMPDIR/neo_bench.raw" > /dev/null 2>&1
    t1=$(now_ns)
    dt=$(python3 -c "print($t1 - $t0)")
    neo_times+=("$dt")
    if (( (i+1) % 10 == 0 )); then echo -n "."; fi
done
echo " done"

read -r neo_med neo_min neo_max neo_mean neo_n <<< "$(stats_of neo_times)"
echo ""
echo "  neospice results:"
echo "    Median: $(format_time "$neo_med")"
echo "    Min:    $(format_time "$neo_min")"
echo "    Max:    $(format_time "$neo_max")"
echo "    Mean:   $(format_time "$neo_mean")"
echo "    Runs:   $neo_n"
echo ""

# --- Benchmark ngspice ---
if [[ -n "$NGSPICE" ]]; then
    echo "Benchmarking ngspice..."
    echo -n "  Warmup: "
    for (( i=0; i<WARMUP; i++ )); do
        "$NGSPICE" -b -r "$TMPDIR/ng_warmup.raw" "$CIRCUIT" > /dev/null 2>&1
        echo -n "."
    done
    echo " done"

    ng_times=()
    echo -n "  Measuring ($RUNS runs): "
    for (( i=0; i<RUNS; i++ )); do
        t0=$(now_ns)
        "$NGSPICE" -b -r "$TMPDIR/ng_bench.raw" "$CIRCUIT" > /dev/null 2>&1
        t1=$(now_ns)
        dt=$(python3 -c "print($t1 - $t0)")
        ng_times+=("$dt")
        if (( (i+1) % 10 == 0 )); then echo -n "."; fi
    done
    echo " done"

    read -r ng_med ng_min ng_max ng_mean ng_n <<< "$(stats_of ng_times)"
    echo ""
    echo "  ngspice results:"
    echo "    Median: $(format_time "$ng_med")"
    echo "    Min:    $(format_time "$ng_min")"
    echo "    Max:    $(format_time "$ng_max")"
    echo "    Mean:   $(format_time "$ng_mean")"
    echo "    Runs:   $ng_n"
    echo ""
fi

# --- Comparison ---
echo "================================================================="
echo "  Summary"
echo "================================================================="
echo ""
printf "  %-12s %12s %12s %12s\n" "" "Median" "Min" "Max"
printf "  %-12s %12s %12s %12s\n" "neospice" "$(format_time "$neo_med")" "$(format_time "$neo_min")" "$(format_time "$neo_max")"

if [[ -n "$NGSPICE" ]]; then
    printf "  %-12s %12s %12s %12s\n" "ngspice" "$(format_time "$ng_med")" "$(format_time "$ng_min")" "$(format_time "$ng_max")"
    echo ""
    speedup=$(python3 -c "print(f'{$ng_med / $neo_med:.1f}')")
    echo "  Speedup (median): ${speedup}x"
    echo ""
    echo "  Note: Both timings include process startup, netlist parsing,"
    echo "        simulation, and .raw file I/O. neospice additionally"
    echo "        pays the same fork/exec overhead as ngspice in this"
    echo "        CLI comparison (unlike the in-process C++ benchmark)."
fi
echo ""
