# Getting started with the Core compiler

## Prerequisites

- C++17 toolchain (clang or gcc)
- CMake >= 3.20
- LLVM 18 development packages (`find_package(LLVM 18.1 REQUIRED ...)`)
- A C compiler (`cc`) — used at *build* time for `corert.o` and at *run*
  time as the linker driver
- Python 3 (only to run the test suite)
- `git` (used by the package manager; the test suite shells out to it)

## Build

```sh
git clone <this repo> core
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build produces:

- `build/core` — the compiler,
- `build/corert.o` — the runtime object (compiled from `runtime/corert.c`
  by a custom command),
- `build/std/*.cr` — the standard library, copied next to the binary.

The driver locates `corert.o` and `std/` relative to the binary
(`Driver::findCompilerData`: `$CORE_HOME`, then `../lib/core`, `lib/core`,
or the executable's own directory), so the build tree works in place — no
install step needed for development.

## Run the tests

```sh
python3 tests/run_tests.py --core build/core
```

Expect `97 passed, 0 failed`. See
[testing.md](testing.md) for what the suite covers and how to add tests.

## Try it

```sh
cd /tmp && mkdir demo && cd demo
cat > main.cr <<'EOF'
func main() {
    say "Hello, Core!"
}
EOF
/home/exedev/core/build/core compile demo main.cr
./demo
```

Useful invocations while developing:

```sh
core emit-ir main.cr          # LLVM IR on stdout
core emit-ir main.cr -O2      # optimized IR
core emit-asm main.cr         # assembly
core compile app main.cr -O2 --debug   # optimized + DWARF
CORE_DUMP_IR=1 core compile app main.cr   # dump IR before optimization
CORE_DBG=1 ...                # parser/codegen debug traces (a few sites)
core check main.cr            # type-check only
```

## Repo layout

```
src/          compiler sources; one phase per pair of files
  Lexer.*     tokens (newline-significant)
  Parser.*    recursive descent -> AST
  AST.h       node definitions + bump arena
  ASTClone.h  deep clone for generic instantiation
  Type.*      interned types; Prims.def primitive table
  Sema.*      name resolution, overloads, monomorphization, checks
  Codegen.*   LLVM IR + debug info
  Driver.*    module graph, optimization pipeline, object emission, linking
  TOML.*      core.toml / core.lock reader/writer
  Project.*   project commands + git packages
  Main.cpp    CLI
runtime/      corert.c — the C runtime linked into hosted builds
std/          standard library in Core (prelude.cr is auto-imported)
tests/        run_tests.py end-to-end suite
examples/     feature-by-feature example programs
docs/         this documentation
```

## Coding conventions

These follow the existing code; keep new code consistent.

- **C++17**, compiled with `-fno-rtti -Wall -Wextra` (from
  `CMakeLists.txt`) — no `dynamic_cast`/`typeid`; use the `Kind` enums and
  `static_cast` downcasts that every phase uses (`auto *f = (DFunc *)d;`).
- **Namespace `core`** for everything; `using namespace llvm;` inside
  `Codegen.cpp`/`Driver.cpp` only.
- **Style**: 2-space indent, braces on the same line, `CamelCase` types,
  `camelCase` functions and locals, `lower_snake` for Core-level names in
  tests/examples. Member fields of big classes end without `_` (match the
  existing `Sema`/`Codegen` members).
- **Strings**: use `strfmt(...)` (printf-style, from `Common.h`) instead of
  `std::stringstream`; never pass user text as the format string.
- **Errors**: `diag.error(loc, msg, help, squiggleLen)`; keep checking and
  use `Type::Invalid` as poison. No exceptions, no `assert` for user
  errors (plain `assert` is fine for compiler-internal invariants).
- **Memory**: AST nodes only via `ASTContext::make`; types only via
  `TypeContext` factories; `Sema`/`ClassLayout`/`GenericInstance` objects
  are `new`-ed and intentionally leak (process lifetime).
- **One concern per file**: a new phase gets a new `.h`/`.cpp` pair
  (see [adding-a-feature.md](adding-a-feature.md) for the pipeline
  walkthrough).

## Project commands (dogfooding)

`core init` / `build` / `run` / `test` / `check` / `install` / `remove` /
`update` / `list` are implemented in `src/Project.cpp` on top of the same
`Driver`. The test suite exercises them against local git repos, so you
can add compiler features and consume them from a scaffolded project
immediately.
