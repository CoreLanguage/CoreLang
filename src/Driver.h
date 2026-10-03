// Core compiler - driver: module discovery, the compile pipeline, linking.
#ifndef CORE_DRIVER_H
#define CORE_DRIVER_H

#include "Codegen.h"
#include "Sema.h"
#include <string>
#include <vector>

namespace core {

struct DriverOptions {
  int optLevel = 0;            // 0..3
  bool sizeOpt = false;        // -Os
  bool debugInfo = false;
  std::string targetTriple;    // empty = host
  bool freestanding = false;
  std::string entryName = "main";
  bool emitObjectOnly = false;
  bool forceOverwrite = false;
  std::vector<std::string> linkLibs;   // -l names
  std::vector<std::string> linkArgs;   // raw linker args
  std::vector<std::string> libPaths;   // -L paths
  // project context
  std::string projectRoot;             // dir containing core.toml ("" if standalone)
  std::vector<std::string> packageSrcDirs; // dependency source roots
  std::string stdDir;                  // stdlib dir (found relative to the core binary)
  std::string runtimeObj;              // corert.o path
};

class Driver {
public:
  Driver(DriverOptions opts) : opts(std::move(opts)) {}

  // Resolve a file path to a module and everything it imports.
  // Returns false on errors (diagnostics emitted).
  bool discoverModules(const std::string &entryPath);

  // Full pipeline: parse -> sema -> codegen -> optimize -> object -> link.
  // Returns 0 on success.
  int compile(const std::string &outputName, const std::string &entryPath);
  int check(const std::string &entryPath);              // typecheck only
  int emitIR(const std::string &entryPath, bool asm_);  // emit-ir / emit-asm
  int linkObject(const std::string &objPath, const std::string &outputName);

  std::vector<ModuleSema> loadedModules;   // stable storage
  std::vector<ModuleSema *> modulePtrs;    // topological order
  ASTContext ctx;                          // shared AST arena
  std::map<std::string, ModuleSema *> byCanonicalPath;

  DriverOptions opts;
  SourceMgr sm;
  Diagnostics diag{sm};
  TypeContext tc;
  Sema *sema = nullptr;
  ModuleSema *entry = nullptr;

  bool resolveImport(const std::string &importerPath, const std::vector<std::string> &parts,
                     std::string &outPath);
  bool loadModule(const std::string &path, std::vector<std::string> &chain);
  int runPipeline(const std::string &outputName, bool writeBinary, bool printIR, bool printAsm);
};

// Locate compiler support files (corert.o, std/) relative to the executable.
std::string findCompilerData(const char *kind, const DriverOptions &opts);

// expose module list for pipeline helper (defined in Driver.cpp)
std::vector<ModuleSema *> &modulePtrs_of(Driver &d);

} // namespace core
#endif
