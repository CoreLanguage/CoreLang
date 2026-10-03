# Pointers

A pointer is a raw machine address with a type attached: `ptr<T>` points at a `T`. Core pointers are honest C pointers — arithmetic scales by the pointee size, `null` is a real null, and dereferencing nothing guards you. What pointers *don't* do is manage lifetimes: that's your job (see [memory-management.md](memory-management.md)).

## The two operators

```core
x = 10
p = &x       // address-of: p is ptr<i32>
*p = 20      // dereference-write: x is now 20
say *p       // dereference-read: 20
```

- `&expr` takes the address of something that has storage: a variable, an array element, a field. `&5` (a temporary) is a compile error.
- `*p` reads or writes the pointee. Dereferencing `null` aborts with a clear runtime error.

## The type `ptr<T>`

`ptr<T>` is a 64-bit address. The special case `ptr<void>` is the opaque/void pointer (an `i8*` under the hood) — the currency of `alloc_bytes`, `memcpy`-style APIs, and C interop.

```core
n: ptr<i32> = null          // explicit null
say n == null               // true
```

## Pointer arithmetic

`p + i` and `p - i` move in **elements**, not bytes — the compiler scales by `sizeof(T)`:

```core
data: [i32; 4] = [10, 20, 30, 40]
mut q = &data[0]
say *(q + 2)     // 30 — moved 2 * 4 bytes
q += 1
say *q           // 20
```

Subtracting pointers yields the **element distance** (`isize`):

```core
say q - &data[0]   // 1
```

All six comparisons work on pointers (`== != < <= > >=`), comparing addresses. Comparing pointers into *different* allocations with `<`/`>` is technically unspecified — stick to same-allocation ordering.

## Pointers to pointers

```core
x = 10
p = &x
pp = &p           // ptr<ptr<i32>>
**pp = 30         // writes x through both levels
say x             // 30
```

## Pointers into structs

Field access through a pointer auto-derefs, so chains read naturally:

```core
import memory

struct Node {
    value: i32
    next: ptr<Node>     // self-referential via pointer
}

n1 = alloc<Node>()
n2 = alloc<Node>()
n1.value = 1
n2.value = 2
n1.next = n2
n2.next = null
say n1.next.value    // 2 — two auto-derefs
free(n1)
free(n2)
```

## Decay: arrays become element pointers

`&array` is a `ptr<[T; N]>`, but in argument position it **decays** to `ptr<T>` (a pointer to the first element). This is what lets pointer-taking functions consume arrays:

```core
func sum_ptr(xs: ptr<i32>, n: i32) -> i32 {
    mut t = 0
    for i in 0..n { t += xs[i] }
    return t
}

func main() {
    data: [i32; 4] = [10, 20, 30, 40]
    say sum_ptr(&data, 4)   // &data decays to ptr<i32>
}
```

`xs[i]` on a pointer is pointer indexing (unchecked!) — inside functions taking `ptr<T>`, there is no bounds info.

## `ptr<void>` and casts

Any pointer converts to `ptr<void>` and back; casts between different pointee types and between integers and pointers require `unsafe` because they can break memory safety:

```core
import memory

func main() {
    raw: ptr<void> = alloc<Node>()        // any ptr<T> -> ptr<void> implicitly
    typed = unsafe { raw as ptr<Node> }   // back with unsafe
    typed.value = 7
    say typed.value
    free(typed as ptr<Node>)              // cast back for free<T>

    addr = unsafe { 0x1000 as ptr<u8> }   // integer -> pointer (MMIO, etc.)
    say unsafe { addr as u64 } == 0x1000  // pointer -> integer
}
```

## null checks

```core
func get_next(n: ptr<Node>) -> i32 {
    if n == null { return 0 }
    if n.next == null { return n.value }
    return n.next.value
}
```

## Complete working example

```core
// pointers.cr
import memory

struct Node {
    value: i32
    next: ptr<Node>
}

func sum_ptr(xs: ptr<i32>, n: i32) -> i32 {
    mut t = 0
    for i in 0..n { t += xs[i] }
    return t
}

func main() {
    x = 10
    p = &x
    say *p        // 10
    *p = 20
    say x         // 20

    data: [i32; 4] = [10, 20, 30, 40]
    mut q = &data[0]
    say *(q + 2)  // 30
    q += 1
    say *q        // 20
    say q - &data[0]   // 1 (element distance)

    pp = &p
    **pp = 30
    say x         // 30

    n: ptr<i32> = null
    say n == null       // true
    say p != null       // true
    say p == &x         // true
    say q > &data[0]    // true

    a = alloc<Node>()
    b = alloc<Node>()
    a.value = 1
    b.value = 2
    a.next = b
    b.next = null
    say a.next.value    // 2
    free(a)
    free(b)

    say sum_ptr(&data, 4)   // 100

    raw: ptr<void> = alloc<Node>()
    typed = unsafe { raw as ptr<Node> }
    typed.value = 7
    say typed.value         // 7
    free(typed as ptr<Node>)
}
```

## Common mistakes

- **Dangling pointers.** After `free(p)`, `p` is a trap. Don't read or write through it; set it to `null` if it might linger.
- **`&` of temporaries.** `&f(x)` or `&5` won't compile — store the value first.
- **Losing the bounds.** A `ptr<T>` carries no length. You must thread the length alongside (`func f(buf: ptr<u8>, n: usize)`).
- **`as` precedence with `&`.** `&x as ptr<T>` parses as `&(x as ptr<T>)` — write `(&x) as ptr<T>`.
- **Pointer arithmetic on bytes when you mean elements** (or vice versa). `p + 1` moves one *element*.
- **Dereferencing `null`.** It aborts with a runtime error — still a crash. Check before deref when the pointer is optional.

## Performance notes

- Pointers are free — they compile to raw addresses with zero indirection overhead.
- Pointer arithmetic at `-O2` is folded into addressing modes; `p[i]` and `*(p + i)` are identical machine code.
- Bounds-checked array indexing costs a compare + branch per access; unchecked pointer indexing in `unsafe` removes it — earn it with a proof comment.

## When to use / not use

- **Use pointers** for: heap allocations, linked structures, big-struct parameters, C interop, optional values (with `null` + checks).
- **Prefer values** (structs, arrays) for small, short-lived data — they're stack-friendly and copy-cheap.
- **Never** store raw pointers in interfaces that outlive their allocation without documenting who frees.
- If you're reaching for pointers to share mutable state between functions, first read [references.md](references.md) — and for concurrency, [concurrency.md](concurrency.md).

Next: [References](references.md).
