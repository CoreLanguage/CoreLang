#!/usr/bin/env bash
# Build a release tarball for the current machine architecture.
#
#   scripts/build-release.sh              -> dist/core-linux-<arch>.tar.gz
#   scripts/build-release.sh --out DIR    -> custom output directory
#
# The tarball contains:
#   bin/core          the compiler (static LLVM, stripped)
#   bin/corepkg       the registry package manager
#   lib/core/corert.o runtime object
#   lib/core/std/     standard library sources
#   LICENSE, README.md
#
# This is the artifact that install.sh downloads and unpacks into /usr/local.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${REPO_ROOT}/dist"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT_DIR="$2"; shift 2 ;;
    *) echo "usage: $0 [--out DIR]" >&2; exit 1 ;;
  esac
done
mkdir -p "$OUT_DIR"

case "$(uname -m)" in
  x86_64)  ARCH="x86_64" ;;
  aarch64|arm64) ARCH="aarch64" ;;
  riscv64) ARCH="riscv64" ;;
  *) echo "error: unsupported architecture '$(uname -m)' (releases cover x86_64, aarch64, riscv64)" >&2; exit 1 ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> building (static LLVM, Release)"
cmake -S "$REPO_ROOT" -B "$TMP/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$TMP/build" -j"$(nproc)" >/dev/null

STAGE="$TMP/stage"
mkdir -p "$STAGE/bin" "$STAGE/lib/core/std"
install -m 755 "$TMP/build/core" "$STAGE/bin/core"
strip "$STAGE/bin/core"
install -m 755 "${REPO_ROOT}/tools/corepkg/corepkg" "$STAGE/bin/corepkg"
install -m 644 "$TMP/build/corert.o" "$STAGE/lib/core/corert.o"
install -m 644 "${REPO_ROOT}"/std/*.cr "$STAGE/lib/core/std/"
install -m 644 "$REPO_ROOT/LICENSE" "$REPO_ROOT/README.md" "$STAGE/"

TARBALL="core-linux-${ARCH}.tar.gz"
tar -czf "$OUT_DIR/$TARBALL" -C "$STAGE" bin lib LICENSE README.md
echo "built $OUT_DIR/$TARBALL ($(du -h "$OUT_DIR/$TARBALL" | cut -f1))"
