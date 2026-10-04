# Functions

```core
func add(a: i32, b: i32) -> i32 { return a + b }
```

Parameters are `name: type`. The return type follows `->`; omit it for
`void`. Every function is a module-level declaration; there are no
nested functions (use lambdas for those).

## Parameters and returns

Default values: `func connect(host: string, port: i32 = 80)`. Trailing
parameters with defaults may be omitted at the call site; the default
expression is evaluated there, each call.

Overloading: same name, different parameter lists, resolved by the
best-scoring candidate (exact type match beats a conversion; a tie is
an error). Overloading combines with defaults, so keep overload sets
small enough that a call resolves unambiguously.

Variadics exist only on `extern` (C) functions:

```core
extern func printf(fmt: ptr<char>, ...) -> i32
```

Extra arguments must be integer, float, pointer, or bool; C default
promotions apply (`f32` widens to `f64`).

The default-return rule: when a function body falls off the end, it
implicitly returns the type's default value (`0`, `0.0`, `false`,
`null`, `""`, tag 0, zero vector, or nothing for `void`). Functions
returning a struct, class, or array must have an explicit `return`.
A function declared `-> never` must not return normally.

`never` is the type of `panic(msg)` and `process.exit(code)`. A call to
a `never` function makes every following statement unreachable, and the
compiler's reachability analysis knows it:

```core
func check(v: i32) -> i32 {
    if v < 0 { panic("negative") }   // never, so no else needed
    return v
}
```

## Generics

```core
func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}
```

Type arguments are inferred from the call (`max(3, 9)`) or written
explicitly (`max<i32>(3, 9)`). Each used instantiation is monomorphized
into its own copy at compile time. There are no trait bounds; if the
concrete type does not support `>` in the body, the error surfaces at
instantiation time. See [generics.md](generics.md).

## Lambdas and closures

```core
fn = func(a: i32) -> i32 { return a * 2 }
say fn(21)                    // 42
log = func(a: i32) { say a }  // return type may be inferred (void)
```

A lambda is a closure value: a pair of a function pointer and an
environment pointer (16 bytes). Lambda parameters require type
annotations. It captures enclosing locals **by value** at creation
time, so a closure never observes later changes to the captured
variable. To share state with a closure, capture a pointer to it:

```core
mut count: i32 = 0
p = &count
inc = func() { *p += 1 }   // writes the variable through the pointer
inc()
say count                  // 1
```

A variable of function type holds a closure. Referencing a plain
function wraps it in a trampoline that ignores the environment, so
passing a named function where a `func()` is expected works.

## Extern functions (C ABI)

```core
extern func puts(s: ptr<char>) -> i32
@link_name("my_c_symbol") extern func thing(x: i32) -> i32
```

`extern` declares a C function: C ABI, no name mangling. Link the
library with `--link=<lib>` on the command line or `link = ["..."]` in
`core.toml`. See [unsafe-and-low-level.md](unsafe-and-low-level.md).

## Methods

Methods are functions declared inside a type. Instance methods receive
an implicit `self: ptr<Type>`; `static func` methods are called as
`TypeName.method(...)`. Constructors are `func init(...)` methods.
See [structs-and-enums.md](structs-and-enums.md) and [oop.md](oop.md).
