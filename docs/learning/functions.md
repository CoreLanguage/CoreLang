# Functions

Functions are declared with `func`, typed parameters, and an optional return type after `->`.

## Syntax

```core
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func hello() {          // no return type = returns void
    say "hello"
}
```

Statements end at newlines (semicolons optional). Braces are required for bodies.

## Returns and the default-return rule

Every function **has** a return value if it declares one. If execution falls off the end without a `return`:

- numeric, `bool`, `char`, pointer, `string`, and simple-enum functions implicitly return the **default value**: `0`, `0.0`, `false`, `null`, `""`, variant 0
- **aggregate returns (structs, classes, arrays) require an explicit `return`** — the compiler rejects a fall-through

```core
func numeric_default() -> i32 { x = 1 }        // returns 0
func string_default() -> string { x = 1 }      // returns ""
func ptr_default() -> ptr<i32> { x = 1 }       // returns null
// func nope() -> SomeStruct { x = 1 }         // ERROR: must return explicitly
```

This makes guard-clause style cheap: return early or fall through to the zero value.

## Parameters

Parameters are plain immutable locals. To "modify" a parameter, copy it into a `mut` local:

```core
func collatz_steps(start: i32) -> i32 {
    mut n = start
    while n != 1 { ... }
}
```

To mutate the caller's variable, pass a pointer (see [pointers.md](pointers.md)):

```core
func bump(x: ptr<i32>) { *x += 1 }
```

## Default arguments

Any trailing parameter can have a default; omitted arguments use it:

```core
func greet(name: string, greeting: string = "Hello") -> string {
    return greeting + ", " + name + "!"
}

func main() {
    say greet("Core")          // Hello, Core!
    say greet("Core", "Hi")    // Hi, Core!
}
```

## Overloads

Functions can share a name when their parameter types make calls unambiguous:

```core
func half(x: i32) -> f64 { return x as f64 / 2.0 }
func half(x: f64) -> f64 { return x / 2.0 }

func main() {
    say half(7)     // picks the i32 overload
    say half(7.0)   // picks the f64 overload
}
```

Overloads must be unambiguous — a call that matches two overloads equally is a compile error. Note the standard library uses this heavily (`say` has one overload per primitive type).

## Recursion

Works exactly as you expect; stack frames are ordinary machine frames:

```core
func fact(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}

func main() { say fact(10) }   // 3628800
```

There is no tail-call guarantee — deep recursion (millions of frames) overflows the stack; convert hot recursions to loops.

## extern: calling C

`extern func` declares a C function with the C ABI and **no name mangling**. Variadic C functions use `...`:

```core
extern func printf(fmt: ptr<char>, ...) -> i32
extern func abs(x: i32) -> i32

func main() {
    printf("hello from libc: %d %s %.2f\n", 7, c_str("strings cross the ABI"), 1.5)
    say abs(-42)
}
```

Variadic arguments must be integers, floats, pointers, or bools. **`string` is a {ptr, len} view — not a C pointer** — so pass `c_str(s)` for `%s`. Full details in [ffi.md](ffi.md).

## Function values and lambdas

The type of a function value is `func(T1, T2) -> R`. Lambdas are written inline with `func`:

```core
func apply(f: func(i32) -> i32, v: i32) -> i32 { return f(v) }

func main() {
    double = func(x: i32) -> i32 { return x * 2 }
    say apply(double, 21)                                // 42
    say apply(func(x: i32) -> i32 { return x + 1 }, 9)   // 10
}
```

**Lambdas capture enclosing locals by value** (at the moment the lambda is created), never by reference:

```core
func main() {
    mut m = 1
    snap = func() -> i32 { return m }
    m = 99
    say snap()   // 1 — the old value was copied in
    say m        // 99
}
```

To share mutable state, capture a **pointer** to heap memory — this is the canonical pattern for thread workers (see [concurrency.md](concurrency.md)):

```core
import memory

func main() {
    cell = alloc<i32>()
    *cell = 10
    get = func() -> i32 { return *cell }
    *cell = 20
    say get()   // 20 — both see the same pointee
    free(cell)
}
```

> **v0.1 caveats:** passing a *named* function as a value (e.g. `apply(add, 1)` for a top-level `add`) currently miscompiles — wrap it in a lambda (`apply(func(x: i32) -> i32 { return add(x, 0) }, 1)`). Returning a lambda that captured another function value can also miscompile. Pass lambdas, not function names, until this firms up.

## Local functions

There are no function-local named functions. Use a lambda bound to a local, or a module-level `func`.

## Complete working example

```core
// functions.cr
func add(a: i32, b: i32) -> i32 { return a + b }
func greet(name: string, greeting: string = "Hello") -> string {
    return greeting + ", " + name + "!"
}
func fact(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}
func numeric_default() -> i32 { x = 1 }      // falls off the end -> 0
func string_default() -> string { x = 1 }    // -> ""
func apply(f: func(i32) -> i32, v: i32) -> i32 { return f(v) }
func swap<T>(a: ptr<T>, b: ptr<T>) {         // generics: see generics.md
    t: T = *a
    *a = *b
    *b = t
}
extern func abs(x: i32) -> i32

func main() {
    say add(2, 3)                 // 5
    say add(add(1, 2), 4)         // 7
    say greet("Core")             // Hello, Core!
    say greet("Core", "Hi")       // Hi, Core!
    say fact(10)                  // 3628800
    say early()                   // 1
    say numeric_default()         // 0
    say string_default() == ""    // true
    say apply(func(x: i32) -> i32 { return x * 2 }, 21)   // 42
    say apply(func(x: i32) -> i32 { return x + 1 }, 9)    // 10
    x = 1
    y = 2
    swap(&x, &y)
    say x                         // 2
    say y                         // 1
    say abs(-9)                   // 9
}

func early() -> i32 { return 1; return 2 }
```

## Common mistakes

- **Returning a struct without `return`.** Aggregate falls-through are compile errors; write the `return`.
- **Expecting `mut` parameters.** Parameters are immutable; copy into a `mut` local or take a pointer.
- **Passing a `string` to a variadic C function.** Use `c_str(s)` — a `string` is a view, not a NUL-terminated pointer.
- **Expecting closures to share mutable state.** Capture is by value; share a pointer.
- **Passing a top-level function name as a value** — currently broken; pass a lambda.
- **Name collisions with overloads.** Two overloads that a call could both match equally are rejected.

## Performance notes

- Functions compile to ordinary native functions; LLVM inlines aggressively at `-O2` — small helpers are free.
- Recursion costs a stack frame per call; the optimizer tail-calls some shapes but don't rely on it.
- Lambdas are compiled as real functions plus a small capture struct — no boxing, no allocation for by-value captures.

## When to use / not use

- Use small pure functions liberally — the optimizer erases them.
- Use overloads when callers shouldn't care about the type; use distinct names when behavior differs meaningfully.
- Don't reach for function values as a substitute for `switch`/`match` — v0.1's function-value support is best kept to simple "pass a lambda to a worker/map" patterns.
- Prefer pointer parameters for big structs you'd otherwise copy; see [references.md](references.md).

Next: [Arrays](arrays.md).
