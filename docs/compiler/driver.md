# The driver: modules, optimization, linking

`src/Driver.h` / `src/Driver.cpp` own everything that is not lexing,
parsing, checking, or IR generation: the module graph, the optimization
pipeline, object emission, and invoking the linker. `Main.cpp` and
`Project.cpp` sit on top of it.

```cpp
class Driver {
public:
  bool discoverModules(const std::string &entryPath);
  int compile(const std::string &outputName, const std::string &entryPath);
  int check(const std::string &entryPath);              // typecheck only
  int emitIR(const std::string &entryPath, bool asm_);  // emit-ir / emit-asm
  int linkObject(const std::string &objPath, const std::string &outputName);

  std::deque<ModuleSema> loadedModules;    // stable storage (deque: stable refs)
  std::vector<ModuleSema *> modulePtrs;    // topological order
  ASTContext ctx;                          // shared AST arena
  std::map<std::string, ModuleSema *> byCanonicalPath;
```

`loadedModules` is a `deque` on purpose: `ModuleSema*` values handed out
while loading stay valid as more modules are appended.

## Finding support files

`findCompilerData(kind, opts)` locates `corert.o` and `std/prelude.cr`:

1. `$CORE_HOME/<kind>` if it exists,
2. else relative to `/proc/self/exe` — the executable's directory itself,
   `../lib/core/`, or `lib/core/` (matching the CMake install layout:
   binary in `bin/`, runtime object in `lib/core`, std in `lib/core/std`).

`Main.cpp` fills `opts.stdDir` and `opts.runtimeObj` from this at startup.

## Module discovery and resolution

`Driver::loadModule(path, chain)` is the whole loader:

- **Canonicalization + dedup.** The path goes through `realpath` and is
  checked against `byCanonicalPath`; a second import of the same file is a
  no-op ("duplicate imports deduplicated" in the test suite).
- **Cycle detection.** `chain` is the active DFS stack. A repeat means a
  cycle:

  ```cpp
  for (auto &c : chain) {
    if (c == canon) {
      std::string cycle;
      for (auto &p : chain) cycle += p + "\n    -> ";
      cycle += canon;
      diag.plainError(strfmt("circular import detected:\n    -> %s", cycle.c_str()));
      return false;
    }
  }
  ```

- **Per file:** read → `Lexer::tokenizeAll` → `Parser::parseFile` → append
  a `ModuleSema` whose `name` is the file stem — then walk the unit's
  `DImport` decls, resolve each, recurse, and record
  `imports[name-or-alias] = module`.
- **`resolveImport`** builds the candidate path `parts.join("/") + ".cr"`
  and probes, in order: the importing file's directory, `<projectRoot>/src/`,
  each installed package source dir (`opts.packageSrcDirs`), and the std
  directory. Failure produces a diagnostic listing all four places searched.

## Topological order

```cpp
static void topoModules(std::vector<ModuleSema *> &out, ModuleSema *m,
                        std::set<ModuleSema *> &seen) {
  if (seen.count(m)) return;
  seen.insert(m);
  for (auto &[n, dep] : m->imports) topoModules(out, dep, seen);
  out.push_back(m);
}
```

Post-order DFS: dependencies come before their importers. The pipeline
runs it on the prelude first, then every loaded module, yielding
`modulePtrs` — the exact order Sema registers and Codegen emits in (so
`sema.modules` always starts with the standard library).

## The pipeline (`runPipelineInternal`)

One function drives every mode (`Check`, `PrintIR`, `PrintAsm`,
`EmitBinary`, `EmitObject`):

1. `llvm::InitializeAllTargetInfos/Targets/MCs/AsmParsers/AsmPrinters` —
   all targets, always, so `--target=` works in any build.
2. Load the prelude (skipped for `--freestanding`), then the entry file.
3. Topo-sort, then `sema->registerModules`, `sema->checkEntry` (skipped for
   freestanding — there is no `main`), `sema->checkAll`.
4. `Codegen::generate` into an `llvm::Module` named `"core-program"`,
   followed by `verifyModule` (failure is an internal error). `CORE_DUMP_IR=1`
   prints the unoptimized IR.
5. **Optimization** — only when `-O1` and above or `-Os`:

   ```cpp
   llvm::OptimizationLevel level = opts.sizeOpt ? llvm::OptimizationLevel::Os
       : opts.optLevel == 1 ? llvm::OptimizationLevel::O1
       : opts.optLevel == 2 ? llvm::OptimizationLevel::O2
       : opts.optLevel == 3 ? llvm::OptimizationLevel::O3
       : llvm::OptimizationLevel::O0;
   llvm::PassBuilder pb;
   llvm::LoopAnalysisManager lam;
   llvm::FunctionAnalysisManager fam;
   llvm::CGSCCAnalysisManager cam;
   llvm::ModuleAnalysisManager mam;
   pb.registerModuleAnalyses(mam);
   pb.registerFunctionAnalyses(fam);
   pb.registerCGSCCAnalyses(cam);
   pb.registerLoopAnalyses(lam);
   pb.crossRegisterProxies(lam, fam, cam, mam);
   llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
   mpm.run(module, mam);
   ```

   That is the stock LLVM pipeline per level — no custom passes. `-O0`
   skips the pipeline entirely.
