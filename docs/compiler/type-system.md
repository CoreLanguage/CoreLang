# The type system

`src/Type.h` / `src/Type.cpp` implement Core's types. The design is
deliberately small: an `enum class TypeKind`, one fat `Type` struct, and an
interning `TypeContext`. There are no type constructors, no subtyping
calculus, no inference algorithm outside generic unification — most of the
"typing rules" are procedural checks in `Sema.cpp` (see
[semantic.md](semantic.md)).

## TypeKind

```cpp
enum class TypeKind {
  Prim,       // primitive (incl. bool/char/string/void/never/vector types)
  Ptr,        // ptr<T>
  Array,      // [T; N]
  Struct,     // struct type (DStruct)
  Class,      // class type (DClass)
  Interface,  // interface/trait value type (fat pointer) (DInterface)
  Enum,       // enum type (DEnum)
  Func,       // function value type: {fnptr, env} closure pair
  Never,
  Invalid,    // error recovery
};
```

A `Type` is one struct with a union-ish set of fields (`prim`, `pointee`,
`elem`/`arrayLen`, `decl` + `genericArgs`, `ifaceDecl`, `ret`/`params`),
plus `isNamedGeneric`/`genericVarName` for standing generic parameters.
Helper predicates (`isInt`, `isAggregate`, `isVector`, ...) are defined on
it; `isAggregate` (struct/class/array/interface/enum) is the set of types
assigned by byte-copying.

## The primitive table (`Prims.def`)

All 29 primitives live in one X-macro file, `src/Prims.def`:

```c
PRIM(void,    void,   0)
PRIM(never,   never,  0)
PRIM(bool,    bool,   1)
PRIM(char,    char,   8)
PRIM(string,  string, 0)
PRIM(i8,      i8,     8)
...
PRIM(f32x4,   simd,   128)
```

`Type.cpp` includes it four times to generate `PrimKind` values,
`primName`, `primKindByName`, and predicate tables:

```cpp
enum PrimKind {
#define PRIM(name, suffix, bits) PRIM_##name,
#include "Prims.def"
  PRIM_COUNT
};
```

Notes:

- `char` is an 8-bit *integer-like* type (`primIsInt` includes it), but not
  signed.
- `usize`/`isize` are 64 bits (x86-64/Linux assumption, visible in
  `primBits`).
- `string` is `Prim` — a value type, `{ptr, len}` at the LLVM level.
- The last ten entries are the SIMD vector primitives; `Type::isVector`
  tests the range `PRIM_f32x4..PRIM_u64x2`.

## Interning

`TypeContext` owns every `Type`. Construction goes through factory methods
that build a string key and intern it:

```cpp
Type &TypeContext::intern(const std::string &key, Type t) {
  auto it = interned.find(key);
  if (it != interned.end()) return *it->second;
  Type *stored = new Type(t);
  interned[key] = stored;
  return *stored;
}

Type *TypeContext::ptr(Type *pointee) {
  return &intern(strfmt("ptr:%p", (void *)pointee), [&] {
    Type t; t.kind = TypeKind::Ptr; t.pointee = pointee; return t;
  }());
}
```

