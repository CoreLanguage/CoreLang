#!/usr/bin/env bash
# Build a .deb from a release tarball. Usage: build-deb.sh VERSION TARBALL [OUTDIR]
set -euo pipefail
VERSION="${1:?usage: build-deb.sh VERSION TARBALL [OUTDIR]}"
TARBALL="$2"
OUTDIR="${3:-dist}"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) DEBARCH="amd64" ;; aarch64|arm64) DEBARCH="arm64" ;; riscv64) DEBARCH="riscv64" ;;
  *) echo "unsupported arch $ARCH" >&2; exit 1 ;;
esac
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/core_${VERSION}_${DEBARCH}/DEBIAN" "$STAGE/pkg"
tar -xzf "$TARBALL" -C "$STAGE/pkg"
mkdir -p "$STAGE/core_${VERSION}_${DEBARCH}/usr/local"
cp -r "$STAGE/pkg/bin" "$STAGE/core_${VERSION}_${DEBARCH}/usr/local/"
mkdir -p "$STAGE/core_${VERSION}_${DEBARCH}/usr/local/lib"
cp -r "$STAGE/pkg/lib/core" "$STAGE/core_${VERSION}_${DEBARCH}/usr/local/lib/"
cat > "$STAGE/core_${VERSION}_${DEBARCH}/DEBIAN/control" <<EOF
Package: core
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${DEBARCH}
Maintainer: Core Language <packages@core-lang.example>
Description: Core compiled systems programming language
 A statically typed, compiled language with LLVM backend,
 manual memory management and a minimal runtime.
EOF
dpkg-deb --build "$STAGE/core_${VERSION}_${DEBARCH}" "$OUTDIR/"
echo "built $OUTDIR/core_${VERSION}_${DEBARCH}.deb"
