# Memory

Core has no garbage collector and no hidden allocation (one exception:
string concatenation allocates). Storage comes from three places:

| Storage | Created by | Lifetime |
|---------|-----------|----------|
| Stack | locals, parameters, temporaries | ends at scope exit |
| Static | globals, string literals, vtables | whole program |
| Heap | `memory.alloc*`, `realloc_array` | until you call `free` |

Locals declared without an initializer are zero-initialized. Heap
memory from `alloc` is not; you get exactly what malloc returns.

The full contract, including the undefined-behavior catalog, is
[language/memory-model.md](../language/memory-model.md). This page is
the practical version.

## Pointers

`ptr<T>` is a raw, non-owning, 8-byte pointer. `null` is valid in every
pointer type. You can take addresses, dereference, and do pointer
arithmetic without an `unsafe` block; Core bounds-checks indexing and
leaves the rest to you.

```core
mut x: i32 = 41
p = &x
*p += 1
say x            // 42
say p[0]         // same as *p
```

Pointer arithmetic scales by the pointee size: `p + 1` advances one
element. `ptr<void>` scales by 1. `p - q` yields an `isize` distance.

```core
arr: [i32; 4] = [10, 20, 30, 40]
p = &arr[0]
say p[2]         // 30, bounds-checked: the compiler knows the length
```

The bounds check on `p[i]` here works because `p` still carries the
array type. A `ptr<T>` obtained from the heap has no length anywhere;
you track the length yourself (usually in a struct, see below).

## Heap allocation

```core
import memory
```

Every heap allocation comes from the `memory` module, and every
allocation must be matched by exactly one `free`. The functions:

| Function | Returns | Initializes |
|----------|---------|-------------|
| `alloc<T>()` | `ptr<T>`, one element | no |
| `alloc_zeroed<T>()` | `ptr<T>` | zeroed |
| `alloc_array<T>(count)` | `ptr<T>` to `count` elements | no |
| `alloc_zeroed_array<T>(count)` | `ptr<T>` | zeroed |
| `alloc_bytes(n)` | `ptr<u8>` | no |
| `alloc_aligned(size, align)` | `ptr<u8>`; align must be a power of two | no |
| `realloc_array<T>(p, count)` | `ptr<T>`, possibly moved | preserved up to old length |
| `free<T>(p)` | - | release, exactly once |
| `memcpy(dst, src, bytes)` | - | regions must not overlap |
| `memset(dst, byte, bytes)` | - | fill with a byte value |
| `memcmp(a, b, bytes)` | `i32`, `<0 / 0 / >0` | - |

None of these store a length or a type tag. A typical growable buffer
keeps the three things it needs and frees them in one place:

```core
import memory

struct Buffer {
    data: ptr<i32>
    len: usize
    cap: usize
}

func buf_push(b: ptr<Buffer>, v: i32) {
    if b.len == b.cap {
        if b.cap == 0 { b.cap = 4 } else { b.cap = b.cap * 2 }
        b.data = realloc_array<i32>(b.data, b.cap)
    }
    b.data[b.len] = v
    b.len += 1
}

func buf_free(b: ptr<Buffer>) {
    free(b.data)
    b.data = null
}
```

`realloc_array` on a null pointer behaves like `alloc_array`, so the
buffer above starts with `data: null, len: 0, cap: 0`.

Ownership is a discipline, not an enforced rule: one owner allocates
and frees. Passing a pointer around is free; just decide who frees.

## What is undefined behavior

The compiler does not stop you from:

- dereferencing `null` or a dangling pointer
- double-freeing or freeing memory you did not allocate
- reading `alloc<T>` memory before writing it
- buffer overflows through raw pointers (array indexing on arrays and
  strings is checked; pointer arithmetic is not)
- data races (unsynchronized access to the same memory from two threads)
- shift counts greater than or equal to the bit width

Signed overflow is defined (wraps). Integer division or modulo by zero
traps with SIGFPE; float division by zero gives IEEE inf/NaN.

Details and the allocator patterns that make this manageable:
[language/memory-model.md](../language/memory-model.md).

## Panics instead of silent corruption

Two failure modes are checked and abort the process with a message on
stderr, e.g. `panic: index 7 out of bounds for length 3 (main.cr:12)`:

- array indexing outside `[T; N]`
- string indexing outside `len(s)`

Inside an `unsafe` block, indexing is unchecked. `panic(msg)` aborts
immediately with your message; `assert(cond)` and `assert(cond, msg)`
report file and line.
