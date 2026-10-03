# Inline assembly

When you need an instruction the compiler won't emit — `rdtsc`, privileged instructions, cache ops — Core embeds LLVM inline assembly directly. Syntax is **LLVM's**: AT&T-style instruction strings on x86-64, with `$N` operand references and constraint strings.

## The builtins

```core
asm_volatile(asmstr, constraints, args...) -> u64   // never optimized away
asm(asmstr, constraints, args...) -> u64            // pure hint; usable as a statement
```

- Both return **u64** — the first output operand.
- Both require `unsafe { }`.
- `asm_volatile` is what you want almost always: hardware interaction has side effects by definition. (`asm` may be deleted/reordered by the optimizer; v0.1 note — using `asm` as an *expression* crashes the compiler, use it only as a statement inside `unsafe { }`.)

## Constraint strings

Comma-separated, LLVM syntax — **outputs first, then inputs**:

| Constraint | Meaning |
|---|---|
| `=r` | output: any general register |
| `=a` | output: `rax` specifically |
| `=A` | output: the `rdx:rax` pair (64-bit result in `rax`, as `rdtsc` produces) |
| `0`, `1`, ... | input tied to output N (in-place read-modify-write) |
| `r`, `i`, `m` | input: register, immediate, memory |

The i-th operand (outputs first, then inputs) is referenced in the asm string as `$0`, `$1`, ...

## Examples

### Read the cycle counter

```core
func rdtsc() -> u64 {
    return unsafe { asm_volatile("rdtsc", "=A") }
}
```

`rdtsc` leaves the tick count in `edx:eax`; constraint `=A` captures it as one 64-bit value.

### Read-modify-write an input

```core
func add_one(x: u64) -> u64 {
    // "=r,0": one output register, input tied to it
    // "$$1":   an escaped immediate ($$ yields a literal $ in AT&T syntax)
    return unsafe { asm_volatile("addq $$1, $0", "=r,0", x) }
}
```

### Multiple instructions and direct registers

```core
func five() -> u64 {
    return unsafe { asm_volatile("movq $$5, $0", "=r") }
}

func plus_input(x: u64) -> u64 {
    // two instructions separated by \n; $1 is the input operand
    return unsafe { asm_volatile("movq $1, %rcx\naddq %rcx, $0", "=r,0", x) }
}
```

Registers are written AT&T-style (`%rax`, `%rcx`); operands are `$N`; literals need the `$$` escape.

### Byte-swap

```core
func bswap_it(x: u64) -> u64 {
    return unsafe { asm_volatile("bswapq $0", "=r,0", x) }
}

func main() {
    v: u64 = 0x0102030405060708
    say bswap_it(v)   // 578437695752307201 (0x0807060504030201)
}
```

## Semantics and gotchas

- The compiler treats the asm string as a black box: it cannot see clobbers. **If your asm modifies a register not in the constraints, you corrupt the generated code.** Keep to operand registers and explicit `%`-registers you save/restore.
- `ebx` is reserved for PIC basing on many targets — instructions that clobber it (`cpuid`) need special handling; avoid in v0.1.
- All operand values move through `u64`; the compiler doesn't type-check your asm's assumptions about widths.
- Memory you read/write indirectly (through pointers passed in) must be marked `memory` in the constraints — v0.1's builtin doesn't expose clobber lists, so indirect-memory asm is at your own risk.

## Complete working example

```core
// asm.cr - x86-64 inline assembly through LLVM
func rdtsc() -> u64 {
    return unsafe { asm_volatile("rdtsc", "=A") }
}

func add_one(x: u64) -> u64 {
    return unsafe { asm_volatile("addq $$1, $0", "=r,0", x) }
}

func three_times(x: u64) -> u64 {
    return unsafe { asm_volatile("imulq $$3, $0", "=r,0", x) }
}

func bswap_it(x: u64) -> u64 {
    return unsafe { asm_volatile("bswapq $0", "=r,0", x) }
}

func five() -> u64 {
    return unsafe { asm_volatile("movq $$5, $0", "=r") }
}

func main() {
    say rdtsc() != 0        // true — the counter runs
    say add_one(41)         // 42
    say three_times(14)     // 42
    v: u64 = 0x0102030405060708
    expect: u64 = 0x0807060504030201
    say bswap_it(v) == expect   // true
    say five()              // 5

    // non-volatile asm as a plain statement
    unsafe {
        asm("nop", "")
    }
    say "nopped"
}
```

## Common mistakes

- **GCC syntax expectations.** Operands are `$N` (LLVM), not `%N`; registers are `%rax` (AT&T), not `rax` — and immediates are `$$1`.
- **Using `asm` (non-volatile) for anything real.** The optimizer may delete it; use `asm_volatile`.
- **Clobbering undeclared registers** — especially `rbx`, `rbp`, `rsp`.
- **Forgetting `unsafe`.** Both builtins are unsafe-only.
- **Large hex literals as u128.** A bare `0x0102030405060708` literal infers `u128` — type it (`v: u64 = ...`) before passing to asm or u64 parameters.
- **Expecting signed/float support.** Operands are u64-only; move floats through memory or bit-cast yourself.

## Performance notes

- Inline asm blocks the optimizer: code around it can't reorder across `asm_volatile`, and nothing inlines into it. Use it for *single instructions*, not sequences the compiler could emit.
- `rdtsc` is ~20–30 cycles; for timing use `time.monotonic_ms()` first, asm when you need finer resolution.
- Check what LLVM emits for your pure-Core version first — `core emit-asm file.cr` often shows it already picked the optimal instruction (bswap, popcnt, etc.).

## When to use / not use

- **Use** for: `rdtsc`, serializing instructions, cache/TLB control, CPUID-ish leaf queries done carefully, freestanding halts (`cli`/`hlt`).
- **Don't** use asm for arithmetic, string ops, or anything portable — LLVM's codegen wins and the code stays cross-platform (see [freestanding-development.md](freestanding-development.md) for target-specific code organization).
- **Don't** build an asm-heavy "hot patching" layer; wrap the three instructions you actually need.

## Exercises

1. Wrap `rdtsc` and measure the cycle count of an empty loop of 1000 iterations (two `rdtsc` calls, difference printed).
2. Write `add_two(x: u64)` with inline asm (`addq $$2, $0`) and verify it against `x + 2` for several values.
3. Do a 32-bit byte swap with `"bswapl $0"` (note: operands are u64 — mask appropriately) and verify against a shift-based swap.
4. Time 1M calls of an asm-based add vs a plain `+` — how much does the compiler's version win by?
5. Use `asm("nop", "")` as a statement inside `unsafe` in a loop, and confirm via `core emit-asm` that the nops survive at `-O0` but vanish at `-O2`.

Next: [Low-level programming](low-level-programming.md).
