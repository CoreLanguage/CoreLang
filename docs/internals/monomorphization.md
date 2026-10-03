# Monomorphization, end to end

Core generics are compile-time templates: every distinct set of type
arguments produces a distinct, fully checked, fully emitted function. This
document walks the whole path with a real example and the real IR the
compiler produces. The type-level view (generic variables, unification) is
in [../compiler/type-system.md](../compiler/type-system.md); this page is
the procedural story.

## The moving parts

| Component | File | Role |
|---|---|---|
| `DFunc::genericParams` | `AST.h` | the `<T, U>` list on a template |
| `GenericInstance` | `Sema.h` | one instantiation: template, cloned func, args, mangled name |
| `Sema::instantiateGeneric` | `Sema.cpp` | dedup + clone + re-check |
| `Sema::unifyTypes` | `Sema.cpp` | infer args from call arguments |
| `ASTCloner::funcBody` | `ASTClone.h` | deep-copies params + body |
| `ECall::genInstance` | `AST.h` | annotation linking a call to its instance |
| `Codegen::emitFuncBody(gi->clonedFunc, gi->args)` | `Codegen.cpp` | emits the instance under its substitution |

```cpp
// A monomorphized generic instance (function or method).
struct GenericInstance {
  DFunc *tmpl = nullptr;
  DFunc *clonedFunc = nullptr; // cloned+checked body (per instance)
  std::vector<Type *> args;
  std::string mangledName;
};
```

## Worked example

Source (`main.cr`, runnable — try `core emit-ir main.cr`):

```cr
import memory

func pick<T>(a: T, b: T) -> T {
    if a < b { return a }
    return b
}

struct Pair {
    x: i32,
    y: i32
}

func main() {
    say pick(3, 9)
    say pick(1.5, 0.5)
    p = alloc<Pair>()
    p.x = 4
    say p.x
    free(p)
}
```

### 1. Parse

`Parser::parseFuncRest` collects `genericParams = ["T"]`. At the two call
sites there are no explicit `<...>` arguments, so nothing special happens —
they become plain `ECall`s whose callee is `EIdent("pick")`.

### 2. Register and check the template

`Sema::checkAll` **skips** functions with `genericParams`:

```cpp
if (d->kind == Decl::Func) {
  auto *f = (DFunc *)d;
  if (f->parent == nullptr && f->genericParams.empty()) checkFuncDecl(f, {});
}
```

The template body is checked only through instantiations.

### 3. First call: inference

`Sema::checkCall` finds `pick` via `lookupFuncsVisible`, checks the
arguments (`i32`, `i32`), then — because `chosen->genericParams` is
non-empty — unifies parameter types against argument types to bind the
variables:

```cpp
std::map<std::string, Type *> vars;
for (auto &gp : chosen->genericParams) vars[gp] = tc.genericVar(gp);
auto *savedSubst = subst;
subst = &vars;
for (size_t pi = 0; pi < chosen->params.size() && pi < call->args.size(); pi++) {
  Type *want = resolveType(chosen->params[pi].type);   // T
  Type *got = call->args[pi]->type;                    // i32
  if (!unifyTypes(want, got, vars)) { inferOk = false; break; }
}
subst = savedSubst;
```

`unifyTypes` binds `T := i32` on the first argument and *confirms* it on
the second (a second candidate binding to a different type would fail the
call). Unbound variables are an error (`cannot infer type parameter 'T'
for 'pick'`); here both are bound.

### 4. Instantiate

```cpp
GenericInstance *Sema::instantiateGeneric(DFunc *tmpl, const std::vector<Type *> &args,
                                          SourceLoc loc) {
  std::string key = mangleFuncName(tmpl, args);       // "_C4main4pickGi32E"
  auto it = instances.find({tmpl, key});
  if (it != instances.end()) return it->second;       // dedup by mangled name
  static int depth = 0;
  if (++depth > 64) { /* "generic instantiation depth exceeded (64)" */ }
  ...
  static ASTContext cloneArena; // process-lifetime arena for instantiation clones
  ASTCloner cloner(cloneArena);
  DFunc *clone = cloneArena.make<DFunc>(tmpl->loc);
  clone->name = tmpl->name;
  clone->genericParams = tmpl->genericParams; // keeps subst active in codegen
  clone->parent = tmpl->parent;
  ...
  cloner.funcBody(tmpl, clone);                       // deep-copies params + body

  std::map<std::string, Type *> sub;
  for (size_t i = 0; i < tmpl->genericParams.size() && i < args.size(); i++)
    sub[tmpl->genericParams[i]] = args[i];
  checkFuncDecl(clone, sub);                          // re-checks under T:=i32
  gi->clonedFunc = clone;
  return gi;
}
```

Key points:

- **Dedup** is keyed on the mangled name, so `pick(3, 9)` and any other
  `pick` over `i32` share one instance.
