# Memory management

Core has **no garbage collector**. Every byte is yours: allocate it, use it, free it. In exchange you get predictable latency, compact binaries, and code that runs anywhere — including kernels and microcontrollers where a GC can't exist.

## The three memory spaces

| Space | Created by | Lifetime | Cost |
|---|---|---|---|
| **Stack** | declaring a local (`x = 10`, `arr: [i32; 8] = ...`) | ends when the scope exits | free |
| **Heap** | `alloc*<T>()` from the `memory` module | until you call `free` | a malloc call |
| **Static** | globals (`BASE: i32 = 50`, string literals) | whole program | free |

Most variables should live on the stack. Go to the heap when data must **outlive the scope**, be **shared**, or be **sized at runtime**.

## The alloc/free family

`import memory` and you get (from `std/memory.cr`):

| Function | Returns | Notes |
|---|---|---|
| `alloc<T>()` | `ptr<T>` | one `T`, **uninitialized** |
| `alloc_zeroed<T>()` | `ptr<T>` | one `T`, zeroed |
| `alloc_array<T>(n)` | `ptr<T>` | `n` elements, uninitialized |
| `alloc_zeroed_array<T>(n)` | `ptr<T>` | `n` elements, zeroed |
| `alloc_bytes(n)` | `ptr<u8>` | raw bytes |
| `alloc_aligned(size, align)` | `ptr<u8>` | power-of-two alignment |
| `realloc_array<T>(p, n)` | `ptr<T>` | grow/shrink (element counts) |
| `free<T>(p)` | — | release; **must** match an allocation |

```core
import memory

struct Point { x: i32, y: i32 }

func main() {
    p = alloc<Point>()       // uninitialized — set every field!
    p.x = 3
    p.y = 4
    say p.x + p.y            // 7
    free(p)

    q = alloc_zeroed<i64>()  // *q == 0 guaranteed
    say *q
    free(q)

    mut arr = alloc_array<i32>(4)
    for i in 0..4 { arr[i] = i }
    arr = realloc_array<i32>(arr, 8)   // grow; old contents preserved
    arr[6] = 77
    say arr[6]
    free(arr)
}
```

