// Core compiler - diagnostics engine.
// Produces richly formatted errors like:
//   error: cannot add `string` and `i32`
//     --> main.cr:12:18
//      12 | say "Age: " + age
//         |             ^~~
#ifndef CORE_DIAG_H
#define CORE_DIAG_H

#include "Common.h"
#include <string>

namespace core {

enum class DiagStyle { Error, Warning, Note };

class Diagnostics {
public:
  explicit Diagnostics(SourceMgr &sm) : sm(sm) {}

  void error(SourceLoc loc, const std::string &msg, const std::string &help = "",
             unsigned squiggleLen = 0);
  void warning(SourceLoc loc, const std::string &msg, const std::string &help = "",
               unsigned squiggleLen = 0);
  void note(SourceLoc loc, const std::string &msg);
  void plainError(const std::string &msg); // no source position

  int errorCount = 0;
  int warningCount = 0;
  bool hasErrors() const { return errorCount > 0; }

  SourceMgr &sm;
};

} // namespace core
#endif
