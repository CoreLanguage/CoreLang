// Core compiler driver entry point.
#include "Common.h"
#include "Diag.h"
#include "Lexer.h"
#include "Parser.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace core;

static int cmdVersion() {
  printf("Core compiler 0.1.0 (LLVM backend)\n");
  return 0;
}

static int cmdHelp(const char *cmd) {
  if (cmd && !strcmp(cmd, "compile")) {
    printf("core compile - compile Core source into a native executable\n");
    printf("\nUSAGE:\n  core compile <output-name> <entry-file.cr> [options]\n");
    printf("\n  The first argument is the exact output binary name.\n");
    printf("  The second argument is the entry-point .cr source file.\n");
    printf("\nOPTIONS:\n");
    printf("  -O0 | -O1 | -O2 | -O3 | -Os   optimization level (default -O0)\n");
    printf("  --debug                       emit DWARF debug info (like -g)\n");
    printf("  --target=<triple>             cross compile (e.g. x86_64, aarch64, riscv64)\n");
    printf("  --freestanding                no OS runtime, no libc linkage\n");
    printf("  --emit-object                 stop after object code generation\n");
    printf("  --force                       overwrite existing output file\n");
    printf("  --link=<lib>                  link library (-l<lib>), repeatable\n");
    printf("  --link-arg=<arg>              pass raw argument to the linker, repeatable\n");
    printf("  --lib-path=<dir>              add library search path (-L<dir>)\n");
    printf("  --entry=<name>                entry point function for --freestanding (default main)\n");
    printf("\nEXAMPLES:\n");
    printf("  core compile mycoolbinary main.cr\n");
    printf("  ./mycoolbinary\n");
    return 0;
  }
  printf("Core - a simple, fast, low-level compiled language\n");
  printf("\nUSAGE: core <command> [options]\n");
  printf("\nCOMMANDS:\n");
  printf("  init [name]        initialize a new Core project\n");
  printf("  compile <out> <file>  compile source files into a native executable\n");
  printf("  build              build the project in the current directory\n");
  printf("  run                build and run the project\n");
  printf("  test               run the project's tests\n");
  printf("  check              type-check without generating code\n");
  printf("  install <pkg>      install a package (git repository)\n");
  printf("  remove <pkg>       remove a package\n");
  printf("  update             update dependencies to newest compatible versions\n");
  printf("  list               list dependencies\n");
  printf("  emit-ir <file>     print the generated LLVM IR\n");
  printf("  emit-asm <file>    print the generated assembly\n");
  printf("  version            print version\n");
  printf("\nRun 'core <command> --help' for command-specific options.\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) return cmdHelp(nullptr);
  std::string cmd = argv[1];
  if (cmd == "--help" || cmd == "help" || cmd == "-h") return cmdHelp(nullptr);
  if (cmd == "version" || cmd == "--version") return cmdVersion();
  if (cmd == "compile") {
    // parse options
    std::string outName, entry;
    for (int i = 2; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--help") return cmdHelp("compile");
      printf("option: %s\n", a.c_str());
    }
    (void)outName;
    (void)entry;
    printf("compile: not yet implemented\n");
    return 1;
  }
  if (cmd == "lex-dump") { // hidden debugging helper
    if (argc < 3) { fprintf(stderr, "usage: core lex-dump <file>\n"); return 2; }
    bool ok;
    std::string text = readFileOrEmpty(argv[2], ok);
    if (!ok) { fprintf(stderr, "error: cannot read %s\n", argv[2]); return 2; }
    SourceMgr sm;
    unsigned id = sm.addFile(argv[2], text);
    Diagnostics diag(sm);
    Lexer lexer(sm, id, diag);
    auto toks = lexer.tokenizeAll();
    for (auto &t : toks) {
      printf("%u:%u  %-10s %s\n", t.loc.line, t.loc.col, tokName(t.kind), t.text.c_str());
    }
    return diag.hasErrors() ? 1 : 0;
  }
  if (cmd == "parse-dump") { // hidden debugging helper
    if (argc < 3) { fprintf(stderr, "usage: core parse-dump <file>\n"); return 2; }
    bool ok;
    std::string text = readFileOrEmpty(argv[2], ok);
    if (!ok) { fprintf(stderr, "error: cannot read %s\n", argv[2]); return 2; }
    SourceMgr sm;
    unsigned id = sm.addFile(argv[2], text);
    Diagnostics diag(sm);
    Lexer lexer(sm, id, diag);
    auto toks = lexer.tokenizeAll();
    ASTContext ctx;
    Parser parser(ctx, sm, id, diag, std::move(toks));
    SourceUnit *unit = parser.parseFile();
    if (!unit) return 1;
    printf("parsed %s: %zu top-level declarations, %d errors\n", unit->path.c_str(),
           unit->decls.size(), diag.errorCount);
    return diag.hasErrors() ? 1 : 0;
  }
  fprintf(stderr, "error: unknown command '%s'\n", cmd.c_str());
  cmdHelp(nullptr);
  return 2;
}
