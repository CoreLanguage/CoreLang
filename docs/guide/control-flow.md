# Control flow

## if / else

The condition must be `bool`; there is no truthiness. Braces are
required. `if` is a statement, not an expression.

```core
if x > 0 {
    say "positive"
} else if x == 0 {
    say "zero"
} else {
    say "negative"
}
```

## while / loop

```core
mut n: i32 = 10
while n > 0 { n -= 1 }

loop {                    // forever; exit with break or return
    if done { break }
}
```

`break` and `continue` target the innermost loop. Using them outside a
loop is a compile error.

## for

Range form. `..` is exclusive, `..=` inclusive. Range bounds must be
integers of the same type (a literal adopts the other side's type).
Ranges are valid only in `for` headers.

```core
for i in 0..10 { say i }        // 0 through 9
for i in 0..=10 { say i }       // 0 through 10
```

Array iteration copies each element. Add `mut` to make the copy
assignable.

```core
mut squares: [i32; 5]
for i in 0..5 { squares[i] = i * i }

mut total: i32 = 0
for v in squares { total += v }

for mut v in squares { v = 0 }  // modifies the copy, not the array
```

C-style form, all three parts optional:

```core
for i = 0; i < n; i += 1 {
    say i
}
```

Iterating strings is not supported in v1; index with `s[i]` (a `char`)
under a bounds check, or use a pointer in `unsafe`.

## switch

For integers, `char`, and enums. No fallthrough: a case body ends at
the next `case`, `default`, or `}`. Multiple values per case are
comma-separated. An unmatched value falls through the whole statement
silently, so include a `default:` when that matters.

```core
switch code {
    case 200, 201: say "ok"
    case 404:      say "missing"
    default:       say "other"
}
```

## match

`match` is an expression. It handles enums with payloads, which
`switch` cannot, and literal patterns for integers, `char`, `bool`, and
pointers.

```core
enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Unit
}

func area(s: Shape) -> f64 {
    return match s {
        Circle(r)  { 3.0 * r * r }
        Rect(w, h) { w * h }
        Unit       { 0.0 }
    }
}
```

An enum match must be exhaustive: every variant covered, or a wildcard
`_` arm present. Payload bindings are immutable locals. Each arm is a
block; the match's value is the block's last expression, and the value
types of all arms must agree.

Literal patterns on non-enum values:

```core
desc = match n {
    0 { "zero" }
    1 { "one" }
    _ { "many" }
}
```

The `Option` and `Result` enums from the prelude are the standard way
to express "might not be there":

```core
o: Option<i32> = Option<i32>.Some(3)
match o {
    Some(v) { say v }
    None    { say "missing" }
}
```

A `string` scrutinee is not supported; compare with `==` or `str_eq`
before the match.
