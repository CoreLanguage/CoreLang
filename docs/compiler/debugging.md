# Debug info

Core emits DWARF debug info via `llvm::DIBuilder` when a binary is built
with `--debug` (alias `-g`); the default is no debug info. Binaries remain
GDB-usable: breakpoints in `.cr` files resolve to real source lines
(verified by the test suite, which runs GDB in batch mode and requires
`main () at ...` in the output).

## Where it is set up

`Codegen::generate` creates the builder and one compile unit per lexed
source file (every module, prelude included, gets its own CU):

```cpp
std::unique_ptr<llvm::DIBuilder> dib;
if (opts.debugInfo) {
  dib = std::make_unique<llvm::DIBuilder>(*mod);
  dbg = dib.get();
  dbgEnabled = true;
  // one compile unit per source file
  for (size_t fi = 0; fi < sema.diag.sm.fileCount(); fi++) {
    std::string path = sema.diag.sm.fileName((unsigned)fi);
    std::string dir = ".", name = path;
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) { dir = path.substr(0, slash); name = path.substr(slash + 1); }
    llvm::DIFile *dif = dbg->createFile(name, dir);
    llvm::DICompileUnit *cu = dbg->createCompileUnit(llvm::dwarf::DW_LANG_C, dif, "core", false, "", 0);
    debugCUs()[(unsigned)fi] = cu;
  }
}
```

Notes on the choices made:

- `DW_LANG_C` is used because Core's types are not described to the
  debugger (see "What GDB shows" below); claiming a richer language without
  emitting type entries would produce worse GDB output.
- The producer string is `"core"`.
- `debugCUs()` maps `SourceMgr` file IDs to `llvm::DIFile*`/CUs. The header
  stores them as `void*` (`std::map<unsigned, void *> cuMapRaw`) to avoid
  including DIBuilder types in `Codegen.h`.

## Function boundaries

`Codegen::emitFuncBody` creates a `DISubprogram` per emitted function and
attaches it:

```cpp
if (dbgEnabled) {
  unsigned fid = f->loc.valid ? f->loc.file : 0;
  ...
  llvm::DISubroutineType *fty = dbg->createSubroutineType(dbg->getOrCreateTypeArray({}));
  curSP = dbg->createFunction(cu, f->name, sym, cu->getFile(),
                              (unsigned)(f->loc.line ? f->loc.line : 1), fty,
                              0, llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
  fnp->setSubprogram(curSP);
}
```

The subroutine type is the empty type array: GDB shows functions
prototyp-less (`main ()` rather than `main (int, char**)`). The symbol name
(`sym`, the mangled name) is passed as the linkage name so stack traces show
overload-distinct symbols.

## Line information

`Codegen::emitDebugLoc(loc)` is called from `emitStmt`/`emitCall` at
statement and call boundaries:

```cpp
void Codegen::emitDebugLoc(SourceLoc loc) {
  if (dbgEnabled && loc.valid && dbg)
    builder.SetCurrentDebugLocation(
        llvm::DebugLoc(llvm::DILocation::get(ctx, loc.line, loc.col, curDebugScope())));
}
```

`curDebugScope()` returns the current `DISubprogram` (or null). Because
IRBuilder attaches the *current* debug location to every instruction it
creates, expression-level granularity falls out for free wherever
`emitDebugLoc` was called before emitting.

`generate` calls `dib->finalize()` at the end — skipping this leaves
incomplete DWARF that GDB ignores.

## What GDB shows

With `core compile app main.cr --debug`:

- `break main` resolves to `main.cr`, the function's defining line, and GDB
  steps through `.cr` lines (statement-granular, thanks to `emitDebugLoc`).
- Frames show mangled Core symbols (e.g. `_C4main4pickGi32E`) since Core
  functions are plain C-like symbols — see
  [../internals/abi.md](../internals/abi.md) for the mangling grammar.
- `info sources` lists each `.cr` file as a separate compile unit.
- Variables have **no** debugger-level type information: locals are DWARF
  `DW_TAG_auto_variable`-less for now (the `DISubroutineType` is empty and
  no `DILocalVariable`s are created), so `print x` works only via the raw
  allocas (`p/x` on the memory) at `-O0`. Optimized builds additionally
  lose allocas to register allocation.

## Extending debug info

The natural next steps, in increasing difficulty, all live in
`Codegen.cpp`:

1. **Variable info.** In `emitFuncBody`'s parameter loop and `emitStmt`'s
   `SLet` case, wrap each alloca with
   `dbg->createAutoVariable`/`insertDeclare` (`llvm::Instruction`
   `llvm.dbg.declare`), and give functions a real `DISubroutineType` built
   from parameter types (you will need a `DIDerivedType` mapping in
   `llvmType`'s neighborhood — i32→`i32` basic types, pointers→pointers,
   structs→`createStructType` with `fieldGEPIndex`-consistent offsets).
2. **Enum/struct descriptions.** Map `TypeKind::Struct`/`Class` to
   `createStructType` using `Sema::ClassLayout` (vptr first), and enums to
   `createEnumerationType` for the simple case.
3. **Line tables for expressions.** More `emitDebugLoc` calls inside
   `emitBinary`/`emitCast` improve stepping fidelity at `-O0`.

Rules to keep it working:

- Guard everything with `dbgEnabled` and keep `dbg` null otherwise — the
  non-debug build must not allocate a `DIBuilder`.
- Any `DISubprogram` must be set *before* `finalize()`; functions declared
  but never emitted (prototypes) should not get subprograms.
- Debug info must not change semantics: everything above is metadata;
  `llvm.dbg.declare` must not extend a value's live range incorrectly
  (attach to the alloca, before the first store).

## Common failure modes

- **Breakpoints resolve to the prelude.** That means the entry CU was
  picked as `cuMap[0]` — check that `f->loc.file` is valid (it comes from
  the parser's `SourceLoc`, which is set for every parsed declaration).
- **Missing `finalize()`** produces binaries GDB opens without symbols; if
  you add an early return path to `generate`, make sure finalize still
  runs.
- **Testing.** The GDB check in `tests/run_tests.py`
  (`test_codegen_internals`) requires `main () at` in GDB output; keep that
  green when touching `createFunction` arguments.
