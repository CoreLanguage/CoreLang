#include "Lexer.h"
#include <algorithm>

namespace core {

const char *const keywordTable[] = {
    // declarations
    "func", "struct", "class", "interface", "trait", "enum", "import", "extern",
    // declarations modifiers
    "pub", "private", "static", "mut", "const", "abstract", "virtual", "override",
    "tls",
    // control flow
    "if", "else", "while", "for", "in", "break", "continue", "return", "switch",
    "case", "default", "match", "loop",
    // misc
    "as", "unsafe", "true", "false", "null", "self", "super", "and", "or",
    "sizeof", "alignof",
    // primitive types
    "void", "never", "bool", "char", "string",
    "i8", "i16", "i32", "i64", "i128",
    "u8", "u16", "u32", "u64", "u128",
    "f32", "f64", "usize", "isize",
    // simd vector types
    "f32x4", "f64x2", "i32x4", "i64x2", "i8x16", "i16x8", "u8x16", "u16x8", "u32x4", "u64x2",
    nullptr,
};

bool isKeyword(const std::string &s) {
  for (int i = 0; keywordTable[i]; i++)
    if (s == keywordTable[i]) return true;
  return false;
}

const char *tokName(Tok t) {
  switch (t) {
  case Tok::EndOfFile: return "end of file";
  case Tok::Newline: return "newline";
  case Tok::Ident: return "identifier";
  case Tok::Kw: return "keyword";
  case Tok::IntLit: return "integer literal";
  case Tok::FloatLit: return "float literal";
  case Tok::CharLit: return "char literal";
  case Tok::StrLit: return "string literal";
  case Tok::Punct: return "operator";
  case Tok::Error: return "error";
  }
  return "?";
}

Lexer::Lexer(SourceMgr &sm, unsigned fileID, Diagnostics &diag)
    : sm(sm), f(sm.file(fileID)), diag(diag), id(fileID) {}

char Lexer::cur() const { return pos < f.text.size() ? f.text[pos] : '\0'; }
char Lexer::peek(size_t n) const {
  return pos + n < f.text.size() ? f.text[pos + n] : '\0';
}
bool Lexer::eof() const { return pos >= f.text.size(); }
char Lexer::advance() {
  char c = f.text[pos++];
  if (c == '\n') { line++; col = 1; atLineStart = true; }
  else { col++; if (c != ' ' && c != '\t' && c != '\r') atLineStart = false; }
  return c;
}
SourceLoc Lexer::here() const {
  SourceLoc l;
  l.file = id;
  l.line = line;
  l.col = col;
  l.offset = (unsigned)pos;
  l.valid = true;
  return l;
}

void Lexer::lexError(const std::string &msg) {
  diag.error(here(), msg, "", 1);
}

void Lexer::skipComment() {
  // line comment
  while (!eof() && cur() != '\n') advance();
}

bool Lexer::lexNumber() {
  // handles: 123 0x1F 0b1010 0o777 1_000 1.5 1e10 1.5e-3  0x1p4(hexfloat skip)
  size_t start = pos;
  SourceLoc loc = here();
  bool isFloat = false;
  if (cur() == '0' && (peek() == 'x' || peek() == 'X')) {
    advance(); advance();
    while (!eof() && (isxdigit(cur()) || cur() == '_')) advance();
  } else if (cur() == '0' && (peek() == 'b' || peek() == 'B')) {
    advance(); advance();
    while (!eof() && (cur() == '0' || cur() == '1' || cur() == '_')) advance();
  } else if (cur() == '0' && (peek() == 'o' || peek() == 'O')) {
    advance(); advance();
    while (!eof() && (cur() >= '0' && cur() <= '7' || cur() == '_')) advance();
  } else {
    while (!eof() && (isdigit(cur()) || cur() == '_')) advance();
    if (cur() == '.' && isdigit(peek())) {
      isFloat = true;
      advance();
      while (!eof() && (isdigit(cur()) || cur() == '_')) advance();
    }
    if ((cur() == 'e' || cur() == 'E') &&
        (isdigit(peek()) || ((peek() == '+' || peek() == '-') && isdigit(peek(2))))) {
      isFloat = true;
      advance();
      if (cur() == '+' || cur() == '-') advance();
      while (!eof() && isdigit(cur())) advance();
    }
  }
  (void)start;
  (void)loc;
  return isFloat;
}

namespace {
// decode an escape sequence after '\'; returns bytes, advances pos/line/col via advance fn
bool decodeEscape(const SourceFile &f, size_t &pos, unsigned &line, unsigned &col,
                  std::string &out, SourceLoc errLoc, Diagnostics &diag) {
  char c = f.text[pos];
  switch (c) {
  case 'n': out += '\n'; break;
  case 't': out += '\t'; break;
  case 'r': out += '\r'; break;
  case '0': out += '\0'; break;
  case '\\': out += '\\'; break;
  case '"': out += '"'; break;
  case '\'': out += '\''; break;
  case 'x': {
    if (pos + 2 < f.text.size() && isxdigit(f.text[pos + 1]) && isxdigit(f.text[pos + 2])) {
      std::string hex = f.text.substr(pos + 1, 2);
      out += (char)strtol(hex.c_str(), nullptr, 16);
      pos += 2;
      col += 2;
    } else {
      diag.error(errLoc, "invalid \\x escape in literal", "expected two hex digits, e.g. \\x41", 2);
      return false;
    }
    break;
  }
  default:
    diag.error(errLoc, strfmt("unknown escape sequence '\\%c'", c),
               "valid escapes: \\n \\t \\r \\0 \\\\ \\\" \\' \\xHH", 2);
    return false;
  }
  pos++;
  col++;
  return true;
}
} // namespace

std::vector<Token> Lexer::tokenizeAll() {
  std::vector<Token> out;
  bool lastWasNewline = true; // start of file counts as "newline before"
  // mutable copies for escape decoding
  while (!eof()) {
    // skip spaces / comments / newlines get emitted once
    char c = cur();
    if (c == ' ' || c == '\t' || c == '\r') { advance(); continue; }
    if (c == '\n') {
      if (!out.empty() && out.back().kind != Tok::Newline) {
        Token t;
        t.kind = Tok::Newline;
        t.loc = here();
        t.text = "\n";
        t.len = 1;
        out.push_back(t);
        lastWasNewline = true;
      }
      advance();
      continue;
    }
    if (c == '/' && peek() == '/') { while (!eof() && cur() != '\n') advance(); continue; }
    if (c == '/' && peek() == '*') {
      SourceLoc cl = here();
      advance(); advance();
      bool closed = false;
      while (!eof()) {
        if (cur() == '*' && peek() == '/') { advance(); advance(); closed = true; break; }
        advance();
      }
      if (!closed) diag.error(cl, "unterminated block comment", "", 2);
      continue;
    }

    Token t;
    t.loc = here();
    t.len = 1;
    t.newlineBefore = lastWasNewline;
    lastWasNewline = false;

    if (isalpha(c) || c == '_') {
      std::string word;
      while (!eof() && (isalnum(cur()) || cur() == '_')) { word += advance(); }
      t.text = word;
      t.len = (unsigned)word.size();
      if (isKeyword(word)) t.kind = Tok::Kw;
      else t.kind = Tok::Ident;
      out.push_back(t);
      continue;
    }
    if (isdigit(c) || (c == '.' && isdigit(peek()))) {
      bool isFloat = lexNumber();
      std::string raw = f.text.substr(t.loc.offset, pos - t.loc.offset);
      t.len = (unsigned)raw.size();
      if (isFloat) {
        std::string clean;
        for (char rc : raw) if (rc != '_') clean += rc;
        t.kind = Tok::FloatLit;
        t.floatValue = strtod(clean.c_str(), nullptr);
        t.text = raw;
      } else {
        t.kind = Tok::IntLit;
        std::string clean;
        for (char rc : raw) if (rc != '_') clean += rc;
        t.text = clean;
        // parse value
        if (clean.rfind("0x", 0) == 0 || clean.rfind("0X", 0) == 0)
          t.intValue = strtoull(clean.c_str() + 2, nullptr, 16);
        else if (clean.rfind("0b", 0) == 0 || clean.rfind("0B", 0) == 0)
          t.intValue = strtoull(clean.c_str() + 2, nullptr, 2);
        else if (clean.rfind("0o", 0) == 0 || clean.rfind("0O", 0) == 0)
          t.intValue = strtoull(clean.c_str() + 2, nullptr, 8);
        else
          t.intValue = strtoull(clean.c_str(), nullptr, 10);
        // detect >64-bit magnitude (more than 19 decimal digits, or a hex
        // literal of 17+ digits, cannot fit u64/i64 magnitude)
        std::string digitsOnly = clean;
        if (digitsOnly.rfind("0x", 0) == 0 || digitsOnly.rfind("0X", 0) == 0)
          digitsOnly = digitsOnly.substr(2);
        else if (digitsOnly.rfind("0b", 0) == 0 || digitsOnly.rfind("0B", 0) == 0)
          digitsOnly = digitsOnly.substr(2);
        else if (digitsOnly.rfind("0o", 0) == 0 || digitsOnly.rfind("0O", 0) == 0)
          digitsOnly = digitsOnly.substr(2);
        else if (digitsOnly.size() > 1 && digitsOnly[0] == '0' &&
                 std::all_of(digitsOnly.begin() + 1, digitsOnly.end(),
                             [](char ch) { return ch >= '0' && ch <= '7'; }))
          digitsOnly = digitsOnly.substr(1);
        if (digitsOnly.size() >= 20) {
          t.big128 = true;
        } else if (digitsOnly.size() == 19) {
          // 19 digits: fits u64 only if <= 18446744073709551615
          if (digitsOnly > "18446744073709551615") t.big128 = true;
        }
      }
      out.push_back(t);
      continue;
    }
    if (c == '"') {
      advance(); // opening quote
      std::string s;
      bool ok = true;
      while (true) {
        if (eof() || cur() == '\n') {
          diag.error(t.loc, "unterminated string literal", "missing closing \"", 1);
          ok = false;
          break;
        }
        if (cur() == '"') { advance(); break; }
        if (cur() == '\\') {
          SourceLoc el = here();
          advance();
          ok = decodeEscape(f, pos, line, col, s, el, diag);
          if (!ok) break;
          continue;
        }
        s += advance();
      }
      if (ok) {
        t.kind = Tok::StrLit;
        t.text = s;
        t.len = (unsigned)s.size() + 2;
        out.push_back(t);
      }
      continue;
    }
    if (c == '\'') {
      advance();
      uint32_t cp = 0;
      bool ok = true;
      if (eof() || cur() == '\n') {
        diag.error(t.loc, "unterminated char literal", "missing closing '", 1);
        ok = false;
      } else if (cur() == '\\') {
        advance();
        std::string tmp;
        SourceLoc el = here();
        ok = decodeEscape(f, pos, line, col, tmp, el, diag);
        if (ok) cp = tmp.empty() ? 0 : (unsigned char)tmp[0];
      } else {
        cp = (unsigned char)advance();
      }
      if (ok) {
        if (cur() != '\'') {
          diag.error(here(), "unterminated char literal", "missing closing '", 1);
        } else advance();
        t.kind = Tok::CharLit;
        t.intValue = cp;
        t.text = strfmt("'%c'", cp ? (char)cp : '?');
        out.push_back(t);
      }
      continue;
    }

    // punctuators, longest match first (3, 2, 1 chars)
    static const char *punct3[] = {"<<=", ">>=", "...", nullptr};
    static const char *punct2[] = {"==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
                                   "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                   "..", "->", nullptr};
    bool matched = false;
    for (int i = 0; punct3[i]; i++) {
      if (cur() == punct3[i][0] && peek() == punct3[i][1] && peek(2) == punct3[i][2]) {
        t.kind = Tok::Punct;
        t.text = punct3[i];
        t.len = 3;
        advance(); advance(); advance();
        matched = true;
        break;
      }
    }
    if (!matched) {
      // careful: "..=" should be matched before ".." and "="
      if (c == '.' && peek() == '.' && peek(2) == '=') {
        t.kind = Tok::Punct; t.text = "..="; t.len = 3;
        advance(); advance(); advance();
        matched = true;
      }
    }
    if (!matched)
      for (int i = 0; punct2[i]; i++) {
        if (c == punct2[i][0] && peek() == punct2[i][1]) {
          // don't treat "1..2" number-dot-dot wrongly: '.' handled above by number lexer
          t.kind = Tok::Punct;
          t.text = punct2[i];
          t.len = 2;
          advance(); advance();
          matched = true;
          break;
        }
      }
    if (!matched) {
      t.kind = Tok::Punct;
      t.text = std::string(1, c);
      t.len = 1;
      advance();
      matched = true;
    }
    out.push_back(t);
  }

  Token eofTok;
  eofTok.kind = Tok::EndOfFile;
  eofTok.loc = here();
  eofTok.newlineBefore = true;
  out.push_back(eofTok);
  return out;
}

} // namespace core
