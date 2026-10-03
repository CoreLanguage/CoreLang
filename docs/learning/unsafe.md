# Unsafe

Some operations can't be checked by the type system: raw memory casts, device MMIO, inline assembly. Core lets you do all of them — inside an explicit `unsafe { ... }` block, so every such site is greppable and reviewable.

## What requires unsafe

| Operation | Why |
|---|---|
| `ptr<T>` → `ptr<U>` cast (different pointees) | reinterpretation can break memory safety |
| `int` → `ptr` and `ptr` → `int` casts | arbitrary addresses aren't valid objects |
| `volatile_load` / `volatile_store` | exist only for memory-mapped IO |
| inline `asm` / `asm_volatile` | the compiler can't verify assembly |
| class downcasts / interface unwraps | the runtime type isn't checked |

Everything else — dereferencing, pointer arithmetic, normal casts (`as` between numerics) — needs no unsafe.

## The block

```core
unsafe {
    // casts, volatile ops, asm — no compiler safety nets here
}
```

`unsafe` is both a **statement** and an **expression** (its value is the last expression in the block):

```core
p = alloc_array<i32>(2)
byte_view: ptr<u8> = unsafe { p as ptr<u8> }   // expression form
addr = unsafe { 0xB8000 as ptr<u16> }          // int -> ptr
back = unsafe { addr as u64 }                  // ptr -> int
```

It does **not** turn off type checking of surrounding code — only the operations listed above require it.

## What unsafe actually unlocks

### 1. Pointer reinterpretation

```core
import memory

func main() {
    p = alloc_array<i32>(2)
    p[0] = 0x41424344
    // peek at the same memory as bytes (little-endian: lowest byte first)
    byte_view: ptr<u8> = unsafe { p as ptr<u8> }
    say *byte_view        // 68 (0x44)
    free(p)
}
```

This is how you read file buffers, network packets, and struct-serialize data.

### 2. Integer-pointer casts (MMIO, device memory)

```core
// VGA text buffer on x86 bare metal
vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> }
unsafe {
    volatile_store(vram, 0x0F00 + ('X' as u16))
}
```

### 3. Volatile accesses

Ordinary loads/stores can be optimized away or reordered; `volatile_load(p)` / `volatile_store(p, v)` forbid that — for registers whose values hardware changes behind the compiler's back:

```core
mut status = 0
sp = &status
unsafe {
    volatile_store(sp, 1)
    say volatile_load(sp)   // 1
}
```

### 4. Inline assembly

See [inline-assembly.md](inline-assembly.md) for the full treatment:

```core
func rdtsc() -> u64 {
    return unsafe { asm_volatile("rdtsc", "=A") }
}
```

### 5. Unchecked indexing (via pointers)

Bounds-checked array indexing aborts on violation; through a `ptr<T>` (e.g. a decayed array argument), indexing is unchecked:

```core
func last_of(xs: ptr<i32>) -> i32 {
    // no bounds check exists for pointer indexing
    return unsafe { xs[2] }   // caller's responsibility: len >= 3
}
```

### 6. Class downcasts and interface unwraps

```core
interface Shouter { func shout() -> string }
class Loud : Shouter {
    pub func init() { }
    pub func shout() -> string { return "LOUD" }
}

func unwrap(sh: Shouter) -> string {
    back = unsafe { sh as Loud }   // no runtime check: sh must really be a Loud
    return back.shout()
}
```

## Rules of thumb

1. **Keep unsafe blocks small** — one operation each where practical; reviewers read the block, not your intentions.
2. **Write the invariant in a comment**: what makes this cast/access valid *right now*?
3. **Wrap, don't sprinkle.** Encapsulate an unsafe op in a safe, checked helper (`fn byte_at(p, i)` with its own assert) — the rest of the code stays clean.
4. **Never** let an unsafe helper take arbitrary pointers; take the owning container so the invariant is checkable in one place.
5. **`unsafe` is not "faster"** — it removes checks; use it where you've proven the check redundant (hot loop) or impossible to express (MMIO).

## Complete working example

```core
// unsafe.cr
import memory

func last_of(xs: ptr<i32>) -> i32 {
    return unsafe { xs[2] }     // unchecked: xs must have >= 3 elements
}

func main() {
    // reinterpret memory
    p = alloc_array<i32>(2)
    p[0] = 0x41424344
    byte_view: ptr<u8> = unsafe { p as ptr<u8> }
    say *byte_view              // 68
    free(p)

    // integer <-> pointer round trip
    fake = unsafe { 0x1000 as ptr<u8> }
    say unsafe { fake as u64 } == 0x1000    // true

    // volatile round trip
    mut x = 0
    xp = &x
    unsafe {
        volatile_store(xp, 42)
        say volatile_load(xp)   // 42
    }

    // unchecked indexing through a decayed pointer
    data: [i32; 3] = [1, 2, 3]
    say last_of(&data)          // 3

    // unsafe as an expression
    y = unsafe { (&x) as ptr<i32> }
    say *y                      // 42
}
```

## Common mistakes

- **Thinking `unsafe` disables *all* checks.** Only the listed operations; types still check.
- **Reinterpreting without a lifetime plan.** A `ptr<u8>` view of a freed buffer is a trap — same rules as [pointers.md](pointers.md).
- **Casting away misalignment.** `p as ptr<i64>` where `p` isn't 8-aligned crashes on some targets — use `alloc_aligned` (see [memory-management.md](memory-management.md)).
- **Volatile everywhere.** Volatile is for hardware-owned memory, not thread sync (use [atomics](concurrency.md)).
- **Downcasting without knowing the concrete type.** `sh as Loud` on a non-Loud object is silent corruption — track what you put in.
- **Skipping unsafe by casting through `u64`.** Same UB, now hidden — that's why int↔ptr casts are unsafe too.

## Performance notes

- unsafe itself is free — it's a compile-time marker, no runtime cost.
- Its real value is *removing* cost: unchecked indexing in verified-range loops, volatile only where required so normal accesses can optimize.
- Keep unsafe blocks out of inlined hot paths unless measured; a wrong "optimization" here costs a debugger day.

## When to use / not use

- **Use unsafe** for FFI struct casting, MMIO/driver registers, freestanding environments, packed wire formats, and proven-hot loops.
- **Don't** use it to silence type errors — Core's `as` rules exist for your benefit.
- **Don't** expose unsafe APIs in library interfaces — wrap them; your users' code should contain zero unsafe.
- When the goal is assembly specifically, go straight to [inline-assembly.md](inline-assembly.md); for hardware access patterns, [low-level-programming.md](low-level-programming.md).

Next: [Inline assembly](inline-assembly.md).
