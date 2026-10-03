# Code generation (LLVM IR)

`src/Codegen.h` / `src/Codegen.cpp` lower the sema-annotated AST to a
single `llvm::Module` using `llvm::IRBuilder<>`. There is no intermediate IR of our
own — Core AST statements map directly to IRBuilder calls — and no
LLVM *pass* is registered by the compiler; optimization is entirely the
PassBuilder default pipeline (see [driver.md](driver.md)).

```cpp
struct CodegenOptions {
  int optLevel = 0;            // 0..3
  bool sizeOpt = false;        // -Os
  bool debugInfo = false;      // --debug / -g
  std::string targetTriple;    // empty = host
  bool freestanding = false;
  std::string entryName = "main";
};
```

`Codegen::generate(module, entryModule)` runs three steps: (1) declare
everything, (2) emit all function bodies (generic instances last — emitting
a body may instantiate more), (3) finalize debug info.

## Setting the DataLayout first

```cpp
bool Codegen::generate(llvm::Module &module, ModuleSema *entryModule) {
  mod = &module;
  { // set the data layout up front so sizeof/alignof see target-true values
    ...
    module.setTargetTriple(tripleStr);
    module.setDataLayout(tm->createDataLayout());
  }
```

This is why `sizeof(T)`/`alignof(T)` are target-true even at `-O0`:
`Codegen::evalConst` computes them from
`mod->getDataLayout().getTypeAllocSize(lt)` /
`getABITypeAlign(lt).value()`. The driver re-sets the DataLayout again
after creating the *real* target machine (same result).

## LLVM type mapping (`Codegen::llvmType`)

| Core type | LLVM type |
|---|---|
| `void`, `never` | `void` |
| `bool` | `i1` |
| `char`, `i8`, `u8` | `i8` |
| `i16/u16` → `i16`, `i32/u32` → `i32`, `i64/u64` → `i64` | integers |
| `i128`, `u128` | `i128` |
| `usize`, `isize` | `i64` |
| `f32` / `f64` | `float` / `double` |
| `string` | `%core.string = type { ptr, i64 }` (ptr, byte length) |
| SIMD `f32x4` … `u64x2` | `<4 x float>`, `<2 x double>`, `<4 x i32>`, `<2 x i64>`, `<16 x i8>`, `<8 x i16>` |
| `ptr<T>` | `ptr` (opaque, addrspace 0) |
| `[T; N]` | `[N x T]` |
| `struct`/`class` | named struct `%core.struct.X` / `%core.class.X` (see below) |
| interface value | `{ ptr, ptr }` fat pointer (object, itable) |
| `enum` (no payloads) | `i32` (the tag) |
| `enum` (with payloads) | named struct `%core.enum.X` wrapping a byte array |
| closure/function value | `{ ptr, ptr }` (fn pointer, env pointer) |

## String layout

`stringType()` creates `%core.string` lazily as
`{ PointerType, i64 }`. String literals become private, NUL-terminated
global byte arrays plus a `ConstantStruct {ptr, len}`:

```cpp
ArrayType *at = ArrayType::get(builder.getInt8Ty(), bytes.size() + 1);
auto *gv = new GlobalVariable(*mod, at, true, GlobalValue::PrivateLinkage,
                              ConstantDataArray::getString(ctx, bytes, true), ".str");
llvm::Constant *ptr = ConstantExpr::getBitCast(gv, PointerType::get(ctx, 0));
return ConstantStruct::get(st, {ptr, ConstantInt::get(builder.getInt64Ty(), bytes.size())});
```

`+` on strings calls `core_rt_str_concat` (malloc in the runtime);
comparisons call `core_rt_str_eq` / `core_rt_str_cmp`; `len(s)` extracts
element 1; `s[i]` GEPs the data pointer with a dynamic bounds check.

## Enum layout: tag + byte-array payload

Simple enums are just `i32`. Data-carrying enums use
`enumStorageType`: a named LLVM struct wrapping one byte array sized to
hold a 4-byte tag plus the largest variant's payload, ABI-aligned:

```cpp
unsigned off = 4;
if (maxAlign > 4) off = (4 + maxAlign - 1) / maxAlign * maxAlign;
unsigned total = hasPayload ? off + maxExtent : 4;
...
st = StructType::create(ctx, ArrayType::get(builder.getInt8Ty(), total), name);
```

For the example `enum Shape { Circle(f64), Rect(f64, f64), Unit }` the IR
is `%core.enum.Shape = type { [24 x i8] }`: tag at offset 0, first payload
at offset 8 (f64 alignment), second at 16. `emitVariantCtor` stores the tag
at offset 0 and each payload at its aligned offset; `emitMatch` loads the
tag via a bitcast and extracts payloads the same way. The default value is
variant 0.

