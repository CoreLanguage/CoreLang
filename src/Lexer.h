// Core compiler - lexer. Turns .cr source text into a flat token stream.
// Newline tokens are significant: they terminate statements.
#ifndef CORE_LEXER_H
#define CORE_LEXER_H

#include "Common.h"
#include "Diag.h"
#include <string>
#include <vector>

namespace core {

enum class Tok {
  EndOfFile,
  Newline,
  Ident,       // text = identifier
  Kw,          // text = keyword
  IntLit,      // intValue (u64), is128 flag, text = raw digits
  FloatLit,    // floatValue
  CharLit,     // intValue = unicode codepoint
  StrLit,      // text = decoded bytes
  Punct,       // text = operator/punctuator
  Error,
};

struct Token {
  Tok kind = Tok::EndOfFile;
  SourceLoc loc;
  std::string text;      // ident name / keyword / punct / decoded string
  unsigned long long intValue = 0;
  bool big128 = false;   // integer literal exceeds 64 bits
  double floatValue = 0.0;
  unsigned len = 0;      // source length (for caret squiggles)
  bool newlineBefore = false;
};

const char *tokName(Tok t);

class Lexer {
public:
  Lexer(SourceMgr &sm, unsigned fileID, Diagnostics &diag);
  // Tokenize the entire file. The returned stream ends with EndOfFile.
  std::vector<Token> tokenizeAll();

private:
  SourceMgr &sm;
  SourceFile &f;
  Diagnostics &diag;
  unsigned id;
  size_t pos = 0;
  unsigned line = 1, col = 1;
  bool atLineStart = true;

  char cur() const;
  char peek(size_t n = 1) const;
  bool eof() const;
  char advance();
  SourceLoc here() const;
  void lexError(const std::string &msg);

  bool lexNumber();
  bool lexIdentOrKeyword();
  bool lexString();
  bool lexChar();
  void skipComment();
};

// Keywords recognised by the lexer. Primitive type names are keywords so they
// can never be shadowed by user variables.
extern const char *const keywordTable[];
bool isKeyword(const std::string &s);

} // namespace core
#endif
