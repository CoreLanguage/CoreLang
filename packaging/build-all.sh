#!/usr/bin/env bash
# Build deb + rpm layout + copy apk/aur scaffolds. Usage: build-all.sh VERSION TARBALL
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:?usage: build-all.sh VERSION TARBALL}"
TARBALL="$2"
OUT="$ROOT/dist/packages/$VERSION"
mkdir -p "$OUT"
bash "$ROOT/packaging/build-deb.sh" "$VERSION" "$TARBALL" "$OUT"
# RPM tree
mkdir -p "$OUT/rpm"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cp "$TARBALL" "$OUT/rpm/SOURCES/core-linux-x86_64.tar.gz"
sed "s/^Version:.*/Version:        $VERSION/" "$ROOT/packaging/rpm/core.spec" > "$OUT/rpm/SPECS/core.spec"
cp "$ROOT/packaging/apk/APKBUILD" "$OUT/" 2>/dev/null || true
cp "$ROOT/packaging/aur/PKGBUILD" "$OUT/PKGBUILD.aur" 2>/dev/null || true
echo "---"
echo "deb built. For rpm run on a Fedora/RHEL host:"
echo "  rpmbuild --define '_topdir $OUT/rpm' -bb $OUT/rpm/SPECS/core.spec"
echo "For apk/aur see packaging/README.md"
