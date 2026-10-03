// Core compiler - recursive descent parser (token stream -> AST).
#ifndef CORE_PARSER_H
#define CORE_PARSER_H

#include "AST.h"
#include "Diag.h"
#include "Lexer.h"
#include "Type.h"

namespace core {

class Parser {
public:
  Parser(ASTContext &ctx, SourceMgr &sm, unsigned fileID, Diagnostics &diag,
         std::vector<Token> toks)
      : ctx(ctx), sm(sm), fileID(fileID), diag(diag), toks(std::move(toks)) {}

  // Returns null on unrecoverable error (diagnostics already emitted).
  SourceUnit *parseFile();

private:
  ASTContext &ctx;
  SourceMgr &sm;
  unsigned fileID;
  Diagnostics &diag;
  std::vector<Token> toks;
  size_t i = 0;

  const Token &tk(size_t off = 0) const {
    size_t j = i + off;
    if (j >= toks.size()) j = toks.size() - 1;
    return toks[j];
  }
  Token peek(size_t off = 0) const { return tk(off); }
  SourceLoc loc() const { return tk().loc; }
  SourceLoc prevLoc() const { return i > 0 ? toks[i - 1].loc : tk().loc; }

  bool at(Tok k) const { return tk().kind == k; }
  bool atPunct(const char *p) const { return tk().kind == Tok::Punct && tk().text == p; }
  bool atKw(const char *k) const { return tk().kind == Tok::Kw && tk().text == k; }
  bool atIdent(size_t off = 0) const { return tk(off).kind == Tok::Ident; }

  Token advance() { return toks[i < toks.size() ? i++ : toks.size() - 1]; }
  void skipNewlines();
  bool eatPunct(const char *p);   // consume if present
  bool eatKw(const char *k);
  bool expectPunct(const char *p, const char *context);
  bool expectKw(const char *k, const char *context);
  Token expectIdent(const char *context);

  int quiet_ = 0; // >0 while speculatively parsing: suppress diagnostics
  int noStructLit_ = 0; // >0 in contexts where `Ident {` is not a struct literal
  void errorAt(const SourceLoc &l, const std::string &msg, const std::string &help = "",
               unsigned len = 0) {
    if (quiet_ > 0) return;
    diag.error(l, msg, help, len);
  }

  // ---- declarations ----
  Decl *parseTopDecl(std::vector<Attr> &attrs);
  DFunc *parseFuncRest(bool isPub, Decl *parent, unsigned mods, std::string linkName);
  DStruct *parseStruct(bool isPub, bool packed);
  DClass *parseClass(bool isPub, bool packed);
  DInterface *parseInterface(bool isPub, bool isTrait);
  DEnum *parseEnum(bool isPub);
  Decl *parseGlobalOrConst(bool isPub, bool forceMut = false, bool forceTLS = false);
  DExtern *parseExtern(bool isPub, std::string linkName);
  DImport *parseImport(bool isPub);

  // ---- statements ----
  Stmt *parseBlock();
  Stmt *parseStatement();
  Stmt *parseLetOrExpr(bool isMut);

  // ---- expressions ----
  Expr *parseExpr(bool noStructLit = false);
  Expr *parseAssignExpr();
  Expr *parseBinary(int minPrec);
  Expr *parseUnary();
  Expr *parsePostfix();
  Expr *parsePrimary(bool noStructLit = false);
  Pattern *parsePattern();
  TypeExpr *parseType();
  std::vector<Param> parseParamList(bool &isVariadic);

public:
  // grammar tables shared with Sema/CodeGen error reporting
  static int binPrec(const std::string &op);
};

} // namespace core
#endif
