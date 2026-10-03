# Core Language Specification

**Version: 0.1.0 (language v1) — normative**

This document is the authoritative specification of the Core programming
language as implemented by the `core` compiler (C++17 + LLVM 18). It
describes what the compiler does today, not aspirations. Companion
documents: [memory-model.md](memory-model.md), [abi.md](abi.md),
[concurrency.md](concurrency.md).

In this specification **is** / **is not**, **shall** / **shall not**, and
**error** are normative. "Error" means compilation fails with a diagnostic
unless the error occurs during execution (e.g. a bounds-check panic).

---

## 1. Compilation model

A Core program is a set of source files with the `.cr` extension. The
compiler:

1. lexes and parses every file into one AST per file (a *module*),
2. resolves names, checks types, and monomorphizes generics,
3. generates LLVM IR for the whole program,
4. optimizes it (level selected by `-O0`…`-O3`, `-Os`),
5. emits a native object file and invokes the system linker (`cc`).

The result is **one native executable**. Every imported `.cr` file is
compiled into that executable (whole-program compilation); binaries do not
need the sources, the standard library, or any package at runtime.

There is no separate compilation, no object caching, and no dynamic
loading of Core code.

### 1.1 Entry point

- The entry file shall define `func main()`.
- `main` shall take no parameters and shall return `void` or `i32` — error
  otherwise. A `void` main returns exit status 0.
- Duplicate `main` definitions in the entry file are an error.

### 1.2 Commands and flags

```
core init [name]            scaffold a project (refuses to overwrite core.toml)
core compile <out> <entry.cr>   compile a standalone file (or module tree)
core build | run | test | check project-based build/run/test/typecheck
core install|remove|update|list <pkg>   package management
core emit-ir <f> | emit-asm <f>     print LLVM IR / assembly
core version
```

`compile` flags: `-O0` (default) `-O1` `-O2` `-O3` `-Os`, `--debug` (DWARF
debug info; GDB breakpoints resolve to `.cr` lines), `--target=<triple>`
(`x86_64`, `aarch64`, `riscv64`; emits a relocatable object — linking
requires a target toolchain), `--freestanding` (no runtime, no libc;
`--entry=<name>` / `@link_name` selects the entry symbol; link with `ld
-nostdlib`), `--emit-object`, `--link=<lib>`, `--link-arg=<arg>`,
`--lib-path=<dir>`.

Compilation **refuses to overwrite** an existing output file or its
intermediate object unless `--force` is passed; `core build`/`run`/
`test` overwrite their own artifacts freely (they are repeatable).

`core test` compiles each `tests/*.cr` file as its own entry (each test
file defines `func main()` and returns nonzero on failure through
`assert`), runs it, and reports `N/M test files passed`.

---

## 2. Lexical structure

### 2.1 Source files and comments

- Source files are byte streams; files use the `.cr` extension.
- `//` starts a line comment; `/* ... */` is a block comment (may span
  lines; block comments do not nest). An unterminated block comment is an
  error.

### 2.2 Statements and newlines

- Statements are newline-terminated. A newline is significant: an operator
  at the start of a new line does **not** continue the previous expression.
- `;` is an optional statement separator and may appear anywhere a newline
  would.

### 2.3 Keywords

The following are reserved and **cannot** be used as identifiers, type
names, field names, or function names:

```
func struct class interface trait enum import extern
pub private static mut const abstract virtual override tls
if else while loop for in break continue return switch case default match
as unsafe true false null self super and or sizeof alignof
void never bool char string
i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64 usize isize
f32x4 f64x2 i32x4 i64x2 i8x16 i16x8 u8x16 u16x8 u32x4 u64x2
```

Note: `private` is reserved but has no effect in v1 (the default for class
members is already private). `and`/`or` are synonyms for `&&`/`||`.

### 2.4 Identifiers

An identifier is `[A-Za-z_][A-Za-z0-9_]*`. An identifier consisting of a
single `_` is the wildcard (§8.6).

### 2.5 Literals

- **Integer literals**: decimal, `0x`/`0X` hex, `0b`/`0B` binary, `0o`/`0O`
  octal. `_` may separate digits (`1_000_000`).
- Untyped integer literal typing: a literal fits its context (§5.2);
  with no constraining context it is `i32` when the value is ≤
  `0x7FFFFFFF`, `i64` when ≤ `0x7FFFFFFFFFFFFFFF`, otherwise `u64`.
  Literals whose digit text is longer than 16 digits are typed `u128`.
- **Float literals**: digits with a fractional part (`1.5`) and/or an
  exponent (`1e10`, `1.5e-3`). Untyped float literals are `f64`.
