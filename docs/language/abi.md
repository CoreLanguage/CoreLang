# Core ABI Reference

This document specifies the Application Binary Interface of compiled
Core code: calling conventions, data layout, virtual dispatch tables,
closure representation, name mangling, and C interop. It matches the
`core` compiler (LLVM 18 backend) on x86-64 Linux exactly; the rules
generalize to other System V targets, and LLVM lowers to each target's
C ABI.

Everything here is observable with `core emit-ir` / `core emit-asm` and
`nm` — the last section shows how.

Related: [SPEC.md](SPEC.md), [memory-model.md](memory-model.md).

---

## 1. Calling convention

Core uses the target's **C calling convention** (System V AMD64 on
x86-64 Linux) via LLVM's default calling convention. There is no Core
specific convention, no structured-exception machinery, and no
unwinding.

- Integer/pointer arguments, in order: `RDI, RSI, RDX, RCX, R8, R9`,
  then the stack.
- Floating-point arguments: `XMM0`–`XMM7`, then the stack.
- An argument that does not fit a register (large aggregates) is passed
  by memory per the System V classification — LLVM lowers this.
- Return value: `RAX` (integers/pointers), `XMM0` (floats), memory for
  large aggregates.
- Variadic calls set `AL` to the number of vector registers used
  (standard SysV; LLVM emits this for `...` calls).

Argument order facts:

- **Instance methods**: `self` is the first argument (a `ptr<Class>`, in
  `RDI`). `func init` likewise takes `self` first.
- **Closure values**: the environment pointer is the first argument,
  before the declared parameters (§6).
- **Virtual calls**: the loaded function pointer is called with the same
  shape as a direct call (self first).
- **Interface calls**: the fat pointer's object pointer is the first
  argument.
- `main` is lowered to a C `main` returning `i32` (a `void` Core main
  returns 0).

Example — `func add(a: Vec, b: Vec) -> Vec` where
`struct Vec { x: f64 }` passes two `{double}` aggregates (SysV
INTEGER-classified, memory here since the IR type is a struct) and
generates:

```
define %core.struct.Vec @_C7_mangle3addIT3VecT3VecE(%core.struct.Vec %a, %core.struct.Vec %b)
```

---

## 2. Parameter and return passing by category

| Core type | Passed as |
|-----------|-----------|
| `i8`–`i64`, `u8`–`u64`, `usize`, `isize` | integer register / stack slot of that width |
| `i128`, `u128` | `i128` (pair of registers or memory per SysV) |
| `f32`, `f64` | SSE register |
| `bool` | `i1` in a register (stored as one byte in memory) |
| `char` | `i8` |
| `ptr<T>`, `ptr<void>` | 64-bit pointer |
| `string` | 16-byte `{ptr, len}` pair — two registers (SysV INTEGER, INTEGER) |
| vectors | SSE register (16 bytes) |
| `enum` (simple) | `i32` |
| `enum` (data-carrying) | its storage struct (§6), passed per SysV classification |
| `struct`, `class`, `[T; N]` | by value, per SysV classification (small ones in registers) |
| interface value | 16-byte fat pointer pair, two registers |
| closure / function value | 16-byte `{fn, env}` pair, two registers |
| `void`/`never` | no value |

Everything is a **copy** (Core has no references); methods receive a
pointer so mutations through `self` are visible to the caller.

---

## 3. Struct layout algorithm

Struct layout is **C-compatible (natural alignment)**:

1. Fields are placed in declaration order.
2. Each field is placed at the current offset rounded up to the field's
   alignment (`alignof(field)`).
3. The struct's size is rounded up to the struct's alignment, which is
   the maximum alignment of its fields.
4. `@packed` sets alignment to 1 and inserts no padding; field offsets
   are exactly the running sum of field sizes.

Verified sizes on x86-64:

```core
struct Inner { a: i32, b: f64 }          // sizeof 16, align 8
                                         //   a at 0, pad 4, b at 8
struct Outer { tag: bool, inner: Inner, c: char }
                                         // sizeof 32, align 8
                                         //   tag 0, pad 7, inner 8..24, c 24, pad 7
@packed
struct Packed { a: i32, b: i8, c: i32 }  // sizeof 9, align 1
```

