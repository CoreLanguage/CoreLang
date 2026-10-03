#include "Diag.h"
#include <algorithm>
#include <cstdio>
#include <unistd.h>

namespace core {

static void emit(Diagnostics &d, DiagStyle style, SourceLoc loc, const std::string &msg,
                 const std::string &help, unsigned squiggleLen) {
  const char *tag = style == DiagStyle::Error ? "error" : style == DiagStyle::Warning ? "warning" : "note";
  const char *colorReset = "\033[0m", *colorRed = "\033[1;31m", *colorBlue = "\033[1;34m",
             *colorGreen = "\033[1;32m", *colorYellow = "\033[1;33m", *colorDim = "\033[2m";
  bool useColor = isatty(2);
  if (!useColor) { colorReset = colorRed = colorBlue = colorGreen = colorYellow = colorDim = ""; }

  const char *tagColor = style == DiagStyle::Error ? colorRed : style == DiagStyle::Warning ? colorYellow : colorBlue;
  if (!loc.valid) {
    fprintf(stderr, "%s%s%s: %s\n", tagColor, tag, colorReset, msg.c_str());
  } else {
    std::string fname = d.sm.fileName(loc.file);
    fprintf(stderr, "%s%s%s: %s\n", tagColor, tag, colorReset, msg.c_str());
    fprintf(stderr, "%s   --> %s:%u:%u%s\n", colorDim, fname.c_str(), loc.line, loc.col, colorReset);
    std::string lineText = d.sm.getLineText(loc.file, loc.line);
    // gutter
    std::string lineNo = std::to_string(loc.line);
    std::string pad(lineNo.size(), ' ');
    if (!lineText.empty()) {
      fprintf(stderr, "%s %s | %s\n", pad.c_str(), lineNo.c_str(), lineText.c_str());
    }
    // caret line
    unsigned col = std::max(1u, loc.col);
    std::string caret(col - 1 + pad.size() + 4, ' ');
    unsigned len = squiggleLen ? squiggleLen : 1;
    if (len <= 1) caret += '^';
    else { caret += '^'; caret += std::string(len - 1, '~'); }
    fprintf(stderr, "%s%s |%s%s%s\n", colorDim, pad.c_str(), colorReset, caret.c_str(), colorReset);
    if (!help.empty()) {
      fprintf(stderr, "%s %s = help: %s%s\n", pad.c_str(), pad.c_str(), help.c_str(), colorReset);
    }
  }
  if (style == DiagStyle::Error) d.errorCount++;
  else if (style == DiagStyle::Warning) d.warningCount++;
}

void Diagnostics::error(SourceLoc loc, const std::string &msg, const std::string &help,
                        unsigned squiggleLen) {
  emit(*this, DiagStyle::Error, loc, msg, help, squiggleLen);
}
void Diagnostics::warning(SourceLoc loc, const std::string &msg, const std::string &help,
                          unsigned squiggleLen) {
  emit(*this, DiagStyle::Warning, loc, msg, help, squiggleLen);
}
void Diagnostics::note(SourceLoc loc, const std::string &msg) {
  emit(*this, DiagStyle::Note, loc, msg, "", 0);
}
void Diagnostics::plainError(const std::string &msg) {
  errorCount++;
  fprintf(stderr, "error: %s\n", msg.c_str());
}

} // namespace core
