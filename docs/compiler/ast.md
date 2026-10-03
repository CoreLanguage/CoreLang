# The AST

`src/AST.h` defines every node the parser produces and all the fields Sema
fills in. `src/ASTClone.h` provides the deep cloner used to instantiate
generics. Nodes are plain structs with a `Kind` tag and a virtual
destructor — no visitor, no shared base beyond `Expr`/`Stmt`/`Decl`.

## The arena

Every node is allocated from `ASTContext`, a bump allocator:

```cpp
class ASTContext {
public:
  template <typename T, typename... Args> T *make(SourceLoc loc, Args &&...args) {
    void *mem = alloc(sizeof(T));
    T *n = new (mem) T(loc, std::forward<Args>(args)...);
    return n;
  }
  ...
private:
  std::deque<std::unique_ptr<char[]>> pool;
  static constexpr size_t CHUNK = 1 << 16;
```

Properties that matter to contributors:

- **No deallocation, ever.** The arena lives for the whole process
  (`Driver::ctx`). Nodes are never freed individually; destructors never
  run. This is also why `Sema::instantiateGeneric` can keep cloned bodies
  alive indefinitely (it uses its own static `ASTContext cloneArena`).
- **16-byte aligned chunks** (`alloc` rounds sizes up to 16).
- Nodes are destructively *mutable after construction*: Sema writes its
  annotations in place. The cloner exists precisely so that two
  instantiations of the same generic body do not share (and clobber) those
  annotations.

## Type syntax: `TypeExpr`

A `TypeExpr` is the *syntactic* type as written; Sema resolves it to a
`Type *` (see [type-system.md](type-system.md)).

```cpp
struct TypeExpr {
  enum Kind { Prim, Named, Array, Func } kind = Prim;
  int prim = 0;                          // Prim: index into PrimKind
  std::vector<std::string> nameParts;    // Named: dotted name
  std::vector<TypeExpr *> genericArgs;   // Named
  TypeExpr *elem = nullptr;              // Array
  Expr *arraySize = nullptr;             // Array (const-folded by sema)
  std::vector<TypeExpr *> paramTypes;    // Func
  TypeExpr *retType = nullptr;           // Func
  SourceLoc loc;
};
```

`ptr<T>` is just a `Named` TypeExpr named `ptr` with one generic argument —
Sema special-cases it; there is no dedicated node.

## Patterns

`Pattern` is used by `match` arms:

