# Compiler architecture

This document maps the `core` compiler: what lives where, the order in which
the phases run, and how a `.cr` file becomes a native binary. All class and
function names refer to the real sources in `src/`.

The compiler is a single C++17 executable (~12k lines) that links against
LLVM 18. There is no separate preprocessor, no bytecode, no VM: the whole
program is compiled to one LLVM module, optimized, emitted as one object
file, and linked with the system `cc` against a thin C runtime
(`runtime/corert.c`).

## Directory map

```
core/
├── src/                 the compiler (one class per phase, one file each)
│   ├── Common.h/.cpp    SourceLoc, SourceMgr, string/path utilities
│   ├── Diag.h/.cpp      Diagnostics: colored errors with caret rendering
│   ├── Lexer.h/.cpp     tokenizer (newline-significant tokens)
│   ├── Parser.h/.cpp    recursive-descent parser -> AST
│   ├── AST.h            node definitions + ASTContext (bump arena)
│   ├── ASTClone.h       deep cloning used for generic instantiation
│   ├── Type.h/.cpp      interned types (TypeContext), Prims.def primitive table
│   ├── Prims.def        X-macro list of all primitive types
│   ├── Sema.h/.cpp      name resolution, overloading, monomorphization, checks
│   ├── Codegen.h/.cpp   LLVM IR generation (IRBuilder + DIBuilder)
│   ├── Driver.h/.cpp    module graph, PassBuilder pipeline, object emission, linking
│   ├── TOML.h/.cpp      minimal TOML reader/writer for core.toml / core.lock
│   ├── Project.h/.cpp   project commands + git-based package manager
│   └── Main.cpp         CLI argument dispatch
├── runtime/corert.c     the C runtime (printing, malloc wrappers, threads, time)
├── std/*.cr             standard library written in Core (prelude is auto-imported)
├── tests/run_tests.py   end-to-end test suite (97 checks)
├── examples/            one directory per language feature
└── build/               CMake build tree; `core` binary + std/ + corert.o
```

The build (`CMakeLists.txt`) compiles `src/*.cpp` into the `core` binary,
compiles `runtime/corert.c` once into `corert.o`, and copies `std/*.cr`
next to the binary. At startup the driver relocates both via
`findCompilerData()` (`CORE_HOME` env var, else relative to `/proc/self/exe`).

## Pipeline overview

```
 .cr files on disk
        │
        ▼
 ┌────────────────────────────── Driver::loadModule ──────────────────────────┐
 │  resolveImport()  →  Lexer::tokenizeAll()  →  Parser::parseFile()          │
 │  (per file, recursively; cycle check + dedup by canonical path)            │
 └────────────────────────────────────────────────────────────────────────────┘
        │  vector<Token>            │  SourceUnit* (list of Decl*)
        ▼                           ▼
 ┌──── Lexer ──────────┐   ┌──── Parser ─────────────┐
 │ flat token stream   │   │ AST arena (ASTContext)  │
 │ with Newline tokens │   │ one SourceUnit per file │
 └─────────────────────┘   └─────────────────────────┘
                                     │
        topological module order     │  (prelude first)
                                     ▼
 ┌──────────────────────────── Sema ──────────────────────────────────────────┐
 │ registerModules()   build ModuleSema symbol tables + ClassLayouts          │
 │ checkEntry()        main() must exist, return void/i32, take no params     │
 │ checkAll()          resolve names/types, assign to Expr sema fields,       │
 │                     instantiateGeneric() for every generic use             │
 └────────────────────────────────────────────────────────────────────────────┘
                                     │  annotated AST + Sema lookup tables
                                     ▼
 ┌──────────────────────────── Codegen ───────────────────────────────────────┐
 │ generate(): one llvm::Module, IRBuilder; DIBuilder when --debug            │
 │ verifyModule() rejects invalid IR before anything else happens             │
 └────────────────────────────────────────────────────────────────────────────┘
                                     │
              -O0: none              │  -O1/-O2/-O3/-Os:
                                     ▼  PassBuilder default pipeline
 ┌──────────────────────────── Driver ────────────────────────────────────────┐
 │ TargetMachine (host or --target triple, Reloc::PIC_)                       │
 │ addPassesToEmitFile(ObjectFile) → <out>.coreobj.o                          │
 │ linkObject(): cc obj corert.o -lm -lpthread -ldl -latomic -o <out>         │
 │   freestanding: ld -nostdlib, no corert.o                                  │
 └────────────────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
                            ONE native executable
```

