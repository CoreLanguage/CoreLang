#include "Driver.h"
#include "Parser.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <unistd.h>
#include <algorithm>
#include <set>

using namespace llvm;

namespace core {

std::string findCompilerData(const char *kind, const DriverOptions &opts) {
  if (const char *home = getenv("CORE_HOME")) {
    std::string p = joinPath(home, kind);
    if (fileExists(p)) return p;
  }
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    std::string exeDir = dirName(buf);
    for (const std::string &rel : {"", "../lib/core", "lib/core"}) {
      std::string p = rel[0] ? joinPath(joinPath(exeDir, rel), kind) : joinPath(exeDir, kind);
      if (fileExists(p)) return p;
    }
  }
  return "";
}

bool Driver::resolveImport(const std::string &importerPath, const std::vector<std::string> &parts,
                           std::string &outPath) {
  std::string rel;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i) rel += "/";
    rel += parts[i];
  }
  std::vector<std::string> candidates;
  std::string importerDir = dirName(importerPath);
  candidates.push_back(joinPath(importerDir, rel + ".cr"));
  if (!opts.projectRoot.empty()) {
    candidates.push_back(joinPath(joinPath(opts.projectRoot, "src"), rel + ".cr"));
  }
  for (const auto &d : opts.packageSrcDirs) {
    candidates.push_back(joinPath(d, rel + ".cr"));
  }
  if (!opts.stdDir.empty()) {
    candidates.push_back(joinPath(opts.stdDir, rel + ".cr"));
  }
  for (auto &c : candidates) {
    if (fileExists(c)) {
      char *real = realpath(c.c_str(), nullptr);
      if (real) {
        outPath = real;
        free(real);
      } else {
        outPath = c;
      }
      return true;
    }
  }
  return false;
}

bool Driver::loadModule(const std::string &path, std::vector<std::string> &chain) {
  char *real = realpath(path.c_str(), nullptr);
  std::string canon = real ? real : path;
  if (real) free(real);

  if (byCanonicalPath.count(canon)) return true; // deduplicated imports

  for (auto &c : chain) {
    if (c == canon) {
      std::string cycle;
      for (auto &p : chain) cycle += p + "\n    -> ";
      cycle += canon;
      diag.plainError(strfmt("circular import detected:\n    -> %s", cycle.c_str()));
      return false;
    }
  }
  chain.push_back(canon);

  bool ok;
  std::string text = readFileOrEmpty(path, ok);
  if (!ok) {
    diag.plainError(strfmt("cannot read source file '%s'", path.c_str()));
    return false;
  }
  unsigned fid = sm.addFile(path, text);
  Lexer lexer(sm, fid, diag);
  auto toks = lexer.tokenizeAll();
  Parser parser(ctx, sm, fid, diag, std::move(toks));
  SourceUnit *unit = parser.parseFile();
  if (!unit) {
    diag.plainError(strfmt("failed to parse '%s'", path.c_str()));
    return false;
  }

  loadedModules.emplace_back();
  ModuleSema *ms = &loadedModules.back();
  ms->path = canon;
  size_t slash = canon.find_last_of('/');
  std::string stem = slash == std::string::npos ? canon : canon.substr(slash + 1);
  stem = stem.substr(0, stem.find_last_of('.'));
  ms->name = stem;
  ms->unit = unit;
  byCanonicalPath[canon] = ms;

  for (Decl *d : unit->decls) {
    if (d->kind != Decl::Import) continue;
    auto *im = (DImport *)d;
    std::string depPath;
    if (!resolveImport(path, im->parts, depPath)) {
      std::string name;
      for (size_t i = 0; i < im->parts.size(); i++) {
        if (i) name += ".";
        name += im->parts[i];
      }
      diag.error(im->loc, strfmt("cannot resolve import '%s'", name.c_str()),
                 "searched: the importing file's directory, the project src/ directory, "
                 "installed dependency packages, and the standard library", 1);
      return false;
    }
    if (!loadModule(depPath, chain)) return false;
    ModuleSema *dep = byCanonicalPath[depPath];
    std::string importName = im->alias.empty() ? im->parts[0] : im->alias;
    bool found = false;
    for (auto &[n, m] : ms->imports)
      if (n == importName) found = true;
    if (!found) ms->imports.push_back({importName, dep});
  }

  chain.pop_back();
  return !diag.hasErrors();
}