> **v0.1 caveat:** `memcpy`/`memset`/`memcmp` exist in the `memory` module but currently fail to compile when called (their internal `ptr<T> -> ptr<void>` casts aren't marked unsafe). Declare the runtime functions yourself and call them inside `unsafe`:
> ```core
> import memory
> extern func core_rt_memcpy(dst: ptr<void>, src: ptr<void>, n: usize) -> void
> // unsafe { core_rt_memcpy(dst as ptr<void>, src as ptr<void>, 16) }
> ```

## The no-GC contract

Three promises the runtime makes — and you must keep:

1. **No hidden allocation.** Nothing you call allocates unless the docs say so (`+` on strings does; see [strings.md](strings.md)).
2. **No background work.** No GC threads, no write barriers, no finalizers. `free` is `free`.
3. **You are the owner.** Every allocation has exactly one owner responsible for freeing it.

## Ownership discipline

Core doesn't enforce ownership — that's the deal. The workable discipline:

- **One owner per allocation.** Whoever allocates, frees — or explicitly hands off.
- **Borrowers don't free.** Functions taking `ptr<T>` to *borrow* must not free it (see [references.md](references.md)).
- **Free in the reverse order of allocation** when structures reference each other.
- **Null out after free** if the pointer might be used again: `p = null` — then `p == null` checks catch bugs.
- **Structs that own heap memory get a `*_free` function** — write it when you write the struct.

## Undefined behavior: double-free and use-after-free

These are not "errors" — they're **undefined behavior**, same as in C:

- **Double free:** the allocator's bookkeeping corrupts; later allocations can return overlapping memory.
- **Use after free:** reads return garbage; writes corrupt the next allocation.
- **Leak:** not UB, but fatal in long-running programs.

Core's guards: `free` checks for `null` (freeing `null` is a no-op), and the debug runtime aborts on clearly-invalid frees. The rest is on you — which is why the arena pattern below is so popular.

## The arena (bump allocator) pattern

Allocate everything for a phase of work from one big block; free it all at once by resetting an offset. No per-object frees, no fragmentation, no use-after-free *within* the arena's lifetime:

```core
// arena.cr
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
    pub func reset() { self.offset = 0 }   // "frees" everything at once
}

func main() {
    arena = Bump { }
    arena.init(1024)

    nums: ptr<i32> = unsafe { arena.alloc_raw(4 * 10, 4) as ptr<i32> }
    for i in 0..10 { nums[i] = i * 3 }
    say nums[7]                 // 21

    quad: ptr<f64> = unsafe { arena.alloc_raw(8 * 4, 8) as ptr<f64> }
    quad[0] = 2.5
    say quad[0]

    say arena.offset > 0        // true
    arena.reset()               // all of it gone in one assignment
    say arena.offset            // 0
}
```

Arenas shine for parsers, request handlers, frame allocators in games — anywhere objects share a lifetime.

## Stack discipline: the zero-cost default

```core
func main() {
    buf: [i32; 64] = [0; 64]   // stack: no alloc, no free
    mat: [[f64; 3]; 3] = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    say mat[1][1]              // 1
    say buf[0]
}
```

Reach for the heap only when the size is runtime-dependent or the data must escape.

## Complete working example

The arena program above is complete — compile with `core compile arena arena.cr && ./arena`. It exercises: single values, zeroed values, arrays, `realloc_array`, aligned raw blocks, and the reset-based arena lifecycle.

## Common mistakes

- **Using `alloc<T>()` and forgetting to initialize.** The memory contains garbage. Prefer `alloc_zeroed` when unsure.
- **`free` twice** — UB. Null the pointer after freeing.
- **`realloc` result discard.** `realloc_array` may move the block — always reassign: `arr = realloc_array<i32>(arr, 8)`.
- **Freeing interior pointers.** Free exactly the pointer the allocator returned.
- **Returning a pointer to a stack local.** It dangles immediately; return the value or heap-allocate.
- **Holding `ptr`s across an arena reset** — after `reset()`, every pointer from that arena is invalid.
- **Leaking strings.** `s + t` allocates with no matching free API — fine for program-lifetime data, a leak if done per-iteration forever.

## Performance notes

- Stack allocation is a no-op at runtime; heap allocation is a function call into malloc. Batch small allocations into arenas.
- `alloc_zeroed` may be cheaper than `alloc` + manual zeroing for large blocks (the OS hands out zeroed pages).
- `realloc_array` can often extend in place; growing geometrically (×2) keeps the amortized cost O(1) per push.
- Whole-program compilation means LLVM can see through your allocations — small heap uses are sometimes optimized away entirely at `-O2`.

## When to use / not use

- **Stack** — the default. Everything that fits and doesn't escape.
- **Heap** — dynamic sizes, long-lived data, shared structures, big buffers.
- **Arena** — groups of objects with one lifetime (parse trees, per-frame data, request contexts).
- **Don't** allocate in tight loops without reuse; hoist the buffer out.
- **Don't** build "smart pointer" emulations; keep ownership boring and explicit.

## Exercises

1. Allocate an `i32`, write through it, print, and free — then set the pointer to `null` and confirm your `free` helper skips it.
2. Grow a heap buffer from 4 to 100 elements with `realloc_array` in a loop, writing every element, and free once at the end.
3. Build the `Bump` arena from this page, allocate 100 `i32`s from it, `reset()`, allocate again — verify the second batch reads correctly.
4. Allocate two `Point`s with `alloc` (uninitialized) and `alloc_zeroed`, print all fields, and explain the difference in a comment.
5. Write a `Pair_free(a: ptr<Node>, b: ptr<Node>)` helper that frees in reverse allocation order and nulls both — then misuse it (double-free) in a comment-only block, explaining what UB would occur.

Next: [Classes](classes.md).