- **Char literals**: `'a'`, escapes `\n \t \r \0 \\ \" \'` and `\xHH`.
  A char is one byte; a char literal holds a single byte value.
- **String literals**: `"text"` with the same escapes. A string literal is
  statically allocated (NUL-terminated for C interop) and lives for the
  whole program. A raw newline inside a string literal is an error.
- `true`, `false`, `null` are literal keywords.
- Other escape sequences are errors; `\x` requires exactly two hex digits.

### 2.6 Operators and punctuators

Longest-match tokenization. Multi-character tokens:

```
<<= >>= ...  == != <= >= && || << >> += -= *= /= %= &= |= ^= .. -> ..=
```

Single-character: `+ - * / % & | ^ ~ ! < > = ( ) [ ] { } , ; : . @`.

---

## 3. Grammar (EBNF, matching the real parser)

```
file            = { attribute | declaration | ";" | newline } ;
attribute       = "@" ident [ "(" string-literal ")" ] newline ;
declaration     = [mods] ( func-decl | struct-decl | class-decl
                | interface-decl | trait-decl | enum-decl
                | import-decl | extern-decl | global-decl | const-decl ) ;
mods            = { "pub" | "static" | "virtual" | "override"
                | "abstract" | "unsafe" | "mut" | "tls" } ;

func-decl       = "func" ident [generic-params] "(" [param-list] ")"
                  ["->" type] ( block | ";" | declaration-boundary ) ;
generic-params  = "<" ident { "," ident } ">" ;
param-list      = param { "," param } [ "..." ] ;
param           = ident ":" type [ "=" expr ] ;
extern-decl     = "extern" func-decl ;        (* body-less prototype *)
import-decl     = "import" ident { "." ident } [ "as" ident ] ;
global-decl     = [ "tls" ] [ "mut" ] ident ":" type [ "=" expr ]
                | [ "mut" ] ident "=" expr ;  (* Go-like; decl-or-assign *)
const-decl      = "const" ident ":" type "=" expr ;

struct-decl     = [attribute "@packed"] "struct" ident [generic-params]
                  "{" { struct-member } "}" ;
struct-member   = { "pub" | "static" } ( field | "func" method ) ;
field           = ident ":" type [ "=" expr ] ;

class-decl      = "class" ident [generic-params] [ ":" base { "," iface } ]
                  "{" { class-member } "}" ;
class-member    = { "pub"|"static"|"virtual"|"override"|"abstract"|"unsafe" }
                  ( field | "func" method ) ;

interface-decl  = ("interface" | "trait") ident [generic-params] "{"
                  { "func" ( proto | method-with-body ) } "}" ;
enum-decl       = "enum" ident [generic-params] "{"
                  variant { ["," | newline] variant } "}" ;
variant         = ident [ "(" type { "," type } ")" ] ;

block           = "{" { statement } "}" ;
statement       = "return" [expr] | "break" | "continue"
                | "if" expr block [ "else" (if-stmt | block) ]
                | "while" expr block | "loop" block
                | for-in | for-c | switch-stmt
                | "unsafe" block
                | const-decl | let-or-expr ;
for-in          = "for" ["mut"] ident "in" expr [range-tail] block ;
range-tail      = (".." | "..=") expr ;      (* forms `for i in a..b` *)
for-c           = "for" [let-or-expr] ";" [expr] ";" [let-or-expr] block ;
let-or-expr     = ["mut"] ident ":" type ["=" expr]      (* declaration *)
                | ["mut"] ident "=" expr                 (* decl or assign *)
                | "say" expr                             (* print sugar *)
                | expr [assign-op expr] ;
switch-stmt     = "switch" expr "{" { "case" expr {"," expr} ":" {stmt}
                      | "default" ":" {stmt} } "}" ;

expr            = assign-expr ;
assign-expr     = binary [assign-op binary] ;
binary          = unary { binop unary } ;   (* precedence §4.1 *)
unary           = ("-" | "!" | "~" | "*" | "&") unary | postfix ;
postfix         = primary { "(" [args] ")" | "[" expr "]" | "." ident
                          | "as" type } ;
primary         = literal | ident ["<" type-list ">"] ["." ident]
                | "(" expr ")" | array-literal | struct-literal
                | "func" "(" [param-list] ")" ["->" type] block  (* lambda *)
                | "unsafe" block | "match" match-expr
                | "sizeof" "(" type ")" | "alignof" "(" type ")"
                | "self" | "null" | "true" | "false" ;
array-literal   = "[" [ (expr {"," expr}) | (expr ";" expr) ] "]" ;
struct-literal  = ident [generic-args] "{"
                  [ ident ":" expr { "," ident ":" expr } [","] ] "}" ;
match-expr      = expr "{" { pattern block } "}" ;
pattern         = "_" | ident ["(" ("_" | ident) {"," ("_"|ident)} ")"]
                | literal-expr ;
```

