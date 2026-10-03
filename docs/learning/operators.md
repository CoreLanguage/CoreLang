# Operators

All operators, what they apply to, and — most importantly — how tightly they bind.

## Arithmetic

| Op | Meaning | Works on |
|---|---|---|
| `+` | add | integers, floats, SIMD (lane-wise) |
| `-` | subtract | integers, floats |
| `*` | multiply | integers, floats |
| `/` | divide | integers (truncating), floats |
| `%` | remainder | integers |

Integer division truncates toward zero: `10 / 3` is `3`. There is no `**` — use `math.pow` for floats.

## Bitwise

| Op | Meaning |
|---|---|
| `&` | AND |
| `\|` | OR |
| `^` | XOR |
| `~` | NOT (unary) |
| `<<` | shift left |
| `>>` | shift right |

```core
say 6 & 3      // 2
say 6 | 3      // 7
say 6 ^ 3      // 5
say ~0 as i32  // -1
say 2 << 4     // 32
say 255 >> 4   // 15
```

## Comparison

`==  !=  <  <=  >  >=` — yield `bool`. They compare integers, floats, chars, bools, pointers, and enums; for strings they're lexicographic byte comparisons. **No truthiness anywhere**: `if n { }` is an error, write `if n != 0 { }`.

## Logical

`&&` and `||` short-circuit (the right side is not evaluated when the result is decided). The words `and` / `or` are accepted as synonyms. Unary `!` negates. Both operands must be `bool`.

```core
say true && !false    // true
say false || true     // true
```

## Assignment and compound assignment

`= += -= *= /= %= &= |= ^= <<= >>=`. Assignment is a statement; compound forms expand the obvious way. The target must be a **mutable** binding (see [variables.md](variables.md)).

## Unary

| Op | Meaning |
|---|---|
| `-` | negate (numbers) |
| `!` | logical not (bool) |
| `&` | address-of — see [pointers.md](pointers.md) |
| `*` | dereference — see [pointers.md](pointers.md) |

## Casts: `as`

`expr as Type` converts explicitly. Categories:

| Conversion | Where allowed |
|---|---|
| int ↔ int, int ↔ float, bool ↔ int, enum ↔ int | always |
| string → `ptr<char>` (FFI) | implicit in call position |
| `ptr<T>` ↔ `ptr<U>` (different pointees), int ↔ ptr | `unsafe` only |
| class downcast, interface unwrap | `unsafe` only |

```core
say 65 as char        // A
say 3.9 as i32        // 3 (truncates toward zero)
say 7 as f64 / 2.0    // 3.5
say true as i32       // 1
```

## Precedence table

From tightest to loosest:

| Level | Operators |
|---|---|
| 1 (highest) | `()` call, `[]` index, `.`, method calls |
| 2 | `as` cast |
| 3 | unary `-` `!` `~` `*` (deref) `&` (address-of) |
| 4 | `*` `/` `%` |
| 5 | `+` `-` |
| 6 | `<<` `>>` |
| 7 | `<` `<=` `>` `>=` |
| 8 | `==` `!=` |
| 9 | `&` |
| 10 | `^` |
| 11 | `\|` |
| 12 | `&&` |
| 13 | `\|\|` |
| 14 (lowest) | assignments |

Two consequences worth memorizing:

1. **`as` binds tighter than unary `&`**: `&x as ptr<i32>` parses as `&(x as ptr<i32>)` — almost never what you meant. Write `(&x) as ptr<i32>`.
2. **Comparison of a comparison to a bool is legal** (`1 < 2 == true`) because `==` is looser than `<`, but it's poor style — drop the `== true`.

## Complete working example

```core
// operators.cr - bit tricks and casts at work
func is_pow2(n: i32) -> bool {
    return n > 0 && (n & (n - 1)) == 0
}

func average(a: i32, b: i32) -> i32 {
    return (a & b) + ((a ^ b) >> 1)   // no-overflow average trick
}

func main() {
    say 1 + 2 * 3          // 7
    say (1 + 2) * 3        // 9
    say 10 / 3             // 3
    say 10 % 3             // 1
    say 2 << 4             // 32
    say 255 >> 4           // 15
    say 6 & 3              // 2
    say 6 | 3              // 7
    say 6 ^ 3              // 5
    say ~0 as i32          // -1
    say true && !false     // true
    say false || true      // true

    mut x = 10
    x += 5
    x <<= 1
    say x                  // 30

    say is_pow2(64)        // true
    say is_pow2(63)        // false
    say average(3, 8)      // 5
    say 65 as char         // A
    say 3.9 as i32         // 3
    say 7 as f64 / 2.0     // 3.5
    say true as i32        // 1
}
```

## Common mistakes

- **`&x as ptr<T>` without parens** — `as` binds tighter; write `(&x) as ptr<T>`.
- **Truthiness.** `if len(s) { }` is rejected; write `if len(s) > 0 { }`.
- **Mixed-width arithmetic.** `i32 + i64` is an error — cast explicitly.
- **`/` on ints expecting floats.** Cast first: `x as f64 / y as f64`.
- **Using `%` on floats.** It's integer-only; use `math` functions for floats.

## Performance notes

- `x / 2` on signed ints is not automatically a shift (rounding differs); LLVM turns exact power-of-two division of non-negative values into shifts anyway — write what's clearest.
- `&` masks are the cheapest bounds/flag checks you can write; LLVM recognizes the `n & (n-1)` pop-two's-complement patterns.
- Short-circuit `&&`/`||` compile to branches — cheap when the left side usually decides, costly when it prevents vectorization of a loop.

## When to use / not use

- Prefer comparisons + explicit casts: Core's lack of implicit conversion is a feature; lean into it.
- Avoid clever bitwise arithmetic in non-hot code — the readable version optimizes just as well.
- Don't use `as` to silence every type error; a cast that changes width or signedness deserves a comment.

Next: [Control flow](control-flow.md).
