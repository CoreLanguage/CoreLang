// Core compiler - command line interface.
#include "Driver.h"
#include "Project.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace core;

static void printGeneralHelp() {
  printf("Core - a simple, fast, low-level compiled language\n");
  printf("\nUSAGE: core <command> [options]\n");
  printf("\nCOMMANDS:\n");
  printf("  init [name]           initialize a new Core project\n");
  printf("  compile <out> <file>  compile Core source into a native executable\n");
  printf("  build                 build the project in the current directory\n");
  printf("  run                   build and run the project\n");
  printf("  test                  run the project's tests\n");
  printf("  check                 type-check without generating code\n");
  printf("  install <pkg>         install a package (git repository)\n");
  printf("  remove <pkg>          remove a package\n");
  printf("  update                update dependencies to newest compatible versions\n");
  printf("  list                  list dependencies\n");
  printf("  emit-ir <file>        print the generated LLVM IR\n");
  printf("  emit-asm <file>       print the generated assembly\n");
  printf("  version               print version\n");
  printf("\nRun 'core <command> --help' for command-specific options.\n");
}

static void printCompileHelp() {
  printf("core compile - compile Core source into a native executable\n");
  printf("\nUSAGE:\n  core compile <output-name> <entry-file.cr> [options]\n");
  printf("\n  The first argument is the exact output binary name.\n");
  printf("  The second argument is the entry-point .cr source file.\n");
  printf("\nOPTIONS:\n");
  printf("  -O0 | -O1 | -O2 | -O3 | -Os   optimization level (default -O0)\n");
  printf("  --debug                       emit DWARF debug info (GDB/LLDB-ready)\n");
  printf("  --target=<triple>             cross compile: x86_64, aarch64, riscv64...\n");
  printf("  --freestanding                no OS runtime, no libc, custom entry point\n");
  printf("  --emit-object                 stop after generating the object file\n");
  printf("  --force                       overwrite an existing output file\n");
  printf("  --link=<lib>                  link library (-l<lib>), repeatable\n");
  printf("  --link-arg=<arg>              raw linker argument, repeatable\n");
  printf("  --lib-path=<dir>              library search path (-L<dir>)\n");
  printf("  --entry=<name>                entry symbol for --freestanding (default main)\n");
  printf("\nEXAMPLES:\n");
  printf("  core compile mycoolbinary main.cr\n");
  printf("  ./mycoolbinary\n");
  printf("\n  core compile server src/main.cr\n");
  printf("  ./server\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printGeneralHelp();
    return 0;
  }
  std::string cmd = argv[1];

  DriverOptions dopts;
  std::vector<std::string> positionals;
  bool help = false;

  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") { help = true; continue; }
    if (a == "-O0") dopts.optLevel = 0;
    else if (a == "-O1") dopts.optLevel = 1;
    else if (a == "-O2") dopts.optLevel = 2;
    else if (a == "-O3") dopts.optLevel = 3;
    else if (a == "-Os") dopts.sizeOpt = true;
    else if (a == "--debug" || a == "-g") dopts.debugInfo = true;
    else if (a.rfind("--target=", 0) == 0) dopts.targetTriple = a.substr(9);
    else if (a == "--freestanding") dopts.freestanding = true;
    else if (a == "--emit-object") dopts.emitObjectOnly = true;
    else if (a == "--force" || a == "-f") dopts.forceOverwrite = true;
    else if (a.rfind("--link=", 0) == 0) dopts.linkLibs.push_back(a.substr(7));
    else if (a.rfind("--link-arg=", 0) == 0) dopts.linkArgs.push_back(a.substr(11));
    else if (a.rfind("--lib-path=", 0) == 0) dopts.libPaths.push_back(a.substr(11));
    else if (a.rfind("--entry=", 0) == 0) dopts.entryName = a.substr(8);
    else positionals.push_back(a);
  }

  if (help) {
    if (cmd == "compile") printCompileHelp();
    else printGeneralHelp();
    return 0;
  }

  // compiler support files
  dopts.stdDir = dirName(findCompilerData("std/prelude.cr", dopts));
  dopts.runtimeObj = findCompilerData("corert.o", dopts);

  if (cmd == "version" || cmd == "--version") {
    printf("Core compiler 0.1.0 (LLVM %s backend)\n", LLVM_VERSION_STRING);
    return 0;
  }
  if (cmd == "compile") {
    if (positionals.size() < 2) {
      fprintf(stderr, "error: usage: core compile <output-name> <entry-file.cr>\n");
      fprintf(stderr, "       run 'core compile --help' for details\n");
      return 2;
    }
    Driver d(dopts);
    return d.compile(positionals[0], positionals[1]);
  }
  if (cmd == "check") {
    if (positionals.empty()) {
      // in a project: check the main source file
      if (fileExists(manifestPath())) positionals.push_back("src/main.cr");
      else {
        fprintf(stderr, "error: usage: core check <file.cr>\n");
        return 2;
      }
    }
    // inside a project: resolve dependencies first
    if (fileExists(manifestPath())) {
      Diagnostics diag(*(new SourceMgr()));
      Manifest m;
      if (setupProjectDriverForCheck(dopts, m, diag)) {
        Driver d(dopts);
        return d.check(positionals[0]);
      }
      return 1;
    }
    Driver d(dopts);
    return d.check(positionals[0]);
  }
  if (cmd == "emit-ir" || cmd == "emit-asm") {
    if (positionals.empty()) {
      fprintf(stderr, "error: usage: core %s <file.cr>\n", cmd.c_str());
      return 2;
    }
    Driver d(dopts);
    return d.emitIR(positionals[0], cmd == "emit-asm");
  }
  if (cmd == "init") {
    std::string name = positionals.empty() ? "" : positionals[0];
    return cmdInit(name, dopts);
  }
  if (cmd == "build") return cmdBuild(dopts);
  if (cmd == "run") return cmdRun(dopts, positionals);
  if (cmd == "test") return cmdTest(dopts);
  if (cmd == "install") {
    if (positionals.empty()) {
      fprintf(stderr, "error: usage: core install <package>\n       e.g. core install github.com/user/library\n");
      return 2;
    }
    return cmdInstall(positionals[0], dopts);
  }
  if (cmd == "remove") {
    if (positionals.empty()) {
      fprintf(stderr, "error: usage: core remove <package-name>\n");
      return 2;
    }
    return cmdRemove(positionals[0], dopts);
  }
  if (cmd == "update") return cmdUpdate(dopts);
  if (cmd == "list") return cmdList(dopts);

  fprintf(stderr, "error: unknown command '%s'\n", cmd.c_str());
  printGeneralHelp();
  return 2;
}
