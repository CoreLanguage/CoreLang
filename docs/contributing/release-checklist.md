# Release checklist

From a clean clone to a verified release of the `core` compiler.

## 1. Clean-clone build

```sh
git clone <repo> core-release-check && cd core-release-check
git checkout v<VERSION>            # the release commit
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Verify the compiler identifies itself:

```sh
./build/core version
# Core compiler 0.1.1 (LLVM 18.x backend)
```

Note the LLVM patch version in the output — releases pin and document it
(`CMakeLists.txt` requires `LLVM 18.1`).

## 2. Full test suite

```sh
python3 tests/run_tests.py --core build/core
```

- Must end `N passed, 0 failed`.
- Run it twice in a row (catches clobbered temp-state assumptions).
- Optionally run `ctest --test-dir build` to exercise the CMake
  integration path with the just-built binary.

## 3. Examples build and run

Every directory under `examples/` is user-facing documentation; release
artifacts must not break them. For each example project (hello,
calculator, fibonacci, sorting, pointers, memory, structs, oop, generics,
modules, packages, threads, atomics, ffi, inline-asm, allocator, driver,
kernel):

```sh
cd examples/<name>
<build/run per its README>     # packages/ uses a mock git repo; kernel uses --freestanding
```

At minimum: `core build` (or the `compile` line from its README) exits 0.

## 4. Optimization matrix

The test suite covers this, but do one manual sweep with a
non-trivial program (e.g. `examples/fibonacci`):

```sh
for opt in -O0 -O1 -O2 -O3 -Os; do
  core compile fib-$opt src/main.cr $opt && ./fib-$opt
done
```

All five must produce identical program output.

## 5. Debug-info sanity

```sh
core compile app src/main.cr --debug
gdb -batch -ex "break main" -ex run ./app | grep "main () at"
```

Must show a `.cr` file/line (this is asserted by the suite; verify once
manually anyway since GDB versions vary).

## 6. Cross and freestanding smoke test

```sh
core compile cross src/main.cr --target=aarch64   # stops at .coreobj.o with a notice
file cross.coreobj.o                              # ELF, AArch64
core compile kernel src/kernel.cr --freestanding --emit-object
nm kernel | grep _start                            # custom entry present
```

## 7. Project & package flow

```sh
mkdir /tmp/relproj && cd /tmp/relproj
core init relproj
cd relproj && core run                      # "Hello, Core!"
core check
# local-repo package round trip (see tests/run_tests.py test_packages):
core install /path/to/local/repo && core list && core remove <pkg>
```

`core install` from a *network* repo is exercised manually before tagging
(git clone + semver tag resolution + lockfile pinning).

## 8. Fresh-machine checks

The driver locates `corert.o` and `std/` relative to the binary
(`findCompilerData`: `$CORE_HOME` → `../lib/core` → `lib/core` → exe dir).
Verify the *installed layout* works, not just the build tree:

```sh
cmake --install build --prefix /tmp/core-install     # bin/core, lib/core/corert.o, lib/core/std
/tmp/core-install/bin/core compile app src/main.cr && ./app
CORE_HOME=/tmp/core-install/lib/core /tmp/core-install/bin/core check src/main.cr
```

## 9. Version and docs

- `src/Main.cpp` prints the version string
  (`"Core compiler 0.1.1 (LLVM %s backend)"`) — bump it deliberately.
- `docs/LANGUAGE-REFERENCE-SUMMARY.md` is the ground truth for what the
  language does; update it (including the "Known limitations" section) in
  the release commit.
- Update `docs/compiler/*`, `docs/internals/*` if internals changed.
- Tag with semver: `git tag v<VERSION>` (tags drive `core install`
  resolution — the format must stay `vX.Y.Z`-parseable by
  `Project::parseSemVer`).

## 10. Release commit hygiene

```sh
git status --short            # no stray build artifacts (build/ is not tracked)
git clean -ndx | review       # confirm nothing needed would be cleaned
```

Commit message convention in this repo: short imperative subject lines
(see `git log` — e.g. "Test suite green: 97 checks pass").

## 11. After tagging

- Build artifacts to ship: the `core` binary, `corert.o`, and `std/*.cr`
  (the install() rules in `CMakeLists.txt` define the layout — matching
  it keeps `findCompilerData` working for users).
- Announce the LLVM version requirement and any new CLI flags
  (`printCompileHelp` in `Main.cpp` is user-facing documentation).

## Common failure modes

- **Built examples, not clean clone.** Stale `build/` hides CMake breakage
  (`CMakeLists.txt` reglobs sources only on reconfigure); always verify
  from a fresh clone.
- **LLVM version drift.** A distro that ships LLVM 19 will fail
  `find_package(LLVM 18.1)` — that failure is a release blocker to
  document, not to paper over with a wider version range.
- **`cc` missing / different libc.** Linking shells out to system `cc`;
  test on the oldest supported distro you claim to support.
- **Package tests passing offline by accident.** They use local repos;
  run one real-network `core install github.com/...` before tagging a
  release that claims package-manager support.
- **`--force` is currently inert.** `Main.cpp` parses `--force`/`-f` into
  `DriverOptions::forceOverwrite`, but the pipeline always overwrites
  outputs (`CD_CreateAlways` in `runPipelineInternal`); don't rely on the
  flag to protect existing files — use fresh directories in release
  scripts.