## Struct and class layout

`structTypeFor` builds named structs (`core.struct.Name` /
`core.class.Name`, generic args appended via `sema.mangleTypeForName`):

- **Classes**: if `ClassLayout::polymorphic` (any `virtual`/`abstract`
  method, an abstract class, or a polymorphic base), field 0 is the vptr:

  ```cpp
  if (t->isClass()) {
    ClassLayout *cl = sema.layoutOf(d);
    if (cl && cl->polymorphic) fields.push_back(PointerType::get(ctx, 0)); // vptr
  }
  ```

  Fields then follow in *base-chain order* (most-base first), matching
  `Sema::fieldIndexOf` which counts the vptr as index 0. Real example:
  `%core.class.Dog = type { ptr, %core.string }` — vptr, then `name`.
- **Structs**: plain field order; `@packed` forwards `isPacked` to
  `StructType::create(..., packed)` so LLVM uses size-1 alignment.
- Field types for generic aggregates are resolved under the instance's
  substitution (`sema.fieldTypeOf`).

`fieldGEPIndex` is the identity function — sema's `memberIndex` and the
LLVM field index agree by construction (both put the vptr at 0).

## Vtables and itables

`Sema::buildLayout` computes `vtableOrder` (base slots first; an override
keeps the base's slot; new virtuals append) and `vtableSlots` (name →
slot). Codegen materializes two kinds of globals, both `LinkOnceODRLinkage`
so duplicates across modules fold at link time:

```cpp
// vtableFor: one slot per virtual method, in layout order
std::string name = "_CV" + sema.mangleTypeForName(tc.getClass(c, {}));
// itableFor: one slot per interface method (trait default bodies allowed)
std::string name = "_CI" + sema.mangleTypeForName(tc.getClass(c, {})) + "_" +
                   sema.mangleTypeForName(tc.getInterface(iface));
```

Real IR: `@_CVT3Dog = linkonce_odr constant [1 x ptr] [ptr
@_C6shapes3Dog5speakIE]` and
`@_CIT3Dog_X9Speakable = linkonce_odr constant [1 x ptr] [...same...]`.

Dispatch:

- **Virtual calls** (`emitCall`, `MemberKind::Method`): a call is virtual
  when *the static type's* vtable contains the method name; codegen loads
  the vptr from `self`, loads slot `cl->vtableSlots.at(f->name)`, and calls
  indirectly (`call.virtual`).
- **Interface calls** (`MemberKind::IfaceMethod`): the fat pointer is
  split (`extractvalue` 0/1), slot `ifaceMethodIndex` is loaded from the
  itable, and the object pointer is passed as the first argument
  (`call.iface`).
- `obj as Interface` (`CastKind::IfaceWrap`) builds the fat pointer
  `{object address, itable}`; the unsafe `IfaceUnwrap` extracts element 0.
- Constructors (`init`) store their class's vtable into `self` on every
  exit (`emitVptrStoreIfInit`), so a derived init running after the base
  init overwrites the base's — the most-derived vtable wins.

## Closures and the uniform calling convention

Every function *value* — a lambda or `&someFunction` — is a two-word
struct `{fn ptr, env ptr}` (`TypeKind::Func`):

```cpp
// Every function VALUE uses the uniform closure ABI: {fn ptr, env ptr}.
// The callee's first parameter is the env pointer (ignored by plain
// functions, holding captured variables for lambdas).
llvm::Value *Codegen::makeClosureForFunction(DFunc *f);
llvm::Value *Codegen::makeClosureValue(llvm::Function *fnPtr, llvm::Value *env);
```

- A lambda compiles to an internal function whose signature is
  `(env: ptr, params...)`, named after its enclosing function
  (`<enclosing symbol>.lambda<N>`).
- Captured variables are laid out in an env struct (one slot per
  `ELambda::Capture`, in capture order) and copied **by value** into a
  `core_rt_alloc`-allocated environment at closure-creation time; inside
  the body, captured names resolve to GEP'd addresses of the env.
- Referencing a plain function as a value wraps it in a *trampoline*
  (`<symbol>.closure`, internal linkage) that forwards all arguments after
  the env — so plain functions and lambdas have identical call sites:
  extract fn and env, pass `{env, args...}`.

Calls through function-typed locals/parameters and callee *expressions*
all go through the same path in `emitCall` (the `call.closure` sites).

## Bounds checks

`checkExpr` sets `EIndex::checkBounds = (unsafeDepth == 0)` for arrays and
strings. Codegen then emits the check inline:

