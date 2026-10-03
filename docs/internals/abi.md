# The Core ABI

This page documents the machine-level contract of compiled Core code:
value semantics, struct/class/enum layout, the closure convention, the
name-mangling grammar, and the rules for calling C. There is *no* separate
ABI library — these rules are implemented across `Sema::mangleFuncName`,
`Codegen::llvmType`/`structTypeFor`/`enumStorageType`, and `runtime/corert.c`
— but they are stable enough to rely on from hand-written asm or a C host.

## Value semantics

Everything is a value, copied on assignment/parameter-passing/return:

- Scalars are passed in registers per the platform C ABI (LLVM's default
  lowering, since Core functions are ordinary LLVM functions with C
  linkage conventions).
- Aggregates (`struct`, `class` handles, arrays, interfaces, enums with
  payloads, `string`, closures) are passed and returned **by value**; LLVM
  lowers small ones into registers, larger ones via `memcpy` (sret), per
  the C ABI of the target.
- `Codegen::emitExpr` on a local of aggregate type emits a *load of the
  whole value* — assignments never alias. There is no move semantics; if
  you need shared mutable state, pass a `ptr<T>` (this is also why lambdas
  capture by value).
- `self` for methods is `ptr<Struct>` / `ptr<Class>` (`Sema::selfTypeOf`);
  it is the first parameter and is a plain pointer — no ownership implied.

## Sizes of the builtin value types (x86-64 Linux)

| Type | Size/align | Layout |
|---|---|---|
| `bool` | 1 (as `i8` storage; `i1` in registers) | |
| `char`, `i8`, `u8` | 1 | |
| `i16/u16` | 2 | |
| `i32/u32/f32` | 4 | |
| `i64/u64/f64/usize/isize` | 8 | |
| `i128/u128` | 16 | |
| `string` | 16, align 8 | `{ ptr data, u64 len }` |
| `ptr<T>` | 8 (opaque `ptr`) | `ptr<void>` is the same `i8*`-equivalent |
| SIMD vectors | 16, align 16 | `<4 x f32>` etc. |
| interface value | 16, align 8 | `{ ptr object, ptr itable }` |
| closure / function value | 16, align 8 | `{ ptr fn, ptr env }` |
| enum, no payloads | 4 | `i32` tag |
| enum, with payloads | 4 + payload area | byte array, see below |

Exact numbers for user types always come from the LLVM `DataLayout` of the
target — `sizeof`/`alignof` compile to those constants (`Codegen::evalConst`).

## Struct layout rules

`struct` follows C layout rules (natural alignment per field, in
declaration order); `@packed` removes padding (LLVM packed struct):

```cpp
llvm::StructType *st = StructType::create(ctx, fields, name, packed);
```

Therefore: a C struct with the same field types is layout-compatible, and
Core structs can be passed to C functions through `ptr<T>`.

## Class layout and vtables

- **Polymorphic** classes (any `virtual`/`abstract` method, or deriving
  from a polymorphic class; `Sema::buildLayout`) carry the **vptr at
  offset 0**, then fields from the most-base class to the most-derived.
  Real IR: `%core.class.Dog = type { ptr, %core.string }`.
