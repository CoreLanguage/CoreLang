# Types

Core's type system is small, static, and honest: every value has a fixed size known at compile time, and nothing converts behind your back.

## Primitive types

| Type | Size (x86-64) | Range / meaning |
|---|---|---|
| `void` | — | function returns nothing |
| `never` | — | function never returns (diverges) |
| `bool` | 1 byte | `true` / `false` |
| `char` | 1 byte | a single byte, written `'A'` |
| `string` | 16 bytes | a *view*: `{ptr, len}` over bytes — see [strings.md](strings.md) |
| `i8` `i16` `i32` `i64` `i128` | 1, 2, 4, 8, 16 | signed integers |
| `u8` `u16` `u32` `u64` `u128` | 1, 2, 4, 8, 16 | unsigned integers |
| `f32` `f64` | 4, 8 | IEEE-754 floats |
| `usize` `isize` | 8 | pointer-sized unsigned/signed; `usize` is what `len()` returns |
| `f32x4` `i32x4` ... | 16 | SIMD vectors — see the `simd` module |

Verify sizes yourself with the compile-time builtins `sizeof(T)` and `alignof(T)`.

```core
func main() {
    say sizeof(i32)      // 4
    say sizeof(u8)       // 1
    say sizeof(f64)      // 8
    say sizeof(string)   // 16: a {ptr, len} pair
    say sizeof([i64; 4]) // 32
    say alignof(i64)     // 8
}
```

There is no `int`/`long`/`short` ladder to memorize — pick the width you mean.

## Integer literals

Integer literals are decimal by default. Hexadecimal, octal, and binary work with the usual prefixes where the lexer accepts them (`0x...`).

An integer literal has no fixed type of its own: **it fits into whatever integer type the context asks for**, as long as the value is in range:

```core
a: u8 = 255        // fits
b: i8 = -100       // fits
c: i64 = 9000000000 // would overflow i32, fine for i64
d: u64 = 18446744073709551615
```

Constant-foldable expressions convert too, so `4 * 10` can initialize a `u8`. A literal (or foldable expression) that doesn't fit the context is a compile error — no silent truncation.

Float literals similarly fit `f32` or `f64` in context; exponent forms work: `1.0e3` is `1000`.

## Floating point

`f64` is the default and what `math.sqrt` etc. take. Division by zero and overflow follow IEEE-754 semantics. Printing uses the runtime's shortest sensible format:

```core
func main() {
    f: f32 = 1.5
    d: f64 = 3.25
    say f * 2.0 as f32   // 3
    say d * 2.0          // 6.5
    say 7 / 2            // 3   (integer division!)
    say 7.0 / 2.0        // 3.5
    say -3.5             // -3.5
    say 1.0e3            // 1000
}
```

## bool

`bool` is `true` or `false`, one byte in memory. Conditions in `if`/`while`/`for` **must** be `bool` — there is no truthiness. Convert explicitly with comparisons or `as`:

```core
n = 5
if n > 0 { say "positive" }
b = n as bool      // explicit int->bool cast
say b && !false
```

## char

`char` is a single **byte** (8-bit), written `'A'`. It participates in arithmetic as a number, and `as` converts freely:

```core
c: char = 'A'
say c as i32        // 65
say 'a' + 1 as char // b
```

Core strings are byte sequences, not Unicode code points — `len("héllo")` counts bytes (`6`), not letters.

## string

`string` is a 16-byte value type: a pointer + a length — a **view** over bytes, not an owning buffer. String literals are static (live for the whole program). Concatenation with `+` allocates a new buffer via the runtime. Full treatment in [strings.md](strings.md).

## usize / isize

Pointer-sized integers (64 bits on x86-64). `usize` is unsigned — the natural type for sizes and counts, and what `len()` returns. Mixing `usize` with fixed-width integers needs an explicit `as`:

```core
s = "hello"
u: usize = len(s)
i: i64 = u as i64
```

## void and never

- `void` — a function returns nothing. `void` has no values you can hold.
- `never` — a function that never returns normally (it always diverges: `panic`, infinite loop, `process.exit`). Calls to `never` functions make following code unreachable, which the compiler exploits:

```core
func fatal(msg: string) -> never {
    panic(msg)
}

func get(index: i32) -> i32 {
    if index < 0 { fatal("negative index") }
    return 1   // compiler knows the `if` never falls through
}
```

## No implicit conversions

Core converts **nothing** implicitly between numeric types — not even widening (`i32` → `i64`) — because silent conversions are where bugs hide. Use `as`:

```core
small: i32 = 100
big: i64 = small as i64       // explicit widening
approx: i32 = 3 as i32        // f64->i32: say (3.9 as i32)
f = 3.9
trunc = f as i32              // truncates toward zero: 3
```

`as` is always allowed between integer types, between int and float, `bool` ↔ int, and enum ↔ int. Pointer casts and class downcasts require `unsafe` (see [unsafe.md](unsafe.md)).

## Complete working example

```core
// types.cr
func classify(c: char) -> string {
    if c >= '0' && c <= '9' { return "digit" }
    if c >= 'a' && c <= 'z' { return "letter" }
    return "other"
}

func main() {
    // every integer width
    a: i8 = 127
    b: u8 = 255
    c: i16 = 32767
    d: u16 = 65535
    e: i32 = 2147483647
    f: u32 = 4294967295
    g: i64 = 9223372036854775807
    h: u64 = 18446744073709551615

    // floats
    fl: f32 = 1.5
    dbl: f64 = 3.25

    // other primitives
    ch: char = 'A'
    s: string = "core"
    bo: bool = true
    us: usize = len("abcd")

    say (a as i32) + (b as i32)   // 382
    say f as u64                  // 4294967295
    say fl * 2.0 as f32           // 3
    say dbl * 2.0                 // 6.5
    say ch as i32                 // 65
    say 'a' + 1 as char           // b
    say s + "!"                   // core!
    say bo                        // true
    say 1.0e3                     // 1000
    say us                        // 4
    say classify('7')             // digit
    say sizeof(string)            // 16
}
```

## Common mistakes

- **Assuming implicit widening.** `i32val + i64val` is a compile error — cast one side explicitly.
- **Integer division surprise.** `7 / 2` is `3`. You asked for integer division and you got it.
- **Reading `len()` as `i32`.** It returns `usize`; cast when you need a signed type.
- **Treating strings as arrays of characters for iteration.** `for` over a string isn't supported in v0.1 — loop over indices and index with `s[i]`.
- **Thinking `string` owns its bytes.** It's a view; see [strings.md](strings.md) for lifetime rules.
- **`sizeof` of a *variable*** — `sizeof` takes a type: `sizeof(i32)`, not `sizeof(x)`.

## Performance notes

- Use the smallest width that's correct, but don't obsess: on x86-64, `i32` arithmetic is usually the fastest; `i8`/`i16` can cost extra sign-extension.
- `f64` is native; `f32` trades precision for half the memory and double SIMD lane count.
- `u128`/`i128` compile to multi-instruction sequences — avoid in hot loops.

## When to use what

- Counters, indices, general-purpose integers: `i32` (or `usize` for sizes).
- Bytes of raw data: `u8`.
- File sizes, capacities, pointer math: `usize`.
- Money, physics, averages: `f64` unless you need `f32` for SIMD width.
- State machines, tagged unions: `enum` (see [enums.md](enums.md)).

Next: [Operators](operators.md).