Keys are structural: `"prim:%d"`, `"ptr:%p"`, `"arr:%p:%lld"`,
`"struct:%p:%p..."` (decl pointer, then each generic arg's address),
`"iface:%p"`, `"fn:%p:%p..."` (ret, then params), `"genvar:name"`.
Consequences:

- **Two types are the same exactly when their `Type*` pointers are equal**
  (the file's header comment says this; `TypeContext::same` relies on it).
- Interned types are never freed — fine, the context lives as long as the
  process (`Driver::tc`).
- `invalid()` and `never()` are two persistent members, not interned
  entries.

`typeToString` renders pretty names for diagnostics
(`ptr<i32>`, `[i32; 5]`, `Box<string>`, function types).

## Generic variables

Inside a template body, a generic parameter `T` resolves to
`tc.genericVar("T")` — an interned type with `kind == Invalid` but
`isNamedGeneric = true`. Two properties follow:

- `TypeContext::same` compares named generics *by name*, not pointer:

  ```cpp
  bool TypeContext::same(Type *a, Type *b) {
    if (a == b) return true;
    ...
    if (a->isNamedGeneric || b->isNamedGeneric) {
      return a->isNamedGeneric && b->isNamedGeneric && a->genericVarName == b->genericVarName;
    }
    return false;
  }
  ```

- `Sema::resolveNamedType` produces them in two situations: when a
  substitution map (`subst`) maps the name (during instantiation checking),
  or — failing that — when the name is a generic parameter of the enclosing
  function/class/struct (a "standing" variable for template checks).

## Generics = substitution + cloning

There is no separate "generic type" kind. A generic `struct Box<T>` becomes
a real type only via `TypeContext::getStruct(decl, args)` — the interned
`Type` carries the concrete `genericArgs`. Sema resolves field types through
`substituteFieldType`, which temporarily swaps `subst` so `T` resolves to
the instance's argument.

Functions work the same way at the body level: `Sema::instantiateGeneric`
deep-clones the template's `DFunc` (`ASTCloner::funcBody`) and re-checks
the clone with `subst` mapping each `genericParams[i]` → `args[i]`. The
whole story, including the worked example with real mangled IR, is in
[../internals/monomorphization.md](../internals/monomorphization.md).

## Unification

`Sema::unifyTypes(want, got, vars)` is the entire inference engine, used
when calling a generic function (and when constructing generic enums):

```cpp
bool Sema::unifyTypes(Type *want, Type *got, std::map<std::string, Type *> &vars) {
  if (!want || !got) return false;
  if (want->isNamedGeneric) {
    auto it = vars.find(want->genericVarName);
    ... // bind the variable, or compare against an existing binding
  }
  if (tc.same(want, got)) return true;
  // literal fit
  if (got->kind == TypeKind::Prim && want->isInt()) return got->isInt();
  if (want->isPtr() && got->isPtr()) {
    // array decay: ptr<[T; N]> argument against ptr<T> binds T to the element
    if (got->pointee->isArray() && want->pointee->isNamedGeneric)
      return unifyTypes(want->pointee, got->pointee->elem, vars);
    return unifyTypes(want->pointee, got->pointee, vars);
  }
  if (want->kind == TypeKind::Struct && got->kind == TypeKind::Struct && want->decl == got->decl) {
    ... // recurse into generic args
  }
  return false;
}
```

That is: bind-or-check on generic variables, recursion through pointers
(with array decay binding the element type), and structural recursion into
same-decl structs. No constraints, no bounds ("No trait bounds in v1").
Unbound variables after unification produce
`cannot infer type parameter 'T' for 'f'; add a cast or explicit type
annotation` — the explicit-args path (`alloc_array<i32>(5)`, handled in
`checkCall` before inference) always works.

## Why there are no implicit conversions

The rule is enforced in exactly one choke point — `Sema::typesAssignable`
— and everything (initializers, assignments, arguments, returns, case
labels) funnels through it. Design rationale, as visible in the code and
tests:

- **Predictability of overflow.** Widening `i32` → `i64` implicitly would
  make signed/unsigned and width interactions invisible at call sites.
  Instead: `error: cannot apply '+' to 'i32' and 'i64'`,
  help `use \`as\` to cast`.
- **No truthiness.** Conditions go through dedicated checks in
  `checkStmt` (`if condition must be bool, got 'i32'`,
  help `Core has no truthy integers: write \`if x != 0\``).
- **Literals are the pressure valve.** Because unsuffixed literals fit any
  in-range integer/float type (and constant-foldable expressions like
  `4 * 10` are accepted for integer parameters), the lack of implicit
  conversions rarely needs an explicit cast. This is implemented inside
  `typesAssignable` (literal fitting mutates `lit->type`) and mirrored in
  `resolveOverload` scoring.
- **`never` is assignable to everything** (`if (src->isNever()) return
  true;`), which lets `panic(...)`-then-continue code typecheck without a
  special case in every caller.

The only *implicit* coercions anywhere are the C-interop conveniences in
`resolveOverload`/`coerceValue`: `string` → `ptr<char>` (data-pointer
extraction), `ptr<[T; N]>` → `ptr<T>` decay, and class upcasts — all
address-preserving, none change interpretation of the bits.

## Common failure modes

- **Interning by non-structural keys.** If you add a `Type` factory, the
  key must capture everything that distinguishes the type (see how
  `getStruct` appends every generic arg's address), or two distinct types
  will unify.
- **Comparing types with `==`.** Pointer equality is right for interned
  types, but generic variables must go through `tc.same`. Raw `==` on
  template-checked code silently fails.
- **Forgetting `TypeKind::Invalid` handling.** New checks must tolerate
  `Invalid` (usually by returning early) or error cascades bury the real
  diagnostic.
- **`isNamedGeneric` leaks.** A standing generic variable that escapes into
  a non-generic context (e.g. stored into a global's type) will render as
  its name and break codegen; instantiation must replace all of them
  (inference errors with `cannot infer type parameter ...` otherwise).
