#!/usr/bin/env bash
# Drive bench_perf with `perf stat` for cycles / IPC / branch-miss / cache.
# Usage:  bash benchmarks/run_perf.sh                  # all scenarios
#         bash benchmarks/run_perf.sh enabled_int       # one scenario
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="${ROOT}/build/benchmarks/bench_perf"
[[ -x "$BIN" ]] || { echo "build first: cmake --build build -j" >&2; exit 1; }

declare -A ITERS=(
    [disabled]=200000000
    [if_false]=200000000
    [every_n_100]=100000000
    [enabled_string]=10000000
    [enabled_int]=10000000
    [enabled_mixed5]=5000000
    [rotating]=2000000
    [devnull_writev]=2000000
)

EVENTS="task-clock,cycles,instructions,branches,branch-misses"
EVENTS="${EVENTS},cache-references,cache-misses"

run_one() {
    local scen="$1"
    local n="${ITERS[$scen]}"
    echo "===== $scen  (iters=$n) ====="
    perf stat -e "$EVENTS" -- "$BIN" "$scen" "$n" 2>&1 \
        | sed -n '/Performance counter stats/,/seconds elapsed/p'
    echo
}

if [[ $# -ge 1 ]]; then
    run_one "$1"
else
    for s in disabled if_false every_n_100 enabled_string enabled_int \
             enabled_mixed5 rotating devnull_writev; do
        run_one "$s"
    done
fi
