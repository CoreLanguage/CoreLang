# Core Memory Model

Core is a manually managed language. There is **no garbage collector**,
no reference counting, no ownership checker, and no hidden allocation
outside the contracts documented here. This document states the rules the
compiler enforces, the rules it does not, and everything the programmer
must guarantee themselves.

Related: [SPEC.md](SPEC.md) (language), [abi.md](abi.md) (layouts),
[concurrency.md](concurrency.md) (threads/atomics).

---

## 1. The three storages

| Storage | Created by | Lifetime | Notes |
|---------|-----------|----------|-------|
| **Stack** | local variables, parameters, temporaries, `alloca`-backed literals | ends when the scope exits | fast; size fixed at declaration |
| **Static** | globals (`g: T = init`), string literals, vtables/itables, `const`s | entire program | globals may be `tls` (per-thread instance) |
| **Heap** | `memory.alloc*` / `realloc_array` | until `free` | exactly as long as you manage it |

- Stack variables are default-initialized when declared without an
  initializer (0, 0.0, `false`, `null`, `""`, tag 0, zero vector, null
  closure). There is no uninitialized read through a declared variable.
- Heap memory is uninitialized unless you use the `_zeroed` variants.
- Globals are zero- or constant-initialized before `main`; `tls` globals
  are zero-initialized per thread.

---

## 2. Allocation APIs (`import memory`)

All heap allocation goes through the `memory` module, which wraps
`malloc`/`calloc`/`realloc`/`free` from libc (via `runtime/corert.c`).
Every function here is exact about what it does:

```core
import memory

p  = alloc<i32>()                  // one i32, uninitialized
q  = alloc_zeroed<i32>()           // one i32, zeroed
a  = alloc_array<i32>(n)           // n elements, uninitialized
b  = alloc_zeroed_array<i32>(n)    // n elements, zeroed
raw = alloc_bytes(len)             // ptr<u8>, uninitialized
big = alloc_aligned(size, align)   // align shall be a power of two
m  = realloc_array<i32>(a, n2)     // resize; returns (possibly new) ptr
free(p)                            // release — exactly once
memcpy(dst, src, bytes)            // copy; regions shall not overlap
memset(dst, 0, bytes)              // fill with a byte value
r = memcmp(a, b, bytes)            // <0 / 0 / >0
```

Contracts:

- `alloc<T>()` allocates exactly `sizeof(T)` bytes, **uninitialized**.
  Reading a field before writing it is undefined (§7).
- `alloc_array<T>(count)` allocates `count * sizeof(T)` bytes. The count
  is not stored anywhere — track the length yourself (e.g. in a struct).
- `alloc_aligned(size, align)` requires a power-of-two `align`;
  alignments ≤ 16 go through plain malloc (already 16-aligned on
  x86-64 Linux), larger alignments use `posix_memalign`.
- `realloc_array<T>(p, count)` may move the block; the old pointer
  becomes invalid. Only call it on pointers from `alloc_array` /
  `realloc_array` (malloc-family), never on interior pointers.
- On out-of-memory the runtime prints `panic: out of memory` and aborts.
  Allocation functions never return null.
- `memcpy` on overlapping regions is undefined; use a loop or `memmove`
  semantics via the runtime if you need overlap (`core_rt_memmove`
  exists in the runtime but is not re-exported in v1).

Nothing else in the language or runtime allocates. In particular:

- `string + string` **does** allocate (it must produce the joined
  bytes). This is the one documented hidden allocation; the result owns
  nothing — it is a view over the fresh buffer, and the buffer has no
  owner unless you keep one.
- Thread spawn allocates a handle and a trampoline block; `join` frees
  them.
- The compiler and optimizer never introduce allocations.

Freestanding builds (`--freestanding`) do not link the runtime at all:
`memory` is unavailable and you bring your own allocator (§8).

---

## 3. Ownership discipline

Core's rule is **single owner + free**:

- Every heap allocation has exactly one owner at every point in time.
- The owner is responsible for calling `free` exactly once.
- Passing a pointer does not transfer ownership — it is an alias. If a
  function "takes" ownership (frees its argument), that is a convention
  the caller must know; the type system does not express it. Document
  such functions in their names (`buf_free`, `account_destroy`) or keep
  ownership at the callsite.