| Kind | Syntax | Meaning |
|---|---|---|
| `Wild` | `_` | matches anything |
| `Var` | `name` | binding (or unit variant — sema resolves capitalized names against the scrutinee's enum in `Sema::checkPattern`) |
| `Variant` | `Name(p1, p2)` | enum variant with payload bindings (subs may be `_`) |
| `Lit` | `5`, `"x"`, `'c'`, `true`, `null` | literal comparison |

Sema annotations: `enumDecl` (`DEnum*`), `variantTag`, `bindType`, and
`payloadTypes` (resolved payload types for `Variant`).

## Expressions and the sema annotation fields

`Expr` carries the *generic* annotation block that all checking writes and
codegen reads:

```cpp
struct Expr {
  enum Kind {
    IntLit, FloatLit, BoolLit, CharLit, StringLit, NullLit,
    Ident, Self, Unary, Binary, Assign, Cast, Call, Member, Index,
    StructLit, ArrayLit, Lambda, Match, Range, Sizeof, Alignof, UnsafeExpr,
  };
  Kind kind;
  SourceLoc loc;
  Type *type = nullptr;           // resolved type; set by Sema
  // --- sema annotations ---
  IdKind idKind = IdKind::Unresolved;
  void *target = nullptr;         // DGlobal*/DConst*/DFunc*/ModuleSema*/Decl*
  int enumTag = -1;               // EnumConst: variant tag
  int builtin = 0;                // Builtin enum
  MemberKind memberKind = MemberKind::Unresolved;
  int memberIndex = -1;           // field index (vptr-aware) / variant index
  void *viaModule = nullptr;      // ModuleSema* when member crosses modules
  void *resolvedFunc = nullptr;   // DFunc* chosen overload
  void *genInstance = nullptr;    // GenericInstance* for generic calls
  int ifaceMethodIndex = -1;
  int castKind = 0;               // CastKind
  int binKind = 0;                // BinKind
  int unKind = 0;                 // UnKind
  bool checkBounds = true;        // EIndex bounds check
  bool baseSelfCall = false;      // ECall: BaseName.method(...) uses caller's self
  void *scopeId = nullptr;        // Scope* where an EIdent local resolved
};
```

Field-by-field, who writes and who reads:

| Field | Written by | Read by |
|---|---|---|
| `type` | `Sema::checkExpr` and friends | everyone |
| `idKind` | `Sema::checkExpr` (`Ident` case) | codegen `emitExpr`/`emitLValue`, sema itself |
| `target` | sema, per `idKind` (`DGlobal*`, `DConst*`, `DEnum*`, `ModuleSema*`, `Decl*`) | codegen |
| `enumTag` | `checkExpr` / `checkVariantCtor` | codegen (variant tag constant) |
| `builtin` | `checkCall` then `checkBuiltinCall` | `emitBuiltinCall` |
| `memberKind` | `checkMemberForRead` / `checkCall` | codegen dispatch (field vs method vs interface vs variant) |
| `memberIndex` | sema (`fieldIndexOf`, variant index; `-2` marks the array-repeat form on `EArrayLit`) | codegen GEP indices |
| `viaModule` | `checkMemberForRead` | codegen (globals across modules) |
| `resolvedFunc` | `resolveOverload` | codegen `declareFunc`/direct calls |
| `genInstance` | `instantiateGeneric` callers | codegen (call the cloned, mangled instance) |
| `ifaceMethodIndex` | `checkCall` (interface callee) | codegen itable slot |
| `castKind` | `checkCast` | `emitCast` |
| `binKind` | `checkBinary` | `emitBinary` |
| `unKind` | `checkExpr` (unary) | `emitExpr` |
| `checkBounds` | `checkExpr` (`Index` case: false inside `unsafe`) | `emitLValue` |
| `baseSelfCall` | `checkCall` (`BaseName.method`) | `emitCall` (uses current `self`) |
| `scopeId` | `checkExpr` (local resolution) | sema capture analysis |

The enums:

```cpp
enum class IdKind { Unresolved, Local, Global, ConstVal, Func, EnumConst,
                    Module, TypeRef, Builtin };
enum class MemberKind { Unresolved, Field, Method, StaticMethod, ModuleMember,
                        IfaceMethod, VariantOf };
enum class CastKind { None, Identity, IntToInt, IntToFloat, FloatToInt,
                      FloatToFloat, PtrToPtr, IntToPtr, PtrToInt, EnumToInt,
                      IntToEnum, BoolToInt, IntToBool, ClassUp, ClassDown,
                      IfaceWrap, IfaceUnwrap, ToNever };
enum class BinKind { None, Arith, Cmp, StrConcat, StrCmp, PtrArith, PtrDiff,
                     PtrCmp, ShortCircuit, Bitwise, Shift, EnumCmp, VecArith };
enum class UnKind { None, Neg, Not, BitNot, Deref, Ref };
```

`CastKind`/`BinKind`/`UnKind` are stored as `int` (set via casts like
`(int)CastKind::IntToInt`) so `ASTClone.h::copyId` can copy the whole
annotation block field-wise.

### Node inventory

| Node | Extra fields | Notes |
|---|---|---|
| `EInt` | `value`, `digits`, `big128` | `digits` keeps the full text for 128-bit literals |
| `EFloat`, `EBool`, `EChar`, `EString`, `ENull` | payload value | `EString.value` is decoded bytes |
| `EIdent` | `name`, `typeArgs` | `typeArgs` set for `Option<i32>` style names |
| `ESelf` | — | `self` in methods |
| `EUnary` | `op`, `operand` | `-`, `!`, `~`, `*`, `&` |
| `EBinary` | `op`, `lhs`, `rhs` | |
| `EAssign` | `op`, `target`, `value` | `"="` (or empty) or compound (`+=` and friends) |
| `ECast` | `e`, `ty` | `as` casts |
| `ECall` | `callee`, `args` | builtin, function, method, variant ctor, closure call |
| `EMember` | `obj`, `name` | field/method/variant/module access |
| `EIndex` | `base`, `index` | arrays, strings, pointers |
| `EStructLit` | `ty`, `fields` | `Name { field: value, ... }` |
| `EArrayLit` | `elems`, `repeat` | `[1, 2, 3]` or `[0; 10]` |
| `ELambda` | `params`, `retType`, `body`, `captures`, `closureType` | closure type is `TypeKind::Func` |
| `EMatch` | `scrutinee`, `arms` (`MatchArm` with `pattern` and `body`) | expression |
| `ERange` | `lo`, `hi`, `inclusive` | only valid in `for ... in` |
| `ESizeof`/`EAlignof` | `ty` | folded by codegen via DataLayout |
| `EUnsafeExpr` | `stmts` | value = last statement's expression |

## Statements

```cpp
struct Stmt {
  enum Kind { KExpr, KLet, KReturn, KIf, KWhile, KFor, KForIn,
              KBreak, KContinue, KSwitch, KBlock, KUnsafe };
```

`SLet` is the most annotated statement, because it models both declaration
and Go-style assignment:

```cpp
struct SLet : Stmt {
  bool isMut = false;
  bool isConst = false;
  bool isDeclOrAssign = false; // `x = expr`: new variable if x not in scope
  std::string name;
  TypeExpr *type = nullptr; // may be null (inferred)
  Expr *init = nullptr;
  Type *resolvedType = nullptr;    // declared/assigned type (sema)
  bool isAssignExisting = false;   // `x = e` resolved as assignment to existing var
  void *assignGlobal = nullptr;    // DGlobal* when assigning a global
};
```

`SForIn` covers both `for x in a..b` (its `iterable` is an `ERange`) and
`for x in array`. `SIf::elseBlock` is an `SBlock` or a nested `SIf` (else-if
chains). `SSwitch::Case` holds a list of values (comma-separated case
labels) plus an `SBlock` body.

## Declarations

```cpp
struct Decl {
  enum Kind { Func, Struct, Class, Interface, Trait, Enum, Global, Const,
              Import, Extern };
```

- `DFunc` — `genericParams`, `params` (`Param` with `name`, `type`,
  `defVal`, `loc`), `retType` (null means void), `body` (null for
  prototypes), the modifier flags (`isPub`/`isExtern`/`isStatic`/
  `isVirtual`/`isOverride`/`isAbstract`/`isUnsafe`/`isVariadic`), `linkName`
  from `@link_name`, `checked` (guards against double-checking during
  generic instantiation), and `parent` (the enclosing `DStruct`/`DClass`/
  `DInterface`/`DEnum` for methods).
- `DStruct` / `DClass` — fields, methods, `genericParams`; classes add
  `base` and `interfaces` plus `isAbstract`. `@packed` sets
  `DStruct::isPacked`.
- `DInterface` — `isTrait` distinguishes `interface` from `trait` (traits
  may carry default method bodies).
- `DEnum` — `EnumVariant` list (`name`, `payloadTypes`, `loc`).
- `DGlobal` — `isMut`, `isTLS` (`tls` globals), `init` (must be
  const-foldable).
- `DConst` — compile-time constant; `folded` caches the LLVM constant.
- `DImport` — `parts` (dotted module path) + optional `alias`.
- `DExtern` — wraps a body-less `DFunc*` `proto` marked `isExtern`.

A parsed file is a `SourceUnit { fileID, path, decls }`.

## Deep cloning (`ASTClone.h`)

`ASTCloner` implements `type`, `pattern`, `expr`, `stmt`, and `funcBody`
(from/to `DFunc`). It is used *only* by `Sema::instantiateGeneric`: each
monomorphized instance gets a private copy of the template body so that
per-instance sema annotations (types, resolved calls) don't leak between
instantiations.

The single subtle part is `copyId`, which copies the sema annotation block:

```cpp
void copyId(Expr *dst, Expr *src) {
  dst->type = src->type;
  dst->idKind = src->idKind;
  ...
}
```

Instantiation clones the *pristine parse* of the template (whose
annotations are still defaults) and re-checks the clone from scratch under
a substitution map via `checkFuncDecl(clone, sub)`. That is deliberate:
cloning an already-checked body would copy template-time annotations (with
generic variables) into the instance.

## Common failure modes

- **Adding an `Expr` kind and missing the cloner.** `ASTCloner::expr`
  switches over every kind and returns `nullptr` for unhandled ones, which
  surfaces later as a null-body instance. Update `ASTClone.h` in the same
  commit as `AST.h`.
- **Adding a sema field and missing `copyId`.** New annotation fields must
  be copied in `copyId`, or generic instances will lose them.
- **Reusing `memberIndex` semantics.** For classes it counts the vptr as
  field 0 (`Sema::fieldIndexOf` starts at `cl->polymorphic ? 1 : 0`); for
  structs it is the plain field index; on `EArrayLit` the value `-2` marks
  the repeat form. A fourth meaning will break codegen GEPs.
- **Allocating with `new`.** AST nodes must come from `ctx.make<T>(...)`;
  a handful of Sema-internal temporaries (the `EInt` results of const
  folding, the `ESelf` synthesized for base calls, the `DFunc pseudo` used
  to type lambda bodies) use plain `new` deliberately and leak by design —
  don't copy that pattern for persistent nodes.
