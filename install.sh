#!/usr/bin/env bash
# =============================================================================
# Core programming language - installer
#
#   ./install.sh                  build + install to /usr/local
#   ./install.sh --prefix DIR     build + install under DIR (e.g. ~/.local)
#   ./install.sh --skip-tests     skip the test suite before installing
#   ./install.sh --uninstall      remove an installed toolchain
#   ./install.sh --help
#
# Layout produced (prefix=/usr/local by default):
#   $prefix/bin/core          the compiler
#   $prefix/lib/core/corert.o runtime object (linked into every program)
#   $prefix/lib/core/std/*.cr standard library sources
# =============================================================================
set -euo pipefail

PREFIX="/usr/local"
SKIP_TESTS=0
UNINSTALL=0
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix|-p)
      [[ $# -ge 2 ]] || fail "--prefix needs a directory argument"
      PREFIX="$2"; shift 2 ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    --uninstall)  UNINSTALL=1; shift ;;
    --help|-h)    sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)            fail "unknown option '$1' (see --help)" ;;
  esac
done

# ---------------------------------------------------------------- uninstall --
if [[ $UNINSTALL -eq 1 ]]; then
  log "Removing Core from $PREFIX"
  rm -f "$PREFIX/bin/core"
  rm -rf "$PREFIX/lib/core"
  ok "removed $PREFIX/bin/core and $PREFIX/lib/core"
  exit 0
fi

# ------------------------------------------------------------ prerequisites --
log "Checking prerequisites"

command -v cmake  >/dev/null 2>&1 || fail "cmake not found - install it (e.g. sudo apt install cmake)"
command -v g++    >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || \
                    fail "no C++ compiler found - install build-essential or clang"
command -v cc     >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || \
                    fail "no C compiler found - needed to link your programs"
command -v git    >/dev/null 2>&1 || fail "git not found - install it"

LLVM_CONFIG="$(command -v llvm-config || command -v llvm-config-18 || true)"
if [[ -z "$LLVM_CONFIG" ]]; then
  fail "llvm-config not found - install LLVM 18 development packages:
       Debian/Ubuntu:  sudo apt install llvm-18-dev
       Fedora:         sudo dnf install llvm18-devel
       Arch:           sudo pacman -S llvm18"
fi
LLVM_VERSION="$("$LLVM_CONFIG" --version)"
ok "cmake $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')"
ok "LLVM $LLVM_VERSION at $(dirname "$LLVM_CONFIG")"

if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
  fail "this script must run from the Core repository (CMakeLists.txt not found in $SOURCE_DIR)"
fi

# -------------------------------------------------------------------- build --
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build}"
log "Configuring (build dir: $BUILD_DIR)"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
  > /dev/null
ok "cmake configured"

log "Building (this may take a few minutes)"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
if ! cmake --build "$BUILD_DIR" -j"$JOBS" > /tmp/core_build.log 2>&1; then
  tail -30 /tmp/core_build.log
  fail "build failed - see /tmp/core_build.log"
fi
ok "built $BUILD_DIR/core"

"$BUILD_DIR/core" version

# -------------------------------------------------------------------- tests --
if [[ $SKIP_TESTS -eq 0 ]]; then
  log "Running the test suite (use --skip-tests to skip)"
  if python3 "$SOURCE_DIR/tests/run_tests.py" --core "$BUILD_DIR/core" > /tmp/core_tests.log 2>&1; then
    tail -1 /tmp/core_tests.log
  else
    tail -20 /tmp/core_tests.log
    fail "tests failed - install aborted (fix or use --skip-tests)"
  fi
else
  log "Skipping tests (--skip-tests)"
fi

# ------------------------------------------------------------------- install --
log "Installing to $PREFIX"
if [[ -d "$PREFIX/bin" && ! -w "$PREFIX/bin" ]] && [[ "$(id -u)" != 0 ]]; then
  SUDO="sudo"
  log "need permission to write to $PREFIX - using sudo"
else
  SUDO=""
fi

$SUDO cmake --install "$BUILD_DIR" --prefix "$PREFIX" > /dev/null
$SUDO chmod 755 "$PREFIX/bin/core"
ok "installed $PREFIX/bin/core"
ok "installed $PREFIX/lib/core/{corert.o, std/}"

# ------------------------------------------------------------------ verify --
if ! command -v core >/dev/null 2>&1; then
  log "note: $PREFIX/bin is not on your PATH; add 'export PATH=$PREFIX/bin:\$PATH'"
fi
VERIFY_DIR="$(mktemp -d)"
printf 'func main() {\n    say "install verified"\n}\n' > "$VERIFY_DIR/hello.cr"
if (cd "$VERIFY_DIR" && core compile hello hello.cr > /dev/null 2>&1 || "$PREFIX/bin/core" compile hello hello.cr > /dev/null 2>&1) \
   && [[ "$(cd "$VERIFY_DIR" && ./hello)" == "install verified" ]]; then
  ok "compile+run verified"
else
  fail "post-install verification failed"
fi
rm -rf "$VERIFY_DIR"

echo
log "Core is installed. Try it:"
echo "  core init myproject && cd myproject && core run"
echo "  docs: $SOURCE_DIR/docs/learning/introduction.md"
