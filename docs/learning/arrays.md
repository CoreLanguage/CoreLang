# Arrays

Core arrays are **fixed-size value types**: the length is part of the type, the data lives inline (no heap, no hidden pointer), and they copy like any other value.

## Syntax

```core
let-like declaration:
a: [i32; 5] = [1, 2, 3, 4, 5]     // literal form
b = [0; 10]                        // repeat form: ten zeros
c: [[f64; 2]; 2] = [[1.0, 0.0], [0.0, 1.0]]   // nested (matrix)
```

- The type is `[T; N]` — element type, semicolon, length.
- The array is stored inline: `sizeof([i64; 4])` is 32. Assigning an array **copies all elements**.
- Arrays can hold any value type: primitives, structs, even other arrays.

## Literals and the repeat form

```core
a: [i32; 5] = [1, 2, 3, 4, 5]
zeros = [0; 4]                       // [0, 0, 0, 0]
ones: [f64; 3] = [1.0; 3]
mixed_structs: [Vec2; 2] = [Vec2 { x: 1.0, y: 0.0 }, Vec2 { x: 0.0, y: 1.0 }]
```

The literal must have exactly `N` elements (or use the repeat form). Nested literals nest naturally.

## Indexing

`arr[i]` reads and writes with **bounds checking** — an out-of-range index aborts the program with a clear message instead of corrupting memory. Indexing with an out-of-range constant fails at compile time when the compiler can prove it.

```core
a: [i32; 5] = [1, 2, 3, 4, 5]
a[2] = 30
say a[2]         // 30
// say a[9]      // runtime abort: bounds check failure
```

Inside `unsafe { }` blocks, indexing is unchecked (see [unsafe.md](unsafe.md)) — that's the zero-cost escape hatch for hot loops.

## len()

`len(arr)` is a compiler builtin returning the **length as `usize`** — a compile-time constant for fixed arrays:

```core
for i in 0..len(a) { a[i] *= 2 }
```

## Iteration

```core
for x in a { say x }            // elements
for i in 0..len(a) { say a[i] } // indices
```

`for x in array` iterates a **copy**? No — elements are yielded one at a time; mutating `x` does not write back. To transform in place, index.

## Passing arrays to functions

Arrays are values: passing one copies it (subject to by-value struct rules — see [references.md](references.md)). For large arrays pass a pointer; a `ptr<[T; N]>` **decays** to `ptr<T>` automatically in argument position, so both calling styles work:

```core
func sum_ptr(xs: ptr<i32>, n: i32) -> i64 {
    mut t: i64 = 0
    for i in 0..n { t += xs[i] as i64 }
    return t
}

func sum_val(xs: [i32; 5]) -> i64 { ... }

func main() {
    a: [i32; 5] = [1, 2, 3, 4, 5]
    say sum_val(&a)      // wait - see below
    say sum_ptr(&a, 5)   // &a decays: ptr<[i32;5]> -> ptr<i32>
}
```

Passing `&a` to a `[i32; 5]` parameter passes the array itself (by value, copied); passing it to a `ptr<i32>` parameter decays to a pointer to the first element. Pointer parameters avoid the copy for large arrays.

## Dynamic-length sequences

Fixed arrays can't grow. When you need growth, allocate on the heap and track a length/capacity yourself — the standard pattern:

```core
import memory

// growable i32 list (see data-structures.md for the full treatment)
struct List {
    data: ptr<i32>
    len: usize
    cap: usize
}

func push(l: ptr<List>, v: i32) {
    if l.len == l.cap {
        l.cap = l.cap * 2
        l.data = realloc_array<i32>(l.data, l.cap)
    }
    l.data[l.len] = v
    l.len += 1
}

func main() {
    mut l: List
    l.data = null
    l.len = 0
    l.cap = 0
    l.cap = 4
    l.data = alloc_array<i32>(l.cap)
    for i in 0..10 { push(&l, i * i) }
    say l.len        // 10
    say l.data[7]    // 49
    free(l.data)
}
```

## Complete working example

```core
// arrays.cr
func sum(xs: [i32; 5]) -> i64 {
    mut t: i64 = 0
    for x in xs { t += x as i64 }
    return t
}

func main() {
    a: [i32; 5] = [1, 2, 3, 4, 5]
    zeros = [0; 4]

    say len(a)          // 5
    say len(zeros)      // 4
    a[2] = 30
    say a[2]            // 30
    say sum(a)          // 42

    mut total = 0
    for x in a { total += x }
    say total           // 42

    for i in 0..len(a) { a[i] = a[i] * 2 }
    say a[0]            // 2

    // nested arrays: a 2x2 matrix
    m: [[i32; 2]; 2] = [[1, 2], [3, 4]]
    say m[1][0]         // 3

    // decay to element pointer for pointer-style code
    say sum_ptr(&a, 5)  // 84
}

func sum_ptr(xs: ptr<i32>, n: i32) -> i64 {
    mut t: i64 = 0
    for i in 0..n { t += xs[i] as i64 }
    return t
}
```

## Common mistakes

- **Assuming arrays are references.** They're values — assigning or passing **copies** the elements.
- **Writing `[i32, 5]`.** The separator in the type is a semicolon: `[i32; 5]`.
- **Growing a fixed array.** You can't; use heap allocation + `realloc_array` as above.
- **`len()` result into an `i32` without a cast.** `len` returns `usize`.
- **Expecting `for x in arr` writes.** The loop variable is a copy; index to mutate.

## Performance notes

- Fixed arrays are stack storage with zero allocation — the fastest container in Core.
- Bounds checks are real branches. In verified hot loops, move indexing inside `unsafe { }` — but only when you've proven the range.
- Large arrays: pass pointers; by-value copies are `memcpy` behind the scenes.
- Row-major nested arrays (`m[row][col]`) iterate cache-friendly left to right.

## When to use / not use

- **Use fixed arrays** when the size is known and small-ish (buffers, lookup tables, matrices).
- **Don't** use them as "lists" — for dynamic growth, build the heap-backed struct above (or see [data-structures.md](data-structures.md) for lists, stacks, queues, and hash maps).
- Don't copy huge arrays around casually; pass `ptr<T>` instead.

Next: [Strings](strings.md).
