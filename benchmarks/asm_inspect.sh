#!/usr/bin/env bash
# Disassemble bench_perf's hot functions to verify the disabled path
# really compiles to "load + cmp + jne" and the enabled path is tight.
#
# Usage: bash benchmarks/asm_inspect.sh [run_disabled|run_enabled_int|...]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="${ROOT}/build/benchmarks/bench_perf"
[[ -x "$BIN" ]] || { echo "build first: cmake --build build -j" >&2; exit 1; }

FN="${1:-run_disabled}"

# Resolve symbol address (handle anonymous-namespace mangling).
ADDR=$(nm "$BIN" | awk -v fn="$FN" '
    $2 ~ /^[Tt]$/ {
        sym = $0
        sub(/^[0-9a-f]+ [Tt] /, "", sym)
        # demangle the symbol via c++filt for easy matching
        cmd = "echo \047" sym "\047 | c++filt"
        cmd | getline dem
        close(cmd)
        if (dem ~ fn) { print $1; exit }
    }')
if [[ -z "$ADDR" ]]; then
    echo "symbol matching '${FN}' not found in $BIN" >&2
    exit 1
fi

echo "===== $FN @ 0x$ADDR ====="
objdump -d --no-show-raw-insn -C --start-address="0x${ADDR}" "$BIN" \
    | awk '/^[0-9a-f]+ <.*>:/{n++; if(n==2) exit} {print}'
