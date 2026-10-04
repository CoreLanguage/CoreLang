# Types

Core is statically typed with no implicit numeric conversions. If two
types differ, you write the cast. This page lists every type and the
conversion rules. Sizes below are for x86-64 System V; other 64-bit
targets match (pointer and `usize` are always 8 bytes).

## Primitive types

| Type | Size (bytes) | Notes |
|------|-------------:|-------|
| `void` | 0 | no value; functions that return nothing |
| `never` | 0 | uninhabited; for functions that never return (`panic`) |
| `bool` | 1 | `true` / `false` only; no truthiness |
| `char` | 1 | one byte, integer-like |
| `i8 i16 i32 i64 i128` | 1,2,4,8,16 | signed integers; two's complement, overflow wraps |
| `u8 u16 u32 u64 u128` | 1,2,4,8,16 | unsigned integers |
| `f32` `f64` | 4, 8 | IEEE floats |
| `usize` | 8 | unsigned; sizes, lengths |
| `isize` | 8 | signed; pointer distances |
| `string` | 16 | a view `{data ptr, len}`; does not own its bytes |
| `f32x4 f64x2 i32x4 i64x2 i8x16 i16x8 u8x16 u16x8 u32x4 u64x2` | 16 | SIMD vectors, lane-wise arithmetic |

`sizeof(T)` and `alignof(T)` are builtins returning `usize` with the
target's true values.

## Literals

- Integer literals: decimal, `0x`, `0b`, `0o`, with `_` separators
  (`1_000_000`). A literal converts to any integer type it fits:
  `i8 x = 100` is fine, `i8 x = 300` is an error.
- With no constraining context, an integer literal is `i32` when it
  fits, else `i64`, else `u64`.
- Float literals are `f64` by default; an integer literal also converts
  to a float type (`f32 x = 1`).
- Char literals (`'a'`, escapes `\n \t \r \0 \\ \" \' \xHH`) hold one
  byte. String literals are static and live for the whole program.

## Named types

| Construct | Example | Notes |
|-----------|---------|-------|
| array | `[i32; 4]` | fixed size, value type, `[1, 2, 3]` or repeat `[0; 10]`; `len(a)` is compile-time |
| pointer | `ptr<T>` | raw, non-owning, 8 bytes; `null` valid |
| string | `string` | 16-byte view; indexing yields `char` with a bounds check |
| struct | `Point { x: 1, y: 2 }` | value type, C layout, see [structs-and-enums.md](structs-and-enums.md) |
| class | `Dog { }` | see [oop.md](oop.md) |
| enum | `Option<i32>.Some(5)` | simple enums are i32-backed; data-carrying ones are tagged unions |
| function | `func(i32) -> i32` | closure pair `{fn ptr, env ptr}`, 16 bytes |
| vector | `f32x4`, `i32x4`, ... | `+ - * /` are lane-wise |

Arrays are values: assignment and argument passing copy the bytes.
`[T; N]` indexing is bounds-checked at runtime outside `unsafe`.

Strings deserve one paragraph: a `string` is a 16-byte view of bytes it
does not own. Literals point at static memory. `+` concatenates and
allocates a fresh buffer (the only hidden allocation in base Core).
Indexing `s[i]` yields a `char` and is bounds-checked against `len(s)`.

## No implicit conversions

There are none, not even `i32` to `i64`. Mixed-width arithmetic and
mixed int/float arithmetic are compile errors; write the cast:

```core
mut a: i32 = 1
mut b: i64 = 2
say a + (b as i32)   // or: (a as i64) + b
```

An integer literal converts to any in-range integer type, so
`mut x: i8 = 100` is fine and `mut x: i8 = 300` is an error. Folded
expressions keep their computed type: `mut y: i8 = 4 * 10` is an
error (`4 * 10` is an `i32`); write `mut y: i8 = 40` or cast.

The implicit conversions that do exist do so because the
representations are identical or the conversion is a safe widening:

| From | To | Where |
|------|----|-------|
| `usize` | `u64` | anywhere (same 8 bytes) |
| `isize` | `i64` | anywhere |
| any `ptr<T>` | `ptr<void>` | argument position |
| `ptr<[T; N]>` | `ptr<T>` | argument position (array decay) |
| `string` | `ptr<char>` | argument position (FFI) |
| class | its base class | argument position |
| class | an interface it implements | argument position |
| `null` | any pointer type | anywhere |
| `never` | anything | anywhere |

## Casts (`as`)

Always allowed: int to int, int to float, float to float, bool to int,
enum to int, identity casts, `never` to anything.

Allowed only inside `unsafe`:

- `ptr<T>` to `ptr<U>` with different pointees
- int to pointer and pointer to int
- class downcasts (no runtime check)
- interface to class unwraps (no runtime check)

Semantics: int to int truncates or sign/zero-extends based on the
source's signedness; float to int truncates toward zero (undefined if
the value does not fit); int to float rounds; float to float converts.

## Default values

A declaration without an initializer gets the type's zero value:

| Type | Default |
|------|---------|
| integers, `char` | `0` |
| floats | `0.0` |
| `bool` | `false` |
| `string` | `""` (null, zero-length view) |
| `ptr<T>` | `null` |
| enum | tag 0, zeroed payload |
| vector | all lanes zero |
| struct / class / array | every field zeroed |

Heap memory from `alloc<T>()` is NOT zeroed; use `alloc_zeroed<T>()`
when you need that.
