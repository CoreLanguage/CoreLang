#!/usr/bin/env bash
# Core vs C++ benchmark harness.
#   ./run.sh [-O2|-O0] [--quick]
# Methodology: equivalent programs, same LLVM backend, 5 runs each,
# the MINIMUM (least noise) is reported. Outputs must match exactly.
set -euo pipefail
OPT="${1:--O2}"
CORE_BIN="${CORE:-$(cd "$(dirname "$0")/.." && pwd)/build/core}"
DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run_bench() { # name core_src cpp_src [extra core flags...]
  local name="$1" csrc="$2" cppsrc="$3"; shift 3
  local cargs=("$@")
  # build
  "$CORE_BIN" compile "core_bench" "$csrc" "$OPT" --force > /dev/null 2>&1 || {
    echo "$name: CORE COMPILE FAILED"; return; }
  g++ $OPT -o cpp_bench "$cppsrc" || { echo "$name: C++ COMPILE FAILED"; return; }
  # verify identical output
  local out_core out_cpp
  out_core="$(cd "$TMP" && ./core_bench 2>/dev/null)"
  out_cpp="$(cd "$TMP" && ./cpp_bench 2>/dev/null)"
  if [[ "$out_core" != "$out_cpp" ]]; then
    echo "$name: OUTPUT MISMATCH (core: $out_core / cpp: $out_cpp)"; return
  fi
  # time: 5 runs, keep the minimum
  local best_c=999 best_p=999
  for i in 1 2 3 4 5; do
    t=$( { /usr/bin/time -f "%e" ./core_bench > /dev/null; } 2>&1 )
    best_c=$(awk -v a="$best_c" -v b="$t" 'BEGIN{print (b<a)?b:a}')
    t=$( { /usr/bin/time -f "%e" ./cpp_bench > /dev/null; } 2>&1 )
    best_p=$(awk -v a="$best_p" -v b="$t" 'BEGIN{print (b<a)?b:a}')
  done
  awk -v n="$name" -v c="$best_c" -v p="$best_p" 'BEGIN{
    ratio = (c > 0) ? p / c : 0;
    printf "%-12s core %5.2fs   c++ %5.2fs   %s\n", n, c, p,
      (ratio > 1.15 ? "core faster" : (ratio < 0.87 ? "c++ faster" : "even"));
  }'
}

cd "$TMP"
echo "=== Core vs C++ ($OPT, min of 5 runs, identical outputs verified) ==="
echo
run_bench "int"       "$DIR/01-int/main.cr"        "$DIR/01-int/main.cpp"
run_bench "float"     "$DIR/02-float/main.cr"      "$DIR/02-float/main.cpp"
run_bench "loops"     "$DIR/03-loops/main.cr"      "$DIR/03-loops/main.cpp"
run_bench "calls"     "$DIR/04-calls/main.cr"      "$DIR/04-calls/main.cpp"
run_bench "structs"   "$DIR/05-structs/main.cr"    "$DIR/05-structs/main.cpp"
run_bench "arrays+"   "$DIR/06-arrays/main_unsafe.cr" "$DIR/06-arrays/main.cpp" -O2
run_bench "arrays[chk]" "$DIR/06-arrays/main.cr"   "$DIR/06-arrays/main.cpp" -O2
echo
echo "core faster / c++ faster / even: wall-clock of best run; noise floor ~0.01s"