static void topoModules(std::vector<ModuleSema *> &out, ModuleSema *m,
                        std::set<ModuleSema *> &seen) {
  if (seen.count(m)) return;
  seen.insert(m);
  for (auto &[n, dep] : m->imports) topoModules(out, dep, seen);
  out.push_back(m);
}

enum class PipelineMode { Check, PrintIR, PrintAsm, EmitBinary, EmitObject };

static int runPipelineInternal(Driver &d, const std::string &outputName,
                               const std::string &entryPath, PipelineMode mode);
std::vector<ModuleSema *> &modulePtrs_of(Driver &d) { return d.modulePtrs; }

int Driver::check(const std::string &entryPath) {
  return runPipelineInternal(*this, "", entryPath, PipelineMode::Check);
}
int Driver::emitIR(const std::string &entryPath, bool asm_) {
  return runPipelineInternal(*this, "", entryPath, asm_ ? PipelineMode::PrintAsm
                                                        : PipelineMode::PrintIR);
}
int Driver::compile(const std::string &outputName, const std::string &entryPath) {
  return runPipelineInternal(*this, outputName, entryPath, PipelineMode::EmitBinary);
}

int runPipelineInternal(Driver &driver, const std::string &outputName, const std::string &entryPath,
                        PipelineMode mode) {
  DriverOptions &opts = driver.opts;
  Diagnostics &diag = driver.diag;
  SourceMgr &sm = driver.sm;

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();

  if (!opts.freestanding) {
    std::string preludePath = opts.stdDir.empty() ? "" : joinPath(opts.stdDir, "prelude.cr");
    if (preludePath.empty() || !fileExists(preludePath)) {
      diag.plainError("cannot find the Core standard library (std/prelude.cr)");
      return 1;
    }
    std::vector<std::string> chain;
    if (!driver.loadModule(preludePath, chain)) return 1;
  }

  Sema semaObj(driver.tc, diag);
  Sema *sema = &semaObj;

  std::vector<std::string> chain;
  if (!driver.loadModule(entryPath, chain)) return 1;
  if (diag.hasErrors()) return 1;

  ModuleSema *preludeModule = nullptr;
  for (auto &m : driver.loadedModules)
    if (m.name == "prelude") preludeModule = &m;
  std::vector<ModuleSema *> order;
  std::set<ModuleSema *> seen;
  if (preludeModule) topoModules(order, preludeModule, seen);
  for (auto &m : driver.loadedModules) topoModules(order, &m, seen);
  driver.modulePtrs = order;

  if (!sema->registerModules(modulePtrs_of(driver))) return 1;
  std::string entryCanon = entryPath;
  {
    char *real = realpath(entryPath.c_str(), nullptr);
    if (real) {
      entryCanon = real;
      free(real);
    }
  }
  ModuleSema *entry = driver.byCanonicalPath.count(entryCanon)
                          ? driver.byCanonicalPath[entryCanon]
                          : &driver.loadedModules.back();
  if (!opts.freestanding) {
    if (!sema->checkEntry(entry)) return 1;
  }
  if (!sema->checkAll()) return 1;
  if (diag.hasErrors()) return 1;

  CodegenOptions copts;
  copts.optLevel = opts.optLevel;
  copts.sizeOpt = opts.sizeOpt;
  copts.debugInfo = opts.debugInfo;
  copts.targetTriple = opts.targetTriple;
  copts.freestanding = opts.freestanding;
  copts.entryName = opts.entryName;
  Codegen cg(*sema, driver.tc, diag, copts);
  llvm::Module module("core-program", cg.llvmContext());
  if (!cg.generate(module, entry)) return 1;
  if (getenv("CORE_DUMP_IR")) module.print(llvm::errs(), nullptr);
  if (verifyModule(module, &llvm::errs())) {
    diag.plainError("internal error: generated LLVM IR failed verification");
    return 1;
  }

  if (opts.optLevel > 0 || opts.sizeOpt) {
    llvm::OptimizationLevel level = opts.sizeOpt ? llvm::OptimizationLevel::Os
        : opts.optLevel == 1 ? llvm::OptimizationLevel::O1
        : opts.optLevel == 2 ? llvm::OptimizationLevel::O2
        : opts.optLevel == 3 ? llvm::OptimizationLevel::O3
        : llvm::OptimizationLevel::O0;
    llvm::PassBuilder pb;
    llvm::ModuleAnalysisManager mam;
    llvm::CGSCCAnalysisManager cam;
    llvm::FunctionAnalysisManager fam;
    llvm::LoopAnalysisManager lam;
    pb.registerModuleAnalyses(mam);
    pb.registerFunctionAnalyses(fam);
    pb.registerCGSCCAnalyses(cam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cam, mam);
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
  }

  if (mode == PipelineMode::PrintIR) {
    module.print(llvm::outs(), nullptr);
    return 0;
  }

  // target machine
  std::string tripleStr = opts.targetTriple.empty()
                              ? std::string(llvm::sys::getDefaultTargetTriple())
                              : opts.targetTriple;
  std::string err;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(tripleStr, err);
  if (!target) {
    diag.plainError(strfmt("unknown target '%s': %s", tripleStr.c_str(), err.c_str()));
    return 1;
  }
  llvm::TargetOptions topts;
  std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
      tripleStr, "generic", "", topts, llvm::Reloc::PIC_,
      std::nullopt,
      opts.optLevel >= 2 ? llvm::CodeGenOptLevel::Aggressive : llvm::CodeGenOptLevel::Default));
  module.setDataLayout(tm->createDataLayout());

  if (mode == PipelineMode::PrintAsm) {
    llvm::SmallString<4096> asmOut;
    llvm::raw_svector_ostream sos(asmOut);
    llvm::legacy::PassManager asmPM;
    if (tm->addPassesToEmitFile(asmPM, sos, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
      diag.plainError("internal error: target cannot emit assembly");
      return 1;
    }
    asmPM.run(module);
    llvm::outs() << asmOut;
    return 0;
  }

  // emit object file
  std::string objFile;
  bool linkAfter = mode == PipelineMode::EmitBinary;
  if (linkAfter) objFile = outputName + ".coreobj.o";
  else objFile = outputName;
  {
    int fd;
    if (sys::fs::openFileForWrite(objFile, fd, sys::fs::CD_CreateAlways)) {
      diag.plainError(strfmt("cannot write object file '%s'", objFile.c_str()));
      return 1;
    }
    llvm::raw_fd_ostream ofs(fd, true);
    llvm::legacy::PassManager codegenPM;
    if (tm->addPassesToEmitFile(codegenPM, ofs, nullptr, llvm::CodeGenFileType::ObjectFile)) {
      diag.plainError("internal error: target cannot emit object files");
      return 1;
    }
    codegenPM.run(module);
    ofs.flush();
  }

  if (mode == PipelineMode::EmitObject) {
    fprintf(stderr, "wrote object file: %s\n", objFile.c_str());
    return 0;
  }
  return driver.linkObject(objFile, outputName);
}



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
    if (runtimeObj.empty()) {
      diag.plainError("cannot find the Core runtime object (corert.o)");
      return 1;
    }
    cmd += " " + runtimeObj;
    cmd += " -lm -lpthread -ldl -latomic";
    for (auto &lp : opts.libPaths) cmd += " -L" + lp;
    for (auto &lib : opts.linkLibs) cmd += " -l" + lib;
    for (auto &la : opts.linkArgs) cmd += " " + la;
    cmd += " -o " + outputName;
  }
  int rc = system(cmd.c_str());
  if (rc != 0) {
    diag.plainError(strfmt("linking failed (exit code %d)\n  command: %s", rc, cmd.c_str()));
    return 1;
  }
#ifndef _WIN32
  std::string chmod = "chmod +x " + outputName;
  system(chmod.c_str());
#endif
  fprintf(stderr, "built: %s\n", outputName.c_str());
  return 0;
}

} // namespace core
