# Variables

Core has one statement form for both *declaring* and *assigning*: `name = expr`. Which one it is depends on whether `name` already exists in scope — the same rule Go uses.

## The three ways to introduce a name

```core
x = 10              // 1. declare: immutable, type inferred
mut y: i32 = 20     // 2. declare: mutable, explicit type
z: f64 = 1.5        // 3. declare: immutable, explicit type
const MAX: i32 = 100 // compile-time constant
```

- `x = 10` — if `x` does **not** exist in scope, this declares an **immutable** variable with the type inferred from the initializer.
- If `x` **does** exist and is mutable, this is an assignment.
- If `x` exists but is immutable, the compiler rejects it: `cannot assign to immutable variable 'x'`. To change a value later, declare it with `mut`.

```core
mut n = 1
n = 2           // fine: n is mutable

m = 1
// m = 2        // ERROR: cannot assign to immutable variable 'm'
```

## The assign-vs-declare rule

A bare `name = expr` statement:

| Situation | Effect |
|---|---|
| `name` not in scope | **declares** an immutable variable |
| `name` is a `mut` variable in scope | **assigns** |
| `name` is an immutable variable in scope | **compile error** |

This rule applies to loop variables too — a C-style loop needs `for mut i = 0; ...` because plain `i = 0` would declare `i` immutable and `i += 1` would then fail.

## Types at declaration

Inference from the initializer is the common case; explicit types document intent and pin the representation:

```core
count = 0            // i32 (integer literals default to i32)
ratio = 0.5          // f64
name = "core"        // string
flag = true          // bool
mut limit: u16 = 65535
mut big: i64 = 9000000000
```

See [types.md](types.md) for every primitive.

## `mut`

`mut` is part of the *binding*, not the type. Immutable by default makes data flow easy to follow: if a variable has no `mut`, its value never changes.

```core
mut total = 0
for i in 0..5 { total += i }   // total is 10
```

## `const`

`const NAME: type = expr` declares a compile-time constant. The initializer must be constant-foldable. Constants live in the type namespace of the module and are readable anywhere in it.

```core
const MAX: i32 = 100
const GREETING: string = "hi"
func main() { say MAX + 1 }    // 101, folded at compile time
```

> **v0.1 caveat:** reading a `const` from **another module** currently evaluates to zero/empty — expose cross-module constants as `pub` globals or accessor functions instead (see [modules.md](modules.md)).

## Globals

At top level, `name = expr` declares a **global**:

```core
BASE: i32 = 50            // immutable global (constant-foldable initializer)
mut hits: i32 = 0         // mutable global
tls tid: i32 = 0          // thread-local global
```

Initializers for globals must be constant-foldable expressions (they run before `main`). Globals are `pub` only if you write `pub` in front — otherwise they're module-private (see [modules.md](modules.md)).

> **v0.1 caveat:** direct assignment to a `mut` global from a function is currently rejected (the parser drops the `mut`). Mutate globals through a pointer instead:

```core
mut hits: i32 = 0

func main() {
    hp: ptr<i32> = &hits
    *hp += 3
    say hits   // 3
}
```

## Shadowing

Re-declaring a name in a **nested block** creates a new binding that shadows the outer one; the outer binding is untouched when the block ends:

```core
mut outer = 1
if true {
    mut outer = 2   // shadows
    say outer       // 2
}
say outer           // 1
```

Shadowing with an identical bare `name = expr` (no `mut`) inside the nested block assigns to the *outer* binding if it is mutable — bare forms never create shadow declarations when the name already resolves in scope.

## Complete working example

```core
// vars.cr
const MAX: i32 = 100

func main() {
    x = 10                  // immutable, inferred i32
    mut y: i32 = 20         // mutable, explicit type
    z: f64 = 1.5            // immutable, explicit type

    y += 5                  // y is 25
    say x + y               // 35 — same-type addition is fine
    say z                   // 1.5

    say MAX                 // 100

    // globals + pointer mutation
    mut outer = 1
    if true {
        mut outer = 2
        say outer           // 2
    }
    say outer               // 1
}
```

Compile and run with `core compile vars vars.cr && ./vars`.

## Common mistakes

- **Forgetting `mut`** on something you reassign: `x = 1; x = 2` declares immutable `x`, then fails. Write `mut x = 1`.
- **C-style loops without `mut`:** `for i = 0; i < n; i += 1` fails — declare the loop variable `mut`.
- **Expecting truthiness.** `if name { }` fails; conditions must be `bool` (see [control-flow.md](control-flow.md)).
- **Shadow confusion:** a nested `mut x = ...` shadows rather than updating; the outer value is restored after the block.
- **Global initializers must be constant.** `BASE: i32 = compute()` won't compile.

## Performance notes

- Immutable locals are trivially optimized; LLVM's passes promote them to registers.
- `const` values are folded at compile time — zero runtime cost.
- Prefer stack locals over globals: globals live in memory (a store/load each use) unless LLVM proves they're local to one function.

## When to use what

- **Bare `x = expr`** — the default; use it everywhere.
- **`mut`** — only when the variable genuinely changes; it's a promise readers can rely on.
- **Explicit types** — at API boundaries, for numeric code where width matters, and whenever inference would be surprising.
- **`const`** — for magic numbers and fixed configuration.
- **Globals** — sparingly; they're mutable shared state. Pass values as parameters, or bundle state in a struct (see [structs.md](structs.md)).

## Exercises

1. Write a program that swaps two variables using a temporary — declare each binding with the right mutability.
2. Show (to yourself) that shadowing in a nested block doesn't affect the outer binding.
3. Compile `x = 1; x = 2` and read the error message. Then fix it two different ways.
4. Create a `tls` global, print it from two threads, and confirm each thread sees its own copy (see [concurrency.md](concurrency.md)).

Next: [Types](types.md).
