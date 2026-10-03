# Adding a feature, end to end

This is the checklist for the most common kinds of compiler change. Every
path follows the same pipeline — **Lexer → Parser → AST → Sema → Type →
Codegen → Driver/Project → Tests → Docs** — so first, the generic recipe.

## The generic recipe

1. **Design check.** Does the feature need a new token, a new AST node, a
   new type, or only new checks? Reuse before adding.
2. **Lexer** (`src/Lexer.cpp`) — keywords/punctuators only.
3. **Parser** (`src/Parser.cpp`) — produce AST; never resolve names.
4. **AST** (`src/AST.h`) — new node/fields; update `src/ASTClone.h` in the
   same commit.
5. **Sema** (`src/Sema.cpp`) — annotate; reuse `typesAssignable` /
   `resolveOverload` / `checkBuiltinCall` as appropriate.
6. **Type** (`src/Type.h/.cpp`, `src/Prims.def`) — only for type-system
   features.
7. **Codegen** (`src/Codegen.cpp`) — consume annotations; pick LLVM
   equivalents.
8. **Driver** (`src/Driver.cpp`) — only if flags/files change.
9. **Tests** (`tests/run_tests.py`) — positive *and* negative.
10. **Docs** (`docs/`) and, if user-visible, `std/` + the language summary
    (`docs/LANGUAGE-REFERENCE-SUMMARY.md` is the ground truth; update it).

Run `python3 tests/run_tests.py --core build/core` after each stage.

---

## Add a keyword

Example: a hypothetical `foreach` alias.

1. `src/Lexer.cpp`: add `"foreach"` to `keywordTable[]` (order in the table
   is irrelevant). It now lexes as `Tok::Kw` and can no longer be a
   variable name — check `std/` and `examples/` for collisions first.
2. `src/Parser.cpp`: handle `atKw("foreach")` in `parseStatement` (or
   `parseTopDecl` for declaration keywords). Use `eatKw`/`expectKw`, and
   `errorAt` (never `diag.error` directly) so speculative parsing stays
   quiet.
3. If it's a statement terminator boundary (like `func` bodies ending at
   declaration starters), add it to the prototype-boundary list in
   `parseFuncRest`.
4. Tests: `expect_output(w, "func main() { foreach ... }", [...])`.
5. Docs: language reference + this file stays.

Pitfall: the lexer makes keywords reserved forever; prefer sugar over new
keywords when possible (see how `say` works without being a keyword).

## Add an operator

Example: a new binary operator `**` (power).