`sizeof`/`alignof` return these target-true values at compile time, so
code never needs to hard-code them.

---

## 4. Class layout and the vtable

### 4.1 Object layout

A class instance lays out as:

1. **vptr** (8 bytes) at offset 0 — only when the class is *polymorphic*
   (it declares `virtual`/`abstract` methods, is `abstract`, or derives
   from a polymorphic class). Non-polymorphic classes have no vptr.
2. Base class fields, root of the chain first.
3. The class's own fields.

Each field is naturally aligned (as structs). Example:

```core
class Base { x: i32 ... }              // non-polymorphic: sizeof 4
class Derived : Base { y: i64 ... }    // sizeof 16: x 0, pad 4, y 8
```

A polymorphic class adds the vptr: `class P { x: i32 }` with a virtual
method is 16 bytes (vptr 0, x 8).

### 4.2 Vtable layout

- One vtable per class: a global array of function pointers named
  `_CV` + mangled class name, e.g. `@_CVT3Dog`, emitted
  `linkonce_odr` (deduplicated across modules).
- Slots: **base slots first** (inherited order preserved); an `override`
  keeps the base's slot index; new virtuals append.
- The vptr stored in each instance points at slot 0 of this array.
- Constructors (`init`) store the class's own vtable into `self` on
  every exit, so the most-derived constructor's store wins after base
  constructors run.
- Virtual dispatch: load slot `i` from the vptr and call it —
  `%vptr = load ptr, ptr %self` ; `%fn = load ptr, ptr gep(%vptr, i)`.

---

## 5. Interface values and itables

- An interface **value** is a fat pointer `{object ptr, itable ptr}`
  (16 bytes), produced by `obj as Interface` (implicit where allowed).
- One **itable** per (class, interface) pair: a global array of function
  pointers named `_CI` + mangled class + `_X` + mangled interface, e.g.
  `@_CIT3Dog_X5Speak`, `linkonce_odr`.
- Slot *i* holds the implementation of the *i*-th method **in the
  interface's declaration order**. The implementation is found by
  walking the class chain from the class upward and taking the first
  non-static method with the interface method's name; if none exists,
  the trait's default body is used.
- A call `s.area()` on an interface value loads slot *i* and calls it
  with the fat pointer's object pointer as `self`.
- Wrapping (`as`) only copies the two pointers — no allocation, no
  registration. Unwrapping (`unsafe`) just reinterprets the object
  pointer; **no check** is performed.

Example IR from the oop example:

```
@_CIT3Cat_X5Shape = linkonce_odr constant [1 x ptr] [ptr @_C4main3Cat4areaIE]
   ...
%fat = insertvalue { ptr, ptr } undef, ptr %obj, 0
%s   = insertvalue { ptr, ptr } %fat, ptr @_CIT3Cat_X5Shape, 1
```

---

## 6. Enum layout

- **Simple enums** (no payloads): the value is an `i32` tag.
  `sizeof(Color)` = 4.
- **Data-carrying enums**: a tagged union.
  1. `i32` tag at offset 0.
  2. The payload area starts at offset 4 rounded up to the maximum
     payload alignment.
  3. Within a variant's payload, fields are laid out like struct fields
     (declaration order, natural alignment).
  4. Total size = payload offset + the maximum payload extent over all
     variants, rounded to the enum's alignment (max of 4 and the payload
     alignments). The inactive payload bytes are zeroed on construction.

Verified:

```core
enum Small { A, B }                          // sizeof 4
enum Shape { Circle(f64), Rect(f64, f64), Unit }
                                             // sizeof 24: tag 0, pad 4, payload 8..24
enum Opt    { Some(i64), None1 }             // sizeof 16: tag 0, pad 4, payload 8..16
```

Note: `alignof` of a data-carrying enum currently reports the alignment
of the raw byte storage (1) even though payload fields are aligned
*within* the value; when placing such enums in memory that hardware or
C code reads, keep the base 8-aligned (or access payloads through Core
only).

---

## 7. Closure representation