Notes that are part of the grammar:

- `import a.b` binds the name `b` (the last component), unless `as`
  renames it.
- The type `ptr<T>` parses as a named type with one type argument.
- Function types are written `func(i32, i32) -> i32`.
- A `func` declaration whose body is replaced by `;` or ends at a
  declaration boundary is a prototype (interfaces, traits, `extern`).
- Call arguments, struct literals, and array literals allow trailing
  commas.
- Closing generic type arguments tolerate the `>>` sequence: in
  `Pair<Vec<i32>, i64>` each `>` is consumed separately.
- `say expr` is statement-level sugar for `say(expr)`; it grabs the whole
  expression so `say x + y` prints `(x + y)`.

---

## 4. Expressions and operators

### 4.1 Operator precedence (exact, from `Parser::binPrec`)

Higher number binds tighter. All binary operators are left-associative.

| Prec | Operators                      | Kind                        |
|-----:|--------------------------------|-----------------------------|
| 10   | postfix: `()` `[]` `.` `as`    | call / index / member / cast |
| 10   | prefix: `-` `!` `~` `*` `&`    | unary                       |
| 10   | `*` `/` `%`                    | multiplicative              |
| 9    | `+` `-`                        | additive                    |
| 8    | `<<` `>>`                      | shift                       |
| 7    | `&`                            | bitwise AND                 |
| 6    | `^`                            | bitwise XOR                 |
| 5    | `\|`                           | bitwise OR                  |
| 4    | `<` `<=` `>` `>=`              | comparison                  |
| 3    | `==` `!=`                      | equality                    |
| 2    | `&&` `and`                     | logical AND (short-circuit) |
| 1    | `\|\|` `or`                    | logical OR (short-circuit)  |
| 0    | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | assignment |

`as` is a postfix operator and binds tighter than every binary operator:
`a + b as i64` parses as `a + (b as i64)`. Unary operators bind at the
same tightness as `* / %` and nest right-to-left (`*&x`, `- -x`).

An operator token that starts a line does not continue the previous
expression (newline-terminated statements, §2.2).

### 4.2 Semantics

- **Arithmetic** `+ - * / %`: defined for integer and float types with
  both operands of the same type. Integer overflow wraps (two's
  complement). Integer division or modulo by zero traps (SIGFPE);
  float division by zero yields IEEE inf/NaN.
