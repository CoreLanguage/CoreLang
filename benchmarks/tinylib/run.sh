#!/usr/bin/env bash
# tinylib race: small library (heap / matrix / number parsing) in Core vs C++.
# Same workload, same LLVM backend, min of 5 runs, outputs verified identical.
set -euo pipefail
OPT="${1:--O2}"
CORE_BIN="${CORE:-$(cd "$(dirname "$0")/../.." && pwd)/build/core}"
DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$TMP"
"$CORE_BIN" compile core_bench "$DIR/core/main.cr" "$OPT" --force > /dev/null 2>&1
g++ "$OPT" -o cpp_bench "$DIR/cpp/main.cpp"

out_core="$(./core_bench)"
out_cpp="$(./cpp_bench)"
if [[ "$out_core" != "$out_cpp" ]]; then
  echo "OUTPUT MISMATCH"; echo "core: $out_core"; echo "cpp:  $out_cpp"; exit 1
fi

echo "=== tinylib ($OPT, min of 5 runs, outputs verified identical) ==="
best_c=999 best_p=999
for i in 1 2 3 4 5; do
  t=$( { /usr/bin/time -f "%e" ./core_bench > /dev/null; } 2>&1 )
  best_c=$(awk -v a="$best_c" -v b="$t" 'BEGIN{print (b<a)?b:a}')
  t=$( { /usr/bin/time -f "%e" ./cpp_bench > /dev/null; } 2>&1 )
  best_p=$(awk -v a="$best_p" -v b="$t" 'BEGIN{print (b<a)?b:a}')
done
awk -v c="$best_c" -v p="$best_p" 'BEGIN{
  ratio = (c > 0) ? p / c : 0;
  printf "core %5.2fs   c++ %5.2fs   %s\n", c, p,
    (ratio > 1.15 ? "core faster" : (ratio < 0.87 ? "c++ faster" : "even"));
}'
