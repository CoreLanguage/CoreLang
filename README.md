# Core

**Core** is a compiled, statically typed, general-purpose systems programming
language with extremely simple syntax and deep low-level control. The compiler
is written in C++17 and uses **LLVM 18** as its backend.

```core
func main() {
    say "Hello, Core!"
}
```

Core gives you raw pointers, manual memory management, `unsafe` blocks,
inline assembly, MMIO-style volatile access, OOP with vtable dispatch,
compile-time-monomorphized generics, and OS threads — with no garbage
collector, no hidden allocations, and no VM. Programs compile to one native
executable; imported modules are baked in.

```console
$ core compile mycoolbinary main.cr
built: mycoolbinary
$ ./mycoolbinary
Hello, Core!
```

## Feature highlights

- **Simple syntax** — `x = 10`, `func add(a: i32, b: i32) -> i32`, brace blocks.
- **All the primitives** — i8..i128, u8..u128, f32/f64, bool, char, string,
  usize/isize, void, never, plus SIMD vector types (f32x4, i32x4, ...).
- **Raw pointers** — `ptr<T>`, `&x`, `*p`, pointer arithmetic, casts, null.
- **Manual memory** — `alloc<T>() / free(ptr) / realloc` in `std/memory`; build
  your own arenas, pools and slabs. **No garbage collector. No hidden allocation.**
- **unsafe** — explicit blocks for volatile/MMIO, raw casts, and inline asm.
- **OOP** — classes, single inheritance, virtual dispatch (vtables),
  interfaces and traits (fat pointers + itables), constructors, `pub/private`.
- **Generics** — monomorphized at compile time (`max<T>`, `Box<T>`).
- **Enums with data** — tagged unions with exhaustive `match`.
- **Modules** — `import math`, recursive resolution, circular-import errors,
  duplicate-import dedup, `pub` visibility; all reachable modules link into
  ONE executable.
- **Concurrency** — pthread-backed threads, mutexes, rwlocks, condvars, and
  sequentially-consistent atomics.
- **FFI** — call C directly (`extern func printf(fmt: ptr<char>, ...) -> i32`);
  link native libraries.
- **Project toolchain** — `core init / build / run / test / check`, plus a
  git-based package manager (`core install github.com/user/repo`) with
  semver resolution and a reproducible `core.lock`.
- **Debugging & inspection** — `--debug` emits DWARF (GDB/LLDB-ready, breakpoints
  resolve to `.cr` lines); `emit-ir` / `emit-asm` expose the LLVM pipeline.
- **Freestanding & cross compilation** — `core compile kernel.elf kernel.cr
  --freestanding --target=x86_64 --emit-object` builds kernel objects with a
  custom entry point; aarch64 and riscv64 targets supported.

## Building from source

Requirements: CMake ≥ 3.20, C++17 compiler, LLVM 18 development libraries
(`llvm-18-dev` / `llvm-dev`), a C compiler for linking produced binaries.

```console
$ ./install.sh                   # builds, tests, installs to /usr/local
```

or manually:

```console
$ cmake -S . -B build
$ cmake --build build
```

The compiler binary lands at `build/core` (with `corert.o` + `std/` beside it).
Install with `./install.sh` (or `cmake --install build`) if desired.

```console
$ ./build/core version
Core compiler 0.1.0 (LLVM 18.1.3 backend)
```

## Quick tour

```core
import memory
import thread

struct Point { x: f64, y: f64 }

interface Shape {
    func area() -> f64
}

class Square : Shape {
    side: f64
    pub func init(s: f64) { self.side = s }
    pub func area() -> f64 { return self.side * self.side }
}

enum Value {
    Int(i32),
    Text(string)
}

func describe(v: Value) -> string {
    match v {
        Int(n)  { return "int: " + to_string(n) }
        Text(s) { return "text: " + s }
    }
}

func main() {
    // variables
    mut x: i32 = 10
    x += 5
    const MAX: i32 = 100
    say x < MAX

    // pointers and manual memory
    p = alloc<Point>()
    p.x = 3.0
    say p.x
    free(p)

    // generics are monomorphized
    say max_of(3, 7)
    say describe(Value.Int(42))

    // threads + atomics
    counter = AtomicI32 { }
    pc = &counter // closures capture by value: share through a pointer
    t = thread.spawn(func() { pc.store(42) })
    thread.join(t)
    say counter.load()
}

func max_of<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}
```

## Repository layout

```
compiler/src     the compiler (lexer, parser, AST, sema, codegen, driver)
compiler/runtime corert.c — the minimal C runtime (printing, memory, threads)
std/             the standard library (prelude + memory/math/thread/time/process/simd)
examples/        small, working example programs for every feature
tests/           end-to-end test suite (tests/run_tests.py)
docs/
  learning/      beginner tutorials (variables ... OS development)
  language/      SPEC.md, memory-model.md, abi.md, concurrency.md
  compiler/      architecture and subsystem guides
  internals/     monomorphization, ABI internals
  contributing/  how to add features; getting started; testing
  packages/      authoring/publishing git packages
```

## Testing

```console
$ cmake --build build
$ python3 tests/run_tests.py --core build/core
```

## Documentation

Start with [docs/learning/introduction.md](docs/learning/introduction.md).
The authoritative language reference is [docs/language/SPEC.md](docs/language/SPEC.md).
Contributing? Read [docs/contributing/getting-started.md](docs/contributing/getting-started.md)
and [docs/contributing/adding-a-feature.md](docs/contributing/adding-a-feature.md).

## License

MIT.