Every function value — lambda, `&fn` reference, or variable of function
type — is the 16-byte pair:

```
{ fn ptr, env ptr }
```

- **Lambdas**: the environment is a heap block (malloc'd at closure
  creation, `core_rt_alloc`) holding one field per captured variable,
  **by value**, in capture order. The lambda's first parameter is this
  environment pointer; the lambda reads its captures out of it.
  Captured *copies* never alias the originals (mutation is not shared —
  see concurrency.md).
  Symbol: the enclosing function's mangled name + `.lambda<N>`
  (internal linkage), e.g. `_C4main3addIi32E.lambda0`.
- **Plain function references** (`&f` where `f` is a normal function)
  are wrapped in an internal **trampoline** named
  `<mangled f>.closure` that drops the environment argument and forwards
  to `f`. The env pointer is null.
- Calls through function values pass the environment pointer as the
  hidden first argument, then the declared parameters.
- There is no reference-counting, no boxing of large captures beyond the
  single env allocation, and no closure allocation when nothing is
  captured (env = null).

---

## 8. Name mangling

### 8.1 Function symbols

```
mangled-fn  = "_C" module-path [ parent ] fn-name [ "I" params "E" ] [ "G" generics "E" ]
module-path = part { part }              ; one part per dot component
part        = digits name                ; length-prefixed component
parent      = digits TypeName            ; enclosing class/struct/interface/trait
fn-name     = digits name
params      = type-enc { type-enc }      ; declared parameter types
generics    = type-enc { "," type-enc }  ; monomorphized type arguments
```

- `extern` functions are **not** mangled: the symbol is the declared
  name, or the `@link_name` value.
- The entry `func main()` compiles to the symbol `main`.

Type encodings (`mangleTypeForName`):

| Type | Encoding |
|------|----------|
| primitives | their name: `i32`, `f64`, `string`, `bool`, `char`, `usize`, `f32x4`, … |
| pointer | `P` + pointee encoding |
| array | `A` + length + `_` + element encoding |
| function | `F` + param encodings + `_` + return encoding |
| struct / class / enum | `T` + digits + name, + `I` args `E` when generic |
| interface / trait | `X` + digits + name |

Verified examples:

```
func say(v: string)  in prelude   ->  _C7prelude3sayIstringE
func say(v: i32)     in prelude   ->  _C7prelude3sayIi32E
func add(a: Vec, b: Vec) in module _mangle
                                  ->  _C7_mangle3addIT3VecT3VecE
func id<T>(x: T) called as id<i32> in module _mangle
                                  ->  _C7_mangle2idGi32E
func max<T> called as max<f64>    ->  _C4_gen3maxGf64E
method speak of class Dog in file main.cr
                                  ->  _C4main3Dog5speakIE
```

Generic-parameter-only signatures (like `id<T>`) omit the `I…E` section
(the template parameter does not resolve at registration time) and
encode the instantiation in `G…E`. A generic parameter appearing in an
`I…E` section prints as `x`/`invalid` only inside template checking; all
emitted symbols have concrete types.

### 8.2 Other symbols

| Entity | Symbol |
|--------|--------|
| global `g` in module `m` | `_CG` + digits + `m` + digits + `g` |
| class vtable | `_CV` + T-encoded class (`_CVT3Dog`) |
| class/interface itable | `_CI` + T-encoded class + `_X` + X-encoded interface (`_CIT3Dog_X5Speak`) |
| lambda | `<mangled enclosing fn>` + `.lambda<N>` |
| function trampoline | `<mangled fn>` + `.closure` |
| string literal | private `.str` globals |

Core symbols never clash with C symbols because of the `_C` prefix and
length-prefixing — `extern` declarations are the only unmangled names
and you control them.

---

## 9. C interop

### 9.1 Declaring and calling C functions

```core
extern func printf(fmt: ptr<char>, ...) -> i32
extern func atof(s: ptr<char>) -> f64
@link_name("my_c_name") extern func thing(x: i32) -> i32
```

- Calls follow the C ABI exactly (§1); no wrapper code is generated.
- `...` accepts extra arguments; they shall be integers, floats,
  pointers, or bools (error otherwise). **Default argument promotions**
  apply, as in C: `f32` widens to `f64`, `bool` widens to `i32`,
  `char` passes as `i8`-in-register.