```cpp
void Codegen::emitBoundsCheck(llvm::Value *idx, long long len, SourceLoc loc) {
  ...
  cond = builder.CreateICmpULT(idx, ConstantInt::get(cast<IntegerType>(idx->getType()), len));
  ...
  builder.CreateCall(rtFunc("core_rt_index_failed", builder.getVoidTy(),
                            {stringType(), builder.getInt64Ty(), builder.getInt64Ty(),
                             builder.getInt64Ty()}),
                     {makeStringConst(file), ConstantInt::get(builder.getInt64Ty(), loc.line),
                      builder.CreateSExt(idx, builder.getInt64Ty()),
                      ConstantInt::get(builder.getInt64Ty(), len >= 0 ? len : 0)});
  builder.CreateUnreachable();
```

Arrays use the compile-time length; strings compare against the runtime
`len` field (`ICmpULT` on zero-extended index). The runtime prints
`panic: index %lld out of bounds for length %llu (file:line)` and aborts.
Pointer indexing (`p[i]`) never checks bounds.

## Builtins

`emitBuiltinCall` lowers the compiler-implemented functions (user
functions may shadow them — `checkCall` only treats a name as a builtin
when `lookupFuncsVisible(name).empty()`):

- **Atomics** — `atomic_load/store/add/sub/swap/cas` and `atomic_fence`
  become LLVM loads/stores/`AtomicRMWInst`/`AtomicCmpXchgInst`/fence with
  `AtomicOrdering::SequentiallyConsistent`. `bool` cells use `i8`
  (LLVM requires byte-sized atomics) and convert at the boundaries.
  `atomic_cas` returns the old value (element 0 of the cmpxchg).
- **Volatile** — `volatile_load`/`volatile_store` are plain load/store with
  `setVolatile(true)`; sema requires an `unsafe` block.
- **Inline asm** — `asm(template, constraints, args...)` /
  `asm_volatile`: template and constraints must be string *literals*
  (extracted via `stringConstantBytes`); arguments widen to `i64`
  (`ZExt` for narrow ints, `PtrToInt` for pointers); result is `u64`;
  built with `llvm::InlineAsm::get` (LLVM syntax, AT&T on x86).
- **SIMD** — `splat_<T>` (`CreateVectorSplat`), `simd_extract_<T>`
  (`CreateExtractElement`), `simd_replace_<T>` (`CreateInsertElement`);
  lane-wise `+ - * /` come through `BinKind::VecArith` as `FAdd`/`Add` etc.
- **Introspection** — `len` (array → constant, string → extract 1),
  `source_file()` / `source_line()` (from the `ECall`'s `SourceLoc`).

## Statements and control flow

Each statement kind has a direct lowering in `emitStmt`: allocas for
locals (`SLet`, honoring `isAssignExisting` for Go-style reassignment),
`if.then/else/end` blocks, `while.cond/body/end`, `for.cond/body/step/end`,
for-in over ranges (`ICmpSLE` when `..=`, else `ICmpSLT`; step +1) and
arrays (index var + GEP), `switch` on an `i32` (`SwitchInst`, cases never
fall through), and `break`/`continue` via `breakStack`/`continueStack`
pairs of `{target, continueTarget}` blocks, each followed by an unreachable
`post.break`/`post.cont` block for dead code. `return` also creates a dead
`post.ret` block so code after it still typechecks in the builder.

The function epilogue implements implicit default returns: functions whose
body can fall through return `emitDefaultValue(retTy)` (zero, `null`,
`"" = {null, 0}`, variant 0, …). Entry `main` is special-cased to
`return i32 0` for the C ABI.

## Common failure modes

- **Verifier trips.** `Driver::runPipelineInternal` runs `verifyModule`
  right after generation; a bad GEP index or non-terminated block fails
  there with LLVM's message. Reproduce with `CORE_DUMP_IR=1` to dump IR.
- **Forgetting `emitDebugLoc`** on new statement kinds — code compiles but
  steps oddly in GDB (see [debugging.md](debugging.md)).
- **Mangling drift.** `funcSymbol` calls `sema.mangleFuncName` with
  `sema.curModule` temporarily set to the *defining* module; bypassing
  `funcSymbol` breaks cross-module references.
- **Aggregate loads are copies.** `emitExpr(Ident)` loads the whole value
  for aggregates (by-value semantics); if you add an optimization that
  reuses the loaded address, you break value semantics for structs.
- **Enum storage assumptions.** Payload offsets are computed, not stored:
  any change to `variantExtent`/`enumStorageType` must keep `emitVariantCtor`,
  `emitMatch` (payload extraction), and `emitDefaultValue` (variant 0) in
  sync.
