# Installation

Core is built from source. The compiler is a C++17 program using **LLVM 18**, so the only real prerequisites are a C/C++ toolchain and LLVM 18 development packages.

## Prerequisites

On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake git \
    llvm-18-dev libclang-18-dev clang libpolly-18-dev
```

You need roughly: `cmake` ≥ 3.20, a C compiler (for the runtime), and LLVM 18.x dev libraries. Verify LLVM is visible to CMake:

```bash
llvm-config-18 --version   # should print 18.x
```

## Building from source

```bash
git clone <core-repo-url> core
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This produces two artifacts in `build/`:

- `core` — the compiler driver (lexing, type checking, LLVM codegen, linking)
- `corert.o` — the small C runtime (printing, malloc wrappers, pthread wrappers, time)

The runtime is intentionally tiny: no garbage collector, no hidden allocation, no background threads. Freestanding builds don't even link it.

Run the test suite to confirm everything works:

```bash
cd build && ctest --output-on-failure
```

## The runtime layout

When you compile a program, the `core` driver needs two things from its installation directory:

- `corert.o` — the runtime object, linked into every hosted executable
- `std/` — the standard library sources (`prelude.cr`, `memory.cr`, `math.cr`, `thread.cr`, ...), which are compiled **with your program** (whole-program compilation)

The driver finds them **next to the binary**: if you have `build/core`, it looks for `build/corert.o` and `build/std/`. That's already true after a source build, so the build directory works as a self-contained toolchain — you can copy `core`, `corert.o`, and `std/` together.

## The `CORE_HOME` environment variable

If you install the toolchain somewhere else (for example `/usr/local/lib/core` with `core` on your `PATH`), set:

```bash
export CORE_HOME=/path/to/toolchain   # contains corert.o and std/
```

Resolution order for `corert.o` and `std/`:

1. `$CORE_HOME` (if set)
2. the directory containing the `core` binary itself
3. `<exe-dir>/../lib/core`, `<exe-dir>/lib/core`

For a normal source build you don't need `CORE_HOME` at all.

## Verifying the install

```bash
cd /tmp
cat > hello.cr <<'EOF'
func main() {
    say "Hello, Core!"
}
EOF
/path/to/core/build/core compile hello hello.cr
./hello
```

Expected output:

```
Hello, Core!
```

Useful commands once installed:

```bash
core version          # prints compiler + LLVM backend version
core emit-ir hello.cr # dump generated LLVM IR
core emit-asm hello.cr# dump assembly
```

## Compiler flags you'll meet later

| Flag | Meaning |
|---|---|
| `-O0` / `-O2` / `-O3` / `-Os` | LLVM optimization level (default `-O0`) |
| `--debug` | DWARF debug info; GDB breakpoints resolve to `.cr` lines |
| `--target=x86_64\|aarch64\|riscv64` | cross-compile (emits a relocatable object) |
| `--freestanding` | no OS runtime, no libc, custom entry point |
| `--emit-object` | stop after the object file |
| `--link=<lib>` / `--link-arg=<arg>` / `--lib-path=<dir>` | linker control for native libraries |

Full details in [projects.md](projects.md) and [freestanding-development.md](freestanding-development.md).

## Common mistakes

- **Building against the wrong LLVM.** The build requires LLVM 18.1 exactly (`find_package(LLVM 18.1 REQUIRED)`). LLVM 15/17/19 will fail at configure time.
- **Moving the `core` binary alone.** Without `corert.o` and `std/` next to it (or `CORE_HOME` set), every compile fails while looking for the runtime.
- **Forgetting `clang`/`cc`.** The final link step invokes the system C compiler; Core doesn't bundle a linker.

## Exercises

1. Build Core, then compile and run the hello program above.
2. Run `core emit-asm hello.cr | head -40` and find your string literal in the assembly.
3. Set `CORE_HOME` to a copy of the toolchain in another directory and confirm `core version` still works.

Next: [First project](first-project.md).
