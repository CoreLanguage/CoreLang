# Syntax

Core's syntax is small and C-like: brace blocks, newline-terminated
statements, `name: type` annotations. This page is the whole grammar in
one place. The exact, normative version is
[SPEC.md](../language/SPEC.md) §2-§4.

## Files, comments, statements

- Source files use the `.cr` extension.
- `//` line comments; `/* ... */` block comments (they do not nest).
- Statements are newline-terminated. `;` is an optional separator.
  An operator at the start of a line does not continue the previous
  expression.

## Identifiers and keywords

An identifier is `[A-Za-z_][A-Za-z0-9_]*`. `_` alone is the wildcard.
Keywords:

```
func struct class interface trait enum import extern
pub private static mut const abstract virtual override tls
if else while loop for in break continue return switch case default match
as unsafe true false null self super and or sizeof alignof
void never bool char string  i8..i128 u8..u128 f32 f64 usize isize
f32x4 f64x2 i32x4 i64x2 i8x16 i16x8 u8x16 u16x8 u32x4 u64x2
```

`private` is reserved but has no effect (class members are already
private by default). `and`/`or` are synonyms for `&&`/`||`.

## Literals

```core
42            // integer: i32 by default, fits context otherwise
0xFF  0b1010  0o17  1_000_000
3.14  1e10    // float: f64 by default
'a'  '\n'  '\x41'   // char: one byte
"text"        // string, NUL-terminated, statically allocated
true  false  null
```

See [types.md](types.md) for the literal typing rules.

## Declarations

```core
// functions and methods
func add(a: i32, b: i32) -> i32 { return a + b }
pub func visible_outside() { }

// generic functions
func id<T>(x: T) -> T { return x }

// extern (C ABI) functions
extern func puts(s: ptr<char>) -> i32
@link_name("my_c_symbol") extern func thing(x: i32) -> i32

// structs, classes, interfaces, traits, enums
struct Point { x: i32, y: i32 }
class Animal : Movable { pub virtual func move_to(x: i32, y: i32) { } }
interface Shape { func area() -> f64 }
trait Named { func name() -> string { return "unnamed" } }
enum Option<T> { Some(T), None }

// globals and constants
count: i32 = 0
mut done = false            // Go-like name-first form also works
tls mut counter: i64 = 0    // thread-local
const MAX: i32 = 100

// imports
import math
import utils.helper as h    // binds `h`
```

## Statements

```core
// variables: x = value declares (immutable); mut makes it assignable
x = 10
mut y: i64 = 20
z: i32            // declared without initializer: zero value

// if - condition must be bool, braces required
if x > 0 { say "pos" } else if x == 0 { say "zero" } else { say "neg" }

// loops
while x > 0 { x -= 1 }
loop { break }
for i in 0..10 { }          // exclusive range
for i in 0..=10 { }         // inclusive
for item in array { }       // arrays; each element is copied
for mut v in array { v = 0 }  // `mut` makes the copy assignable
for i = 0; i < n; i += 1 { }  // C-style, all three parts optional

// switch - no fallthrough, integer/char/enum scrutinee
switch x {
    case 1, 2: say "small"
    case 3: say "three"
    default: say "other"
}

// break / continue target the innermost loop
// return [expr]

// unsafe block
unsafe { p2 = int_to_ptr(0xB8000) }

// const inside a block
const LIMIT: i32 = 64

// say expr - print sugar; takes the whole expression
say x + y
```

## Expressions

```core
n = 2 + 3 * 4
flag = n > 8 && n < 20
big = n as i64
s = "a" + "b"        // string concat, allocates
p = &x               // address of an lvalue
v = *p               // dereference
a = arr[i]           // index (bounds-checked)
f = pt.x             // field / method access
c = f32x4_splat(1.0) // vectors: see stdlib.md
fn = func(a: i32) -> i32 { return a * 2 }   // lambda, captures by value
size = sizeof(Point)
align = alignof(i64)
m = match opt { Some(v) { v }, None { 0 } } // match is an expression
```

## Operator precedence

Higher binds tighter. All binary operators are left-associative.

| Prec | Operators | Kind |
|-----:|-----------|------|
| 10 | `()` `[]` `.` `as` (postfix); `- ! ~ * &` (prefix) | call, index, member, cast, unary |
| 10 | `*` `/` `%` | multiplicative |
| 9 | `+` `-` | additive |
| 8 | `<<` `>>` | shift |
| 7 | `&` | bitwise AND |
| 6 | `^` | bitwise XOR |
| 5 | `\|` | bitwise OR |
| 4 | `<` `<=` `>` `>=` | comparison |
| 3 | `==` `!=` | equality |
| 2 | `&&` `and` | logical AND (short-circuit) |
| 1 | `\|\|` `or` | logical OR (short-circuit) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | assignment |

Consequences worth knowing:

- `as` is postfix and binds tighter than every binary operator:
  `a + b as i64` is `a + (b as i64)`. Parenthesize casts on whole
  expressions.
- Unary operators sit at multiplicative precedence and nest
  right-to-left: `*&x`, `- -x`.

## Call sugar

`say expr` is statement-level sugar for `say(expr)`, and it grabs the
whole expression: `say x + y` prints `x + y`.