- There is no `defer`, no destructors, no RAII. `free` is an ordinary
  call; place it where the lifetime ends (often right after the last
  use, or in an explicit `_free` function per type, as in
  `examples/manual-memory`).

There is no GC pause, no write barrier, no finalizer, and no weak
references — ever. A Core binary's memory behavior is exactly the libc
allocator's behavior plus what you write.

---

## 4. Lifetimes

- A local variable's address (`&x`) is valid only until the enclosing
  scope exits. Returning or storing `&local` anywhere creates a dangling
  pointer; the compiler does **not** reject it (no borrow checker, no
  escape analysis). This is undefined behavior.
- Heap allocations live until `free`, regardless of scope.
- String literals and other static data live for the whole program.
- A `string` view is valid exactly as long as the memory its `data`
  points to: literals forever; concatenation results until someone frees
  the buffer (nobody owns it automatically — treat concatenated strings
  as owned by the enclosing scope); `str_from_c(p)` for as long as the C
  side's buffer lives.
- The idiom for sharing across functions/threads is heap allocation plus
  a pointer, as in the threads example: `pa = &acc` would be dangling in
  a spawned thread if `acc` were a local of the spawning function —
  allocate the shared state instead.

---

## 5. Dangling pointers

A pointer is dangling when its target's lifetime has ended. Sources:

- `free` without nulling: `free(p)` — `p` still holds the address.
- Returning/storing `&local`.
- `realloc_array` moving a block while another pointer aliases it.

Dereferencing a dangling pointer is undefined behavior (§7). Defensive
idioms the stdlib encourages:

```core
free(p)
p = null          // use-after-free then traps instead of corrupting
```

- `*null` and `null[i]` dereference address 0 and fault (SIGSEGV) — a
  crash, not silent corruption, on Linux.
- Use-after-**scope** is not detected by any tool in the toolchain; run
  Valgrind or ASan (`cc`-linked binaries are ordinary executables) when
  debugging.

---

## 6. Double free and friends

- **Double free**: calling `free` twice on the same allocation is
  undefined behavior (glibc aborts with "double free or corruption" in
  the common case, but this is not guaranteed).
- **Freeing a non-heap pointer** (a stack address, a string literal, a
  `&global`) is undefined behavior.
- **Freeing an interior pointer** (base + offset) is undefined behavior.
- **Leaking** is fully defined behavior: memory is never reclaimed.
  Leaks are bugs but never safety hazards in Core.

---

## 7. Undefined-behavior catalog

In Core, the following operations are undefined behavior. The compiler
neither checks for them nor documents their results; LLVM is free to
assume they never happen.

1. Dereferencing `null`, a dangling pointer, or a freed pointer.
2. Dereferencing a misaligned pointer (e.g. reading an `i64` from a
   `ptr<u8>` not 8-aligned — natural alignment of the pointee is
   required, except through `@packed` struct access which uses byte
   loads).
3. Out-of-bounds access **inside `unsafe`** (array, string, or pointer
   indexing beyond the allocation).
4. Double free, freeing non-heap or interior pointers.
5. Reading uninitialized heap memory (types without default semantics —
   although every Core scalar has bit patterns that are "a value",
   reading a `string` or `ptr` field of garbage produces a garbage view
   whose use is then UB by rule 1).
6. Data races: unsynchronized concurrent access to the same non-atomic
   memory where at least one access is a write (concurrency.md §3).
7. Shift counts ≥ the bit width of the (left) operand.
8. Integer division or modulo by zero: a hardware trap (SIGFPE), not
   recoverable in-process.
9. Signed integer overflow is **defined** in Core: it wraps in
   two's-complement (LLVM `add`/`sub`/`mul` without `nsw`).
10. Inline `asm` with wrong constraints, and `volatile` access to memory
    that is not actually device memory — by nature outside the model.