- Non-polymorphic classes have no vptr and behave like structs.
- The **vtable** is a global array of function pointers, one slot per
  virtual method in `ClassLayout::vtableOrder` (base slots first,
  overrides keep the base's slot, new virtuals append), `LinkOnceODR`:

  ```llvm
  @_CVT3Dog = linkonce_odr constant [1 x ptr] [ptr @_C6shapes3Dog5speakIE]
  ```

  Symbol: `_CV` + mangled class type. Constructors store the class's
  vtable into `self` on every exit (`Codegen::emitVptrStoreIfInit`); when
  a derived `init` calls `Animal.init(...)`, the base's store happens
  first and the derived's last — the most-derived vtable wins.
- There are no runtime type IDs, no downcast checks, and no
  destructors/vtable slot 0/-1 entries (unlike C++; slot indices start at
  0 and are purely method slots).

## Interfaces (fat pointers) and itables

`obj as SomeInterface` produces `{ ptr object, ptr itable }`. The
**itable** is a per-(class, interface) global array with one function
pointer per interface method, in interface declaration order (trait
default bodies fill slots the class doesn't implement):

```llvm
@_CIT3Dog_X9Speakable = linkonce_odr constant [1 x ptr] [ptr @_C6shapes3Dog5speakIE]
```

Symbol: `_CI` + mangled class + `_` + mangled interface. Interface method
calls load `itable[slot]` and call with the object pointer as the first
argument. Unwrapping (`as` back to a class, unsafe-only) extracts element
0 and performs no check.

## Enums

- Simple enums are `i32` tags. `Color.Red` is the constant `0`.
- Data-carrying enums are a byte array: a 4-byte tag at offset 0, then the
  largest variant's payload at its ABI-aligned offset
  (`Codegen::enumStorageType`). For `enum Shape { Circle(f64),
  Rect(f64, f64), Unit }`: `%core.enum.Shape = type { [24 x i8] }` —
  tag at 0, `f64` payload at 8 and 16 for `Rect`.
- `match` loads the tag and compares; payload bindings load from the
  aligned offsets. Variant 0 is the default value.

## Closures

A function *value* (lambda, or `&func`) is `{ fn ptr, env ptr }`:

- The **env pointer is the callee's first parameter** for every function
  called through a closure value. Plain functions are wrapped in a
  trampoline (`<mangled>.closure`, internal linkage) that drops the env
  argument, so *all* indirect calls look identical:

  ```llvm
  %fn  = extractvalue { ptr, ptr } %closure, 0
  %env = extractvalue { ptr, ptr } %closure, 1
  %r   = call i32 %fn(ptr %env, i32 %x)
  ```

- Lambda environments are heap-allocated (`core_rt_alloc`) at creation and
  captures are copied **by value** into it in `ELambda::captures` order.
  There is no reference capture in v1.
- `std.thread.spawn` relies on exactly this ABI: its extern declares
  `core_rt_thread_spawn(fn: func())` and the C runtime stores `{fn, env}`
  and calls `fn(env)` on the new thread (`CRThreadStart` in `corert.c`).

## Mangling

Implemented in `Sema::mangleFuncName` (functions) and
`Sema::mangleTypeForName` (types); globals use their own scheme in
`Codegen::globalFor`. The grammar:

```
function   := "_C" module [parent] name ["I" params "E"] ["G" args "E"]
module     := <len><part> { <len><part> }        -- dotted module name split on "."
parent     := <len><TypeName>                    -- methods only (defining type)
name       := <len><functionName>
params     := mangledType { mangledType }        -- no separator; omitted entirely
                                                  if a template param doesn't resolve
args       := mangledType { "," mangledType }    -- generic arguments

mangledType :=
    <prim name>                      -- i32, u64, f64, bool, char, string, ...
  | "P" mangledType                  -- ptr<T>
  | "A" <len> "_" mangledType        -- [T; N]
  | "F" { mangledType } "_" mangledType   -- func(params) -> ret
  | "T" <len> <name> ["I" args "E"]  -- struct/class/enum (+ generic args)
  | "X" <len> <name>                 -- interface/trait
  | "never" | "invalid"
```

`<len>` is the decimal byte length of the following name part (Itanium-style).
Examples from real programs:

| Source | Symbol |
|---|---|
| `func main()` (entry) | `main` |
| `say(v: string)` in prelude | `_C7prelude3sayIstringE` |
| `pick<T>(a: T, b: T)` in module `main`, instance `<i32>` | `_C4main4pickGi32E` |
| `alloc<T>()` in `memory`, instance `<Pair>` | `_C6memory5allocIEGT4PairE` |
| method `speak` on class `Dog` in module `shapes` | `_C6shapes3Dog5speakIE` |
| global `counter` in module `main` | `_CG4main7counter` |
| vtable of `Dog` | `_CVT3Dog` |
| itable for `Dog`/`Speakable` | `_CIT3Dog_X9Speakable` |
| lambda inside `_C4main4pickGi32E` | `_C4main4pickGi32E.lambda0` |

Special cases:

- `extern` functions use their C name (`@link_name` value if present, else
  the declared name) — **no mangling**, C ABI.
- The entry `main` (module-level, non-extern) keeps the symbol `main`.
- `@link_name` on any function overrides the mangled name entirely.
- Global variables: `_CG` + module (length-prefixed parts) + name
  (length-prefixed).

## C interop rules

- `extern func puts(s: ptr<char>) -> i32` declares a C function: called
  with the C calling convention, `isVariadic` honored, no `self`, no env.
- **Varargs promotion** matches C: `f32` widens to `f64`
  (`builder.CreateFPExt`), `bool` widens to `i32`, in
  `Codegen::emitCallArgs`. Sema additionally rejects non-primitive
  variadic arguments (`variadic argument N must be an integer, float,
  pointer or bool for the C ABI`).
- `string` converts to `ptr<char>` implicitly in argument position
  (`coerceValue` extracts the data pointer); NUL-termination is **not**
  guaranteed — use `c_str(s)` (literals happen to be NUL-terminated).
- Structs passed by value to C are layout-compatible (see above); pass
  `ptr<Struct>` for anything ABI-sensitive.
- Return values follow the C ABI for the target, since Core returns plain
  LLVM values; entry `main` is adjusted to return `i32` (`ret i32 0` for
  `void main`).

## Freestanding / custom entry

`--freestanding` compiles without the prelude or runtime and links with
`ld -nostdlib`; the entry symbol is chosen via `@link_name("_start")` (see
`examples/kernel`). Nothing else about the ABI changes: the same struct
layouts, the same mangling, the same closure convention.

## Common failure modes

- **Hand-written asm constraints.** Inline-asm arguments are widened to
  `i64` and pointers are `ptrtoint`'d (`Codegen::emitBuiltinCall`); write
  constraints accordingly (`r` registers, not `*m`).
- **Assuming `char*` from `string`.** `string` is `{ptr, len}`; only
  `c_str`/implicit conversion extracts the pointer, and `+` allocates —
  don't cache those pointers across allocations.
- **Copy vs. reference.** Lambdas capturing a local copy it; two closures
  created in the same function see *independent* envs. Capture
  `ptr<T>` to share.
- **Mangling instability across compiler versions.** Symbols are not
  guaranteed stable between releases yet (whole-program compilation makes
  this mostly moot); never link objects built by different compiler
  versions.