1. **Lexer**: add `"**"` to `punct2` (it's 2 chars). Verify nothing
   precedence-clashes (`*` still lexes alone; longest-match runs first).
2. **Parser**: `parseAssignExpr` for compound-assignment forms (add `"**="`
   to the op list and `EAssign::op`); binary use needs no parser change
   beyond lexing — `parseBinary` accepts any punct with a nonzero
   `binPrec`.
3. **Precedence**: give `**` a slot in `Parser::binPrec` (e.g. return 11,
   binding tighter than `*`).
4. **Sema** (`checkBinary`): decide the operand rules and set
   `b->binKind`. Reuse an existing `BinKind` if semantics match
   (`BinKind::Arith`) or add a new one — if you add one, mirror it in
   `emitBinary`.
5. **Codegen** (`emitBinary`): emit the LLVM op. `**` has no single LLVM
   instruction for ints — emit a call to `llvm.powi` or lower to repeated
   multiplication.
6. Tests: precedence interactions (`say 2 ** 3 * 4`), negative tests for
   bad operand types.

## Add a primitive type

Example: `f16`.

1. `src/Prims.def`: add `PRIM(f16, f16, 16)` **in the canonical order**
   (the `PRIM_COUNT`/range checks in `Type::isVector` depend on the SIMD
   block staying last).
2. `src/Lexer.cpp`: add `"f16"` to `keywordTable` (primitives are
   keywords).
3. `src/Type.cpp`: extend `primBits` (16), and audit `primIsSigned` /
   `primIsFloat` (`f16` → float).
4. `src/Codegen.cpp` `llvmType`: `case PRIM_f16: return builder.getHalfTy();`
5. Audit every `switch (t->prim)` — `Codegen::evalConst`, `emitCast`
   (`FloatToFloat` already generic), atomics (reject non-byte sizes), say
   printing (`std/prelude.cr` needs an overload + runtime function).
6. Tests: assignment, cast to/from, arithmetic; a negative test for mixed
   arithmetic with `f32`.

## Add an AST node

1. `src/AST.h`: add the `Kind` enum entry and the struct; give it sema
   annotation space *in the base `Expr`* if it needs resolution.
2. `src/Parser.cpp`: produce it.
3. `src/ASTClone.h`: add the case in `ASTCloner::expr`/`stmt` — an
   unhandled kind returns `nullptr` and breaks generics subtly.
4. `src/Sema.cpp`: check it (set `type` and annotations); add to
   `terminates`/`containsBreak` if it's a control-flow construct.
5. `src/Codegen.cpp`: `emitExpr`/`emitStmt` case.
6. Tests + docs.

Example to copy: `ESizeof` is the smallest full journey (parse → resolve →
`evalConst` in codegen).

## Add a type-system feature

Example: a new `TypeKind` or a new coercion.

- New `TypeKind`: extend the enum, `Type`'s predicates, `typeToString`,
  **and every `switch` over `kind`** — the compiler has no
  exhaustiveness checking for these (`-Wall` helps via `-Wswitch` on
  enums). Grep `case TypeKind::` to find them all.
- New coercion (say `ptr<T>` ↔ `ptr<const T>`): implement in
  `Sema::typesAssignable` (assignments), `Sema::resolveOverload`
  (scoring), and `Codegen::coerceValue` (the actual IR); all three must
  agree or you get accepted-but-miscompiled calls.
- Update `unifyTypes` if the coercion should bind generic variables.

## Add an optimization

Two options:

- **Let LLVM do it** (preferred): the `-O1..-O3/-Os` pipelines in
  `Driver::runPipelineInternal` already include inlining, vectorization,
  etc. If IR patterns block a pass, fix the *pattern* in `Codegen.cpp`
  (this is how the optimizer sees constants: `Codegen::evalConst`).
- **A custom pass**: add it after `buildPerModuleDefaultPipeline`:

  ```cpp
  llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
  // mpm.addPass(MyPass());
  ```

  Register analyses through the same `pb`; keep `-O0` pass-free (the
  tests assert `-O0` output behavior, and the `-O2` constant-fold test in
  `test_codegen_internals` is your regression guard).

## Add an LLVM mapping

When changing how a Core construct maps to LLVM (new lowering for an
existing construct, new intrinsic, changed calling convention):

- Touch only `Codegen.cpp` if the ABI is unchanged; update
  [../internals/abi.md](../internals/abi.md) and the layout tables in
  [../compiler/codegen.md](../compiler/codegen.md) if it is.
- `verifyModule` right after generation is your first line of defense;
  `CORE_DUMP_IR=1` is your microscope.

## Add a stdlib primitive

Example: expose `core_rt_foo` from the runtime.

1. `runtime/corert.c`: implement `void core_rt_foo(...)` (thin wrapper,
   panic on OOM where relevant — copy the `core_rt_alloc` style).
2. The custom build command recompiles `corert.o` automatically on rebuild.
3. `std/<module>.cr`: `extern func core_rt_foo(...) -> ...` plus a `pub
   func` wrapper with Core-level types. Follow `std/memory.cr`'s pattern
   (`unsafe { ... }` for pointer casts).
4. Tests in `tests/run_tests.py` (the suite compiles against the real
   `std/`, so nothing to register — imports resolve through
   `opts.stdDir`).
5. Document in the module header comment and the matching page under
   `docs/stdlib/` (create one if the module has none).

## Add a module feature

Module semantics live in two places:

- Resolution order / discovery: `Driver::resolveImport` + `loadModule`
  (see [../compiler/driver.md](../compiler/driver.md)). To add, say, a
  `core.toml`-based import alias, touch `resolveImport`'s candidate list.
- Binding/name rules: `Sema::lookupTypeVisible` / `lookupFuncsVisible` /
  `visible` (the `pub` check). Keep the own-module → imports → prelude
  order, and keep `ModuleSema::imports` population in `loadModule` in sync
  with `Driver`'s dedup.

Test with `test_modules`' layout (recursive imports, dedup, cycle error).

## Add a package feature

`src/Project.cpp`: `fetchPackage` (spec parsing: `repo@^1.2.0`, local
paths, github shorthand), `constraintMatches` (semver), lockfile
(`LockEntry`, `saveLockfile`), manifest (`loadManifest`). The package
cache is `packageCacheDir()/git` with `worktree add --detach` checkouts.
Test pattern: `test_packages` builds a *local* git repo with tags — copy
it; no network needed.

## Add a CLI command

1. `src/Main.cpp`: dispatch in `main()` (see the `if (cmd == "...")`
   chain), extend `printGeneralHelp`.
2. Implement `cmdYourThing(DriverOptions &)` in `src/Project.cpp` (declare
   in `src/Project.h`) — reuse `setupProjectDriver` if it needs the
   manifest, or construct a `Driver` directly.
3. Return codes: 0 success, nonzero failure (the test suite checks these).
4. Add to the test suite (see `test_project_commands`).

## Add a project-file feature

`core.toml` keys are read in `Project::loadManifest` — add the key there,
a `Manifest` field, and consumption in `setupProjectDriver`. `core.lock`
writes go through `saveLockfile`; keep it purely additive (old lockfiles
must keep loading — `loadLockfile` tolerates missing keys).

## Add a linker feature

`Driver::linkObject` builds one command line. New flags flow from
`Main.cpp` → `DriverOptions` (`--link=`, `--link-arg=`, `--lib-path=` are
the existing examples) → the `cmd` string. For freestanding, decide
whether the flag belongs in the `ld -nostdlib` branch too. Test with
`--link-arg` on a real binary (the suite links pthread implicitly via
corert).

---

## Worked micro-example: following one change through the pipeline

The `..=` operator (inclusive ranges) touched:

1. **Lexer** — special-cased before `punct2` so `..=` wins over `..` + `=`
   (`Lexer.cpp`, comment: `// careful: "..=" should be matched before ".." and "="`).
2. **Parser** — `parseStatement`'s for-in branch builds `ERange` with
   `inclusive = true`.
3. **AST** — existing `ERange::inclusive` field.
4. **Sema** — range bounds unify (`refitBound` re-types constant bounds to
   match each other).
5. **Codegen** — `emitStmt`'s `KForIn` range case picks `ICmpSLE` when
   `inclusive`:

   ```cpp
   llvm::Value *cond = up ? builder.CreateICmpSLE(cur, end) : builder.CreateICmpSLT(cur, end);
   ```

6. **Tests** — `for i in 0..=5` expectations in `test_basics`.

That's the whole pipeline for a language feature — most additions are
smaller than they look once you find the analogous existing one.