11. `unsafe` casts that produce a `ptr<T>` pointing at a `T` that isn't
    there (type confusion), including class downcasts of instances that
    are not the derived type and interface unwraps of the wrong class.
12. Escaping the address of a local whose scope has exited (§4).

Everything else in the language is defined behavior, including:
wrapping arithmetic, unsigned overflow, comparing pointers to different
objects with `<`, `memcpy` of overlapping zero-length regions, and
leaking memory.

---

## 8. Aliasing

Core places **no restrictions on aliasing**:

- `ptr<T>` and `ptr<U>` may point at overlapping memory; there is no
  `restrict`, no `noalias`, no borrow rules.
- Multiple mutable aliases to the same object are allowed and are the
  programmer's responsibility to coordinate (locks or atomics across
  threads).
- The optimizer only assumes strict aliasing between accesses the
  compiler itself can see as distinct LLVM types within one function;
  pointer casts through `unsafe` (e.g. `ptr<u8>` type-punning) remain
  valid because Core lowers `ptr<T>` to a single address-space pointer
  type — there is no TBAA-based rejection of type punning.
- `memcpy` of overlapping regions is the one aliasing violation in the
  API surface (§2).

---

## 9. unsafe as the marker for dangerous operations

`unsafe` marks the operations whose correctness the compiler cannot
check (SPEC.md §8.7). It exists to make audits cheap: grep for `unsafe`
and you have the complete list of places where memory-safety guarantees
are in your hands.

- `unsafe` is **not** a scope for skipping bounds checks alone — it is
  required for the six operation classes listed in the spec, and it
  disables bounds checks for indexing lexically inside it.
- `unsafe` nests and does not propagate: calling a safe function from
  `unsafe` does not make that function's internals unchecked.
- `unsafe` as an expression yields its last expression statement's
  value: `p = unsafe { 1 as ptr<i32> }`.

What does **not** require `unsafe` (by design, documented as the Core
trade-off): plain `*p` dereference, `&x`, pointer arithmetic, `p[i]`,
array/string indexing (bounds-checked), and passing strings to C.

---

## 10. volatile semantics

`volatile_load(ptr<T>) -> T` and `volatile_store(ptr<T>, v)` (both
`unsafe`) compile to LLVM volatile load/store:

- Each access **shall** happen and shall not be elided, duplicated (for
  one source-level access), or reordered with other volatile accesses to
  the same address.
- Volatile accesses are **not** atomic and **not** synchronization: they
  provide no visibility guarantees across threads. Their purpose is
  memory-mapped I/O and hardware registers.
- The pointer's target should be device memory or memory the hardware
  mutates; for shared mutable state between threads use atomics (§11)
  or locks.

Example (`examples/kernel/main.cr`):

```core
vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> }
unsafe { volatile_store(vram + i, color + msg[i] as u16) }
```

---

## 11. Atomics

Two layers, both **sequentially consistent (seq_cst)** — Core exposes no
weaker orderings in v1:

Builtins (compiler-implemented, on `ptr<T>` where `T` is an integer or
`bool`):

```core
atomic_load(p)              // -> T
atomic_store(p, v)          // store, void
atomic_add(p, v) -> T       // returns the PREVIOUS value
atomic_sub(p, v) -> T
atomic_swap(p, v) -> T
atomic_cas(p, expected, new) -> T   // store new if *p == expected;
                                    // returns the OLD value either way
atomic_fence()              // seq_cst fence
```

Wrapper classes in `thread` (`AtomicI32`, `AtomicI64`, `AtomicBool`,
`AtomicUsize`) with `load`/`store`/`add`/`sub`/`swap`/
`compare_exchange` methods.

Semantics:

- All operations are seq_cst: there is a single total order of
  atomic operations on all memory, agreed by every thread (LLVM
  `SequentiallyConsistent` ordering; `atomic_cas` is a strong CAS — no
  spurious failure).
- `AtomicBool` is stored as an `i8` cell (LLVM requires byte-sized
  atomics); values read back are `0`/`false` and nonzero/`true`.
- Atomics synchronize-with each other: a seq_cst store that is read by a
  seq_cst load makes everything before the store visible after the load.
  This is the only lock-free visibility guarantee in Core.
