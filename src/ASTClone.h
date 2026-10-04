// Deep AST cloning, used for generic monomorphization: each instantiation
// gets its own copy of the template body so semantic annotations can be
// attached per-instance without interference.
#ifndef CORE_ASTCLONE_H
#define CORE_ASTCLONE_H

#include "AST.h"

namespace core {

class ASTCloner {
public:
  explicit ASTCloner(ASTContext &ctx) : ctx(ctx) {}

  TypeExpr *type(TypeExpr *t) {
    if (!t) return nullptr;
    TypeExpr *n = ctx.makeNoLoc<TypeExpr>();
    *n = *t; // shallow copy of scalars
    n->loc = t->loc;
    n->genericArgs.clear(); // vectors were shallow-copied; rebuild deep
    for (auto *ga : t->genericArgs) n->genericArgs.push_back(type(ga));
    if (t->elem) n->elem = type(t->elem);
    if (t->arraySize) n->arraySize = expr(t->arraySize);
    if (t->retType) n->retType = type(t->retType);
    n->paramTypes.clear();
    for (auto *p : t->paramTypes) n->paramTypes.push_back(type(p));
    return n;
  }

  Pattern *pattern(Pattern *p) {
    if (!p) return nullptr;
    Pattern *n = ctx.makeNoLoc<Pattern>();
    *n = *p;
    n->loc = p->loc;
    for (auto *s : p->subs) n->subs.push_back(pattern(s));
    if (p->litExpr) n->litExpr = expr(p->litExpr);
    return n;
  }

  Expr *expr(Expr *e) {
    if (!e) return nullptr;
    switch (e->kind) {
    case Expr::IntLit: { auto *n = ctx.make<EInt>(e->loc); *n = *(EInt *)e; return n; }
    case Expr::FloatLit: { auto *n = ctx.make<EFloat>(e->loc); *n = *(EFloat *)e; return n; }
    case Expr::BoolLit: { auto *n = ctx.make<EBool>(e->loc); *n = *(EBool *)e; return n; }
    case Expr::CharLit: { auto *n = ctx.make<EChar>(e->loc); *n = *(EChar *)e; return n; }
    case Expr::StringLit: { auto *n = ctx.make<EString>(e->loc); *n = *(EString *)e; return n; }
    case Expr::NullLit: return ctx.make<ENull>(e->loc);
    case Expr::Ident: { auto *n = ctx.make<EIdent>(e->loc, ((EIdent *)e)->name); copyId(n, e); return n; }
    case Expr::Self: { auto *n = ctx.make<ESelf>(e->loc); copyId(n, e); return n; }
    case Expr::Unary: {
      auto *u = (EUnary *)e;
      auto *n = ctx.make<EUnary>(e->loc, u->op, expr(u->operand));
      copyId(n, e);
      return n;
    }
    case Expr::Binary: {
      auto *b = (EBinary *)e;
      auto *n = ctx.make<EBinary>(e->loc, b->op, expr(b->lhs), expr(b->rhs));
      copyId(n, e);
      return n;
    }
    case Expr::Assign: {
      auto *a = (EAssign *)e;
      auto *n = ctx.make<EAssign>(e->loc, a->op, expr(a->target), expr(a->value));
      copyId(n, e);
      return n;
    }
    case Expr::Cast: {
      auto *c = (ECast *)e;
      auto *n = ctx.make<ECast>(e->loc, expr(c->e), type(c->ty));
      copyId(n, e);
      return n;
    }
    case Expr::Call: {
      auto *c = (ECall *)e;
      std::vector<Expr *> args;
      for (auto *a : c->args) args.push_back(expr(a));
      auto *n = ctx.make<ECall>(e->loc, expr(c->callee), std::move(args));
      n->expectedType = c->expectedType;
      copyId(n, e);
      return n;
    }
    case Expr::Member: {
      auto *m = (EMember *)e;
      auto *n = ctx.make<EMember>(e->loc, expr(m->obj), m->name);
      n->callTypeArgs = m->callTypeArgs; // shared TypeExprs (types are immutable)
      copyId(n, e);
      return n;
    }
    case Expr::Index: {
      auto *m = (EIndex *)e;
      auto *n = ctx.make<EIndex>(e->loc, expr(m->base), expr(m->index));
      copyId(n, e);
      return n;
    }
    case Expr::StructLit: {
      auto *s = (EStructLit *)e;
      auto *n = ctx.make<EStructLit>(e->loc);
      n->ty = type(s->ty);
      for (auto &f : s->fields) n->fields.push_back({f.first, expr(f.second)});
      copyId(n, e);
      return n;
    }
    case Expr::ArrayLit: {
      auto *s = (EArrayLit *)e;
      auto *n = ctx.make<EArrayLit>(e->loc);
      for (auto *el : s->elems) n->elems.push_back(expr(el));
      if (s->repeat) n->repeat = expr(s->repeat);
      copyId(n, e);
      return n;
    }
    case Expr::Lambda: {
      auto *l = (ELambda *)e;
      auto *n = ctx.make<ELambda>(e->loc);
      for (auto &p : l->params) n->params.push_back({p.name, type(p.type), p.defVal ? expr(p.defVal) : nullptr, p.loc});
      n->retType = type(l->retType);
      n->body = stmt(l->body);
      copyId(n, e);
      return n;
    }
    case Expr::UnsafeExpr: {
      auto *u = (EUnsafeExpr *)e;
      auto *n = ctx.make<EUnsafeExpr>(e->loc);
      for (auto *x : u->stmts) n->stmts.push_back(stmt(x));
      copyId(n, e);
      return n;
    }
    case Expr::Match: {
      auto *m = (EMatch *)e;
      auto *n = ctx.make<EMatch>(e->loc);
      n->scrutinee = expr(m->scrutinee);
      for (auto &arm : m->arms) n->arms.push_back({pattern(arm.pattern), stmt(arm.body)});
      copyId(n, e);
      return n;
    }
    case Expr::Range: {
      auto *r = (ERange *)e;
      auto *n = ctx.make<ERange>(e->loc);
      n->lo = expr(r->lo);
      n->hi = expr(r->hi);
      n->inclusive = r->inclusive;
      n->type = r->type;
      return n;
    }
    case Expr::Sizeof: { auto *n = ctx.make<ESizeof>(e->loc); n->ty = type(((ESizeof *)e)->ty); n->type = e->type; return n; }
    case Expr::Alignof: { auto *n = ctx.make<EAlignof>(e->loc); n->ty = type(((EAlignof *)e)->ty); n->type = e->type; return n; }
    }
    return nullptr;
  }