- Core never inserts a catch/finally frame; `panic` is `abort()`, which
  is C-compatible.

### 9.2 Which types cross the ABI safely

| Core type | C equivalent | Notes |
|-----------|--------------|-------|
| `i8…i64`, `u8…u64` | `int8_t…int64_t` | exact widths |
| `i128`/`u128` | `__int128` | gcc/clang compatible |
| `usize`/`isize` | `size_t`/`ssize_t` | 64-bit |
| `f32`, `f64` | `float`, `double` | |
| `bool` | `bool` (`_Bool`) | one byte in memory |
| `char` | `uint8_t`-ish byte | not a wide char |
| `ptr<T>` | `T*` | `ptr<void>` = `void*` |
| `string` | — | does not cross directly |
| `struct` (Core) | matching C struct | same layout algorithm (§3), so a mirrored C struct works |
| enum | `int32_t` (simple) | data-carrying enums are not C-compatible |
| vectors | `__m128`-style | lane order matches |

- A `string` **converts implicitly to `ptr<char>` in argument position**:
  the callee receives the data pointer. Core string literals are
  NUL-terminated precisely so they can be passed to C.
- Any `ptr<T>` converts implicitly to `ptr<void>` (`void*`), and
  `usize`/`isize` to `u64`/`i64` (identical representation, no bits
  change) — handy for C APIs taking `void*` or `size_t`.
- To receive strings from C, declare the parameter `ptr<char>` and wrap
  it with `str_from_c` (or `c_str` to go the other way).
- Structs: declare a Core `struct` mirroring the C struct field-for-
  field (layout algorithm is C-compatible; use `@packed` for C packed
  structs). Passing works by value in both directions.
- Callbacks: C functions expecting `void (*)(void*)` can be handed a
  Core closure value — the representation is `{fn, env}` with the
  env pointer as the first argument, which matches the
  `void(*)(void*)`-style trampoline pattern used by the runtime's
  pthread wrappers (the C side calls `fn(env)`).

### 9.3 Linking native libraries

- `core compile --link=<lib> --lib-path=<dir> --link-arg=<arg>` and the
  project manifest `[build]` section (`link`, `link-paths`,
  `link-args`) add `-l`/`-L`/raw arguments to the final `cc` link.
- The host runtime is linked as `corert.o` plus `-lm -lpthread -ldl
  -latomic`; your libraries come after those.

---

## 10. String representation

- `string` = `{data: ptr, len: usize}` — a **view**, 16 bytes, value
  semantics (copying copies the view, not the bytes).
- Literals: private static globals, NUL-terminated, `len` excludes the
  terminator; program lifetime.
- `s + t`: calls `core_rt_str_concat`, which mallocs `len(a)+len(b)`
  bytes and returns a view over them — the buffer has **no owner**; the
  language does not free it. Strings created by concatenation live until
  the process exits unless you manage the buffer yourself.
- `c_str(s)` returns the data pointer (valid as long as the view's
  bytes are); `str_from_c(p)` builds `{p, strlen(p)}`.
- Comparisons are `memcmp`-based; `==` also compares lengths first.
- `s[i]` yields the i-th **byte** (`char`), bounds-checked against
  `len`. There is no UTF-8 decoding in v1.

---

## 11. Inspecting the ABI

```console
# LLVM IR for a file (whole module tree) — see struct types, mangling,
# vtables, closure trampolines:
core emit-ir main.cr | less

# Machine code:
core emit-asm main.cr | less

# Symbols of a built binary:
core compile demo main.cr
nm demo | grep _C
```

Useful greps:

```console
core emit-ir main.cr | grep -E "^@_C[IV]"     # vtables and itables
core emit-ir main.cr | grep -E "^define"      # all mangled function symbols
core emit-ir main.cr | grep "core.struct"     # lowered struct types
nm demo | grep " _CG"                         # globals
```

`--debug` adds DWARF info: GDB resolves breakpoints to `.cr` lines and
`info functions` shows the mangled symbols above with their source
locations.
