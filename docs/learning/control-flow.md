# Control flow

Core has the classic set: `if`/`else`, `while`, three flavors of `for`, `break`/`continue`, `switch`, and `match`. Everything is brace-scoped; conditions must be `bool`.

## if / else

```core
if cond {
    // ...
} else if other {
    // ...
} else {
    // ...
}
```

**Conditions must be `bool`** — no truthiness. `if n {}` or `if s {}` are compile errors; write the comparison you mean.

`if` is a statement, not an expression — you can't write `x = if c { 1 } else { 2 }`. Use `match`, which *is* an expression:

```core
x = 5
label = match x {
    0 { "zero" }
    _ { "nonzero" }
}
```

## while

```core
mut n = 0
while n < 5 { n += 1 }
```

`while true { ... break ... }` is the idiomatic infinite loop with an exit condition. No `do-while` — put the check in the loop or use `break`.

## for

Three forms:

```core
// 1. exclusive range: 0, 1, ..., n-1
for i in 0..n { ... }

// 2. inclusive range: 0, 1, ..., n
for i in 0..=n { ... }

// 3. iterate an array's elements
for x in array { ... }

// 4. C-style: init; cond; step
for mut i = 0; i < n; i += 1 { ... }
```

Notes:

- Ranges are half-open `..` or closed `..=`; both directions of counting work (`for i in 5..1` counts down? — no: v0.1 ranges count **up**; use a C-style loop with `i -= 1` to count down).
- The loop variable is immutable per iteration unless you declare `for mut i in ...` (needed for C-style stepping).
- `for x in array` yields **elements**, not indices, and works on fixed arrays and ranges alike.
- **Strings are not iterable** in v0.1 — loop over `0..len(s)` and index with `s[i]`.

```core
// all three together
mut sum = 0
for i in 0..10 { sum += i }        // 45
for i in 1..=4 { sum += i }        // +10
for mut i = 10; i > 0; i -= 1 { sum += i }  // +55
```

## break and continue

Work in `while` and `for` loops. `break` leaves the innermost loop; `continue` skips to the next iteration.

```core
for i in 0..5 {
    if i == 2 { continue }
    if i == 4 { break }
    say i          // 0, 1, 3
}
```

## switch

`switch` dispatches on **integers and enums** (no fallthrough — each case is separate):

```core
switch value {
    case 1:  say "one"
    case 2:  say "two"
    default: say "other"
}
```

- `case` values must match the switched type exactly (enum cases are written `Color.Green`).
- `default:` is optional; with no match, execution continues after the switch.
- For `char` subjects use `match` (char literals are match patterns) — see below.

## match

`match` is Core's pattern-matching workhorse. It works on **enums** (with payload binding), literals, and anything with `==`; it's an **expression**; it's **exhaustive over enums** — every variant must be covered or you provide a `_` wildcard.

```core
enum Op {
    Add(i32, i32),
    Neg(i32),
    Id
}

func eval(e: Op) -> i32 {
    return match e {
        Add(a, b) { a + b }    // payload binding: two fields
        Neg(v)    { -v }
        Id        { 7 }        // plain variant
    }
}
```

Patterns on plain values: literals, bindings (a bare identifier matches anything and binds it), and `_` wildcard:

```core
n = 7
desc = match n {
    0 { "zero" }
    7 { "lucky" }
    v { to_string(v) + "?" }   // binding: any other value
}
```

Matching `Option<T>` and `Result<T, E>` is the standard error-handling style — see [error-handling.md](error-handling.md).

## Complete working example

```core
// control.cr - FizzBuzz-ish with every construct
func classify(n: i32) -> string {
    if n % 15 == 0 { return "fizzbuzz" }
    else if n % 3 == 0 { return "fizz" }
    else if n % 5 == 0 { return "buzz" }
    return to_string(n)
}

func collatz_steps(start: i32) -> i32 {
    mut n = start
    mut steps = 0
    while n != 1 {
        if n % 2 == 0 { n /= 2 }
        else { n = 3 * n + 1 }
        steps += 1
    }
    return steps
}

func main() {
    // if/else chain
    for i in 1..=15 { print(classify(i) + " ") }
    print("\n")

    // while + break
    mut n = 0
    while true {
        n += 1
        if n * n > 50 { break }
    }
    say n                   // 8

    // continue
    mut odd_sum = 0
    for i in 0..10 {
        if i % 2 == 0 { continue }
        odd_sum += i
    }
    say odd_sum             // 25

    // C-style with mutable loop variable
    mut p = 1
    for mut i = 0; i < 4; i += 1 { p *= 2 }
    say p                   // 16

    // switch on an integer
    x = 2
    switch x {
        case 1: say "one"
        case 2: say "two"
        default: say "many"
    }

    // match as an expression
    label = match x {
        0 { "zero" }
        2 { "two" }
        _ { "other" }
    }
    say label               // two

    // match over enum payloads
    say eval(Op.Add(2, 3))  // 5
}

// (the enum Op and func eval from the match section above)
func eval(e: Op) -> i32 {
    return match e {
        Add(a, b) { a + b }
        Neg(v)    { -v }
        Id        { 7 }
    }
}
```

## Common mistakes

- **Non-bool conditions.** `if x { }`, `while n { }` — rejected. Use `x != 0`, `n > 0`, etc.
- **Forgetting `mut` in C-style loops** (`for i = 0; i < n; i += 1` → declare `for mut i = ...`).
- **Off-by-one on ranges.** `0..n` is `n` iterations; `0..=n` is `n + 1`.
- **Expecting fallthrough in `switch`.** There is none; each case stands alone.
- **`match` without exhaustiveness.** For enums, cover every variant or add `_`.
- **`match` arms with different types.** All arms must produce the same type when the match is used as an expression.
- **Iterating a string with `for`** — not supported yet; index manually.
- **`mut` parameters.** Parameters are plain locals; write `func f(start: i32) { mut n = start ... }` instead.

## Performance notes

- `switch` on dense integers compiles to a jump table; on sparse values, to a comparison tree. Same for `match` on plain integers.
- `match` on enums lowers to a tag check plus (for payloads) field loads — no boxing, no allocation.
- Loop ranges compile to the same machine code as C-style loops; choose whichever reads better.

## When to use / not use

- `for i in 0..n` — default choice over arrays/slices.
- `while` — when the termination condition isn't a count.
- `switch` — flat dispatch on integers/enums without payloads.
- `match` — anything enum-shaped, or when you want a value out of a decision.
- Avoid deep `else if` ladders over enums — `match` is exhaustive and the compiler checks it for you.

Next: [Functions](functions.md).
