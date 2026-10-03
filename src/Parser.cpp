#include "Parser.h"
#include <cassert>

namespace core {

int Parser::binPrec(const std::string &op) {
  if (op == "or" || op == "||") return 1;
  if (op == "and" || op == "&&") return 2;
  if (op == "==" || op == "!=") return 3;
  if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
  if (op == "|") return 5;
  if (op == "^") return 6;
  if (op == "&") return 7;
  if (op == "<<" || op == ">>") return 8;
  if (op == "+" || op == "-") return 9;
  if (op == "*" || op == "/" || op == "%") return 10;
  return 0;
}

void Parser::skipNewlines() {
  while (at(Tok::Newline)) advance();
}

bool Parser::eatPunct(const char *p) {
  if (atPunct(p)) { advance(); return true; }
  return false;
}
bool Parser::eatKw(const char *k) {
  if (atKw(k)) { advance(); return true; }
  return false;
}
bool Parser::expectPunct(const char *p, const char *context) {
  if (eatPunct(p)) return true;
  errorAt(loc(), strfmt("expected '%s' %s, found %s", p, context,
                        tk().kind == Tok::EndOfFile ? "end of file" : ("'" + tk().text + "'").c_str()),
          "", tk().len);
  return false;
}
bool Parser::expectKw(const char *k, const char *context) {
  if (eatKw(k)) return true;
  errorAt(loc(), strfmt("expected '%s' %s, found %s", k, context,
                        tk().kind == Tok::EndOfFile ? "end of file" : ("'" + tk().text + "'").c_str()),
          "", tk().len);
  return false;
}
Token Parser::expectIdent(const char *context) {
  if (atIdent()) return advance();
  if (tk().kind == Tok::Kw)
    errorAt(loc(), strfmt("expected %s, found keyword '%s' (keywords cannot be used as names)",
                          context, tk().text.c_str()), "", tk().len);
  else
    errorAt(loc(), strfmt("expected %s, found %s", context, tokName(tk().kind)),
            "", tk().len);
  return Token();
}

// ---------------------------------------------------------------- types -----
TypeExpr *Parser::parseType() {
  TypeExpr *t = ctx.makeNoLoc<TypeExpr>();
  t->loc = loc();
  if (tk().kind == Tok::Kw) {
    const std::string &k = tk().text;
    // primitive?
    int pk = primKindByName(k);
    if (pk >= 0) {
      t->kind = TypeExpr::Prim;
      t->prim = pk;
      advance();
      return t;
    }
    if (k == "func") { // func(i32, i32) -> i32
      advance();
      if (!expectPunct("(", "in function type")) return nullptr;
      std::vector<TypeExpr *> params;
      skipNewlines();
      if (!atPunct(")")) {
        while (true) {
          TypeExpr *pt = parseType();
          if (!pt) return nullptr;
          params.push_back(pt);
          skipNewlines();
          if (eatPunct(",")) { skipNewlines(); continue; }
          break;
        }
      }
      if (!expectPunct(")", "in function type")) return nullptr;
      TypeExpr *ret = nullptr;
      if (eatPunct("->")) ret = parseType();
      t->kind = TypeExpr::Func;
      t->paramTypes = params;
      t->retType = ret;
      return t;
    }
  }
  if (eatPunct("[")) { // [elem; size]
    TypeExpr *elem = parseType();
    if (!elem) return nullptr;
    if (!expectPunct(";", "in array type (syntax: [i32; 5])")) return nullptr;
    Expr *sz = parseExpr();
    if (!sz) return nullptr;
    if (!expectPunct("]", "in array type")) return nullptr;
    t->kind = TypeExpr::Array;
    t->elem = elem;
    t->arraySize = sz;
    return t;
  }
  if (atIdent() || tk().kind == Tok::Kw) {
    // dotted name, e.g. ptr<T>, math.Vec, utils.math.Foo
    std::vector<std::string> parts;
    if (!atIdent() && tk().kind == Tok::Kw && !isKeyword(tk().text)) { /*unreachable*/ }
    if (tk().kind == Tok::Kw) {
      errorAt(loc(), strfmt("'%s' is a keyword and cannot be used as a type name", tk().text.c_str()), "", tk().len);
      return nullptr;
    }
    parts.push_back(advance().text);
    while (atPunct(".") && atIdent(1)) { // peek after dot must be ident
      advance();
      parts.push_back(advance().text);
    }
    t->kind = TypeExpr::Named;
    t->nameParts = parts;
    if (atPunct("<")) {
      // generic args: ident < type, type >  followed by non-{ or anything
      advance();
      skipNewlines();
      while (true) {
        TypeExpr *ga = parseType();
        if (!ga) return nullptr;
        t->genericArgs.push_back(ga);
        skipNewlines();
        if (eatPunct(",")) { skipNewlines(); continue; }
        break;
      }
      if (!expectPunct(">", "closing generic type arguments")) return nullptr;
    }
    return t;
  }
  errorAt(loc(), strfmt("expected a type, found %s", tokName(tk().kind)),
          "", tk().len);
  return nullptr;
}

// ------------------------------------------------------------ patterns ------
Pattern *Parser::parsePattern() {
  Pattern *p = ctx.makeNoLoc<Pattern>();
  p->loc = loc();
  // `_` lexes as an identifier, so accept both forms
  if (atPunct("_") || (atIdent() && tk().text == "_")) { advance(); p->kind = Pattern::Wild; return p; }
  if (atIdent()) {
    std::string name = advance().text;
    if (atPunct("(")) {
      p->kind = Pattern::Variant;
      p->name = name;
      advance();
      skipNewlines();
      if (!atPunct(")")) {
        while (true) {
          if (atPunct("_") || (atIdent() && tk().text == "_")) { advance(); auto *w = ctx.makeNoLoc<Pattern>(); w->kind = Pattern::Wild; w->loc = prevLoc(); p->subs.push_back(w); }
          else if (atIdent()) { auto *v = ctx.makeNoLoc<Pattern>(); v->kind = Pattern::Var; v->loc = loc(); v->name = advance().text; p->subs.push_back(v); }
          else {
            errorAt(loc(), "expected identifier or '_' in variant pattern binding", "", tk().len);
            return nullptr;
          }
          skipNewlines();
          if (eatPunct(",")) { skipNewlines(); continue; }
          break;
        }
      }
      if (!expectPunct(")", "closing variant pattern")) return nullptr;
      return p;
    }
    // bare identifier: variant name (unit variant) or binding; resolved in sema
    p->kind = Pattern::Var;
    p->name = name;
    return p;
  }
  if (tk().kind == Tok::IntLit || tk().kind == Tok::StrLit || tk().kind == Tok::CharLit ||
      atKw("true") || atKw("false") || atKw("null") || (tk().kind == Tok::Punct && tk().text == "-")) {
    Expr *e = parseExpr();
    if (!e) return nullptr;
    p->kind = Pattern::Lit;
    p->litExpr = e;
    return p;
  }
  errorAt(loc(), strfmt("expected a pattern, found %s", tokName(tk().kind)),
          "patterns: `_`, a binding name, `Variant(x, _)`, or a constant", tk().len);
  return nullptr;
}

// ------------------------------------------------------------ expressions ---
Expr *Parser::parseExpr(bool noStructLit) {
  if (noStructLit) {
    noStructLit_++;
    Expr *e = parseAssignExpr();
    noStructLit_--;
    return e;
  }
  return parseAssignExpr();
}

Expr *Parser::parseAssignExpr() {
  Expr *lhs = parseBinary(1);
  if (!lhs) return nullptr;
  if (tk().kind == Tok::Punct) {
    const std::string &t = tk().text;
    if (t == "=" || t == "+=" || t == "-=" || t == "*=" || t == "/=" || t == "%=" ||
        t == "&=" || t == "|=" || t == "^=" || t == "<<=" || t == ">>=") {
      std::string op = advance().text;
      Expr *rhs = parseBinary(1);
      if (!rhs) return nullptr;
      return ctx.make<EAssign>(lhs->loc, op, lhs, rhs);
    }
  }
  return lhs;
}

Expr *Parser::parseBinary(int minPrec) {
  Expr *lhs = parseUnary();
  if (!lhs) return nullptr;
  while (true) {
    if (tk().kind != Tok::Punct && tk().kind != Tok::Kw) break;
    std::string op = tk().text;
    int prec;
    if (tk().kind == Tok::Kw) {
      if (op != "and" && op != "or") break;
      prec = binPrec(op);
    } else {
      prec = binPrec(op);
      if (prec == 0) break;
    }
    if (prec < minPrec) break;
    // operator on its own line does not continue the expression
    if (tk().newlineBefore) break;
    advance();
    Expr *rhs = parseBinary(prec + 1);
    if (!rhs) return nullptr;
    lhs = ctx.make<EBinary>(lhs->loc, op, lhs, rhs);
  }
  return lhs;
}

Expr *Parser::parseUnary() {
  SourceLoc l = loc();
  if (tk().kind == Tok::Punct &&
      (tk().text == "-" || tk().text == "!" || tk().text == "~" || tk().text == "*" ||
       tk().text == "&")) {
    std::string op = advance().text;
    Expr *e = parseUnary();
    if (!e) return nullptr;
    if (op == "-") {
      // fold negative literals: keep the magnitude + sign flag so typing/range
      // checks see the true (negative) extent
      if (e->kind == Expr::IntLit) {
        auto *ie = (EInt *)e;
        if (!ie->neg) {
          auto *ne = ctx.make<EInt>(l);
          ne->value = ie->value;
          ne->neg = true;
          ne->digits = "-" + ie->digits;
          ne->big128 = ie->big128;
          return ne;
        }
      }
      if (e->kind == Expr::FloatLit) {
        auto *fe = (EFloat *)e;
        auto *ne = ctx.make<EFloat>(l);
        ne->value = -fe->value;
        return ne;
      }
    }
    return ctx.make<EUnary>(l, op, e);
  }
  return parsePostfix();
}

Expr *Parser::parsePostfix() {
  Expr *e = parsePrimary();
  if (!e) return nullptr;
  while (true) {
    if (atPunct("(") && !tk().newlineBefore) {
      advance();
      skipNewlines();
      std::vector<Expr *> args;
      if (!atPunct(")")) {
        while (true) {
          Expr *a = parseAssignExpr();
          if (!a) return nullptr;
          args.push_back(a);
          skipNewlines();
          if (eatPunct(",")) {
            skipNewlines();
            if (atPunct(")")) break; // trailing comma
            continue;
          }
          break;
        }
      }
      SourceLoc cl = prevLoc();
      if (!expectPunct(")", "closing call arguments")) return nullptr;
      e = ctx.make<ECall>(cl, e, std::move(args));
      continue;
    }
    if (atPunct("[") && !tk().newlineBefore) {
      advance();
      skipNewlines();
      Expr *idx = parseExpr();
      if (!idx) return nullptr;
      skipNewlines();
      if (!expectPunct("]", "closing index expression")) return nullptr;
      e = ctx.make<EIndex>(e->loc, e, idx);
      continue;
    }
    if (atPunct(".") && !tk().newlineBefore) {
      advance();
      Token nm = expectIdent("member name after '.'");
      if (nm.kind != Tok::Ident && nm.kind != Tok::Kw) return nullptr;
      e = ctx.make<EMember>(e->loc, e, nm.text);
      continue;
    }
    if (atKw("as") && !tk().newlineBefore) {
      advance();
      TypeExpr *ty = parseType();
      if (!ty) return nullptr;
      e = ctx.make<ECast>(e->loc, e, ty);
      continue;
    }
    break;
  }
  return e;
}

Expr *Parser::parsePrimary(bool noStructLit) {
  if (noStructLit_ > 0) noStructLit = true;
  SourceLoc l = loc();
  const Token &t0 = tk();
  if (t0.kind == Tok::IntLit) {
    advance();
    auto *e = ctx.make<EInt>(l);
    e->value = t0.intValue;
    e->digits = t0.text;
    e->big128 = t0.big128;
    return e;
  }
  if (t0.kind == Tok::FloatLit) {
    advance();
    auto *e = ctx.make<EFloat>(l);
    e->value = t0.floatValue;
    return e;
  }
  if (t0.kind == Tok::CharLit) {
    advance();
    auto *e = ctx.make<EChar>(l);
    e->value = (unsigned)t0.intValue;
    return e;
  }
  if (t0.kind == Tok::StrLit) {
    advance();
    auto *e = ctx.make<EString>(l);
    e->value = t0.text;
    return e;
  }
  if (t0.kind == Tok::Kw) {
    if (t0.text == "true" || t0.text == "false") {
      advance();
      auto *e = ctx.make<EBool>(l);
      e->value = t0.text == "true";
      return e;
    }
    if (t0.text == "null") { advance(); return ctx.make<ENull>(l); }
    if (t0.text == "self") { advance(); return ctx.make<ESelf>(l); }
    if (t0.text == "sizeof" || t0.text == "alignof") {
      bool isSizeof = t0.text == "sizeof";
      advance();
      if (!expectPunct("(", isSizeof ? "in sizeof(...)" : "in alignof(...)")) return nullptr;
      TypeExpr *ty = parseType();
      if (!ty) return nullptr;
      if (!expectPunct(")", isSizeof ? "closing sizeof(...)" : "closing alignof(...)")) return nullptr;
      if (isSizeof) {
        auto *e = ctx.make<ESizeof>(l);
        e->ty = ty;
        return e;
      }
      auto *e = ctx.make<EAlignof>(l);
      e->ty = ty;
      return e;
    }
    if (t0.text == "func") { // lambda expression
      advance();
      auto *lam = ctx.make<ELambda>(l);
      if (!expectPunct("(", "in lambda parameter list")) return nullptr;
      bool varUnused = false;
      auto params = parseParamList(varUnused);
      if (!expectPunct(")", "closing lambda parameter list")) return nullptr;
      lam->params.clear();
      for (auto &p : params) {
        ELambda::Param lp;
        lp.name = p.name;
        lp.type = p.type;
        lp.defVal = p.defVal;
        lp.loc = p.loc;
        lam->params.push_back(lp);
      }
      if (eatPunct("->")) {
        lam->retType = parseType();
        if (!lam->retType) return nullptr;
      }
      lam->body = parseBlock();
      if (!lam->body) return nullptr;
      return lam;
    }
    if (t0.text == "unsafe") {
      advance();
      Stmt *b = parseBlock();
      if (!b) return nullptr;
      auto *ue = ctx.make<EUnsafeExpr>(l);
      ue->stmts = ((SBlock *)b)->stmts;
      return ue;
    }
    if (t0.text == "match") {
      advance();
      Expr *scrut = parseExpr(true);
      if (!scrut) return nullptr;
      skipNewlines();
      if (!expectPunct("{", "after match expression")) return nullptr;
      auto *m = ctx.make<EMatch>(l);
      m->scrutinee = scrut;
      skipNewlines();
      while (!atPunct("}")) {
        if (at(Tok::EndOfFile)) {
          errorAt(loc(), "unexpected end of file inside match expression", "", 0);
          return nullptr;
        }
        Pattern *p = parsePattern();
        if (!p) return nullptr;
        skipNewlines();
        Stmt *body = parseBlock(); // '{' consumed by parseBlock
        if (!body) return nullptr;
        m->arms.push_back({p, body});
        skipNewlines();
      }
      advance(); // }
      return m;
    }
  }
  if (t0.kind == Tok::Ident) {
    // possible struct literal: Name { ... } or Name<T> { ... }
    std::vector<std::string> parts;
    parts.push_back(advance().text);
    while (atPunct(".") && tk(1).kind == Tok::Ident) {
      advance();
      parts.push_back(advance().text);
    }
    std::vector<TypeExpr *> gargs;
    bool hasGenerics = false;
    if (atPunct("<") && !tk().newlineBefore) {
      // try to parse generic args, only committing if followed by '{'
      size_t save = i;
      quiet_++;
      advance();
      skipNewlines();
      bool ok = true;
      while (true) {
        TypeExpr *ga = parseType();
        if (!ga) { ok = false; break; }
        gargs.push_back(ga);
        skipNewlines();
        if (eatPunct(",")) { skipNewlines(); continue; }
        break;
      }
      quiet_--;
      if (ok && eatPunct(">") &&
          ((atPunct("{") && !tk().newlineBefore) || atPunct(".") ||
           (atPunct("(") && !tk().newlineBefore))) {
        hasGenerics = true;
      } else {
        i = save; // rewind: not a generic struct literal
        gargs.clear();
      }
    }
    if (atPunct("{") && !tk().newlineBefore && !noStructLit) {
      auto *sl = ctx.make<EStructLit>(l);
      auto *ty = ctx.makeNoLoc<TypeExpr>();
      ty->kind = TypeExpr::Named;
      ty->nameParts = parts;
      ty->genericArgs = gargs;
      ty->loc = l;
      sl->ty = ty;
      advance(); // {
      skipNewlines();
      while (!atPunct("}")) {
        if (at(Tok::EndOfFile) || at(Tok::Newline)) {
          errorAt(loc(), "unexpected end of struct literal", "struct literals list every field: `User { name: \"Alex\", age: 20 }`", tk().len);
          return nullptr;
        }
        Token fn = expectIdent("field name in struct literal");
        if (fn.kind != Tok::Ident) return nullptr;
        skipNewlines();
        if (!expectPunct(":", "after field name in struct literal")) return nullptr;
        skipNewlines();
        Expr *fv = parseAssignExpr();
        if (!fv) return nullptr;
        sl->fields.push_back({fn.text, fv});
        skipNewlines();
        if (eatPunct(",")) { skipNewlines(); continue; }
        break;
      }
      if (!expectPunct("}", "closing struct literal")) return nullptr;
      sl->loc = l;
      return sl;
    }
    // plain (possibly dotted) identifier
    Expr *e = ctx.make<EIdent>(l, parts[0]);
    if (hasGenerics) {
      auto *id = (EIdent *)e;
      auto *ty = ctx.makeNoLoc<TypeExpr>();
      ty->kind = TypeExpr::Named;
      ty->nameParts = parts;
      ty->genericArgs = gargs;
      ty->loc = l;
      id->typeArgs = ty;
    }
    for (size_t k = 1; k < parts.size(); k++) {
      e = ctx.make<EMember>(parts[k].size() ? e->loc : l, e, parts[k]);
    }
    // member access after generic args: `Option<i32>.Some(...)`
    if (hasGenerics && atPunct(".")) {
      while (atPunct(".") && (tk(1).kind == Tok::Ident || tk(1).kind == Tok::Kw)) {
        advance();
        Token nm = advance();
        e = ctx.make<EMember>(e->loc, e, nm.text);
      }
    }
    return e;
  }
  if (atPunct("(")) {
    advance();
    skipNewlines();
    Expr *e = parseExpr();
    if (!e) return nullptr;
    skipNewlines();
    if (!expectPunct(")", "closing parenthesized expression")) return nullptr;
    return e;
  }
  if (atPunct("[")) { // array literal [1, 2, 3] or [0; 10]
    advance();
    skipNewlines();
    auto *al = ctx.make<EArrayLit>(l);
    if (!atPunct("]")) {
      while (true) {
        Expr *el = parseAssignExpr();
        if (!el) return nullptr;
        al->elems.push_back(el);
        skipNewlines();
        if (eatPunct(";")) {
          // repeat syntax [value; count]
          Expr *n = parseExpr();
          if (!n) return nullptr;
          skipNewlines();
          al->repeat = n;
          skipNewlines();
          break;
        }
        if (eatPunct(",")) { skipNewlines(); if (atPunct("]")) break; continue; }
        break;
      }
    }
    if (!expectPunct("]", "closing array literal")) return nullptr;
    return al;
  }
  errorAt(l, strfmt("expected an expression, found %s", tokName(t0.kind)),
          "", t0.len);
  return nullptr;
}

// ------------------------------------------------------------ statements ----
Stmt *Parser::parseBlock() {
  SourceLoc l = loc();
  if (!expectPunct("{", "to start a block")) return nullptr;
  auto *b = ctx.make<SBlock>(l);
  skipNewlines();
  while (!atPunct("}")) {
    if (at(Tok::EndOfFile)) {
      errorAt(loc(), "unexpected end of file: missing '}'", "", 0);
      return nullptr;
    }
    Stmt *s = parseStatement();
    if (!s) return nullptr;
    b->stmts.push_back(s);
    // require separator
    if (at(Tok::Newline)) { skipNewlines(); continue; }
    if (atPunct("}")) break;
    if (atPunct(";")) { advance(); skipNewlines(); continue; }
    errorAt(loc(), strfmt("expected newline or '}' after statement, found '%s'", tk().text.c_str()),
            "", tk().len);
    return nullptr;
  }
  advance(); // }
  return b;
}

Stmt *Parser::parseStatement() {
  SourceLoc l = loc();
  if (atKw("return")) {
    advance();
    if (at(Tok::Newline) || atPunct("}") || atPunct(";"))
      return ctx.make<SReturn>(l, (Expr *)nullptr);
    Expr *e = parseExpr();
    if (!e) return nullptr;
    return ctx.make<SReturn>(l, e);
  }
  if (atKw("break")) { advance(); return ctx.make<SBreak>(l); }
  if (atKw("continue")) { advance(); return ctx.make<SContinue>(l); }
  if (atKw("if")) {
    advance();
    Expr *cond = parseExpr(true);
    if (!cond) return nullptr;
    skipNewlines();
    Stmt *thenB = parseBlock();
    if (!thenB) return nullptr;
    auto *s = ctx.make<SIf>(l);
    s->cond = cond;
    s->thenBlock = thenB;
    size_t save = i;
    skipNewlines();
    if (atKw("else")) {
      advance();
      skipNewlines();
      if (atKw("if")) s->elseBlock = parseStatement();
      else {
        skipNewlines();
        s->elseBlock = parseBlock();
      }
      if (!s->elseBlock) return nullptr;
    } else {
      i = save; // no else: restore the newline as the statement separator
    }
    return s;
  }
  if (atKw("while")) {
    advance();
    Expr *cond = parseExpr(true);
    if (!cond) return nullptr;
    skipNewlines();
    Stmt *body = parseBlock();
    if (!body) return nullptr;
    auto *s = ctx.make<SWhile>(l);
    s->cond = cond;
    s->body = body;
    return s;
  }
  if (atKw("for")) {
    advance();
    // detect form: `for x in ...` / `for mut x in ...` vs C-style `for i = 0; ...`
    bool looksLikeForIn = false;
    if (atIdent() && tk(1).kind == Tok::Kw && tk(1).text == "in") looksLikeForIn = true;
    if (atKw("mut") && tk(1).kind == Tok::Ident && tk(2).kind == Tok::Kw && tk(2).text == "in")
      looksLikeForIn = true;
    if (looksLikeForIn) {
      auto *s = ctx.make<SForIn>(l);
      s->isMut = eatKw("mut");
      Token v = expectIdent("loop variable");
      if (v.kind != Tok::Ident) return nullptr;
      s->varName = v.text;
      advance(); // 'in'
      Expr *it = parseExpr(true);
      if (!it) return nullptr;
      // ranges: 0..n or 0..=n
      if (atPunct("..") || atPunct("..=")) {
        bool incl = advance().text == "..=";
        Expr *hi = parseExpr(true);
        if (!hi) return nullptr;
        auto *r = ctx.make<ERange>(it->loc);
        r->lo = it;
        r->hi = hi;
        r->inclusive = incl;
        it = r;
      }
      s->iterable = it;
      skipNewlines();
      s->body = parseBlock();
      if (!s->body) return nullptr;
      return s;
    }
    auto *s = ctx.make<SFor>(l);
    if (!atPunct(";")) {
      s->init = parseLetOrExpr(false);
      if (!s->init) return nullptr;
    }
    if (!expectPunct(";", "after for-loop initializer")) return nullptr;
    if (!atPunct(";")) {
      s->cond = parseExpr(true);
      if (!s->cond) return nullptr;
    }
    if (!expectPunct(";", "after for-loop condition")) return nullptr;
    if (!atPunct("{") && !at(Tok::Newline)) {
      s->step = parseLetOrExpr(false);
      if (!s->step) return nullptr;
    }
    skipNewlines();
    s->body = parseBlock();
    if (!s->body) return nullptr;
    return s;
  }
  if (atKw("switch")) {
    advance();
    Expr *scrut = parseExpr(true);
    if (!scrut) return nullptr;
    skipNewlines();
    if (!expectPunct("{", "after switch expression")) return nullptr;
    auto *s = ctx.make<SSwitch>(l);
    s->scrutinee = scrut;
    skipNewlines();
    while (!atPunct("}")) {
      if (at(Tok::EndOfFile)) {
        errorAt(loc(), "unexpected end of file inside switch", "", 0);
        return nullptr;
      }
      if (eatKw("case")) {
        SSwitch::Case c;
        while (true) {
          Expr *v = parseExpr(true);
          if (!v) return nullptr;
          c.values.push_back(v);
          if (eatPunct(",")) continue;
          break;
        }
        skipNewlines();
        if (!expectPunct(":", "after case values")) return nullptr;
        auto *cb = ctx.make<SBlock>(loc());
        skipNewlines();
        while (!atKw("case") && !atKw("default") && !atPunct("}")) {
          if (at(Tok::EndOfFile)) {
            errorAt(loc(), "unexpected end of file inside switch case", "", 0);
            return nullptr;
          }
          Stmt *cs = parseStatement();
          if (!cs) return nullptr;
          cb->stmts.push_back(cs);
          if (at(Tok::Newline)) { skipNewlines(); continue; }
          if (atKw("case") || atKw("default") || atPunct("}")) break;
          if (atPunct(";")) { skipNewlines(); continue; }
          errorAt(loc(), strfmt("expected newline or '}' after statement in case, found '%s'",
                                tk().text.c_str()), "", tk().len);
          return nullptr;
        }
        c.body = cb;
        s->cases.push_back(c);
        continue;
      }
      if (eatKw("default")) {
        skipNewlines();
        if (!expectPunct(":", "after default")) return nullptr;
        auto *cb = ctx.make<SBlock>(loc());
        skipNewlines();
        while (!atKw("case") && !atKw("default") && !atPunct("}")) {
          if (at(Tok::EndOfFile)) {
            errorAt(loc(), "unexpected end of file inside switch default", "", 0);
            return nullptr;
          }
          Stmt *cs = parseStatement();
          if (!cs) return nullptr;
          cb->stmts.push_back(cs);
          if (at(Tok::Newline)) { skipNewlines(); continue; }
          if (atKw("case") || atKw("default") || atPunct("}")) break;
          if (atPunct(";")) { skipNewlines(); continue; }
          errorAt(loc(), strfmt("expected newline or '}' after statement in default, found '%s'",
                                tk().text.c_str()), "", tk().len);
          return nullptr;
        }
        s->defaultBody = cb;
        continue;
      }
      errorAt(loc(), strfmt("expected 'case' or 'default' inside switch, found '%s'",
                            tk().text.c_str()), "", tk().len);
      return nullptr;
    }
    advance(); // }
    return s;
  }
  if (atKw("unsafe")) {
    advance();
    Stmt *b = parseBlock();
    if (!b) return nullptr;
    auto *s = ctx.make<SUnsafe>(l);
    s->stmts = ((SBlock *)b)->stmts;
    return s;
  }
  if (atPunct("{")) return parseBlock(); // bare nested block
  if (atKw("mut")) return parseLetOrExpr(true);
  if (atKw("const")) {
    advance();
    Token n = expectIdent("constant name");
    if (n.kind != Tok::Ident) return nullptr;
    if (!expectPunct(":", "after constant name")) return nullptr;
    TypeExpr *ty = parseType();
    if (!ty) return nullptr;
    if (!expectPunct("=", "constants require an initializer")) return nullptr;
    Expr *init = parseExpr();
    if (!init) return nullptr;
    auto *sl = ctx.make<SLet>(l);
    sl->name = n.text;
    sl->type = ty;
    sl->init = init;
    sl->isConst = true;
    return sl;
  }
  return parseLetOrExpr(false);
}

Stmt *Parser::parseLetOrExpr(bool forceMut) {
  SourceLoc l = loc();
  bool isMut = forceMut;
  if (eatKw("mut")) isMut = true;
  // declaration form: [mut] name : type [= init]
  if (atIdent() && tk(1).kind == Tok::Punct && tk(1).text == ":") {
    Token n = advance();
    advance(); // ':'
    TypeExpr *ty = parseType();
    if (!ty) return nullptr;
    Expr *init = nullptr;
    if (eatPunct("=")) {
      init = parseExpr();
      if (!init) return nullptr;
    }
    auto *sl = ctx.make<SLet>(l);
    sl->name = n.text;
    sl->type = ty;
    sl->init = init;
    sl->isMut = isMut;
    return sl;
  }
  // plain `name = init` declaration (Go-like) when it cannot be an assignment:
  // decide in sema — the parser builds SLet with a sentinel init=nullptr only if
  // next is '='; otherwise fall through to expression/assignment.
  if (atIdent() && tk(1).kind == Tok::Punct && tk(1).text == "=") {
    // This is either a new variable (`x = 10`) or an assignment. The sema pass
    // resolves the difference; the parser marks it via a Let with empty type.
    Token n = advance();
    advance(); // '='
    Expr *init = parseExpr();
    if (!init) return nullptr;
    auto *sl = ctx.make<SLet>(l);
    sl->name = n.text;
    sl->init = init;
    sl->isMut = isMut; // `mut x = ...` declares a mutable variable
    sl->isDeclOrAssign = true;
    return sl;
  }
  // `say expr` is statement-level sugar for `say(expr)`: the print statement.
  // It grabs the full expression so `say x + y` prints (x + y).
  if (atIdent() && tk().text == "say") {
    const Token &n = tk(1);
    bool special = n.kind == Tok::Punct &&
                   (n.text == "." || n.text == "=" || n.text == "+=" ||
                    n.text == "-=" || n.text == "*=" || n.text == "/=" || n.text == "%=" ||
                    n.text == "&=" || n.text == "|=" || n.text == "^=" || n.text == "<<=" ||
                    n.text == ">>=" || n.text == ",");
    bool endsExpr = n.kind == Tok::Newline || n.kind == Tok::EndOfFile ||
                    (n.kind == Tok::Punct && n.text == "}");
    if (!special && !endsExpr) {
      Token sayTok = advance();
      Expr *arg = parseBinary(1);
      if (!arg) return nullptr;
      auto *ident = ctx.make<EIdent>(sayTok.loc, "say");
      Expr *call = ctx.make<ECall>(sayTok.loc, ident, std::vector<Expr *>{arg});
      return ctx.make<SExpr>(l, call);
    }
  }
  // plain expression or assignment statement
  Expr *e = parseAssignExpr();
  if (!e) return nullptr;
  return ctx.make<SExpr>(l, e);
}

// ------------------------------------------------------------ declarations --
std::vector<Param> Parser::parseParamList(bool &isVariadic) {
  std::vector<Param> params;
  skipNewlines();
  if (atPunct(")")) return params;
  while (true) {
    // variadic marker: '...' (single token)
    if (atPunct("...")) {
      advance();
      isVariadic = true;
      return params;
    }
    if (atPunct(".")) {
      errorAt(loc(), "unexpected '.'", "", 1);
      return params;
    }
    Token n = expectIdent("parameter name");
    if (n.kind != Tok::Ident) return params;
    if (!expectPunct(":", "after parameter name")) return params;
    TypeExpr *ty = parseType();
    if (!ty) return params;
    Expr *def = nullptr;
    if (eatPunct("=")) {
      def = parseExpr();
      if (!def) return params;
    }
    params.push_back({n.text, ty, def, n.loc});
    skipNewlines();
    if (eatPunct(",")) {
      skipNewlines();
      if (atPunct(")")) break;
      continue;
    }
    break;
  }
  return params;
}

namespace {
struct FuncMods {
  bool isPub = false, isStatic = false, isVirtual = false, isOverride = false,
       isAbstract = false, isUnsafe = false;
};
} // namespace

DFunc *Parser::parseFuncRest(bool isPub, Decl *parent, unsigned mods, std::string linkName) {
  SourceLoc l = prevLoc();
  Token n = expectIdent("function name");
  if (n.kind != Tok::Ident) return nullptr;
  auto *f = ctx.make<DFunc>(n.loc);
  f->name = n.text;
  f->isPub = isPub;
  f->isStatic = mods & MOD_STATIC;
  f->isVirtual = mods & MOD_VIRTUAL;
  f->isOverride = mods & MOD_OVERRIDE;
  f->isAbstract = mods & MOD_ABSTRACT;
  f->isUnsafe = mods & MOD_UNSAFE;
  f->linkName = linkName;
  f->parent = parent;
  if (eatPunct("<")) { // generic params
    while (true) {
      Token g = expectIdent("generic parameter name");
      if (g.kind != Tok::Ident) return nullptr;
      f->genericParams.push_back(g.text);
      if (eatPunct(",")) continue;
      break;
    }
    if (!expectPunct(">", "closing generic parameter list")) return nullptr;
  }
  if (!expectPunct("(", "to start function parameters")) return nullptr;
  bool variadic = false;
  f->params = parseParamList(variadic);
  f->isVariadic = variadic;
  if (!expectPunct(")", "closing function parameters")) return nullptr;
  if (eatPunct("->")) {
    f->retType = parseType();
    if (!f->retType) return nullptr;
  }
  if (atPunct(";")) { advance(); f->body = nullptr; return f; } // prototype with ';'
  // a prototype may also end at a newline / declaration boundary (extern + interfaces)
  if (at(Tok::Newline) || at(Tok::EndOfFile) || atPunct("}") || atKw("extern") || atKw("pub") ||
      atKw("func") || atKw("struct") || atKw("class") || atKw("interface") || atKw("trait") ||
      atKw("enum") || atKw("import") || atPunct("@") || atKw("abstract") || atKw("virtual") ||
      atKw("override")) {
    f->body = nullptr;
    return f; // newline NOT consumed: parseFile handles it
  }
  f->body = parseBlock();
  if (!f->body) return nullptr;
  return f;
}

DStruct *Parser::parseStruct(bool isPub, bool packed) {
  SourceLoc l = prevLoc();
  Token n = expectIdent("struct name");
  if (n.kind != Tok::Ident) return nullptr;
  auto *s = ctx.make<DStruct>(n.loc);
  s->name = n.text;
  s->isPub = isPub;
  s->isPacked = packed;
  if (eatPunct("<")) {
    while (true) {
      Token g = expectIdent("generic parameter name");
      if (g.kind != Tok::Ident) return nullptr;
      s->genericParams.push_back(g.text);
      if (eatPunct(",")) continue;
      break;
    }
    if (!expectPunct(">", "closing generic parameter list")) return nullptr;
  }
  skipNewlines();
  if (!expectPunct("{", "to start struct body")) return nullptr;
  skipNewlines();
  while (!atPunct("}")) {
    if (at(Tok::EndOfFile)) {
      errorAt(loc(), "unexpected end of file inside struct", "", 0);
      return nullptr;
    }
    if (getenv("CORE_DBG")) fprintf(stderr, "[struct] loop top, tk='%s' kind=%d\n", tk().text.c_str(), (int)tk().kind);
    unsigned smethods = 0;
    while (atKw("pub") || atKw("static")) {
      std::string m = advance().text;
      if (m == "pub") smethods |= MOD_PUB;
      else smethods |= MOD_STATIC;
    }
    if (eatKw("func")) {
      DFunc *m = parseFuncRest(smethods & MOD_PUB, s, smethods, "");
      if (!m) return nullptr;
      s->methods.push_back(m);
    } else {
      Token fn = expectIdent("field or method name in struct body");
      if (fn.kind != Tok::Ident) return nullptr;
      if (!expectPunct(":", "after field name")) return nullptr;
      TypeExpr *ty = parseType();
      if (!ty) return nullptr;
      Expr *defVal = nullptr;
      if (eatPunct("=")) {
        defVal = parseExpr();
        if (!defVal) return nullptr;
      }
      s->fields.push_back({fn.text, ty, defVal, fn.loc});
      if (getenv("CORE_DBG")) fprintf(stderr, "[struct] field %s ok, next=%s\n", fn.text.c_str(), tk().text.c_str());
    }
    if (at(Tok::Newline)) { skipNewlines(); continue; }
    if (atPunct(",")) { advance(); skipNewlines(); continue; }
    if (atPunct("}")) break;
    if (atPunct(";")) { advance(); skipNewlines(); continue; }
    errorAt(loc(), strfmt("expected newline or '}' in struct body, found '%s'", tk().text.c_str()), "", tk().len);
    return nullptr;
  }
  advance(); // }
  return s;
}

DClass *Parser::parseClass(bool isPub, bool packed) {
  SourceLoc l = prevLoc();
  Token n = expectIdent("class name");
  if (n.kind != Tok::Ident) return nullptr;
  auto *c = ctx.make<DClass>(n.loc);
  c->name = n.text;
  c->isPub = isPub;
  if (eatPunct("<")) {
    while (true) {
      Token g = expectIdent("generic parameter name");
      if (g.kind != Tok::Ident) return nullptr;
      c->genericParams.push_back(g.text);
      if (eatPunct(",")) continue;
      break;
    }
    if (!expectPunct(">", "closing generic parameter list")) return nullptr;
  }
  if (eatPunct(":")) {
    TypeExpr *first = parseType();
    if (!first) return nullptr;
    c->base = first;
    while (eatPunct(",")) {
      TypeExpr *iface = parseType();
      if (!iface) return nullptr;
      c->interfaces.push_back(iface);
    }
  }
  skipNewlines();
  if (!expectPunct("{", "to start class body")) return nullptr;
  skipNewlines();
  while (!atPunct("}")) {
    if (at(Tok::EndOfFile)) {
      errorAt(loc(), "unexpected end of file inside class", "", 0);
      return nullptr;
    }
    unsigned mods = 0;
    while (atKw("pub") || atKw("static") || atKw("virtual") || atKw("override") ||
           atKw("abstract") || atKw("unsafe")) {
      std::string m = advance().text;
      if (m == "pub") mods |= MOD_PUB;
      else if (m == "static") mods |= MOD_STATIC;
      else if (m == "virtual") mods |= MOD_VIRTUAL;
      else if (m == "override") mods |= MOD_OVERRIDE;
      else if (m == "abstract") mods |= MOD_ABSTRACT;
      else if (m == "unsafe") mods |= MOD_UNSAFE;
    }
    if (eatKw("func")) {
      DFunc *m = parseFuncRest(mods & MOD_PUB, c, mods, "");
      if (!m) return nullptr;
      c->methods.push_back(m);
    } else {
      bool fieldPub = mods & MOD_PUB;
      Token fn = expectIdent("field name in class body");
      if (fn.kind != Tok::Ident) return nullptr;
      if (!expectPunct(":", "after field name")) return nullptr;
      TypeExpr *ty = parseType();
      if (!ty) return nullptr;
      Expr *defVal = nullptr;
      if (eatPunct("=")) {
        defVal = parseExpr();
        if (!defVal) return nullptr;
      }
      c->fields.push_back({fn.text, ty, defVal, fn.loc});
      (void)fieldPub;
    }
    if (at(Tok::Newline)) { skipNewlines(); continue; }
    if (atPunct(",")) { advance(); skipNewlines(); continue; }
    if (atPunct("}")) break;
    if (atPunct(";")) { advance(); skipNewlines(); continue; }
    errorAt(loc(), strfmt("expected newline or '}' in class body, found '%s'", tk().text.c_str()), "", tk().len);
    return nullptr;
  }
  advance(); // }
  return c;
}

DInterface *Parser::parseInterface(bool isPub, bool isTrait) {
  SourceLoc l = prevLoc();
  Token n = expectIdent("interface name");
  if (n.kind != Tok::Ident) return nullptr;
  auto *d = ctx.make<DInterface>(n.loc, isTrait);
  d->name = n.text;
  d->isPub = isPub;
  if (eatPunct("<")) {
    while (true) {
      Token g = expectIdent("generic parameter name");
      if (g.kind != Tok::Ident) return nullptr;
      d->genericParams.push_back(g.text);
      if (eatPunct(",")) continue;
      break;
    }
    if (!expectPunct(">", "closing generic parameter list")) return nullptr;
  }
  skipNewlines();
  if (!expectPunct("{", "to start interface body")) return nullptr;
  skipNewlines();
  while (!atPunct("}")) {
    if (at(Tok::EndOfFile)) {
      errorAt(loc(), "unexpected end of file inside interface", "", 0);
      return nullptr;
    }
    if (!expectKw("func", "in interface body")) return nullptr;
    DFunc *m = parseFuncRest(isPub, d, 0, "");
    if (!m) return nullptr;
    d->methods.push_back(m);
    if (at(Tok::Newline)) { skipNewlines(); continue; }
    if (atPunct(";")) { skipNewlines(); continue; }
    if (atPunct("}")) break;
    errorAt(loc(), strfmt("expected newline or '}' in interface body, found '%s'", tk().text.c_str()), "", tk().len);
    return nullptr;
  }
  advance(); // }
  return d;
}

DEnum *Parser::parseEnum(bool isPub) {
  SourceLoc l = prevLoc();
  Token n = expectIdent("enum name");
  if (n.kind != Tok::Ident) return nullptr;
  auto *e = ctx.make<DEnum>(n.loc);
  e->name = n.text;
  e->isPub = isPub;
  if (eatPunct("<")) {
    while (true) {
      Token g = expectIdent("generic parameter name");
      if (g.kind != Tok::Ident) return nullptr;
      e->genericParams.push_back(g.text);
      if (eatPunct(",")) continue;
      break;
    }
    if (!expectPunct(">", "closing generic parameter list")) return nullptr;
  }
  skipNewlines();
  if (!expectPunct("{", "to start enum body")) return nullptr;
  skipNewlines();
  while (!atPunct("}")) {
    if (at(Tok::EndOfFile)) {
      errorAt(loc(), "unexpected end of file inside enum", "", 0);
      return nullptr;
    }
    Token v = expectIdent("variant name in enum body");
    if (v.kind != Tok::Ident) return nullptr;
    EnumVariant var;
    var.name = v.text;
    var.loc = v.loc;
    if (eatPunct("(")) {
      skipNewlines();
      if (!atPunct(")")) {
        while (true) {
          TypeExpr *pt = parseType();
          if (!pt) return nullptr;
          var.payloadTypes.push_back(pt);
          skipNewlines();
          if (eatPunct(",")) { skipNewlines(); continue; }
          break;
        }
      }
      if (!expectPunct(")", "closing variant payload types")) return nullptr;
    }
    e->variants.push_back(var);
    if (at(Tok::Newline)) { skipNewlines(); continue; }
    if (eatPunct(",")) { skipNewlines(); continue; }
    if (atPunct("}")) break;
    if (atPunct(";")) { advance(); skipNewlines(); continue; }
    errorAt(loc(), strfmt("expected newline, ',' or '}' in enum body, found '%s'", tk().text.c_str()), "", tk().len);
    return nullptr;
  }
  advance(); // }
  return e;
}

Decl *Parser::parseGlobalOrConst(bool isPub, bool forceMut, bool forceTLS) {
  SourceLoc l = loc();
  bool isConst = atKw("const");
  bool isMut = forceMut, isTLS = forceTLS;
  if (isConst) advance();
  else {
    if (eatKw("tls")) isTLS = true;
    isMut = isMut || eatKw("mut");
    if (!isTLS && eatKw("tls")) isTLS = true;
  }
  Token n = expectIdent(isConst ? "constant name" : "variable name");
  if (n.kind != Tok::Ident) return nullptr;
  // friendly error for top-level code like `say "hi"` or `foo()`
  if (!atPunct(":") && !atPunct("=")) {
    errorAt(n.loc, strfmt("'%s' at top level looks like code, but top-level code is not executed",
                          n.text.c_str()),
            "wrap it in a function: `func main() { ... }` - see `core init`", n.text.size());
    return nullptr;
  }
  TypeExpr *ty = nullptr;
  if (atPunct(":")) {
    advance();
    ty = parseType();
    if (!ty) return nullptr;
  }
  Expr *init = nullptr;
  if (eatPunct("=")) {
    init = parseExpr();
    if (!init) return nullptr;
  } else if (isConst || !ty) {
    errorAt(l, isConst ? "constants require an initializer"
                       : "globals with an inferred type require an initializer (e.g. `x = 10`)",
            "", tk().len);
    return nullptr;
  }
  if (isConst) {
    auto *d = ctx.make<DConst>(n.loc);
    d->name = n.text;
    d->type = ty;
    d->init = init;
    d->isPub = isPub;
    return d;
  }
  auto *d = ctx.make<DGlobal>(n.loc);
  d->name = n.text;
  d->type = ty;
  d->init = init;
  d->isMut = isMut;
  d->isTLS = isTLS;
  d->isPub = isPub;
  return d;
}

DExtern *Parser::parseExtern(bool isPub, std::string linkName) {
  SourceLoc l = prevLoc();
  if (!expectKw("func", "after 'extern'")) return nullptr;
  DFunc *proto = parseFuncRest(isPub, nullptr, 0, linkName);
  if (!proto) return nullptr;
  proto->isExtern = true;
  proto->body = nullptr;
  auto *d = ctx.make<DExtern>(l);
  d->proto = proto;
  return d;
}

DImport *Parser::parseImport(bool isPub) {
  SourceLoc l = prevLoc();
  auto *d = ctx.make<DImport>(l);
  while (true) {
    Token p = expectIdent("module name in import");
    if (p.kind != Tok::Ident) return nullptr;
    d->parts.push_back(p.text);
    if (atPunct(".")) { advance(); continue; }
    break;
  }
  if (eatKw("as")) {
    Token a = expectIdent("import alias");
    if (a.kind != Tok::Ident) return nullptr;
    d->alias = a.text;
  }
  return d;
}

SourceUnit *Parser::parseFile() {
  auto *unit = ctx.makeNoLoc<SourceUnit>();
  unit->fileID = fileID;
  unit->path = sm.fileName(fileID);
  skipNewlines();
  std::vector<Attr> attrs;
  while (!at(Tok::EndOfFile)) {
    if (at(Tok::Newline)) { advance(); continue; }
    if (atPunct(";")) { advance(); continue; }
    // attributes
    if (atPunct("@")) {
      advance();
      Token an = expectIdent("attribute name");
      if (an.kind != Tok::Ident) return nullptr;
      Attr a;
      a.name = an.text;
      a.loc = an.loc;
      if (eatPunct("(")) {
        if (tk().kind != Tok::StrLit) {
          errorAt(loc(), "expected string literal in attribute", "", tk().len);
          return nullptr;
        }
        a.value = advance().text;
        if (!expectPunct(")", "closing attribute")) return nullptr;
      }
      attrs.push_back(a);
      skipNewlines();
      continue;
    }
    Decl *d = parseTopDecl(attrs);
    if (!d) return nullptr;
    attrs.clear();
    unit->decls.push_back(d);
    if (at(Tok::Newline) || at(Tok::EndOfFile)) { skipNewlines(); continue; }
    if (atPunct(";")) { skipNewlines(); continue; }
    errorAt(loc(), strfmt("expected newline after declaration, found '%s'", tk().text.c_str()), "", tk().len);
    return nullptr;
  }
  return unit;
}

Decl *Parser::parseTopDecl(std::vector<Attr> &attrs) {
  bool isPub = false, packed = false;
  unsigned mods = 0;
  std::string linkName;
  for (auto &a : attrs) {
    if (a.name == "packed") packed = true;
    else if (a.name == "link_name") linkName = a.value;
    else if (a.name == "inline" || a.name == "repr") {} // accepted, currently no-op
    else {
      errorAt(a.loc, strfmt("unknown attribute '@%s'", a.name.c_str()),
              "available attributes: @packed, @link_name, @inline, @repr", a.name.size() + 1);
      return nullptr;
    }
  }
  // modifiers may appear in any order
  while (true) {
    if (atKw("pub")) { isPub = true; advance(); continue; }
    if (atKw("static")) { mods |= MOD_STATIC; advance(); continue; }
    if (atKw("virtual")) { mods |= MOD_VIRTUAL; advance(); continue; }
    if (atKw("override")) { mods |= MOD_OVERRIDE; advance(); continue; }
    if (atKw("abstract")) { mods |= MOD_ABSTRACT; advance(); continue; }
    if (atKw("unsafe")) { mods |= MOD_UNSAFE; advance(); continue; }
    if (atKw("mut")) { mods |= MOD_MUT; advance(); continue; }
    if (atKw("tls")) { mods |= MOD_TLS; advance(); continue; }
    break;
  }
  if (atKw("import")) { advance(); return parseImport(isPub); }
  if (atKw("func")) {
    advance();
    return parseFuncRest(isPub, nullptr, mods, linkName);
  }
  if (atKw("struct")) { advance(); return parseStruct(isPub, packed); }
  if (atKw("class")) { advance(); return parseClass(isPub, packed); }
  if (atKw("interface")) { advance(); return parseInterface(isPub, false); }
  if (atKw("trait")) { advance(); return parseInterface(isPub, true); }
  if (atKw("enum")) { advance(); return parseEnum(isPub); }
  if (atKw("extern")) { advance(); return parseExtern(isPub, linkName); }
  if (atKw("const")) return parseGlobalOrConst(isPub, mods & MOD_MUT, mods & MOD_TLS);
  if (atKw("mut") || atKw("tls") || atIdent())
    return parseGlobalOrConst(isPub, mods & MOD_MUT, mods & MOD_TLS);
  errorAt(loc(), strfmt("expected a declaration, found %s", tokName(tk().kind)),
          "declarations: func, struct, class, interface, trait, enum, import, extern, global variables, const",
          tk().len);
  return nullptr;
}

} // namespace core
