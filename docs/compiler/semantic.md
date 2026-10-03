# Semantic analysis

`src/Sema.h` / `src/Sema.cpp` (about 3,300 lines) do everything between the
AST and codegen: module registration, name resolution, type checking,
overload resolution, access control, capture analysis, const evaluation,
exhaustiveness and return-path analysis, and generic monomorphization.
Codegen later *reads* the annotations Sema wrote (see
[ast.md](ast.md)) and never re-resolves anything.

The class is one flat object:

```cpp
class Sema {
public:
  bool registerModules(std::vector<ModuleSema *> &mods);
  bool checkAll();
  bool checkEntry(ModuleSema *m);

  std::vector<ModuleSema *> modules; // dependency order, prelude first
  std::map<DFunc *, ModuleSema *> funcModule; // defining module per function
  std::map<Decl *, ClassLayout *> layouts;
  std::map<std::pair<DFunc *, std::string>, GenericInstance *> instances;
  std::vector<GenericInstance *> instanceOrder;
  ModuleSema *prelude = nullptr;
  ...
```

Checking runs in three phases, invoked from `Driver::runPipelineInternal`:
`registerModules` → `checkEntry` → `checkAll`.

## Phase 1: registration (`registerModules`)

The driver hands Sema the modules in topological order (prelude first).
Registration walks every declaration and fills the defining module's
symbol table:

```cpp
// One resolved .cr module (file).
struct ModuleSema {
  std::string name;      // dotted module name, e.g. "utils.math"
  std::string path;      // canonical file path
  SourceUnit *unit = nullptr;
  bool isPrelude = false;

  std::map<std::string, Decl *> types;                 // struct/class/interface/trait/enum
  std::map<std::string, std::vector<DFunc *>> funcs;   // overload sets
  std::map<std::string, DGlobal *> globals;
  std::map<std::string, DConst *> consts;
  std::vector<std::pair<std::string, ModuleSema *>> imports; // name -> module
};
```

- Duplicates are errors *within one module*: `duplicate type 'X' in module
  'Y'`, `duplicate global`, `duplicate constant`. Two modules may both
  export the same name; imports pick one.
- Functions land in `funcs[name]`, a `std::vector<DFunc *>` — every name
  maps to an **overload set** (`extern` declarations included).
- `funcModule` and `declModule` remember the defining module of each
  function/type so methods can be checked with the right module context.
- After all types are registered, `buildLayout` runs for every class (base
  chains may cross modules, so this needs two passes).

## Symbol lookup and access control

Lookup is a three-step chain, always in this order:

```cpp
Decl *Sema::lookupTypeVisible(const std::string &name, ModuleSema **via) {
  if (Decl *d = lookupTypeOwn(name)) return d;         // 1. own module
  for (auto &[iname, mod] : curModule->imports) { ... } // 2. imports, in order
  if (prelude && prelude != curModule) { ... }          // 3. the prelude
  return nullptr;
}
```

`lookupFuncsVisible` is analogous (it collects the full overload set from
the first scope that has any). `Sema::visible` is the access-control rule:

```cpp
bool Sema::visible(Decl *d, ModuleSema *from) {
  bool isPub = false;
  switch (d->kind) {
  case Decl::Func: isPub = ((DFunc *)d)->isPub; break;
  ... // every Decl kind has an isPub flag
  }
  (void)from;
  return isPub;
}
```

Everything is private to its module unless written `pub`; the `from`
parameter is currently unused (no friends, no package-internal visibility).
Non-`pub` members are simply invisible: referencing one produces
`unknown name 'x'` or, on a module-qualified access,
`module 'm' has no public member 'x'` / `function 'm.f' is not public`.

Class *member* access (fields/methods) has a second axis: class members
default to private and `pub` on the member makes them public; struct
members are public by default.

## Phase 2: entry check (`checkEntry`)