  Stmt *stmt(Stmt *s) {
    if (!s) return nullptr;
    switch (s->kind) {
    case Stmt::KExpr: { auto *n = ctx.make<SExpr>(s->loc, expr(((SExpr *)s)->e)); return n; }
    case Stmt::KLet: {
      auto *l = (SLet *)s;
      auto *n = ctx.make<SLet>(s->loc);
      n->isMut = l->isMut;
      n->isConst = l->isConst;
      n->isDeclOrAssign = l->isDeclOrAssign;
      n->name = l->name;
      n->type = type(l->type);
      n->init = expr(l->init);
      n->resolvedType = l->resolvedType;
      n->isAssignExisting = l->isAssignExisting;
      return n;
    }
    case Stmt::KReturn: { auto *n = ctx.make<SReturn>(s->loc, expr(((SReturn *)s)->e)); return n; }
    case Stmt::KIf: {
      auto *x = (SIf *)s;
      auto *n = ctx.make<SIf>(s->loc);
      n->cond = expr(x->cond);
      n->thenBlock = stmt(x->thenBlock);
      n->elseBlock = stmt(x->elseBlock);
      return n;
    }
    case Stmt::KWhile: {
      auto *x = (SWhile *)s;
      auto *n = ctx.make<SWhile>(s->loc);
      n->cond = expr(x->cond);
      n->body = stmt(x->body);
      return n;
    }
    case Stmt::KFor: {
      auto *x = (SFor *)s;
      auto *n = ctx.make<SFor>(s->loc);
      n->init = stmt(x->init);
      n->cond = expr(x->cond);
      n->step = stmt(x->step);
      n->body = stmt(x->body);
      return n;
    }
    case Stmt::KForIn: {
      auto *x = (SForIn *)s;
      auto *n = ctx.make<SForIn>(s->loc);
      n->isMut = x->isMut;
      n->varName = x->varName;
      n->iterable = expr(x->iterable);
      n->body = stmt(x->body);
      return n;
    }
    case Stmt::KBreak: return ctx.make<SBreak>(s->loc);
    case Stmt::KContinue: return ctx.make<SContinue>(s->loc);
    case Stmt::KSwitch: {
      auto *x = (SSwitch *)s;
      auto *n = ctx.make<SSwitch>(s->loc);
      n->scrutinee = expr(x->scrutinee);
      for (auto &c : x->cases) {
        std::vector<Expr *> vs;
        for (auto *v : c.values) vs.push_back(expr(v));
        n->cases.push_back({std::move(vs), stmt(c.body)});
      }
      n->defaultBody = stmt(x->defaultBody);
      return n;
    }
    case Stmt::KBlock: {
      auto *n = ctx.make<SBlock>(s->loc);
      for (auto *x : ((SBlock *)s)->stmts) n->stmts.push_back(stmt(x));
      return n;
    }
    case Stmt::KUnsafe: {
      auto *n = ctx.make<SUnsafe>(s->loc);
      for (auto *x : ((SUnsafe *)s)->stmts) n->stmts.push_back(stmt(x));
      return n;
    }
    }
    return nullptr;
  }

  // Clone a function's parameters and body (declaration identity preserved).
  void funcBody(DFunc *from, DFunc *to) {
    to->params.clear();
    for (auto &p : from->params)
      to->params.push_back({p.name, type(p.type), p.defVal ? expr(p.defVal) : nullptr, p.loc});
    to->retType = type(from->retType);
    to->body = stmt(from->body);
  }

private:
  ASTContext &ctx;
  void copyId(Expr *dst, Expr *src) {
    dst->type = src->type;
    dst->idKind = src->idKind;
    dst->target = src->target;
    dst->enumTag = src->enumTag;
    dst->builtin = src->builtin;
    dst->memberKind = src->memberKind;
    dst->memberIndex = src->memberIndex;
    dst->viaModule = src->viaModule;
    dst->resolvedFunc = src->resolvedFunc;
    dst->genInstance = src->genInstance;
    dst->ifaceMethodIndex = src->ifaceMethodIndex;
    dst->castKind = src->castKind;
    dst->binKind = src->binKind;
    dst->unKind = src->unKind;
    dst->checkBounds = src->checkBounds;
    dst->scopeId = src->scopeId;
  }
};

} // namespace core
#endif
