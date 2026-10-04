# Unsafe and low-level

Core lets you write device drivers, kernels, and FFI bindings. The
operations that can corrupt memory without a bounds check being able
to catch it are gated behind `unsafe` blocks. Everything else
(pointers, arithmetic, indexing) is available everywhere.

## unsafe blocks

```core
unsafe {
    vram = 0xB8000 as ptr<u16>
}
```

An `unsafe` block is both a statement and an expression (its value is
the block's last expression). Nesting is fine. Exactly these operations
require it:

- `as` casts between pointer types with different pointees
- int to pointer and pointer to int casts
- `volatile_load` / `volatile_store`
- `asm` / `asm_volatile`
- class downcasts (`base as Derived`) and interface-to-class unwraps,
  neither of which is checked at runtime

Plain dereference `*p`, `&x`, pointer arithmetic, and `p[i]` are
allowed anywhere. Core checks what it can (bounds) and marks the rest
unsafe.

Inside an `unsafe` block, array and string indexing is unchecked too.

## MMIO and volatile

`volatile_load(ptr<T>) -> T` and `volatile_store(ptr<T>, v)` compile to
LLVM volatile accesses: they are neither elided, duplicated, nor
reordered with other volatile accesses. This is what memory-mapped I/O
needs.

```core
// VGA text buffer on bare x86-64
func put(msg: string) {
    vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> }
    color: u16 = 0x0F00                       // white on black
    for i in 0..26 {
        unsafe { volatile_store(vram + i, color + msg[i] as u16) }
    }
}
```

Volatile accesses are neither atomic nor barriers. Do not use them for
thread synchronization; use `thread` atomics instead.

## Inline assembly

```core
func ticks() -> u64 {
    return unsafe { asm_volatile("rdtsc", "=A") }
}

func add_one(x: u64) -> u64 {
    // AT&T syntax; constraints are LLVM-style
    return unsafe { asm_volatile("addq $$1, $0", "=r,0", x) }
}
```

`asm` and `asm_volatile` take a template string, a constraint string,
and optional input operands. They exist for the cases where nothing
else works: privileged instructions, atomics without a builtin, CSR
access. Wrong constraints are undefined behavior, and the compiler
cannot check them.

## FFI

`extern func` declares a C function: C ABI, no mangling, symbol = the
declared name.

```core
extern func printf(fmt: ptr<char>, ...) -> i32
extern func atoi(s: ptr<char>) -> i32
@link_name("my_c_symbol") extern func thing(x: i32) -> i32

func main() {
    printf("hello from libc: %d %s\n", 7, c_str("via prelude c_str"))
}
```

Rules:

- Parameters and returns use the C ABI ([language/abi.md](../language/abi.md)).
  Pointers are raw; `string` converts to `ptr<char>` implicitly in
  argument position (the view's data pointer).
- `...` marks a C variadic; extra arguments must be integer, float,
  pointer, or bool. `f32` widens to `f64`, `bool` to `i32`, per C
  promotion rules.
- Link with `--link=<lib>` / `--link-arg=<arg>` / `--lib-path=<dir>` on
  the command line, or `link = ["m"]` in `core.toml`.
- `@link_name("...")` renames the emitted symbol for a declaration.
- Memory you hand to C and memory C hands back is your job: free what
  libc allocated with libc's functions, and vice versa.

Strings cross the ABI as `ptr<char>`; `c_str(s)` gets the pointer,
`str_from_c(p)` builds a view over a C string. A string literal's
pointer is valid for the whole program; a string you built with `+`
lives only as long as you keep the buffer.

## Freestanding and OS development

`--freestanding` compiles without the runtime and libc. The program
defines its own entry point and links itself:

```core
@link_name("_start") func kmain() -> never {
    vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> }
    msg = "Hello from Core bare metal!"
    for i in 0..26 {
        unsafe { volatile_store(vram + i, 0x0F00 + msg[i] as u16) }
    }
    unsafe { asm("cli", "") }
    loop { }
}
```

Build with:

```console
core compile kernel.elf main.cr --freestanding --target=x86_64 --emit-object
ld -T linker.ld -o kernel.elf kernel.elf.coreobj.o
```

Cross builds stop at a relocatable object; the target toolchain links.
A worked kernel example with a linker script lives in examples/kernel.
`--entry=<name>` changes the entry symbol from `main`.

## What unsafe does not mean

`unsafe` does not turn off type checking or bounds checks on ordinary
code outside the block. It only licenses the list above. Everything
inside the block is your responsibility: alignment, lifetimes, races,
aliasing, the works (catalog: [memory.md](memory.md),
[language/memory-model.md](../language/memory-model.md)).
