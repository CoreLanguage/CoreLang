# Introduction to Core

Core is a small, compiled systems programming language. You write `.cr` source files, the compiler turns them into LLVM IR, LLVM optimizes them, and you get **one native executable** — no interpreter, no VM, no runtime to install on target machines.

```
main.cr → lex → parse → type check → LLVM IR → optimized machine code → native binary
```

The whole language is deliberately small. If you know C, you already know 80% of Core's semantics: values are passed by value, memory is manual, and nothing happens behind your back.

## Design philosophy

**Simple syntax.** Statements end at newlines, blocks use braces, declarations read left to right: `func add(a: i32, b: i32) -> i32`. There are no headers, no templates-with-angle-bracket-puzzles, no visibility ceremony. The language is small enough to hold in your head.

**Manual memory.** There is **no garbage collector**. You allocate with `alloc<T>()` and release with `free(p)`. This means predictable latency, tiny binaries, and code that works where a GC can't go: kernels, drivers, embedded targets.

**LLVM-optimized native code.** Core uses LLVM 18 as its backend, so you get the same industrial optimization pipeline production compilers use: inlining, vectorization, constant folding across your whole program. `core emit-asm file.cr` shows you exactly what your CPU will run.

**Explicit everything.** No implicit numeric conversions (not even `i32` → `i64`). Conditions must be `bool` — no truthiness. Pointer casts that can break memory safety require an `unsafe { }` block. What the machine does is what you wrote.

## How Core compares

| | C | Rust | Zig | Core |
|---|---|---|---|---|
| Memory | manual | ownership/borrow checker | manual + allocators | manual, no GC |
| Compile-time safety | few checks | very strong | good | moderate (types, exhaustive `match`) |
| Speed of learning | days to master the edges | months | weeks | **hours** |
| Metaprogramming | preprocessor macros | macros, generics | comptime | generics only |
| Binary output | native | native | native | native (LLVM) |
| Unchecked freedom | everywhere | gated by `unsafe` | sprinkled | gated by `unsafe` blocks |

In one sentence: **Core is what C would look like if it were designed today with a modern optimizer and fifty years of lessons about syntax — keeping the manual memory and dropping the ceremony.**

Choose Core when you want C-level control with a friendlier language: CLI tools, algorithms, learning how computers work, prototyping drivers or kernels. Choose Rust when you need its borrow-checker guarantees on a large team. Choose C when you need its ecosystem or compiler availability on exotic platforms.

## A taste of Core

```core
// hello.cr
func main() {
    say "Hello, Core!"
}
```

```core
// A growable buffer — allocated and freed by you.
import memory

struct Buffer {
    data: ptr<i32>
    len: usize
    cap: usize
}

func push(b: ptr<Buffer>, v: i32) {
    if b.len == b.cap {
        b.cap = b.cap * 2
        b.data = realloc_array<i32>(b.data, b.cap)
    }
    b.data[b.len] = v
    b.len += 1
}

func main() {
    mut buf: Buffer
    buf.data = null
    buf.len = 0
    buf.cap = 0

    buf.cap = 4
    buf.data = alloc_array<i32>(buf.cap)
    for i in 0..10 { push(&buf, i * i) }

    say buf.len          // 10
    say buf.data[7]      // 49
    free(buf.data)
}
```

## What's in this tutorial series

1. [Installation](installation.md) — build the compiler from source
2. [First project](first-project.md) — `core init`, `core run`, project layout
3. [Variables](variables.md), [Types](types.md), [Operators](operators.md)
4. [Control flow](control-flow.md), [Functions](functions.md)
5. Data: [Arrays](arrays.md), [Strings](strings.md), [Structs](structs.md), [Enums](enums.md)
6. Under the hood: [Pointers](pointers.md), [References (and their absence)](references.md), [Memory management](memory-management.md), [Unsafe](unsafe.md)
7. Abstraction: [Classes](classes.md), [OOP](oop.md), [Generics](generics.md)
8. [Error handling](error-handling.md), [Data structures](data-structures.md), [Algorithms](algorithms.md)
9. Projects at scale: [Modules](modules.md), [Projects](projects.md), [Packages](packages.md)
10. Systems work: [Concurrency](concurrency.md), [FFI](ffi.md), [Inline assembly](inline-assembly.md), [Low-level programming](low-level-programming.md), [Freestanding](freestanding-development.md), [OS development](os-development.md)

## Known v0.1 limitations (honesty section)

Core is young. Things it deliberately does **not** have yet:

- No closures capturing by reference — lambdas capture locals **by value** (share state through pointers instead; see [Concurrency](concurrency.md))
- No `if` expressions — use `match`, which *is* an expression
- No trait bounds on generics
- Whole-program compilation — no separate object caching, so big projects recompile fully
- Cross builds (`--target=aarch64`, `--target=riscv64`) stop at object files; linking needs a target toolchain

## Common mistakes

- Expecting a package manager, REPL, or interpreter — Core is compile-from-source, and the only runtime artifact is your binary.
- Comparing Core to Python-style dynamism: there is no truthiness, no implicit conversion, no reflection. Types and errors are explicit by design.
- Skipping the build: Core is a compiled toolchain; the "install" is a `cmake` build (next chapter).

## Performance notes

- Core's optimizer is LLVM's, so idiomatic straightforward code usually compiles to the same machine code as hand-tuned C — measure before contorting your design.
- The whole-program compilation model trades incremental builds for full cross-module optimization.

## Exercises

1. Build Core from source (next chapter) and run the hello program above.
2. Compile any example from `examples/` and run `core emit-asm` on its source — find your string literal in the assembly output.
3. Compile the same program at `-O0` and `-Os` and compare the binary sizes.
4. Skim `docs/LANGUAGE-REFERENCE-SUMMARY.md` and list three features Core deliberately does *not* have; write a sentence on why each might be a deliberate choice.

Next: [Installation](installation.md).
