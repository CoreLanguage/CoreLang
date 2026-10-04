#!/usr/bin/env bash
# =============================================================================
# Core programming language - installer
#
#   curl -fsSL <release-url>/install.sh | bash
#
# Detects your CPU architecture, downloads the matching release tarball,
# and installs the compiler to /usr/local (bin/core + lib/core/...).
#
# Options (env vars):
#   CORE_INSTALL_PREFIX=/usr/local   where to install (default /usr/local)
#   CORE_RELEASE_URL=...             where to download from
#   CORE_UNINSTALL=1                 remove an installed toolchain
# =============================================================================
set -euo pipefail

PREFIX="${CORE_INSTALL_PREFIX:-/usr/local}"
# Where to download from. The tarballs live in the repository itself
# (dist/core-linux-<arch>.tar.gz), fetched from raw.githubusercontent.com.
# Override with CORE_RELEASE_URL to serve from anywhere else.
BASE_URL="${CORE_RELEASE_URL:-https://raw.githubusercontent.com/snitchbossdotcom/corelang/main/dist}"
AUTH=()
if [[ -n "${GH_TOKEN:-}" ]]; then
  AUTH=(-H "Authorization: Bearer $GH_TOKEN")
fi

say()  { printf '%s\n' "$*"; }
ok()   { printf '\033[1;32m ok\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------- uninstall ----
if [[ "${CORE_UNINSTALL:-0}" = "1" ]]; then
    say "Removing Core from $PREFIX"
    rm -f "$PREFIX/bin/core"
    rm -rf "$PREFIX/lib/core"
    ok "removed"
    exit 0
fi

# ------------------------------------------------------------ platform ------
case "$(uname -s):$(uname -m)" in
    Linux:x86_64)          ARCH="x86_64" ;;
    Linux:aarch64|Linux:arm64) ARCH="aarch64" ;;
    Linux:riscv64)         ARCH="riscv64" ;;
    Linux:*)               fail "unsupported CPU architecture: $(uname -m). Core releases cover x86_64, aarch64 and riscv64 (64-bit only)." ;;
    *)                     fail "unsupported platform: $(uname -s). Core releases are Linux-only." ;;
esac

# ------------------------------------------------------------ fetch ---------
TARBALL="core-linux-${ARCH}.tar.gz"
URL="${BASE_URL%/}/$TARBALL"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say "Installing Core (linux/${ARCH})"
say "  from $URL"
say "  into $PREFIX"

FETCH=""
for c in curl wget; do
    command -v "$c" >/dev/null 2>&1 && FETCH="$c" && break
done
[[ -n "$FETCH" ]] || fail "neither curl nor wget found - install one and retry"

case "$FETCH" in
    curl) curl -fSL --retry 3 "${AUTH[@]}" -o "$TMP/$TARBALL" "$URL" || \
            fail "download failed: $URL
If the repo is private, export GH_TOKEN=<your token> with the repo scope,
or set CORE_RELEASE_URL to a location you control." ;;
    wget) wget -q --header="${AUTH:+Authorization: Bearer $GH_TOKEN}" -O "$TMP/$TARBALL" "$URL" || \
            fail "download failed: $URL
If the repo is private, export GH_TOKEN=<your token> with the repo scope,
or set CORE_RELEASE_URL to a location you control." ;;
esac

# ------------------------------------------------------------ unpack --------
tar -xzf "$TMP/$TARBALL" -C "$TMP"
[[ -x "$TMP/bin/core" ]] || fail "tarball is missing bin/core - corrupted download?"

# ------------------------------------------------------------ install -------
if [[ ! -w "$PREFIX" ]]; then
    [[ "$(id -u)" = "0" ]] || SUDO="sudo"
fi
[[ "${SUDO:-}" = "sudo" ]] && say "  (need root for $PREFIX - asking for your password)"

mkdir -p "$PREFIX/bin" "$PREFIX/lib"
$SUDO rm -f  "$PREFIX/bin/core"
$SUDO rm -rf "$PREFIX/lib/core"
$SUDO cp -r "$TMP/bin"     "$PREFIX/"
$SUDO cp -r "$TMP/lib/core" "$PREFIX/lib/"

# ------------------------------------------------------------ verify --------
if ! "$PREFIX/bin/core" version >/dev/null 2>&1; then
    fail "installed binary does not run - your Linux may be missing base libraries (libz, libzstd, libtinfo)"
fi

ok "installed $($PREFIX/bin/core version)"
say
say "Get started:"
say "  core init myproject && cd myproject && core run"