- **Shifts** `<<` `>>`: the right operand shall be an integer (any
  width; it is cast to the left operand's type). `>>` is arithmetic for
  signed types, logical for unsigned. A shift count ≥ the bit width is
  undefined (avoid it).
- **Bitwise** `& | ^ ~`: integers only.
- **Logical** `&& || and or`: `bool` operands only; short-circuit
  evaluation is guaranteed (the right side is not evaluated when the
  left side decides).
- **Comparison** `== != < <= > >=`: numeric operands shall have the same
  type (no implicit conversion). Result is `bool`. Signed types compare
  signed, unsigned compare unsigned. Pointers compare unsigned.
  `bool` supports `==`/`!=` only. Enums support `==`/`!=` on the same
  enum type.
- **Strings**: `+` concatenates (allocates a fresh buffer via the
  runtime); `== != < <= > >=` compare lexicographically byte-wise.
- **Pointers**: `p + i`, `i + p`, `p - i` scale by the pointee size
  (`ptr<void>` scales by 1). `p - q` yields `isize`, the element
  distance. `p[i]` is `*(p + i)`. All comparison operators work on
  pointers of the same type.
- **Vectors**: `+ - * /` on equal vector types are lane-wise (§simd in
  the stdlib overview).
- **Assignment** `=` evaluates the right side first, then stores.
  Compound assignments (`+=` …) read-modify-write. The left side shall
  be an lvalue — a variable, `*p`, a field `x.f`, or `a[i]` — error
  otherwise. Assignment to an immutable variable/global or a `const` is
  an error. Assignment is an expression; its value is the stored value.
- **`as` casts**:
  - Always allowed: int↔int, int↔float, float↔float, bool↔int,
    enum↔int, identity casts, and `never` → anything.
  - Allowed only inside `unsafe`: `ptr<T>` ↔ `ptr<U>` with different
    pointees, int↔pointer (both directions), class downcasts (no runtime
    check exists), interface→class unwraps (no runtime check).
  - Class upcasts (`Derived as Base`) and class → implemented-interface
    (`obj as Interface`) are safe; the interface conversion is implicit
    in argument position.
  - `string` → `ptr<char>` is implicit in argument position (FFI):
    the view's data pointer is passed.
- Cast semantics: int→int truncates or sign/zero-extends (source
  signedness decides); float→int truncates toward zero; int→float is
  exact or rounds; float→float converts.

### 4.3 Lvalues

An expression is an lvalue exactly when it is a local or global
variable, a dereference `*p`, a field access `x.f`, or an index
expression `a[i]`. Taking the address of a temporary is an error.

---

## 5. Types

### 5.1 Primitive types (x86-64 System V sizes)

| Type      | Size (bytes) | Align | LLVM type      | Notes                        |
|-----------|-------------:|------:|----------------|------------------------------|
| `void`    | 0            | —     | void           | no value                     |
| `never`   | 0            | —     | void           | uninhabited (§8.5)           |
| `bool`    | 1            | 1     | `i1`           | `true`/`false` only          |
| `char`    | 1            | 1     | `i8`           | one byte; integer-like       |
| `i8…i128` | 1,2,4,8,16   | =size | `iN`           | signed                       |
| `u8…u128` | 1,2,4,8,16   | =size | `iN`           | unsigned                     |
| `usize`   | 8            | 8     | `i64`          | unsigned; sizes, `len`       |
| `isize`   | 8            | 8     | `i64`          | signed; pointer distances    |
| `string`  | 16           | 8     | `{ptr, i64}`   | view `{data, len}` (§5.5)    |
| `f32x4`   | 16           | 16    | `<4 x float>`  | SIMD vector                  |
| `f64x2`   | 16           | 16    | `<2 x double>` | SIMD vector                  |
| `i32x4`   | 16           | 16    | `<4 x i32>`    | SIMD vector                  |
| `i64x2`   | 16           | 16    | `<2 x i64>`    | SIMD vector                  |
| `i8x16`   | 16           | 16    | `<16 x i8>`    | SIMD vector                  |
| `i16x8`   | 16           | 16    | `<8 x i16>`    | SIMD vector                  |
| `u8x16`…`u64x2` | 16     | 16    | vector         | unsigned twins of the above  |

`sizeof(T)` and `alignof(T)` are compile-time builtins returning `usize`
with target-true values. For data-carrying enums, `alignof` reports the
alignment of the raw storage (1 byte) even though payload fields are
aligned within the value — see abi.md §5.

### 5.2 Type compatibility and literal fitting

- There are **no implicit numeric conversions** — not even `i32` → `i64`.
  Mixing widths, or float with integer, in one operation is an error;
  write `x as i64`. The sole exceptions, for identical representations:
  `usize` ↔ `u64` and `isize` ↔ `i64` convert implicitly in argument and
  assignment position.
- An **integer literal** converts to any integer type in range
  (`i8 x = 300` is an error: 300 does not fit in i8).
- A **float literal** converts to `f32` or `f64`; an integer literal also
  converts to a float type (`f32 x = 1`).
- A **constant-foldable expression** of integer literals (`4 * 10`,
  `MAX - 1`, char/bool constants) converts to any in-range integer type,
  e.g. as a call argument.
- `null` converts to any pointer type.
- In a binary operation, an untyped literal adopts the other operand's
  type when it fits ("literal refitting").
- Implicit conversions that exist:
  - any `ptr<T>` → `ptr<void>` (opaque pointer; no bits change);
  - `ptr<[T; N]>` → `ptr<T>` in argument position (array decay);
  - `string` → `ptr<char>` in argument position (FFI);
  - class upcasts and class → implemented interface;
  - `ptr<Derived>` → `ptr<Base>`;
  - `usize` ↔ `u64`, `isize` ↔ `i64` (identical representations).
- Array literals with a known target element type check each element
  against it. An array literal of (pointers to) classes widens to the
  common base class when one exists.
- A value of type `never` is assignable to every type.

### 5.3 Arrays

`[T; N]` is a fixed-size value type of exactly `N * sizeof(T)` bytes with
the element's alignment. `N` shall be a compile-time constant between 1
and 2³⁰ — error otherwise. Literals: `[1, 2, 3]` and the repeat form
`[0; 10]` (count shall be constant). `len(arr)` is a compile-time
constant `usize`. Indexing is bounds-checked outside `unsafe`
(memory-model.md §10). Arrays are values: assignment and argument
passing copy the bytes.

### 5.4 Pointers

`ptr<T>` is a raw, non-owning pointer (8 bytes, address space 0).
`ptr<void>` is the opaque pointer (LLVM `i8*`); it may hold any address
and scales by 1 in arithmetic. `&x` takes the address of an lvalue;
`*p` dereferences — allowed anywhere, with no `unsafe` required for
plain dereference. `null` is representable in every pointer type.
See memory-model.md for lifetime and safety rules.

### 5.5 Strings

`string` is a **value type**: a view `{data: ptr, len: usize}` (16
bytes). Strings do not own their bytes. Literals point at static memory
and live for the whole program. `+` concatenates and allocates a fresh
buffer via the runtime — the only hidden allocation in base Core.
Indexing `s[i]` yields `char` with a dynamic bounds check against
`len(s)`. `c_str(s)` exposes the data pointer; `str_from_c(p)` builds a
view over a NUL-terminated C string. Comparisons are byte-lexicographic.

### 5.6 Structs

```
struct Point {
    x: i32
    y: i32
    pub func sum() -> i32 { return self.x + self.y }
}
```

- Fields are laid out in declaration order with natural alignment
  (C-compatible layout; abi.md §3). `@packed` removes all padding
  (alignment 1).
- Struct members are **public by default**; `pub` is accepted for
  clarity.
- Methods dispatch statically. Instance methods receive an implicit
  `self: ptr<Point>`. `static func` methods are called `Point.make(...)`.
- A struct literal shall list every field that has no declared default
  (`Point { x: 1, y: 2 }`); fields with defaults
  (`retries: i32 = 3`) may be omitted and are then initialized from the
  default. Unlisted fields without defaults are an error.
- If the type has an `init` constructor, the literal may omit fields
  (they get their defaults or zero) and `init` runs after the listed
  fields are stored.
- Structs are values: copying copies the bytes.

### 5.7 Classes

```
class Animal {
    name: string
    pub func init(name: string) { self.name = name }
    pub virtual func speak() -> string { return "..." }
}
class Dog : Animal, Named {        // single base class + interfaces/traits
    pub func init() { Animal.init("dog") }
    pub override func speak() -> string { return "Woof" }
}
```

- Single inheritance: `class D : B`. Components after the first `:`
  (comma-separated) are interfaces or traits the class implements.
- Class members are **private by default**; `pub` makes them visible
  outside the module.
- A class is **polymorphic** when it declares a `virtual` or `abstract`
  method, is `abstract` itself, or derives from a polymorphic class.
  Polymorphic classes carry a **vtable pointer at offset 0** (abi.md §4).
- `virtual func` dispatches dynamically. `override func` replaces the
  base's vtable slot (slot index is kept). `abstract func` has no body;
  an abstract class cannot be instantiated.
- Method overload resolution walks from the receiver's static type and
  uses the most-derived class that declares the name. Calls dispatch
  through the vtable exactly when the method is in the static type's
  vtable; everything else is a static (direct) call.
- Constructors are `func init(...)` methods. The class literal
  `Dog { }` zero-initializes fields and calls the `init` whose
  parameters all have defaults. `init` calls the base constructor as
  `Animal.init("dog")` (implicit `self` passes through). Every `init`
  stores its own class's vtable into `self` on every exit, so the
  **most-derived constructor's store wins**.
- `BaseName.method(...)` inside a subclass calls the base implementation
  directly with the current `self` (super-style dispatch).
- Classes are values (copying copies the vptr and fields); use
  `ptr<Class>` for shared, polymorphic objects.

### 5.8 Interfaces and traits

```
interface Shape { func area() -> f64 }
trait Named { func name() -> string { return "unnamed" } }
```

- An `interface` is a pure method contract (prototypes only). A `trait`
  may provide default method bodies. Classes implement them by listing
  them after the base class: `class C : Base, Shape, Named` (or
  `class C : Shape` when there is no base — the first name after `:` is
  then treated as an implementation, not inheritance).
- An interface **value** is a fat pointer `{object ptr, itable ptr}`
  (16 bytes), created by `obj as Interface` (implicit in argument
  position). A method call loads slot *i* of the itable, where *i* is
  the method's index in the interface declaration (abi.md §5).
- A trait default body is used when the class does not implement the
  method.
- `obj as Interface` requires the class to implement the interface —
  error otherwise. Unwrapping an interface value back to a concrete class
  requires `unsafe` and performs no check.
- Generic interfaces are not supported in v1.

### 5.9 Enums

```
enum Color { Red, Green, Blue }                  // simple
enum Shape { Circle(f64), Rect(f64, f64), Unit } // data-carrying
enum Option<T> { Some(T), None }
```

- Simple enums are `i32`-backed. Variants are values: `Color.Red`, or
  the bare `Red` when unique across visible enums — ambiguity is an
  error; qualify it.
- Data-carrying enums are tagged unions: an `i32` tag at offset 0 and a
  payload area starting at the next offset aligned to the most-aligned
  payload (abi.md §6). `sizeof` is `payload offset + largest payload
  extent`. Constructing a variant zeroes the inactive payload bytes.
- Construction: `Shape.Circle(2.0)`, `Option<i32>.Some(5)` (payload
  arguments may infer the enum's generic arguments), or a bare
  `Some(5)` when the variant name is unique. A unit variant used with
  `(...)` and no arguments is allowed.
- `==`/`!=` compare tags and work on any enum of the same type.

### 5.10 Function types and closures

`func(i32) -> i32` is the type of a **closure value**: the pair
`{fn ptr, env ptr}` (16 bytes). Sources of closure values: `&function`
references, `func(...){...}` lambdas, and variables of function type.
Calling one passes the environment pointer as a hidden first argument.
Lambdas capture enclosing locals **by value** at creation time
(concurrency.md §4). There are no bare function pointers — everything is
a closure pair, and referencing a plain function wraps it in an internal
trampoline that ignores the environment. Lambda parameters require type
annotations; the return type may be inferred (defaults to `void`).

### 5.11 Generics

```
func id<T>(x: T) -> T { return x }
struct Pair<A, B> { first: A, second: B }
class Box<T> { value: T, pub func init(v: T) { self.value = v } }
enum Option<T> { Some(T), None }
```

- Generic functions, methods, structs, classes, and enums are
  **monomorphized at compile time**: each used instantiation is cloned,
  re-checked with the type parameters substituted, and given a mangled
  symbol (abi.md §7). Instantiation depth is capped at 64 — exceeding it
  is an error (recursive generic expansion).
- Type arguments are inferred by unifying parameter types against
  argument types, or supplied explicitly: `id<i32>(7)`,
  `Pair<i32> { ... }`, `alloc_array<i32>(5)`.
- Failure to infer a parameter is an error ("cannot infer type parameter
  'T'").
- There are **no trait bounds** in v1: a generic body may use any
  operation on a type parameter that the concrete argument supports;
  errors surface at instantiation time.
- Generic interfaces are not supported.

---

## 6. Variables, constants, globals

- `x = 10` — declares an **immutable** local `x` with inferred type; if
  `x` already exists in scope (local or module global), this is an
  assignment instead (Go-like). Assigning to an existing immutable name
  is an error.
- `mut x = 10`, `x: i32 = 10`, `mut x: i32 = 10` — declarations; `mut`
  makes the variable assignable.
- A declaration without an initializer defaults to the type's default
  value (§8.4). Re-declaring the same name in the same scope is an
  error; shadowing an outer scope is allowed; shadowing a type name is
  an error.
- `const MAX: i32 = 100` — compile-time constant; the initializer shall
  be constant-foldable. Reading a constant never touches memory.
- Globals: `name: type = init` or `mut name: type = init` (`tls` makes
  them thread-local). Initializers shall be constant-foldable (literals,
  `const`s, arithmetic on them, `sizeof`/`alignof`, string literals,
  `null`, enum values). Globals without initializers are zeroed. Globals
  are initialized before `main`; there is no dynamic initialization.
- The name-first forms also declare globals at top level, with inferred
  types: `count = 10` (immutable) and `mut done = false` (mutable).
  From inside a function, `name = value` assigns to an existing global —
  or declares a new local (§6 list above).
- Global initializers shall be compile-time constants: class/struct
  literals are not allowed as global initializers — declare the global
  with its type (`m: Mutex`) and run `m.init()` in `main`.

---

## 7. Statements

- **if/else** — the condition shall be `bool` (no truthiness); `else if`
  chains; braces are required.
- **while** — condition `bool`. `break`/`continue` target the innermost
  loop; `break`/`continue` outside a loop is an error.
- **loop** — `loop { ... }` runs the body forever (equivalent to
  `while true`); exit with `break` or `return`.
- **for-in** — `for i in 0..n` (exclusive) / `for i in 0..=n`
  (inclusive). Ranges are valid only here; a range expression elsewhere
  is an error. Range bounds shall be integers of the same type; a
  literal bound adopts the other side's type. `for x in array` iterates
  elements (each iteration copies the element; `mut` on the binding
  makes the copy assignable). Iterating strings is not supported in v1;
  iterating raw pointers is an error.
- **C-style for** — `for i = 0; i < n; i += 1 { ... }`; init and step
  may be declarations or expressions; all three parts are optional.
- **switch** — the scrutinee shall be an integer, `char`, or enum.
  Cases: `case 1, 2:` …, an optional `default:`. **No fallthrough** — a
  case body ends at the next `case`/`default`/`}`. Case values shall be
  assignable to the scrutinee type. `default` is optional; an unmatched
  value falls through the statement (no panic).
- **match** (expression) — §8.6.
- **unsafe block** — §8.7; a statement and an expression.
- **return** — §8.4.

---

## 8. Functions

### 8.1 Declaration

```
func add(a: i32, b: i32) -> i32 { return a + b }
```

- Parameters are `name: type` with optional default values
  (`b: i32 = 10`). Default expressions are evaluated at the call site
  when the argument is omitted; trailing parameters with defaults may be
  omitted.
- Functions may be overloaded (same name, different parameter lists).
- Function-local function declarations are not allowed in v1; use
  lambdas.
- `pub` marks a function visible to importing modules.

### 8.2 Overload resolution

Candidates are the overload set of the name in scope (own module first,
then imports, then the prelude). A candidate matches when:

- the argument count fits (exact arity, or all omitted trailing
  parameters have defaults; a variadic `extern` also accepts extra
  arguments), and
- every argument is assignable to its parameter type, either exactly
  (score 2) or through a conversion from §5.2 (score 1).

The highest-scoring candidate wins; a tie is an **error** ("ambiguous
overload"). User-defined functions shadow compiler builtins of the same
name.

### 8.3 Extern functions

```
extern func puts(s: ptr<char>) -> i32
extern func printf(fmt: ptr<char>, ...) -> i32
@link_name("my_c_symbol") extern func thing(x: i32) -> i32
```

- `extern` declares a C function: **C ABI, no mangling**; the symbol is
  the declared name, or the `@link_name` attribute value.
- `...` marks a variadic C function. Extra arguments shall be integer,
  float, pointer, or bool — error otherwise. C default promotions apply:
  `f32` widens to `f64`, `bool` widens to `i32` (abi.md §8).
- `@link_name` may also rename a non-extern function's symbol.

### 8.4 Returns and the default-return rule

- `return expr` / `return`.
- When a function body **falls off the end**:
  - functions returning numeric types, `bool`, `char`, pointer, `string`,
    enum, vector, or `void` implicitly return the type's **default
    value**: `0`, `0.0`, `false`, `null`, `""` (a null, zero-length
    view), tag 0 with zeroed payload, a zero vector, or nothing.
  - functions returning an aggregate (`struct`, `class`, array) shall
    have an explicit `return` — otherwise it is an **error** ("can reach
    the end of its body without returning a '...'").
  - a `never` function that can return normally is an **error**.
- The reachability analysis understands: `return`; a statement call to a
  `never` function; `if` where both branches terminate; `while true`
  whose body contains no `break`; `switch` with a `default:` where every
  case and the default terminate; and any block containing a terminating
  statement. `break`/`continue` do not terminate a function.

### 8.5 The `never` type

`never` is the type of computations that do not return normally (e.g.
`panic(msg)`, `process.exit(code)`).

- A function declared `-> never` shall not return normally: every path
  must diverge (call a `never` function, or `return`) — error otherwise.
- An expression of type `never` is assignable to every type, and
  `never` propagates through operators and casts.
- A statement call to a `never` function makes all following statements
  of the block unreachable; the function's termination analysis treats
  the call as divergence.

### 8.6 match expressions

```
area = match s {
    Circle(r)    { 3.0 * r * r }
    Rect(w, h)   { w * h }
    Unit         { 0.0 }
}
name = match n {            // non-enum scrutinee: literal patterns
    0 { "zero" }
    _ { "many" }
}
```

- The scrutinee may be an enum, integer, `char`, `bool`, `string`, or
  pointer (literal patterns compare with `==`).
- Patterns: `_` (wildcard), a binding name (binds the whole scrutinee,
  **immutable**), an enum variant with payload bindings `Some(v)` /
  `Some(v, _)`, a bare unit-variant name, or a constant/literal
  expression.
- An enum match shall be **exhaustive** — every variant covered or a
  wildcard/binding arm present — error otherwise ("not exhaustive;
  missing: ..."). Any binding arm (e.g. `other`) also acts as the
  default arm.
- Each arm is a block. The match's value is the **last expression
  statement** of the arm's block (a trailing `return` also works). Arm
  value types shall agree after literal fitting; if every arm produces
  nothing, the match has type `void`.
- Payload bindings are immutable locals of the payload's type.
- Dispatch compares the tag against each variant pattern in order; there
  is no fallthrough.

### 8.7 unsafe

`unsafe { ... }` is both a statement and an expression (its value is the
block's last expression statement; `void` when there is none). The
following are **only allowed inside `unsafe`**:

- `as` casts between pointer types with different pointees,
- int→pointer and pointer→int casts,
- `volatile_load` / `volatile_store`,
- `asm` / `asm_volatile`,
- class downcasts (`base as Derived`),
- interface-to-class unwraps (`iface as Class`).

Plain `*p` dereference, `&x`, pointer arithmetic, `p[i]`, and indexing
are allowed outside `unsafe` (documented trade-off: Core checks what it
can — bounds — and marks the rest `unsafe`). Nested `unsafe` blocks are
fine.

---

## 9. Modules, imports, visibility

- Each `.cr` file is a module named by its file stem (`utils/math.cr` is
  module `math` — the stem, not the directory, is the name).
- `import math`, `import utils.helper` (binds `helper`),
  `import x as y`. The import binds a **module name**, used as
  `math.sqrt(...)` or `math.Vec`.
- **Resolution order** for `import a.b` (first hit wins):
  1. the directory of the importing file (`a/b.cr`),
  2. the project `src/` directory,
  3. each dependency package's checkout directory, in manifest order,
  4. the standard library directory.
  Unresolvable imports are errors listing the searched locations.
- The **prelude** (`std/prelude.cr`) is loaded into every program before
  anything else; its public declarations (`say`, `Option`, `assert`, …)
  are visible everywhere without an import. Explicit imports shadow
  prelude names.
- `pub` on any declaration makes it visible to importing modules;
  without `pub`, referencing it from another module is an error ("not
  public"). `pub import` parses but does not re-export in v1: every user
  of a module imports it directly.
- **Cycles are errors** — "circular import detected" (a file
  transitively importing itself). Importing the same file twice is
  deduplicated.
- Modules are checked and compiled in dependency (topological) order,
  prelude first.

---

## 10. Memory model (summary)

Core has **no garbage collector, ever**. Storage comes from three
places: the stack (locals, parameters, temporaries), static memory
(globals, string literals, vtables), and the heap (explicit `memory`
module calls). Ownership is a discipline — single owner allocates and
frees — not an enforced rule. See [memory-model.md](memory-model.md) for
the full contract: allocation APIs, lifetimes, dangling pointers,
double-free, aliasing, the UB catalog, `volatile`, atomics, and
allocator patterns.

Bounds checking: array indexing is checked against the static length
and string indexing against the dynamic length; a failed check prints
`panic: index N out of bounds for length L (file:line)` to stderr and
aborts the process. Inside `unsafe`, indexing is unchecked.

---

## 11. Execution and linking

- The compiler emits one object file (`<name>.coreobj.o`, removed after
  linking) and invokes `cc` with the Core runtime (`corert.o`) plus
  `-lm -lpthread -ldl -latomic`, the project's `[build]` link settings,
  and user `--link*` flags. The result is a self-contained native
  executable; imports are baked in (§1).
- `main` is lowered to a C `main`; a `void` main returns exit status 0.
- Command-line arguments are read from `/proc/self/cmdline` (Linux) via
  `process.arg_count()` / `process.arg(i)`.
- `--freestanding` omits the runtime and libc; the program defines its
  own entry (`@link_name("_start")`, typically `-> never`) and links
  with `ld -nostdlib`.
- Cross builds (`--target=`) stop at the relocatable object; a target
  toolchain performs linking (or pass `--link-arg` for a cross linker).
- The runtime (`runtime/corert.c`) is a thin C library: printing, string
  operations, malloc wrappers, panic/assert reporting, pthread wrappers,
  time. It performs no garbage collection, spawns no background threads,
  and allocates nothing beyond what its functions document (e.g. string
  concatenation, thread handles).

---

## 12. Standard library surface (summary)

- **prelude** (auto-imported): `say` overloads for every primitive and
  `ptr<T>`, `print` (no newline; string/i32/i64/u64), `assert` (1 or 2
  args), `panic(msg) -> never`, `c_str`, `str_from_c`, `str_eq`,
  `str_cmp`, `Option<T>`, `Result<T, E>`.
- **memory**: `alloc<T>`, `alloc_zeroed<T>`, `alloc_array<T>`,
  `alloc_zeroed_array<T>`, `alloc_bytes`, `alloc_aligned`,
  `realloc_array<T>`, `free<T>`, `memcpy`, `memset`, `memcmp`.
- **math**: `PI`, `E`, `sqrt`, `pow`, `sin`, `cos`, `abs` (f64/i32/i64),
  `floor`, `ceil`, `min/max/clamp<T>`.
- **thread**: `spawn`/`join`, `Mutex`, `RwLock`, `Cond`,
  `AtomicI32/I64/Bool/Usize`, and the atomic builtins.
- **time**: `time_ms`, `monotonic_ms`, `sleep_ms`.
- **process**: `exit -> never`, `arg_count`, `arg`.
- **simd**: `splat`, `extract`, `replace` per vector type, `sum`; `+ -
  * /` on vectors are lane-wise.

See [../stdlib/overview.md](../stdlib/overview.md) for the full
contract and [../packages/overview.md](../packages/overview.md) for how
third-party packages differ.