- The **clone is re-checked from the pristine parse** — annotation fields
  on the template are still defaults, so the instance gets its own
  resolution (a `pick<string>` body resolves `<` to string comparison).
- `checkFuncDecl(clone, sub)` sets `subst` for the body check; recursive
  generic calls inside the body instantiate *more* instances (the depth
  counter guards runaway recursion), and `instanceOrder` keeps them in
  creation order.
- The call site is annotated: `call->genInstance = gi;` and the callee's
  `resolvedFunc` is the *template*.

### 5. Mangle

`Sema::mangleFuncName(tmpl, args)` produces the symbol. For the template
`pick` in module `main` with argument `i32`:

```
_C 4main 4pick G i32 E
```

See [abi.md](abi.md) for the full grammar. One subtlety visible in real IR:
the `I<params>E` section that encodes parameter types appears for
non-generic functions (`_C7prelude3sayIstringE`) but is **omitted** when a
template's parameter types can't resolve outside a substitution (they are
generic variables at mangling time) — instances are distinguished by the
`G...E` section alone.

### 6. Codegen

`Codegen::generate` declares every instance after the regular functions,
then emits bodies:

```cpp
// generic instances (instantiation may create more while emitting)
for (size_t i = 0; i < sema.instanceOrder.size(); i++) {
  GenericInstance *gi = sema.instanceOrder[i];
  declareFunc(gi->clonedFunc, gi->args);
}
```

and `emitCall` dispatches to the instance at each annotated call site.
Because `genericParams` was kept on the clone, `emitFuncBody` rebuilds the
substitution (`instanceSubst`) so `T`-typed fields/params map to the right
LLVM types during body emission.

### 7. The real IR

From `core emit-ir main.cr` (abridged):

```llvm
%core.struct.Pair = type { i32, i32 }

define i32 @_C4main4pickGi32E(i32 %a, i32 %b) {
entry:
  ...
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %if.then, label %if.end
  ...
}

define double @_C4main4pickGf64E(double %a, double %b) {
entry:
  ...
}

define ptr @_C6memory5allocIEGT4PairE() {
entry:
  %0 = call ptr @core_rt_alloc(i64 8)
  ...
}

define void @_C6memory4freeGT4PairE(ptr %p) { ... }

define i32 @main() {
entry:
  %call = call i32 @_C4main4pickGi32E(i32 3, i32 9)
  call void @_C7prelude3sayIi32E(i32 %call)
  %call1 = call double @_C4main4pickGf64E(double 1.500000e+00, double 5.000000e-01)
  ...
  %call2 = call ptr @_C6memory5allocIEGT4PairE()
  ...
}
```

Observations:

- Two *bodies* of `pick` exist, one per argument set, each compiled to the
  right comparison (`slt` vs. its double counterpart).
- `alloc<Pair>()` used the explicit-args path (`EIdent::typeArgs`): no
  inference needed; the mangled name is `_C6memory5allocIEGT4PairE`
  (`I` + `E` = empty parameter list, then `G` `T4Pair` `E`).
- `@main` returns `i32` (the C ABI wrapper — Core `main` returning `void`
  still emits `i32 0`).

## Generic structs, classes, enums

The same machinery at the type level: `Box<i32>` is
`TypeContext::getStruct(DStruct*, [i32])` — an interned type whose `decl`
is the template and whose `genericArgs` are the arguments. Field/method
types resolve through `Sema::substituteFieldType`/`subst`. Generic *classes*
also clone: their generic methods instantiate per class instance, and the
LLVM struct type name embeds the mangled arguments
(`structTypeFor`: `core.class.Box.` + `sema.mangleTypeForName(arg)`).

Generic enums (`Option<T>`) resolve tag/payload layouts per instance in
`Codegen::enumStorageType` (name like `core.enum.Option.i32`) — variant
constructors and `match` payloads are laid out with the substituted types.

## Common failure modes

- **Instance state leaking.** Anything cached per-`DFunc` (like
  `DFunc::checked`) must distinguish template vs. clones; the clone gets
  its own `checked` flag. If you add per-function caches, key them on the
  `DFunc*` you were given, and remember clones are distinct pointers.
- **Forgetting `subst` restoration.** `checkFuncDecl` and
  `instantiateGeneric` save/restore `subst`, `curFunc`, `curModule`, and
  scope state. Code paths that set `subst` without restoring corrupt every
  later resolution.
- **Calling the template instead of the instance.** Codegen must route
  through `c->genInstance` when set; adding a new call path that uses
  `resolvedFunc` directly will emit a call to a symbol that was never
  defined.
- **Static state.** `instantiateGeneric`'s `cloneArena` and depth counter
  are function-`static`s — one Sema per process is the current assumption
  (the driver creates exactly one).
