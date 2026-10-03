# Generics

Generics let one function or type work over many element types. Core generics are **monomorphized**: the compiler stamps out a specialized copy for each type you use, like C++ templates — but with type checking of the *template* itself.

## Generic functions

```core
func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}

func main() {
    say max(3, 9)        // T = i32
    say max(1.5, 0.5)    // T = f64
    say max('a', 'z')    // T = char
    say max<i32>(4, 2)   // explicit call-site arguments
}
```

- `<T>` after the name declares a type parameter; use `T` in params/returns/locals.
- **Inference**: calls usually infer `T` from the argument types. Unification requires the arguments to *agree* — `max(1, 2.5)` is an error (`i32` vs `f64`).
- **Explicit args**: `max<i32>(4, 2)` pins the type when inference is ambiguous or you want a specific monomorphization.

The generic body is type-checked once — with `T` abstract — so `max` only compiles for types that support `<` and `>` with the exact operators you used. **There are no trait bounds in v1**: nothing declares "T must be comparable"; if a type doesn't support the operations, that instantiation fails to compile.

## Generic types

```core
struct Pair<A, B> {
    first: A
    second: B
}

struct Box<T> { value: T }

func main() {
    p: Pair<string, i32> = Pair<string, i32> { first: "age", second: 30 }
    say p.first + "!"
    say p.second
}
```

Type arguments are written after the name at every use: `Box<i32>`, `Pair<string, i32>`. Generic types monomorphize like functions: `Box<i32>` and `Box<string>` are two distinct concrete types.

> **Nested generics need a space**: `ptr<Box<T> >` — the lexer reads `>>` as one token, so `ptr<Box<T>>` is a parse error.

### Generic struct methods

Fields can use `T`, but **methods that take or return `T` don't work in v0.1** (the type parameter isn't visible in method signatures). The workaround is generic free functions:

```core
struct Box<T> { value: T }

func box_get<T>(b: Box<T>) -> T { return b.value }
func box_set<T>(b: ptr<Box<T> >, v: T) { b.value = v }

func main() {
    bx: Box<i32> = Box<i32> { value: 5 }
    say box_get(bx)       // 5
    box_set(&bx, 9)       // pass a pointer to mutate
    say bx.value          // 9
}
```

Generic **classes** exist, with the same restriction — an `init` taking a `T` parameter doesn't compile yet; construct via field literals or factory functions.

## Pointer parameters: the workhorse pattern

Generic functions + pointers cover most "container algorithm" needs:

```core
func swap<T>(a: ptr<T>, b: ptr<T>) {
    t: T = *a
    *a = *b
    *b = t
}

func sum<T>(arr: ptr<T>, n: i32) -> i64 {
    mut total: i64 = 0
    for i in 0..n { total += arr[i] as i64 }
    return total
}

func main() {
    x = 1
    y = 2
    swap(&x, &y)
    say x        // 2
    data: [i32; 4] = [1, 2, 3, 4]
    say sum(&data, 4)   // 10 — decay, then T = i32
}
```

Note `arr[i] as i64` — arithmetic *inside* a generic body must work for whatever `T` becomes, so restrict bodies to operations every instantiation supports (or cast).

## Monomorphization details

- Each used instantiation is compiled once: `max<i32>`, `max<f64>`, `max<char>` become separate native functions with mangled names like `_C3maxIi32E`.
- No code bloat unless you use many instantiations — same model as C++ templates, Rust (non-dyn) generics.
- Instantiation happens during semantic analysis; a generic body that doesn't compile for `T` fails when first instantiated.
- Generic definitions live where you wrote them (whole-program compilation means no export/import dance).

## Complete working example

```core
// generics.cr
struct Pair<A, B> {
    first: A
    second: B
}

struct Box<T> { value: T }

func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}

func swap<T>(a: ptr<T>, b: ptr<T>) {
    t: T = *a
    *a = *b
    *b = t
}

func sum<T>(arr: ptr<T>, n: i32) -> i64 {
    mut total: i64 = 0
    for i in 0..n { total += arr[i] as i64 }
    return total
}

func box_get<T>(b: Box<T>) -> T { return b.value }
func box_set<T>(b: ptr<Box<T> >, v: T) { b.value = v }

func main() {
    say max(3, 9)          // 9
    say max(1.5, 0.5)      // 1.5
    say max('a', 'z')      // z
    say max<i32>(4, 2)     // 4

    p: Pair<string, i32> = Pair<string, i32> { first: "age", second: 30 }
    say p.first + "!"      // age!
    say p.second           // 30

    x = 1
    y = 2
    swap(&x, &y)
    say x                  // 2
    say y                  // 1

    data: [i32; 4] = [1, 2, 3, 4]
    say sum(&data, 4)      // 10

    bx: Box<i32> = Box<i32> { value: 5 }
    say box_get(bx)        // 5
    box_set(&bx, 9)
    say bx.value           // 9
}
```

## Common mistakes

- **Mixed argument types.** `max(1, 2.5)` — inference can't unify; cast one side or specify `max<f64>(1, 2.5)`.
- **`ptr<Box<T>>` without a space.** Write `ptr<Box<T> >`.
- **Assuming bounds.** Nothing guarantees `T` is comparable/printable — a bad instantiation is a compile error at the *use site*, with a mangled-name message. Read the unmangled parts.
- **Generic methods on generic structs** — not supported yet; use generic free functions.
- **Trying to constrain `T`** — no bounds exist; document requirements in a comment.

## Performance notes

- Monomorphized generics run at full speed — each instantiation is hand-tailored machine code with no dispatch, boxing, or vtables.
- LLVM inlines generic functions like any other; small ones vanish.
- Many instantiations of big functions grow the binary — usual template-library tradeoff.
- Generic code with `as i64`-style widening inside loops can cost; specialize manually for hot paths if needed.

## When to use / not use

- **Use generics** for data-structure/algorithms helpers (max, swap, box) and type-safe containers — see [data-structures.md](data-structures.md).
- **Don't** genericize one-use functions; concrete types are easier to read.
- **Don't** try to emulate Haskell/Rust-style type classes — Core v1 generics are just parameterized code.
- For heterogeneous collections, see [oop.md](oop.md) (interface fat pointers) — or store a tagged enum (see [enums.md](enums.md)).

## Exercises

1. Write generic `min<T>` alongside `max<T>` and test both on `i32`, `f64`, and `char`.
2. Use `swap<T>` from this page on an `i32` pair and a `string` pair.
3. Write `first_of<T>(b: Box<T>) -> T` and use it on `Box<i32>` and `Box<string>`.
4. Write `contains<T>(arr: ptr<T>, n: i32, key: T) -> bool` — note in a comment what operations `T` must support (no bounds!).
5. Declare a `ptr<Box<i32> >` variable (mind the space), point it at a local Box, and mutate through it — then try the `>>` spelling and read the parse error.

Next: [Enums](enums.md).