6. **Target machine** — `TargetRegistry::lookupTarget` on
   `opts.targetTriple` or the host triple; CPU `"generic"`,
   `Reloc::PIC_`, and `CodeGenOptLevel::Aggressive` when `optLevel >= 2`
   (else `Default`). The module's DataLayout is refreshed from this
   machine before emission.
7. **Emission** — `emit-ir`/`emit-asm` print to stdout; otherwise an
   object file is written via `addPassesToEmitFile(...,
   llvm::CodeGenFileType::ObjectFile)` to `<output>.coreobj.o` (or
   `<output>` itself for `--emit-object`).

## Linking

```cpp
int Driver::linkObject(const std::string &objPath, const std::string &outputName) {
  std::string cmd;
  if (opts.freestanding) {
    cmd = "ld -nostdlib";
    for (auto &la : opts.linkArgs) cmd += " " + la;
    cmd += " " + objPath + " -o " + outputName;
  } else {
    cmd = "cc " + objPath;
    std::string runtimeObj = opts.runtimeObj.empty() ? findCompilerData("corert.o", opts)
                                                     : opts.runtimeObj;
    ...
    cmd += " " + runtimeObj;
    cmd += " -lm -lpthread -ldl -latomic";
    for (auto &lp : opts.libPaths) cmd += " -L" + lp;
    for (auto &lib : opts.linkLibs) cmd += " -l" + lib;
    for (auto &la : opts.linkArgs) cmd += " " + la;
    cmd += " -o " + outputName;
  }
  int rc = system(cmd.c_str());
```

- Hosted builds link the program object + `corert.o` + libm/pthread/dl/
  libatomic via the system `cc`, then `chmod +x` and print `built: <name>`.
- Freestanding builds use `ld -nostdlib` with only user-supplied
  `--link-arg`s; the C runtime is *not* linked (`corert.c`'s header comment:
  "Freestanding (kernel) builds do NOT link this file").

## Cross-compilation and freestanding

- `--target=aarch64` (or `x86_64`, `riscv64`, any registered triple):
  codegen and emission use that triple. Since no target linkers are
  bundled, linking stops at the relocatable object with a notice — unless
  the user passes `--link-arg=...` arguments for their cross toolchain, in
  which case the normal `cc`/`ld` command is built with them. The test
  suite checks the object is genuine AArch64 machine code (ELF header
  `e_machine = EM_AARCH64`).
- `--freestanding`: no prelude, no runtime, no `main` requirement; the
  entry symbol comes from `@link_name("_start")` on the entry function
  (`CodegenOptions::entryName`). Object emission is requested together with
  `--emit-object` in practice (see `examples/kernel`).

## Project mode

`Project.cpp` wraps the driver for the manifest commands. `setupProjectDriver`
loads `core.toml`, fetches/refreshes each dependency (resolving into
`opts.packageSrcDirs`, one level of transitive dependencies), and merges
`[build]` link flags. `cmdBuild`/`cmdRun` compile `<source-dir>/main.cr`
with `Driver::compile(projectName, entry)`; `cmdTest` compiles and runs
each `tests/*.cr` as its own binary. See
[../contributing/getting-started.md](../contributing/getting-started.md)
for the command list; the manifest/lockfile formats are read by
`Project::loadManifest`/`loadLockfile` in `src/Project.cpp`.

## Common failure modes

- **Stale `corert.o` / missing std.** `findCompilerData` searches relative
  to the *binary*; running `build/core` from a tree where `corert.o` wasn't
  built fails with `cannot find the Core runtime object (corert.o)`.
  Rebuild, or set `CORE_HOME`.
- **Import shadowing.** Resolution order means a `utils.cr` next to the
  importing file wins over the project `src/` and the stdlib — surprising
  when refactoring shared modules.
- **Cycles are fatal, not broken by design.** There is no forward-declaration
  story across modules; mutually-recursive modules are rejected. Split the
  shared declarations into a third module.
- **`-Os` and `-O` levels interact.** `sizeOpt` wins over `optLevel` in the
  pipeline choice; passing both `-O3` and `-Os` silently optimizes for
  size.
- **`--emit-object` with hosted mode still skips linking** — the output is
  `outputName` itself (not `outputName.coreobj.o`), which can surprise
  scripts expecting the intermediate name.