The entry file must define exactly one `main`, with no parameters and a
`void` or `i32` return. Anything else is rejected before checking begins
(so a broken `main` doesn't cascade into thousands of follow-on errors).

## Phase 3: checking (`checkAll`)

`checkAll` walks all modules and checks every non-generic function body via
`checkFuncDecl` (generic templates are checked lazily per instantiation);
globals and consts get their initializers checked. `checkFuncDecl`
save/restores the whole checking context around each body — this is what
makes recursion into `instantiateGeneric` safe:

```cpp
ModuleSema *savedModule = curModule;
DFunc *savedFunc = curFunc;
Type *savedRet = curReturnType;
auto *savedSubst = subst;
Scope *savedScope = curScope;
int savedUnsafe = unsafeDepth, savedLoop = loopDepth;
```

It also installs the implicit `self` local (a pointer, from
`Sema::selfTypeOf`) for instance methods, checks default arguments, and —
after `checkBlock` — runs the return-path analysis described below.

### Scopes

Locals live in a linked list of `Scope` structures (`Sema::Scope`, with
`LocalVar{type, isMut, isConst, declLoc, declScope, declDepth}`).
`canReadVar` walks the chain. `declDepth` (the block nesting depth at
declaration time) is the basis of lambda capture analysis.

## Overload resolution (`resolveOverload`)

Resolution is scoring-based. A candidate is viable if the call's arity
works (exact, variadic, or a fully-defaulted tail); viable candidates get a
score:

- **2** — every argument's type already matches the parameter exactly
  (`tc.same`).
- **1** — a match was found through any *coercion*:
  - generic unification (`unifyTypes` binds type variables),
  - constant-foldable argument fitting an in-range integer parameter
    (`evalConstUint` + `primBits` range check),
  - integer/float literal fitting (`stripNeg` handles `-5`),
  - class upcast or class→implemented-interface,
  - `ptr<[T; N]>` → `ptr<T>` (array decay),
  - `string` → `ptr<char>` (FFI),
  - `ptr<Derived>` → `ptr<Base>`.

```cpp
int score = 2;
bool matches = true;
quiet_++;
... // per-argument checks; coercions do: score = std::min(score, 1);
subst = savedSubstRO;
quiet_--;
if (!matches) continue;
if (score > bestScore) { bestScore = score; best = f; tie = false; }
else if (score == bestScore && best != nullptr) tie = true;
```

Outcomes:

- One best candidate → chosen; `ECall::resolvedFunc` and the callee's
  `idKind`/`resolvedFunc` are annotated.
- Tie among scores → `call to 'f' is ambiguous` with the help text
  `add explicit casts to clarify argument types`.
- No viable candidate → `no matching function 'f' for arguments (types...)`
  plus `candidates take: f(...) | f(...)` — signatures are re-rendered
  under `quiet_` so template probes don't emit noise.

Note the checking order: **arguments are checked before resolution**
(`checkExpr` on each `arg`), because scoring needs argument types.

## Assignment and the "no implicit conversions" rule

`typesAssignable(dst, src, srcExpr, loc, what)` is the single gate for
initialization, assignment, argument passing and returns. It allows: exact
sameness, `never` sources, `null` into pointers, literal fitting
(an unsuffixed integer literal fits any integer type whose range contains
it; float literals fit `f32`/`f64`; integer literals fit floats), class
upcasts, pointer upcasts, class→interface, array-decay and string→`ptr<char>`.
Everything else fails with

```
error: cannot assign value 'i32' to 'f64'
   = help: use an explicit cast: `value as f64`
```

(when both sides are numeric). Literals are *mutated in place* on success
(`lit->type = dst`) so codegen emits the right LLVM constant.

## Const evaluation

`Sema::constFold` is a small, total evaluator over `EInt`/`EFloat`/
`EBool`/`EChar`/`EString`/`ENull` literals, unary `-`, and integer binary
`+ - * / % & | ^ << >>` (division/modulo by zero yields no fold, shifts
clamp to `c < 64`). `evalConstUint` wraps it and is the arbiter for:

- array sizes (`resolveType`: `array size must be a compile-time constant`),
- array-repeat counts,
- const-foldable call arguments in overload scoring, and
- `isConstFoldable` (used when re-typing range bounds in `for i in a..b`).

`sizeof`/`alignof` are *not* folded by Sema — they need the LLVM
`DataLayout`, so `Codegen::evalConst` computes them, and `constFold`
deliberately returns `nullptr` for them. `DConst::folded` caches the
resulting LLVM constant per constant declaration.

## Capture analysis (lambdas)

Lambdas capture enclosing locals **by value**. During `checkExpr` of an
`EIdent` (or `self`), Sema consults the stack of lambdas currently being
checked:

```cpp
void Sema::recordCapture(const std::string &name, Type *type, void *declScope) {
  // Scopes carry depths: a local whose declaration depth is shallower than a
  // lambda's base depth lives outside that lambda and must be captured.
  for (auto &[lam, baseDepth] : lambdaStack) {
    if (!canReadVar(name, lv)) return;
    if (lv.declDepth < baseDepth) {
      ... lam->captures.push_back({name, type, nullptr});
    }
  }
}
```

`lambdaStack` holds `(ELambda *, base depth)` pairs pushed by
`checkExpr(Lambda)`; a local declared at a shallower depth than a lambda's
base is a capture. `ELambda::captures` is what codegen turns into the
closure environment struct (see [codegen.md](codegen.md)).

## Return-path analysis (`terminates` / `containsBreak`)

After checking a body, `checkFuncDecl` calls `Sema::terminates(body)`:

- `return` terminates; `break`/`continue` do **not** (they exit the loop,
  not the function).
- `if` terminates only when both branches exist and terminate.
- `while true` terminates only if the body has no reachable `break`
  (`containsBreak`).
- `switch` terminates only with a `default` where every case terminates.
- A block terminates if any statement in it terminates.
- An expression statement whose type is `never` terminates (calls to
  `never`-returning functions like `panic` or `exit`).

The *use* of the result implements the language's fall-through rule:

```cpp
if (!terminates(f->body)) {
  Type *rt = curReturnType;
  bool numericish = rt->isNumeric() || rt->isBool() || rt->isPtr() || rt->isString()
                    || rt->isEnum() || rt->isVector() || rt->isVoid();
  if (rt->isNever()) { /* "must not return normally" error */ }
  else if (!numericish) {
    // "can reach the end of its body without returning a '<T>'"
  }
}
```

Scalar-ish returns implicitly return their default value (codegen's
`emitDefaultValue`); aggregates require an explicit `return`.

## Match checking (`checkMatch` / `checkPattern`)

Per arm: `checkPattern` resolves the pattern against the scrutinee type
(unit variants become `Variant` tags, bindings collect `(name, type)`
pairs, payload types resolve under the enum's generic substitution), the
body is checked with bindings in scope, and the arm's value type is the
type of its last expression statement. Arm types are unified (with literal
refitting through `typesAssignable`). For enum scrutinees without a `_`
arm, exhaustiveness is enforced by tag coverage:

```
error: match on enum 'Color' is not exhaustive; missing: Blue
   = help: add the missing variants or a `_` wildcard arm
```

## Diagnostics approach

- Errors are emitted through `Diagnostics` and counted; checking always
  *continues* past an error (using `Type::Invalid` as a poison value that
  suppresses cascades: `typesAssignable` accepts `Invalid` silently, and
  `tc.invalid()` is returned as the type of failed expressions).
- Speculative work (overload probing, signature rendering) increments
  `Sema::quiet_`; `resolveType` and error call sites check it.
- Every error carries a `SourceLoc` and usually a `help` string with the
  concrete fix — see [diagnostics.md](diagnostics.md).

## Common failure modes

- **Forgetting a `Decl` kind in `visible()`** — new declarations silently
  become module-private.
- **Checking generic templates eagerly.** `checkAll` skips functions with
  `genericParams`; checking a template body outside instantiation would
  resolve its type variables to standing `genericVar`s and pollute
  `instances`.
- **Order dependence of literal types.** `EIdent`/`EInt` get default types
  at first check (`i32`, `f64`, ...); code that re-checks an expression
  must reset `e->type` first (see `refit` in `checkBinary`).
- **`self` availability.** `selfTypeOf` returns non-null only for
  class/struct instance methods; interface/trait prototypes get `self`
  differently (the fat pointer's data pointer), so code that assumes `self`
  exists for every `parent != nullptr` function breaks interface dispatch.
- **The depth-64 instantiation limit.** Deeply mutually-recursive generics
  stop at `generic instantiation depth exceeded (64)`; this is intentional
  protection against unbounded monomorphization.