The entry point is `Main.cpp`, which parses CLI flags into `DriverOptions`
and dispatches to `Driver::compile` / `Driver::check` / `Driver::emitIR` or
the project commands in `Project.cpp` (`cmdBuild`, `cmdRun`, `cmdTest`,
`cmdInstall`, ...).

## How a file becomes a binary

Consider `core compile app main.cr`:

1. **Discovery** — `Driver::loadModule("main.cr", chain)` canonicalizes the
   path with `realpath`, errors on cycles (a path appearing twice in `chain`),
   and dedups repeats via `byCanonicalPath`. It reads the file, lexes and
   parses it into a `SourceUnit`, then recursively loads every `import`
   after `resolveImport()` finds it (importer's directory, project `src/`,
   installed packages, std).
2. **Ordering** — `topoModules()` produces a post-order (dependencies
   first); `runPipelineInternal` prepends the prelude, so
   `Sema::modules` always starts with the standard library.
3. **Registration** — `Sema::registerModules` fills each module's
   `ModuleSema` tables (`types`, `funcs` overload sets, `globals`,
   `consts`), rejects duplicate names, and builds `ClassLayout`s
   (vtable slot order, base chains).
4. **Entry check** — `Sema::checkEntry` requires a unique `main` returning
   `void` or `i32` with no parameters (skipped for `--freestanding`).
5. **Checking** — `Sema::checkAll` walks every non-generic function body,
   annotating expressions (see [semantic.md](semantic.md)) and calling
   `instantiateGeneric` for each distinct generic argument tuple.
6. **Codegen** — `Codegen::generate` sets the target `DataLayout` first (so
   `sizeof`/`alignof` are target-true), declares all functions/globals,
   then emits bodies; generic instances are emitted last (and may trigger
   further declarations). The module is verified with `verifyModule`.
7. **Optimize** — with `-O1` and above, `PassBuilder::buildPerModuleDefaultPipeline`
   runs LLVM's standard pipeline; `-O0` runs none.
8. **Emit** — a `TargetMachine` (host triple unless `--target=`) emits an
   object file named `<output>.coreobj.o` via
   `addPassesToEmitFile(..., llvm::CodeGenFileType::ObjectFile)`.
9. **Link** — `Driver::linkObject` shells out to `cc <obj> corert.o -lm
   -lpthread -ldl -latomic -o <output>` (or `ld -nostdlib` in freestanding
   mode) and prints `built: <output>`.

Whole-program compilation is deliberate: every imported `.cr` file ends up
in the same LLVM module and the same binary, so the output has no runtime
dependency on the sources.

## Cross-cutting invariants

- **One arena.** All AST nodes live in `Driver::ctx` (an `ASTContext` bump
  allocator) and are freed at process exit. Cloned generic bodies live in a
  separate process-lifetime arena inside `Sema::instantiateGeneric`.
- **Pointer identity = type identity.** `TypeContext` interns every type;
  `tc.same(a, b)` is pointer equality (except generic variables).
- **Sema annotates, codegen reads.** Codegen never resolves names. Every
  `Expr` carries the sema-computed fields (`idKind`, `resolvedFunc`,
  `binKind`, ...) listed in [ast.md](ast.md).
- **Diagnostics are recoverable.** Each phase checks `diag.hasErrors()` at
  phase boundaries, not per call; `Type::Invalid` and score `-1` overloads
  let checking continue after an error to report more of them.

## Where to read next

| Topic | Document |
|---|---|
| Tokens and lexing | [lexer.md](lexer.md) |
| Grammar as implemented | [parser.md](parser.md) |
| AST nodes and sema fields | [ast.md](ast.md) |
| Checking and overload resolution | [semantic.md](semantic.md) |
| The type system and generics | [type-system.md](type-system.md) |
| LLVM lowering and runtime layout | [codegen.md](codegen.md) |
| Module graph, optimization, linking | [driver.md](driver.md) |
| Debug info | [debugging.md](debugging.md) |
| Errors | [diagnostics.md](diagnostics.md) |
| Monomorphization end-to-end | [../internals/monomorphization.md](../internals/monomorphization.md) |
| ABI, mangling, C interop | [../internals/abi.md](../internals/abi.md) |
| Contributing | [../contributing/getting-started.md](../contributing/getting-started.md) |