- Atomics are lock-free only when the target supports it (on x86-64:
  naturally-aligned ≤ 8-byte operations; `i128` atomics use the libatomic
  path linked via `-latomic`).
- Non-atomic access mixed with atomic access to the same location is a
  data race (§7 rule 6) — initialize through atomics too.

---

## 12. Custom allocators

Because every allocation is explicit, user allocators are ordinary
libraries. The runtime contract they must meet: return `ptr<void>`/typed
pointers with the requested alignment, never return null (panic instead),
and document whether `free` is a no-op.

### Bump allocator (arena, O(1) alloc, free-all)

From `examples/allocator/main.cr` — allocate a block up front, hand out
interior slices, reset to reuse:

```core
import memory

struct Bump {
    base: ptr<u8>
    offset: usize
    cap: usize
    pub func init(capacity: usize = 1024) {
        self.base = alloc_aligned(capacity, 16)
        self.offset = 0
        self.cap = capacity
    }
    pub func alloc_raw(n: usize, align: usize) -> ptr<void> {
        mut aligned = (self.offset + align - 1) / align * align
        if aligned + n > self.cap {
            panic("bump allocator out of memory")
        }
        self.offset = aligned + n
        unsafe {
            return (self.base + aligned) as ptr<void>
        }
    }
    pub func reset() { self.offset = 0 }
}
```

Properties: allocation is an integer add; `reset()` reclaims everything
(no per-object `free`); memory is handed out with the requested
alignment by rounding `offset` up.

### Typed arena

Wrap the bump with a generic helper so callers get typed pointers:

```core
import memory

struct Arena {
    bump: Bump
    pub func init(cap: usize) { self.bump.init(cap) }
    pub func alloc<T>() -> ptr<T> {
        unsafe { self.bump.alloc_raw(sizeof(T), alignof(T)) as ptr<T> }
    }
    pub func alloc_array<T>(n: usize) -> ptr<T> {
        unsafe { self.bump.alloc_raw(n * sizeof(T), alignof(T)) as ptr<T> }
    }
}
```

`alignof(T)` (compile-time) keeps every payload naturally aligned, so no
misaligned-access UB (§7 rule 2) is possible.

### Free-list pool (fixed-size objects)

For many same-sized objects, allocate blocks and maintain a free list
through the objects' own storage:

```core
import memory

const CHUNK: usize = 64

struct Pool {
    free_list: ptr<ptr<Node>>     // singly linked through node storage
    block: ptr<u8>
}
```

Allocate a block of `CHUNK * sizeof(Node)` on first use, thread the
nodes together through a `next: ptr<Node>` field (or through the raw
bytes with `memcpy`), and push/pop on alloc/free. O(1) alloc/free, no
fragmentation, and the pool's `deinit` frees the whole block.

General rules for custom allocators:

- Return pointers aligned to `alignof(T)` for typed allocations.
- Never return null — `panic` on exhaustion (matching the runtime's
  contract).
- Do not call `memory.free` on interior pointers; the pool/arena owns
  the backing block.
- For freestanding targets, implement `core_rt_alloc`-equivalents
  yourself and implement `memory` against them; nothing in the language
  assumes malloc.

---

## 13. Bounds checking behavior

- **Arrays** (`[T; N]`): the length is part of the type. Outside
  `unsafe`, every index is checked: `index <u N`, else the program
  prints `panic: index N out of bounds for length L (file:line)` to
  stderr and aborts (SIGABRT). Inside `unsafe`, the check is omitted.
- **Strings**: the length is dynamic. Indexing checks `index <u len(s)`
  with the same panic. Inside `unsafe`, unchecked.
- **Pointers** `p[i]`: never checked (it is pointer arithmetic).
- The check compares as **unsigned**, so negative indexes fail the
  check (the index is sign-extended for the panic message only).
- The panic is not catchable — there are no exceptions; the process
  aborts. `core check`/`assert` cannot trap it either.
- Bounds checks are real branches; LLVM's optimizer eliminates those it
  can prove redundant (e.g. `for i in 0..len`).
