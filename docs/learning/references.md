# References (there aren't any)

Core has **no references** — no `T&`, no borrowing, no reference-counting. Everything is a **value**: assignment copies, argument passing copies, returns copy. When you need to share or mutate, you use an explicit **pointer**. This tutorial maps the C++/Rust habits you might have onto Core's model.

## The rule: everything is a value

```core
struct Vec2 { x: f64, y: f64 }

func try_modify(v: Vec2) {
    v.x = 999           // modifies the LOCAL copy
}

func main() {
    a = Vec2 { x: 1.0, y: 2.0 }
    b = a               // full copy of the struct
    b.x = 5.0
    say a.x             // 1 — a is untouched
    try_modify(a)
    say a.x             // 1 — the argument was a copy
}
```

Arrays behave the same way (they're inline values), as do strings (they copy the 16-byte *view*, not the bytes — see [strings.md](strings.md)).

## What replaces a reference? A pointer.

Any place another language uses a reference — out-parameters, in-place mutation, shared ownership — Core uses `ptr<T>`:

### Out-parameters

```core
func divmod(a: i32, b: i32, rem: ptr<i32>) -> i32 {
    *rem = a % b
    return a / b
}

func main() {
    mut r = 0
    q = divmod(17, 5, &r)
    say q       // 3
    say r       // 2
}
```

### In-place mutation

```core
func normalize(v: ptr<Vec2>) {
    len_sq = (*v).x * (*v).x + (*v).y * (*v).y
    // auto-deref makes field access nicer:
    v.x = v.x / len_sq
    v.y = v.y / len_sq
}
```

### "Returning" multiple values

Either use out-params as above, or return a struct — copies are cheap for small data:

```core
struct MinMax { min: i32, max: i32 }

func minmax(a: [i32; 4]) -> MinMax {
    mut m = MinMax { min: a[0], max: a[0] }
    for x in a {
        if x < m.min { m.min = x }
        if x > m.max { m.max = x }
    }
    return m
}
```

## Passing structs by pointer: the standard pattern

Large structs should be passed by pointer to avoid the copy. The by-convention shape is `func thing_do(s: ptr<Thing>)`, with methods where it clarifies intent:

```core
import memory

struct Buffer {
    data: ptr<i32>
    len: usize
    cap: usize
}

func buf_init(b: ptr<Buffer>, cap: usize) {
    b.data = alloc_array<i32>(cap)
    b.len = 0
    b.cap = cap
}

func buf_push(b: ptr<Buffer>, v: i32) {
    if b.len == b.cap {
        b.cap = b.cap * 2
        b.data = realloc_array<i32>(b.data, b.cap)
    }
    b.data[b.len] = v
    b.len += 1
}

func buf_free(b: ptr<Buffer>) {
    free(b.data)
}

func main() {
    mut b: Buffer
    b.data = null
    b.len = 0
    b.cap = 0
    buf_init(&b, 4)
    for i in 0..10 { buf_push(&b, i * i) }
    say b.len          // 10
    say b.data[7]      // 49
    buf_free(&b)
}
```

This is exactly how the standard library's own APIs work (`thread.spawn` takes a lambda; `Mutex` methods mutate through the object's pointer; see [concurrency.md](concurrency.md)).

## Comparison to what you may know

| Habit from | Core equivalent |
|---|---|
| C++ `T&` parameter | `ptr<T>` parameter, call with `&x` |
| C++ `const T&` | pass the value if small; `ptr<T>` if large (no const-pointers in v0.1) |
| Rust `&mut T` | `ptr<T>` — but the compiler doesn't enforce exclusivity; discipline is yours |
| Rust references are valid forever | Core pointers can dangle; see [memory-management.md](memory-management.md) |
| C++ move semantics | just copies; move by nulling out the source pointer yourself |

There are no `const` pointers in v0.1: a `ptr<T>` can always write. If a function mustn't mutate, don't write through the pointer — the language trusts you.

## Shared ownership

There's none built in — no `shared_ptr`, no GC. The owning party is whoever allocated, and that's **explicit**:

```core
import memory

struct Node {
    value: i32
    next: ptr<Node>
}

// owner: main. borrower: print_node — borrows, never frees.
func print_node(n: ptr<Node>) {
    if n == null { return }
    say n.value
}

func main() {
    n = alloc<Node>()
    n.value = 7
    n.next = null
    print_node(n)    // borrow
    free(n)          // exactly one owner frees
}
```

Rule of thumb: **the function that allocates documents (and does, or delegates) the free.** Borrowers never free. See [memory-management.md](memory-management.md) for the full ownership discipline.

## Complete working example

```core
// refs.cr - byvalue vs pointer semantics side by side
struct Counter {
    hits: i32
}

// takes a copy: caller's Counter is unchanged
func bump_copy(c: Counter) -> i32 {
    c.hits += 1
    return c.hits
}

// takes a pointer: caller's Counter IS changed
func bump_shared(c: ptr<Counter>) {
    c.hits += 1
}

func main() {
    mut c = Counter { hits: 0 }

    say bump_copy(c)    // 1
    say c.hits          // 0 — copy semantics

    bump_shared(&c)
    bump_shared(&c)
    say c.hits          // 2 — shared through pointer

    // array: byvalue copy
    a: [i32; 3] = [1, 2, 3]
    mut b = a
    b[0] = 99
    say a[0]            // 1

    // string: view is copied, bytes shared
    s = "core"
    t = s
    say t == s          // true — both view the same static bytes
}
```

## Common mistakes

- **Expecting a called function to mutate your struct.** It received a copy — pass `&struct`.
- **Two owners.** If two structs hold the same heap pointer, decide *now* who frees, or you'll double-free (undefined behavior).
- **Returning a pointer to a local.** The local dies at return; the pointer dangles. Return the value, or allocate.
- **Const illusions.** `ptr<T>` can write; "read-only" is a comment, not a type.

## Performance notes

- By-value passing of small structs (≤ 16 bytes, 2 machine words) is essentially free — registers.
- Big structs by value are `memcpy`s at every call site. Pass pointers.
- No aliasing analysis: with two live `ptr<T>` to the same memory, LLVM must be conservative; fewer live pointers = better optimization.

## When to use / not use

- **Values** for small data, return-by-value APIs, and anything that shouldn't be shared.
- **Pointers** for mutation, big data, linked structures, and shared state with a single owner.
- If you miss borrow checking: keep borrows short, never free through a borrower, and consider asserting invariants with `assert` (see [error-handling.md](error-handling.md)).

## Exercises

1. Write `bump_copy(c: Counter)` and `bump_shared(c: ptr<Counter>)` for a `Counter` struct; call each twice and print the field to show the difference.
2. Write `minmax(a: [i32; 5]) -> MinMax` returning a struct with both extremes.
3. Return multiple values from `parse_rgb(hex: u32, out: ptr<Color>)` — a return plus an out-param.
4. Define a 4-field struct, pass it by value and by pointer to functions that read all fields, and time 100k iterations of each with `time.monotonic_ms()` (import `time`).
5. Copy an array into a second binding, mutate the copy, and print the original — then explain in a comment why the same doesn't apply to `ptr`-shared data.

Next: [Memory management](memory-management.md).
