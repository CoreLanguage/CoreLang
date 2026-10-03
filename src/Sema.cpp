#include "Sema.h"
#include "ASTClone.h"
#include <algorithm>
#include <cassert>

namespace core {

// forward helpers used across checking
static Expr *stripNeg(Expr *e) {
  while (e && e->kind == Expr::Unary && ((EUnary *)e)->op == "-")
    e = ((EUnary *)e)->operand;
  return e;
}

static bool builtinByName(const std::string &n, int &out) {
  struct Entry { const char *name; int b; };
  static const Entry table[] = {
    {"len", (int)Builtin::Len},
    {"source_file", (int)Builtin::SourceFile},
    {"source_line", (int)Builtin::SourceLine},
    {"atomic_load", (int)Builtin::AtomicLoad},
    {"atomic_store", (int)Builtin::AtomicStore},
    {"atomic_add", (int)Builtin::AtomicAdd},
    {"atomic_sub", (int)Builtin::AtomicSub},
    {"atomic_swap", (int)Builtin::AtomicSwap},
    {"atomic_cas", (int)Builtin::AtomicCas},
    {"atomic_fence", (int)Builtin::AtomicFence},
    {"volatile_load", (int)Builtin::VolatileLoad},
    {"volatile_store", (int)Builtin::VolatileStore},
    {"asm", (int)Builtin::Asm},
    {"asm_volatile", (int)Builtin::AsmVolatile},
    {nullptr, 0},
  };
  for (int i = 0; table[i].name; i++)
    if (n == table[i].name) { out = table[i].b; return true; }
  if (n.rfind("splat_", 0) == 0) { out = (int)Builtin::Splat; return true; }
  if (n.rfind("simd_extract_", 0) == 0) { out = (int)Builtin::SimdExtract; return true; }
  if (n.rfind("simd_replace_", 0) == 0) { out = (int)Builtin::SimdReplace; return true; }
  return false;
}

// ============================================================ registration ==
bool Sema::registerModules(std::vector<ModuleSema *> &mods) {
  modules = mods;
  for (auto *m : modules) byPath[m->path] = m;
  for (auto *m : modules) {
    if (m->name == "prelude") prelude = m;
    curModule = m;
    for (Decl *d : m->unit->decls) {
      switch (d->kind) {
      case Decl::Struct: {
        auto *s = (DStruct *)d;
        if (m->types.count(s->name)) {
          diag.error(d->loc, strfmt("duplicate type '%s' in module '%s'", s->name.c_str(), m->name.c_str()));
          return false;
        }
        m->types[s->name] = s;
        declModule[s] = m;
        buildStructLayout(s);
        break;
      }
      case Decl::Class: {
        auto *c = (DClass *)d;
        if (m->types.count(c->name)) {
          diag.error(d->loc, strfmt("duplicate type '%s' in module '%s'", c->name.c_str(), m->name.c_str()));
          return false;
        }
        m->types[c->name] = c;
        declModule[c] = m;
        break;
      }
      case Decl::Interface: case Decl::Trait: {
        auto *it = (DInterface *)d;
        if (m->types.count(it->name)) {
          diag.error(d->loc, strfmt("duplicate type '%s' in module '%s'", it->name.c_str(), m->name.c_str()));
          return false;
        }
        m->types[it->name] = it;
        declModule[it] = m;
        break;
      }
      case Decl::Enum: {
        auto *e = (DEnum *)d;
        if (m->types.count(e->name)) {
          diag.error(d->loc, strfmt("duplicate type '%s' in module '%s'", e->name.c_str(), m->name.c_str()));
          return false;
        }
        m->types[e->name] = e;
        declModule[e] = m;
        break;
      }
      case Decl::Func: case Decl::Extern: {
        DFunc *f = d->kind == Decl::Func ? (DFunc *)d : ((DExtern *)d)->proto;
        if (d->kind == Decl::Extern) f->isExtern = true;
        m->funcs[f->name].push_back(f);
        funcModule[f] = m;
        break;
      }
      case Decl::Global: {
        auto *g = (DGlobal *)d;
        if (m->globals.count(g->name) || m->consts.count(g->name)) {
          diag.error(d->loc, strfmt("duplicate global '%s'", g->name.c_str()));
          return false;
        }
        m->globals[g->name] = g;
        break;
      }
      case Decl::Const: {
        auto *g = (DConst *)d;
        if (m->globals.count(g->name) || m->consts.count(g->name)) {
          diag.error(d->loc, strfmt("duplicate constant '%s'", g->name.c_str()));
          return false;
        }
        m->consts[g->name] = g;
        break;
      }
      default: break; // imports handled by the driver
      }
    }
  }
  // build class layouts (after all types registered, so bases resolve)
  for (auto *m : modules) {
    curModule = m;
    for (Decl *d : m->unit->decls) {
      if (d->kind == Decl::Class) buildLayout((DClass *)d);
    }
  }
  return !diag.hasErrors();
}

bool Sema::visible(Decl *d, ModuleSema *from) {
  bool isPub = false;
  switch (d->kind) {
  case Decl::Func: isPub = ((DFunc *)d)->isPub; break;
  case Decl::Struct: isPub = ((DStruct *)d)->isPub; break;
  case Decl::Class: isPub = ((DClass *)d)->isPub; break;
  case Decl::Interface: case Decl::Trait: isPub = ((DInterface *)d)->isPub; break;
  case Decl::Enum: isPub = ((DEnum *)d)->isPub; break;
  case Decl::Global: isPub = ((DGlobal *)d)->isPub; break;
  case Decl::Const: isPub = ((DConst *)d)->isPub; break;
  case Decl::Extern: isPub = ((DExtern *)d)->proto->isPub; break;
  default: isPub = false;
  }
  (void)from;
  return isPub;
}

Decl *Sema::lookupTypeOwn(const std::string &name) {
  if (!curModule) return nullptr;
  auto it = curModule->types.find(name);
  return it != curModule->types.end() ? it->second : nullptr;
}

Decl *Sema::lookupTypeVisible(const std::string &name, ModuleSema **via) {
  if (Decl *d = lookupTypeOwn(name)) return d;
  // imports
  for (auto &[iname, mod] : curModule->imports) {
    auto it = mod->types.find(name);
    if (it != mod->types.end() && visible(it->second, curModule)) {
      if (via) *via = mod;
      return it->second;
    }
  }
  // prelude last
  if (prelude && prelude != curModule) {
    auto it = prelude->types.find(name);
    if (it != prelude->types.end() && visible(it->second, curModule)) {
      if (via) *via = prelude;
      return it->second;
    }
  }
  return nullptr;
}

std::vector<DFunc *> Sema::lookupFuncsVisible(const std::string &name, ModuleSema **via) {
  std::vector<DFunc *> out;
  if (curModule) {
    auto it = curModule->funcs.find(name);
    if (it != curModule->funcs.end()) out = it->second;
  }
  if (!out.empty()) return out;
  for (auto &[iname, mod] : curModule->imports) {
    auto it = mod->funcs.find(name);
    if (it != mod->funcs.end()) {
      std::vector<DFunc *> pub;
      for (auto *f : it->second) if (visible(f, curModule)) pub.push_back(f);
      if (!pub.empty()) {
        if (via) *via = mod;
        return pub;
      }
    }
  }
  if (prelude && prelude != curModule) {
    auto it = prelude->funcs.find(name);
    if (it != prelude->funcs.end()) {
      for (auto *f : it->second) if (visible(f, curModule)) out.push_back(f);
    }
  }
  return out;
}

ModuleSema *Sema::lookupModuleRef(const std::string &name) {
  if (!curModule) return nullptr;
  for (auto &[iname, mod] : curModule->imports)
    if (iname == name) return mod;
  return nullptr;
}

// ================================================================ layouts ==
ClassLayout *Sema::layoutOf(Decl *d) {
  auto it = layouts.find(d);
  return it != layouts.end() ? it->second : nullptr;
}

void Sema::buildStructLayout(DStruct *s) {
  auto *l = new ClassLayout();
  l->cls = nullptr;
  layouts[s] = l;
}

void Sema::buildLayout(DClass *c) {
  if (layouts.count(c)) return;
  auto *l = new ClassLayout();
  l->cls = c;
  layouts[c] = l;

  // resolve base: the first name may be an interface (implement, not inherit)
  if (c->base) {
    Type *bt = resolveType(c->base);
    if (bt && bt->isInterface()) {
      DInterface *iface = (DInterface *)bt->ifaceDecl;
      bool already = false;
      for (auto &[iff, _] : l->interfaces)
        if (iff == iface) already = true;
      if (!already) l->interfaces.push_back({iface, ""});
      c->base = nullptr; // no base class: implementing, not inheriting
    } else if (!bt || !bt->isClass()) {
      diag.error(c->base->loc, "base class must be a class type", "", 1);
      return;
    } else {
      DClass *base = (DClass *)bt->decl;
      if (base == c) {
        diag.error(c->loc, strfmt("class '%s' cannot inherit from itself", c->name.c_str()));
        return;
      }
      if (!layouts.count(base)) buildLayout(base);
      l->base = base;
    }
  }
  // polymorphic?
  bool poly = false;
  for (DFunc *mth : c->methods)
    if (mth->isVirtual || mth->isAbstract) poly = true;
  if (l->base && layoutOf(l->base)->polymorphic) poly = true;
  // abstract classes are polymorphic so vptr exists for dynamic dispatch
  if (c->isAbstract) poly = true;
  for (auto *ie : c->interfaces) {
    Type *it = resolveType(ie);
    if (it && it->isInterface()) l->interfaces.push_back({(DInterface *)it->ifaceDecl, ""});
  }
  l->polymorphic = poly;
  // vtable slots: base slots first, overrides keep slot, new virtuals append
  if (l->base) l->vtableOrder = layoutOf(l->base)->vtableOrder;
  for (DFunc *mth : c->methods) {
    if (!(mth->isVirtual || mth->isAbstract || mth->isOverride)) continue;
    auto it = std::find_if(l->vtableOrder.begin(), l->vtableOrder.end(),
                           [&](DFunc *v) { return v->name == mth->name; });
    if (it != l->vtableOrder.end()) *it = mth;
    else l->vtableOrder.push_back(mth);
  }
  for (size_t i = 0; i < l->vtableOrder.size(); i++)
    l->vtableSlots[l->vtableOrder[i]->name] = (int)i;
}

// ============================================================ type resolve ==
Type *Sema::resolveType(TypeExpr *te) {
  if (!te) return tc.prim(PRIM_void);
  switch (te->kind) {
  case TypeExpr::Prim:
    if (te->prim == PRIM_never) return tc.never();
    return tc.prim(te->prim);
  case TypeExpr::Named: {
    return resolveNamedType(te);
  }
  case TypeExpr::Array: {
    Type *elem = resolveType(te->elem);
    if (!elem) return nullptr;
    bool ok = true;
    unsigned long long n = evalConstUint(te->arraySize, ok);
    if (!ok) {
      diag.error(te->arraySize ? te->arraySize->loc : te->loc,
                 "array size must be a compile-time constant", "", 1);
      return nullptr;
    }
    if (n == 0 || n > (1ULL << 30)) {
      diag.error(te->loc, "array size must be between 1 and 2^30", "", 1);
      return nullptr;
    }
    return tc.array(elem, (long long)n);
  }
  case TypeExpr::Func: {
    Type *ret = te->retType ? resolveType(te->retType) : tc.prim(PRIM_void);
    std::vector<Type *> params;
    for (auto *p : te->paramTypes) params.push_back(resolveType(p));
    return tc.func(ret, params);
  }
  }
  return nullptr;
}

Type *Sema::resolveNamedType(TypeExpr *te) {
  // builtin pointer type constructor: ptr<T>
  if (te->nameParts.size() == 1 && te->nameParts[0] == "ptr") {
    if (te->genericArgs.size() == 1) {
      Type *pointee = resolveType(te->genericArgs[0]);
      if (!pointee) return nullptr;
      return tc.ptr(pointee);
    }
    diag.error(te->loc, "ptr expects exactly one type argument: `ptr<T>`", "", 3);
    return nullptr;
  }
  // generic variable in scope?
  if (te->nameParts.size() == 1 && te->genericArgs.empty() && subst) {
    auto it = subst->find(te->nameParts[0]);
    if (it != subst->end()) return it->second;
  }
  if (te->nameParts.size() == 1 && te->genericArgs.empty()) {
    // inside a generic template body without subst: create standing var
    if (curFunc && std::find(curFunc->genericParams.begin(), curFunc->genericParams.end(),
                             te->nameParts[0]) != curFunc->genericParams.end())
      return tc.genericVar(te->nameParts[0]);
    Decl *pd = curFunc ? curFunc->parent : nullptr;
    if (pd && pd->kind == Decl::Class) {
      auto *c = (DClass *)pd;
      if (std::find(c->genericParams.begin(), c->genericParams.end(), te->nameParts[0]) != c->genericParams.end())
        return tc.genericVar(te->nameParts[0]);
    }
    if (pd && pd->kind == Decl::Struct) {
      auto *c = (DStruct *)pd;
      if (std::find(c->genericParams.begin(), c->genericParams.end(), te->nameParts[0]) != c->genericParams.end())
        return tc.genericVar(te->nameParts[0]);
    }
  }
  // primitive vector shorthand etc. handled by Prim kind; here: user types
  Decl *d = nullptr;
  ModuleSema *via = nullptr;
  if (te->nameParts.size() == 1) {
    d = lookupTypeVisible(te->nameParts[0], &via);
  } else {
    // module-qualified: math.Vec
    ModuleSema *mod = lookupModuleRef(te->nameParts[0]);
    if (mod) {
      std::string tn = te->nameParts[1];
      auto it = mod->types.find(tn);
      if (it != mod->types.end() && visible(it->second, curModule)) {
        d = it->second;
        via = mod;
      }
    }
  }
  if (!d) {
    std::string name;
    for (size_t i = 0; i < te->nameParts.size(); i++) {
      if (i) name += ".";
      name += te->nameParts[i];
    }
    if (quiet_ == 0)
      diag.error(te->loc, strfmt("unknown type '%s'", name.c_str()), "", 1);
    return nullptr;
  }
  std::vector<Type *> args;
  for (auto *ga : te->genericArgs) {
    Type *at = resolveType(ga);
    if (!at) return nullptr;
    args.push_back(at);
  }
  if (d->kind == Decl::Struct) {
    auto *s = (DStruct *)d;
    if (!s->genericParams.empty() && args.size() != s->genericParams.size()) {
      diag.error(te->loc, strfmt("struct '%s' expects %zu type arguments, got %zu",
                                 s->name.c_str(), s->genericParams.size(), args.size()));
      return nullptr;
    }
    return tc.getStruct(s, args);
  }
  if (d->kind == Decl::Class) {
    auto *c = (DClass *)d;
    if (!c->genericParams.empty() && args.size() != c->genericParams.size()) {
      diag.error(te->loc, strfmt("class '%s' expects %zu type arguments, got %zu",
                                 c->name.c_str(), c->genericParams.size(), args.size()));
      return nullptr;
    }
    return tc.getClass(c, args);
  }
  if (d->kind == Decl::Interface || d->kind == Decl::Trait) {
    if (!te->genericArgs.empty()) {
      diag.error(te->loc, "generic interfaces are not supported yet");
      return nullptr;
    }
    return tc.getInterface(d);
  }
  if (d->kind == Decl::Enum) {
    auto *e = (DEnum *)d;
    if (!e->genericParams.empty() && args.size() != e->genericParams.size()) {
      diag.error(te->loc, strfmt("enum '%s' expects %zu type arguments, got %zu",
                                 e->name.c_str(), e->genericParams.size(), args.size()));
      return nullptr;
    }
    return tc.getEnum(e, args);
  }
  diag.error(te->loc, strfmt("'%s' is not a type", te->nameParts[0].c_str()));
  return nullptr;
}

// ============================================================== mangling ====
std::string Sema::mangleFuncName(DFunc *f, const std::vector<Type *> &genericArgs) {
  if (f->isExtern) return f->linkName.empty() ? f->name : f->linkName;
  if (!f->linkName.empty()) return f->linkName; // explicit symbol override (@link_name)
  if (f->name == "main" && f->parent == nullptr && !f->isExtern) return "main";
  std::string out = "_C";
  // module path components: the DEFINING module of this function
  std::string mod;
  auto fm = funcModule.find(f);
  if (fm != funcModule.end()) mod = fm->second->name;
  else if (curModule) mod = curModule->name;
  if (f->parent) {
    auto tm = declModule.find(f->parent);
    if (tm != declModule.end()) mod = tm->second->name;
  }
  size_t start = 0;
  while (true) {
    size_t dot = mod.find('.', start);
    std::string part = mod.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    out += std::to_string(part.size()) + part;
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (f->parent) {
    std::string pn;
    if (f->parent->kind == Decl::Class) pn = ((DClass *)f->parent)->name;
    else if (f->parent->kind == Decl::Struct) pn = ((DStruct *)f->parent)->name;
    else if (f->parent->kind == Decl::Interface || f->parent->kind == Decl::Trait)
      pn = ((DInterface *)f->parent)->name;
    out += std::to_string(pn.size()) + pn;
  }
  out += std::to_string(f->name.size()) + f->name;
  // parameter types disambiguate overloads (and specialize generics);
  // resolution is quiet: unresolved (template) params use a placeholder
  std::vector<Type *> ptypes;
  bool ptypesOk = true;
  quiet_++;
  for (auto &p : f->params) {
    Type *pt = resolveType(p.type);
    if (!pt) { ptypesOk = false; break; }
    ptypes.push_back(pt);
  }
  quiet_--;
  if (ptypesOk && !f->isExtern) {
    out += "I";
    for (size_t pi = 0; pi < ptypes.size(); pi++)
      out += mangleTypeForName(ptypes[pi]);
    out += "E";
  }
  if (!genericArgs.empty()) {
    out += "G";
    for (size_t gi = 0; gi < genericArgs.size(); gi++) {
      if (gi) out += ",";
      out += mangleTypeForName(genericArgs[gi]);
    }
    out += "E";
  }
  return out;
}

// type -> readable mangled fragment
std::string Sema::mangleTypeForName(Type *t) {
  if (!t) return "x";
  switch (t->kind) {
  case TypeKind::Prim: return primName(t->prim);
  case TypeKind::Never: return "never";
  case TypeKind::Invalid: return "invalid";
  case TypeKind::Ptr: return "P" + mangleTypeForName(t->pointee);
  case TypeKind::Array: return "A" + std::to_string(t->arrayLen) + "_" + mangleTypeForName(t->elem);
  case TypeKind::Func: {
    std::string s = "F";
    for (auto *p : t->params) s += mangleTypeForName(p);
    s += "_" + mangleTypeForName(t->ret);
    return s;
  }
  case TypeKind::Struct: case TypeKind::Class: case TypeKind::Enum: {
    Decl *d = (Decl *)t->decl;
    std::string n;
    if (d->kind == Decl::Struct) n = ((DStruct *)d)->name;
    else if (d->kind == Decl::Class) n = ((DClass *)d)->name;
    else n = ((DEnum *)d)->name;
    std::string s = "T" + std::to_string(n.size()) + n;
    if (!t->genericArgs.empty()) {
      s += "I";
      for (size_t i = 0; i < t->genericArgs.size(); i++) {
        if (i) s += ",";
        s += mangleTypeForName(t->genericArgs[i]);
      }
      s += "E";
    }
    return s;
  }
  case TypeKind::Interface: {
    Decl *d = (Decl *)t->ifaceDecl;
    std::string n = d->kind == Decl::Trait ? ((DInterface *)d)->name : ((DInterface *)d)->name;
    return "X" + std::to_string(n.size()) + n;
  }
  }
  return "?";
}

GenericInstance *Sema::findInstance(DFunc *tmpl, const std::vector<Type *> &args) {
  std::string key = mangleFuncName(tmpl, args);
  auto it = instances.find({tmpl, key});
  if (it != instances.end()) return it->second;
  return nullptr;
}

// ============================================================ checking ======
unsigned long long Sema::evalConstUint(Expr *e, bool &ok) {
  ok = true;
  Expr *folded = constFold(e);
  if (!folded) { ok = false; return 0; }
  if (folded->kind == Expr::IntLit) return ((EInt *)folded)->value;
  if (folded->kind == Expr::CharLit) return ((EChar *)folded)->value;
  if (folded->kind == Expr::BoolLit) return ((EBool *)folded)->value ? 1 : 0;
  ok = false;
  return 0;
}

Expr *Sema::constFold(Expr *e) {
  if (!e) return nullptr;
  switch (e->kind) {
  case Expr::IntLit: case Expr::FloatLit: case Expr::BoolLit:
  case Expr::CharLit: case Expr::StringLit: case Expr::NullLit:
    return e;
  case Expr::Unary: {
    auto *u = (EUnary *)e;
    Expr *inner = constFold(u->operand);
    if (inner && inner->kind == Expr::IntLit && u->op == "-") {
      auto *ie = (EInt *)inner;
      auto *n = new EInt(u->loc);
      n->value = (unsigned long long)(0 - ie->value);
      n->digits = "-" + ie->digits;
      return n;
    }
    return nullptr;
  }
  case Expr::Binary: {
    auto *b = (EBinary *)e;
    Expr *l = constFold(b->lhs);
    Expr *r = constFold(b->rhs);
    if (!l || !r) return nullptr;
    if (l->kind == Expr::IntLit && r->kind == Expr::IntLit) {
      unsigned long long a = ((EInt *)l)->value, c = ((EInt *)r)->value;
      auto *n = new EInt(b->loc);
      const std::string &op = b->op;
      if (op == "+") n->value = a + c;
      else if (op == "-") n->value = a - c;
      else if (op == "*") n->value = a * c;
      else if (op == "/") { if (!c) return nullptr; n->value = a / c; }
      else if (op == "%") { if (!c) return nullptr; n->value = a % c; }
      else if (op == "&") n->value = a & c;
      else if (op == "|") n->value = a | c;
      else if (op == "^") n->value = a ^ c;
      else if (op == "<<") n->value = c < 64 ? a << c : 0;
      else if (op == ">>") n->value = c < 64 ? a >> c : 0;
      else return nullptr;
      return n;
    }
    return nullptr;
  }
  case Expr::Sizeof: case Expr::Alignof:
    return nullptr; // resolved by codegen once layouts exist
  default:
    return nullptr;
  }
}

bool Sema::isLValue(Expr *e) {
  switch (e->kind) {
  case Expr::Ident: {
    auto *id = (EIdent *)e;
    return id->idKind == IdKind::Local || id->idKind == IdKind::Global;
  }
  case Expr::Unary:
    return ((EUnary *)e)->op == "*";
  case Expr::Member:
    return ((EMember *)e)->memberKind == MemberKind::Field;
  case Expr::Index:
    return true;
  default:
    return false;
  }
}

bool Sema::typesAssignable(Type *dst, Type *src, Expr *srcExpr, SourceLoc loc,
                           const std::string &what) {
  if (!dst || !src) return false;
  if (tc.same(dst, src)) return true;
  if (src->isNever()) return true;
  if (src->kind == TypeKind::Invalid || dst->kind == TypeKind::Invalid) return true; // error recovery

  // null literal to any pointer
  if (srcExpr && srcExpr->kind == Expr::NullLit && dst->isPtr()) {
    srcExpr->type = dst;
    return true;
  }
  // literal fitting
  Expr *lit = srcExpr ? stripNeg(srcExpr) : nullptr;
  if (lit && lit->kind == Expr::IntLit && dst->isInt()) {
    unsigned bits = primBits(dst->prim);
    unsigned long long v = ((EInt *)lit)->value;
    bool fits = true;
    if (dst->prim == PRIM_i128) {
      fits = true; // 128-bit range: literal came from at most 128 bits
    } else if (dst->prim == PRIM_u128) {
      fits = true;
    } else if (primIsSigned(dst->prim) && bits < 64) {
      long long sv = (long long)v;
      fits = sv >= -(1LL << (bits - 1)) && sv <= (1LL << (bits - 1)) - 1;
    } else if (bits < 64) {
      fits = (v >> bits) == 0;
    }
    if (fits) {
      lit->type = dst;
      if (srcExpr != lit) srcExpr->type = dst;
      return true;
    }
    diag.error(loc, strfmt("%s '%llu' does not fit in type '%s'", what.c_str(), v,
                           typeToString(dst).c_str()));
    return false;
  }
  if (lit && lit->kind == Expr::FloatLit && dst->isFloat()) {
    lit->type = dst;
    if (srcExpr != lit) srcExpr->type = dst;
    return true;
  }
  if (lit && lit->kind == Expr::IntLit && dst->isFloat()) {
    // integer literal as float (e.g. f32 x = 1)
    lit->type = dst;
    if (srcExpr != lit) srcExpr->type = dst;
    return true;
  }
  // class upcast
  if (src->isClass() && dst->isClass()) {
    for (DClass *b = (DClass *)src->decl; b;) {
      if (b == (DClass *)dst->decl) return true;
      ClassLayout *bl = layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
  }
  // pointer upcast: ptr<Derived> -> ptr<Base>
  if (src->isPtr() && dst->isPtr() && src->pointee->isClass() && dst->pointee->isClass()) {
    for (DClass *b = (DClass *)src->pointee->decl; b;) {
      if (b == (DClass *)dst->pointee->decl) return true;
      ClassLayout *bl = layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
  }
  // class -> implemented interface
  if (src->isClass() && dst->isInterface()) {
    ClassLayout *sl = layoutOf((DClass *)src->decl);
    DInterface *di = (DInterface *)dst->ifaceDecl;
    for (auto &[iff, _] : sl->interfaces)
      if (iff == di) return true;
  }
  // array literal with a known target type: elements check against the target's
  // element type (enables heterogeneous-but-compatible literals)
  if (srcExpr && srcExpr->kind == Expr::ArrayLit && dst->isArray()) {
    auto *al = (EArrayLit *)srcExpr;
    if (!al->repeat && (long long)al->elems.size() != dst->arrayLen) {
      diag.error(srcExpr->loc, strfmt("array literal has %zu elements, expected %lld",
                                      al->elems.size(), dst->arrayLen));
      return false;
    }
    size_t n = al->repeat ? (size_t)dst->arrayLen : al->elems.size();
    for (size_t i = 0; i < n; i++) {
      Expr *el = al->elems[i < al->elems.size() ? i : 0];
      if (!typesAssignable(dst->elem, el->type, el, el->loc, "array element")) return false;
    }
    al->type = dst;
    return true;
  }
  // ptr<[T; N]> -> ptr<T>: same address, element view (array decay)
  if (src->isPtr() && src->pointee->isArray() && dst->isPtr() &&
      tc.same(src->pointee->elem, dst->pointee))
    return true;
  // FFI: string -> ptr<char> (the string view's data pointer)
  if (src->isString() && dst->isPtr() && dst->pointee->isPrim() &&
      dst->pointee->prim == PRIM_char)
    return true;
  // array -> same handled by same(); no decay

  diag.error(loc, strfmt("cannot assign %s '%s' to '%s'", what.c_str(), typeToString(src).c_str(),
                         typeToString(dst).c_str()),
             dst->isInt() && src->isInt()
                 ? "use an explicit cast: `value as " + typeToString(dst) + "`"
                 : "");
  return false;
}

// -------------------------------------------------------------- scope util --
void Sema::recordCapture(const std::string &name, Type *type, void *declScope) {
  // Scopes carry depths: a local whose declaration depth is shallower than a
  // lambda's base depth lives outside that lambda and must be captured.
  LocalVar lv;
  (void)declScope;
  for (auto &[lam, baseDepth] : lambdaStack) {
    if (!canReadVar(name, lv)) return;
    if (lv.declDepth < baseDepth) {
      bool found = false;
      for (auto &c : lam->captures)
        if (c.name == name) found = true;
      if (!found) lam->captures.push_back({name, type, nullptr});
    }
  }
}

bool Sema::canReadVar(const std::string &name, LocalVar &out) {
  for (Scope *s = curScope; s; s = s->parent) {
    auto it = s->vars.find(name);
    if (it != s->vars.end()) {
      out = it->second;
      return true;
    }
  }
  return false;
}

Decl *Sema::lookupVariantCtor(const std::string &name, Type **outEnumTy) {
  if (!curModule) return nullptr;
  // search visible enums for a variant with this name
  Decl *found = nullptr;
  auto checkEnum = [&](Decl *d, ModuleSema *via) {
    if (d->kind != Decl::Enum) return;
    auto *e = (DEnum *)d;
    for (auto &v : e->variants)
      if (v.name == name) {
        if (found) {
          diag.error(e->loc, strfmt("variant name '%s' is ambiguous; qualify it as '%s.%s'",
                                    name.c_str(), e->name.c_str(), name.c_str()));
        } else {
          found = d;
          if (outEnumTy) *outEnumTy = nullptr;
        }
      }
  };
  for (auto &[tn, d] : curModule->types) checkEnum(d, curModule);
  if (!found)
    for (auto &[iname, mod] : curModule->imports)
      for (auto &[tn, d] : mod->types)
        if (visible(d, curModule)) checkEnum(d, mod);
  if (!found && prelude && prelude != curModule)
    for (auto &[tn, d] : prelude->types)
      if (visible(d, curModule)) checkEnum(d, prelude);
  return found;
}

// ------------------------------------------------------------ check entry ---
bool Sema::checkEntry(ModuleSema *m) {
  auto it = m->funcs.find("main");
  if (it == m->funcs.end() || it->second.empty()) {
    diag.plainError(strfmt("no `main` function in entry file '%s'", m->path.c_str()));
    return false;
  }
  DFunc *f = it->second[0];
  if (it->second.size() > 1) {
    diag.error(f->loc, "multiple `main` functions defined", "", 4);
    return false;
  }
  Type *ret = f->retType ? resolveType(f->retType) : tc.prim(PRIM_void);
  if (!(ret->isVoid() || (ret->isInt() && ret->prim == PRIM_i32))) {
    diag.error(f->loc, "main must return void or i32", "", 4);
    return false;
  }
  if (!f->params.empty()) {
    diag.error(f->loc, "main takes no parameters", "", 4);
    return false;
  }
  entryModule = m;
  return true;
}

bool Sema::checkAll() {
  for (auto *m : modules) {
    curModule = m;
    for (Decl *d : m->unit->decls) {
      if (d->kind == Decl::Func) {
        auto *f = (DFunc *)d;
        if (f->parent == nullptr && f->genericParams.empty()) checkFuncDecl(f, {});
      } else if (d->kind == Decl::Class) {
        for (DFunc *mth : ((DClass *)d)->methods)
          if (mth->genericParams.empty()) checkFuncDecl(mth, {});
      } else if (d->kind == Decl::Struct) {
        for (DFunc *mth : ((DStruct *)d)->methods)
          if (mth->genericParams.empty()) checkFuncDecl(mth, {});
      } else if (d->kind == Decl::Interface || d->kind == Decl::Trait) {
        for (DFunc *mth : ((DInterface *)d)->methods)
          if (mth->genericParams.empty() && mth->body) checkFuncDecl(mth, {});
      } else if (d->kind == Decl::Global) {
        auto *g = (DGlobal *)d;
        if (g->type) resolveType(g->type);
        if (g->init) checkExpr(g->init);
      } else if (d->kind == Decl::Const) {
        auto *c = (DConst *)d;
        if (c->type) resolveType(c->type);
        if (c->init) checkExpr(c->init);
      }
    }
  }
  return !diag.hasErrors();
}

// ======================================================== function checking ==
void Sema::checkFuncDecl(DFunc *f, std::map<std::string, Type *> genericSubst) {
  if (f->isExtern || f->body == nullptr) return;
  if (f->checked) return;
  f->checked = true;

  // save/restore global checking state (generic instantiation recurses)
  ModuleSema *savedModule = curModule;
  DFunc *savedFunc = curFunc;
  Type *savedRet = curReturnType;
  auto *savedSubst = subst;
  Scope *savedScope = curScope;
  int savedUnsafe = unsafeDepth, savedLoop = loopDepth;

  subst = genericSubst.empty() ? nullptr : &genericSubst;
  curFunc = f;
  Scope *fnScope = new Scope();
  fnScope->parent = nullptr;
  curScope = fnScope;
  unsafeDepth = 0;
  loopDepth = 0;

  // determine defining module (methods may be checked from other modules)
  {
    auto fm = funcModule.find(f);
    if (fm != funcModule.end()) curModule = fm->second;
    else if (f->parent) {
      Decl *pd = f->parent;
      for (auto *m : modules) {
        for (auto &[tname, td] : m->types)
          if (td == pd) curModule = m;
      }
    }
  }

  // implicit self for instance methods (including constructors)
  if (f->parent && !f->isStatic && f->parent->kind != Decl::Interface &&
      f->parent->kind != Decl::Trait) {
    Type *selfTy = selfTypeOf(f);
    if (selfTy) {
      LocalVar lv;
      lv.type = selfTy;
      lv.isMut = true;
      lv.declLoc = f->loc;
      lv.declScope = fnScope;
      fnScope->vars["self"] = lv;
    }
  }

  for (auto &p : f->params) {
    Type *pt = resolveType(p.type);
    if (!pt) goto done;
    if (pt->isVoid()) {
      diag.error(p.loc, strfmt("parameter '%s' cannot have type void", p.name.c_str()));
      continue;
    }
    if (p.defVal) {
      checkExpr(p.defVal);
      if (p.defVal->type && !typesAssignable(pt, p.defVal->type, p.defVal, p.loc, "default value"))
        goto done;
    }
    LocalVar lv;
    lv.type = pt;
    lv.isMut = true;
    lv.declLoc = p.loc;
    lv.declScope = fnScope;
    lv.declDepth = 0;
    (*fnScope).vars[p.name] = lv;
  }

  curReturnType = f->retType ? resolveType(f->retType) : tc.prim(PRIM_void);
  checkBlock(f->body);

  if (!terminates(f->body)) {
    Type *rt = curReturnType;
    bool numericish = rt->isNumeric() || rt->isBool() || rt->isPtr() || rt->isString() ||
                      rt->isEnum() || rt->isVector() || rt->isVoid();
    if (rt->isNever()) {
      diag.error(f->loc, strfmt("never function '%s' must not return normally", f->name.c_str()),
                 "make every path diverge: call a `never` function, or `return`", 0);
    } else if (!numericish) {
      diag.error(f->loc, strfmt("function '%s' can reach the end of its body without returning a '%s'",
                                f->name.c_str(), typeToString(rt).c_str()),
                 "numeric/pointer/string/enum functions implicitly return their default value (0, null, \"\"); add an explicit `return` for aggregates", 0);
    }
  }

done:
  subst = savedSubst;
  curScope = savedScope;
  curFunc = savedFunc;
  curReturnType = savedRet;
  curModule = savedModule;
  unsafeDepth = savedUnsafe;
  loopDepth = savedLoop;
}

Type *Sema::selfTypeOf(DFunc *f) {
  if (!f->parent) return nullptr;
  Decl *pd = f->parent;
  if (pd->kind == Decl::Class) {
    auto *c = (DClass *)pd;
    std::vector<Type *> args;
    for (auto &gp : c->genericParams) {
      Type *t = tc.genericVar(gp);
      if (subst) {
        auto it = subst->find(gp);
        if (it != subst->end()) t = it->second;
      }
      args.push_back(t);
    }
    return tc.ptr(tc.getClass(c, args));
  }
  if (pd->kind == Decl::Struct) {
    auto *s = (DStruct *)pd;
    std::vector<Type *> args;
    for (auto &gp : s->genericParams) {
      Type *t = tc.genericVar(gp);
      if (subst) {
        auto it = subst->find(gp);
        if (it != subst->end()) t = it->second;
      }
      args.push_back(t);
    }
    return tc.ptr(tc.getStruct(s, args));
  }
  return nullptr;
}

// ------------------------------------------------------------ statements ----
void Sema::checkBlock(Stmt *blockStmt) {
  if (!blockStmt || blockStmt->kind != Stmt::KBlock) return;
  Scope blockScope;
  blockScope.parent = curScope;
  blockScope.depth = curScope ? curScope->depth + 1 : 0;
  Scope *saved = curScope;
  curScope = &blockScope;
  auto *b = (SBlock *)blockStmt;
  for (Stmt *s : b->stmts) checkStmt(s);
  curScope = saved;
}

void Sema::checkStmt(Stmt *s) {
  switch (s->kind) {
  case Stmt::KExpr: {
    checkExpr(((SExpr *)s)->e);
    break;
  }
  case Stmt::KLet: {
    auto *l = (SLet *)s;
    Type *declTy = l->type ? resolveType(l->type) : nullptr;
    if (l->init) {
      checkExpr(l->init);
      Type *it = l->init->type;
      if (!it) return;
      if (it->isVoid()) {
        diag.error(l->loc, strfmt("cannot initialize '%s' from a void expression", l->name.c_str()));
        return;
      }
      if (!declTy) declTy = it;
      if (!typesAssignable(declTy, it, l->init, l->loc, "value")) return;
    } else if (!declTy) {
      diag.error(l->loc, strfmt("variable '%s' needs a type or an initializer", l->name.c_str()),
                 "write `x: i32 = 0` or `x = 0`", 0);
      return;
    }
    if (!declTy || declTy->isVoid()) {
      diag.error(l->loc, strfmt("variable '%s' cannot have type void", l->name.c_str()));
      return;
    }
    // untyped name-first form may be an assignment to an existing variable
    bool treatAsDecl = !(l->isDeclOrAssign);
    if (l->isDeclOrAssign) {
      LocalVar lv;
      if (canReadVar(l->name, lv)) {
        if (!lv.isMut) {
          diag.error(l->loc, strfmt("cannot assign to immutable variable '%s'", l->name.c_str()),
                     "declare it with `mut x = ...`", 1);
          return;
        }
        if (lv.isConst) {
          diag.error(l->loc, strfmt("cannot assign to constant '%s'", l->name.c_str()));
          return;
        }
        if (!typesAssignable(lv.type, l->init->type, l->init, l->loc, "value")) return;
        l->resolvedType = lv.type;
        l->isAssignExisting = true;
        return;
      }
      auto git = curModule->globals.find(l->name);
      if (git != curModule->globals.end()) {
        DGlobal *g = git->second;
        Type *gt = resolveType(g->type);
        if (!g->isMut) {
          diag.error(l->loc, strfmt("cannot assign to immutable global '%s'", l->name.c_str()),
                     "declare it with `mut " + l->name + ": " + typeToString(gt) + "`", 1);
          return;
        }
        if (!typesAssignable(gt, l->init->type, l->init, l->loc, "value")) return;
        l->isAssignExisting = true;
        l->resolvedType = gt;
        l->assignGlobal = g;
        return;
      }
      auto cit = curModule->consts.find(l->name);
      if (cit != curModule->consts.end()) {
        diag.error(l->loc, strfmt("cannot assign to constant '%s'", l->name.c_str()));
        return;
      }
      // declare a new local (Go-like `x = init`)
      if (curScope->vars.count(l->name)) {
        diag.error(l->loc, strfmt("variable '%s' is already declared in this scope", l->name.c_str()));
        return;
      }
      LocalVar nlv;
      nlv.type = declTy;
      nlv.isMut = l->isMut;
      nlv.declLoc = l->loc;
      nlv.declScope = curScope;
      nlv.declDepth = curScope ? curScope->depth : 0;
      curScope->vars[l->name] = nlv;
      l->resolvedType = declTy;
      l->isAssignExisting = false;
      return;
    }
    (void)treatAsDecl;
    // explicit typed declaration (or `mut x = ...`)
    if (curScope->vars.count(l->name)) {
      diag.error(l->loc, strfmt("variable '%s' is already declared in this scope", l->name.c_str()));
      return;
    }
    if (lookupTypeOwn(l->name)) {
      diag.error(l->loc, strfmt("variable '%s' shadows a type name", l->name.c_str()));
      return;
    }
    LocalVar lv;
    lv.type = declTy;
    lv.isMut = l->isMut;
    lv.isConst = l->isConst;
    lv.declLoc = l->loc;
    lv.declScope = curScope;
    lv.declDepth = curScope ? curScope->depth : 0;
    curScope->vars[l->name] = lv;
    l->resolvedType = declTy;
    l->isAssignExisting = false;
    break;
  }
  case Stmt::KReturn: {
    auto *r = (SReturn *)s;
    if (!curFunc) return;
    if (r->e) {
      checkExpr(r->e);
      Type *rt = r->e->type;
      if (!rt) return;
      if (curReturnType && !typesAssignable(curReturnType, rt, r->e, s->loc, "return value")) return;
    } else if (curReturnType && !curReturnType->isVoid() && !curReturnType->isNever()) {
      diag.error(s->loc, strfmt("return with no value in function returning '%s'",
                                typeToString(curReturnType).c_str()));
    }
    break;
  }
  case Stmt::KIf: {
    auto *i = (SIf *)s;
    checkExpr(i->cond);
    if (i->cond->type && !i->cond->type->isBool() && !i->cond->type->isNever())
      diag.error(i->cond->loc, strfmt("if condition must be bool, got '%s'",
                                      typeToString(i->cond->type).c_str()),
                 "Core has no truthy integers: write `if x != 0`", 1);
    checkBlock(i->thenBlock);
    if (i->elseBlock) {
      if (i->elseBlock->kind == Stmt::KBlock) checkBlock(i->elseBlock);
      else checkStmt(i->elseBlock);
    }
    break;
  }
  case Stmt::KWhile: {
    auto *w = (SWhile *)s;
    checkExpr(w->cond);
    if (w->cond->type && !w->cond->type->isBool() && !w->cond->type->isNever())
      diag.error(w->cond->loc, strfmt("while condition must be bool, got '%s'",
                                      typeToString(w->cond->type).c_str()));
    loopDepth++;
    checkBlock(w->body);
    loopDepth--;
    break;
  }
  case Stmt::KFor: {
    auto *f = (SFor *)s;
    Scope forScope;
    forScope.parent = curScope;
    Scope *saved = curScope;
    curScope = &forScope;
    if (f->init) checkStmt(f->init);
    if (f->cond) {
      checkExpr(f->cond);
      if (f->cond->type && !f->cond->type->isBool() && !f->cond->type->isNever())
        diag.error(f->cond->loc, "for condition must be bool");
    }
    loopDepth++;
    checkBlock(f->body);
    loopDepth--;
    if (f->step) checkStmt(f->step);
    curScope = saved;
    break;
  }
  case Stmt::KForIn: {
    auto *f = (SForIn *)s;
    Type *varTy = nullptr;
    if (f->iterable->kind == Expr::Range) {
      auto *r = (ERange *)f->iterable;
      checkExpr(r->lo);
      checkExpr(r->hi);
      // literal bound adopts the other side's type (e.g. `0..len(s)`)
      auto refitBound = [&](Expr *&side, Expr *other) {
        if (!side || !other || !other->type) return;
        if ((side->kind == Expr::IntLit || isConstFoldable(side)) && other->type->isInt() &&
            side->type && !tc.same(side->type, other->type)) {
          // re-type constant bounds to the other side's type
          bool ok = true;
          unsigned long long v = evalConstUint(side, ok);
          if (ok) {
            unsigned bits = primBits(other->type->prim);
            bool fits = bits >= 64 || (v >> bits) == 0;
            if (primIsSigned(other->type->prim) && bits < 64) {
              long long sv = (long long)v;
              fits = sv >= -(1LL << (bits - 1)) && sv <= (1LL << (bits - 1)) - 1;
            }
            if (fits) {
              Expr *retyped = new EInt(side->loc);
              ((EInt *)retyped)->value = v;
              retyped->type = other->type;
              side = retyped;
            }
          }
        }
      };
      refitBound(r->lo, r->hi);
      refitBound(r->hi, r->lo);
      if (r->lo->type && r->hi->type) {
        if (!r->lo->type->isInt() || !r->hi->type->isInt()) {
          diag.error(r->loc, "range bounds must be integers");
        } else if (!tc.same(r->lo->type, r->hi->type)) {
          diag.error(r->loc, strfmt("range bounds must have the same type ('%s' vs '%s')",
                                    typeToString(r->lo->type).c_str(), typeToString(r->hi->type).c_str()));
        }
        varTy = r->lo->type;
      }
    } else {
      checkExpr(f->iterable);
      Type *it = f->iterable ? f->iterable->type : nullptr;
      if (!it) { /* error already reported */ }
      else if (it->isArray()) varTy = it->elem;
      else if (it->kind == TypeKind::Prim && it->prim == PRIM_string) {
        diag.error(f->iterable->loc, "iterating strings is not supported yet",
                   "index characters with s[i] and len(s)", 1);
      } else if (it->isPtr()) {
        diag.error(f->iterable->loc, "cannot iterate a raw pointer",
                   "use pointer arithmetic in a for loop", 1);
      } else {
        diag.error(f->iterable->loc, strfmt("cannot iterate value of type '%s'",
                                            typeToString(it).c_str()),
                   "ranges (`0..n`) and fixed arrays (`[i32; 5]`) are iterable", 1);
      }
    }
    Scope forScope;
    forScope.parent = curScope;
    Scope *saved = curScope;
    curScope = &forScope;
    if (varTy) {
      LocalVar lv;
      lv.type = varTy;
      lv.isMut = f->isMut;
      lv.declLoc = f->loc;
      lv.declScope = &forScope;
      forScope.vars[f->varName] = lv;
    }
    loopDepth++;
    checkBlock(f->body);
    loopDepth--;
    curScope = saved;
    break;
  }
  case Stmt::KBreak:
    if (loopDepth == 0) diag.error(s->loc, "break outside of a loop");
    break;
  case Stmt::KContinue:
    if (loopDepth == 0) diag.error(s->loc, "continue outside of a loop");
    break;
  case Stmt::KSwitch: {
    auto *sw = (SSwitch *)s;
    checkExpr(sw->scrutinee);
    Type *st = sw->scrutinee ? sw->scrutinee->type : nullptr;
    if (!st) return;
    if (!st->isInt() && !st->isEnum() && !(st->isPrim() && st->prim == PRIM_char)) {
      diag.error(sw->scrutinee->loc, strfmt("switch requires an integer, char or enum, got '%s'",
                                            typeToString(st).c_str()),
                 "use `match` for enums and general pattern matching", 1);
      return;
    }
    for (auto &c : sw->cases) {
      for (Expr *v : c.values) {
        checkExpr(v);
        if (v->type && !typesAssignable(st, v->type, v, v->loc, "case value")) continue;
      }
      checkBlock(c.body);
    }
    if (sw->defaultBody) checkBlock(sw->defaultBody);
    break;
  }
  case Stmt::KBlock:
    checkBlock(s);
    break;
  case Stmt::KUnsafe: {
    unsafeDepth++;
    for (Stmt *us : ((SUnsafe *)s)->stmts) checkStmt(us);
    unsafeDepth--;
    break;
  }
  }
}

// ====================================================== expression checking ==
Type *Sema::typeOf(Expr *e) { return e ? e->type : nullptr; }

void Sema::checkExpr(Expr *e, bool lvalue) {
  if (!e) return;
  switch (e->kind) {
  case Expr::IntLit: {
    auto *ie = (EInt *)e;
    if (e->type) return; // already coerced by literal fitting
    if (ie->big128) { e->type = tc.prim(PRIM_u128); return; }
    if (ie->value <= 0x7FFFFFFFLL) e->type = tc.prim(PRIM_i32);
    else if (ie->value <= 0x7FFFFFFFFFFFFFFFLL) e->type = tc.prim(PRIM_i64);
    else e->type = tc.prim(PRIM_u64);
    return;
  }
  case Expr::FloatLit:
    e->type = e->type ? e->type : tc.prim(PRIM_f64);
    return;
  case Expr::BoolLit:
    e->type = tc.prim(PRIM_bool);
    return;
  case Expr::CharLit:
    e->type = tc.prim(PRIM_char);
    return;
  case Expr::StringLit:
    e->type = tc.prim(PRIM_string);
    return;
  case Expr::NullLit:
    e->type = tc.ptr(tc.invalid());
    return; // refined by typesAssignable
  case Expr::Ident: {
    auto *id = (EIdent *)e;
    if (id->idKind != IdKind::Unresolved) {
      e->type = e->type;
      return;
    }
    const std::string &name = id->name;
    // type with explicit generic args: `Option<i32>` used as a value namespace
    if (id->typeArgs) {
      Type *t = resolveType(id->typeArgs);
      if (!t) return;
      id->idKind = IdKind::TypeRef;
      id->target = lookupTypeVisible(name);
      e->type = t;
      return;
    }
    LocalVar lv;
    if (canReadVar(name, lv)) {
      id->idKind = IdKind::Local;
      id->scopeId = lv.declScope;
      e->type = lv.type;
      if (!lambdaStack.empty()) recordCapture(name, lv.type, lv.declScope);
      return;
    }
    auto git = curModule->globals.find(name);
    if (git != curModule->globals.end()) {
      id->idKind = IdKind::Global;
      id->target = git->second;
      e->type = resolveType(git->second->type);
      return;
    }
    auto cit = curModule->consts.find(name);
    if (cit != curModule->consts.end()) {
      id->idKind = IdKind::ConstVal;
      id->target = cit->second;
      e->type = resolveType(cit->second->type);
      return;
    }
    // builtin used as value?
    int b;
    if (builtinByName(name, b)) {
      diag.error(id->loc, strfmt("'%s' is a builtin function; call it with arguments", name.c_str()));
      e->type = tc.invalid();
      return;
    }
    // module reference?
    if (ModuleSema *mod = lookupModuleRef(name)) {
      id->idKind = IdKind::Module;
      id->target = mod;
      e->type = nullptr;
      return;
    }
    // type reference?
    if (Decl *td = lookupTypeVisible(name)) {
      id->idKind = IdKind::TypeRef;
      id->target = td;
      e->type = nullptr;
      return;
    }
    // unit variant value? (e.g. `Red` for enum Color)
    if (Decl *ed = lookupVariantCtor(name)) {
      auto *en = (DEnum *)ed;
      for (size_t vi = 0; vi < en->variants.size(); vi++) {
        if (en->variants[vi].name == name && en->variants[vi].payloadTypes.empty()) {
          id->idKind = IdKind::EnumConst;
          id->target = en;
          id->enumTag = (int)vi;
          e->type = tc.getEnum(en, {});
          return;
        }
      }
    }
    // function reference (single overload) or &fn
    auto fit = curModule->funcs.find(name);
    std::vector<DFunc *> cands;
    if (fit != curModule->funcs.end()) cands = fit->second;
    else cands = lookupFuncsVisible(name);
    if (!cands.empty()) {
      if (cands.size() == 1) {
        id->idKind = IdKind::Func;
        id->resolvedFunc = cands[0];
        std::vector<Type *> params;
        for (auto &p : cands[0]->params) params.push_back(resolveType(p.type));
        e->type = tc.func(cands[0]->retType ? resolveType(cands[0]->retType) : tc.prim(PRIM_void), params);
        return;
      }
      diag.error(id->loc, strfmt("'%s' is overloaded (%zu overloads); call it with arguments or take `&%s` "
                                 "after selecting via a call context", name.c_str(), cands.size(), name.c_str()));
      e->type = tc.invalid();
      return;
    }
    diag.error(id->loc, strfmt("unknown name '%s'", name.c_str()),
               "variables must be declared before use", 1);
    e->type = tc.invalid();
    return;
  }
  case Expr::Self: {
    LocalVar lv;
    if (canReadVar("self", lv)) {
      e->type = lv.type;
      ((ESelf *)e)->scopeId = lv.declScope;
      if (!lambdaStack.empty()) recordCapture("self", lv.type, lv.declScope);
    } else {
      diag.error(e->loc, "self is only valid inside methods");
      e->type = tc.invalid();
    }
    return;
  }
  case Expr::Unary: {
    auto *u = (EUnary *)e;
    checkExpr(u->operand, u->op == "*" ? false : lvalue);
    Type *ot = u->operand->type;
    if (!ot) return;
    if (u->op == "*") {
      if (!ot->isPtr()) {
        diag.error(e->loc, strfmt("cannot dereference value of type '%s'", typeToString(ot).c_str()),
                   "only ptr<T> values can be dereferenced with *", 1);
        e->type = tc.invalid();
        return;
      }
      e->type = ot->pointee;
      u->unKind = (int)UnKind::Deref;
      return;
    }
    if (u->op == "&") {
      if (u->operand->kind == Expr::Ident && ((EIdent *)u->operand)->idKind == IdKind::Func) {
        // &function -> closure value
        DFunc *f = (DFunc *)((EIdent *)u->operand)->resolvedFunc;
        std::vector<Type *> params;
        for (auto &p : f->params) params.push_back(resolveType(p.type));
        e->type = tc.func(f->retType ? resolveType(f->retType) : tc.prim(PRIM_void), params);
        u->unKind = (int)UnKind::Ref;
        return;
      }
      if (!isLValue(u->operand)) {
        diag.error(e->loc, "cannot take the address of a temporary value",
                   "assign it to a variable first, or use alloc<T>()", 1);
        e->type = tc.invalid();
        return;
      }
      e->type = tc.ptr(ot);
      u->unKind = (int)UnKind::Ref;
      return;
    }
    if (u->op == "-") {
      if (ot->isInt() || ot->isFloat()) { e->type = ot; u->unKind = (int)UnKind::Neg; return; }
      diag.error(e->loc, strfmt("cannot negate value of type '%s'", typeToString(ot).c_str()));
      e->type = tc.invalid();
      return;
    }
    if (u->op == "!") {
      if (ot->isBool()) { e->type = tc.prim(PRIM_bool); u->unKind = (int)UnKind::Not; return; }
      diag.error(e->loc, strfmt("'!' expects bool, got '%s'", typeToString(ot).c_str()));
      e->type = tc.invalid();
      return;
    }
    if (u->op == "~") {
      if (ot->isInt()) { e->type = ot; u->unKind = (int)UnKind::BitNot; return; }
      diag.error(e->loc, strfmt("'~' expects an integer, got '%s'", typeToString(ot).c_str()));
      e->type = tc.invalid();
      return;
    }
    return;
  }
  case Expr::Binary: {
    checkBinary((EBinary *)e);
    return;
  }
  case Expr::Assign: {
    checkAssign((EAssign *)e);
    return;
  }
  case Expr::Cast: {
    checkCast((ECast *)e);
    return;
  }
  case Expr::Call: {
    e->type = checkCall((ECall *)e);
    return;
  }
  case Expr::Member: {
    e->type = checkMemberForRead((EMember *)e);
    return;
  }
  case Expr::Index: {
    auto *ix = (EIndex *)e;
    checkExpr(ix->base);
    checkExpr(ix->index);
    Type *bt = ix->base ? ix->base->type : nullptr;
    Type *it = ix->index ? ix->index->type : nullptr;
    if (!bt || !it) return;
    if (bt->isArray()) {
      if (!it->isInt()) {
        diag.error(ix->index->loc, strfmt("array index must be an integer, got '%s'",
                                          typeToString(it).c_str()));
        return;
      }
      ix->checkBounds = unsafeDepth == 0;
      e->type = bt->elem;
      return;
    }
    if (bt->isString()) {
      if (!it->isInt()) {
        diag.error(ix->index->loc, "string index must be an integer");
        return;
      }
      ix->checkBounds = unsafeDepth == 0;
      e->type = tc.prim(PRIM_char);
      return;
    }
    if (bt->isPtr()) {
      // p[i] == *(p + i)
      ix->checkBounds = false;
      e->type = bt->pointee;
      return;
    }
    diag.error(ix->base->loc, strfmt("cannot index value of type '%s'", typeToString(bt).c_str()),
               "fixed arrays, strings and pointers support indexing", 1);
    return;
  }
  case Expr::StructLit: {
    auto *sl = (EStructLit *)e;
    Type *ty = resolveType(sl->ty);
    if (!ty) return;
    if (!ty->isStruct() && !ty->isClass()) {
      diag.error(e->loc, strfmt("type '%s' is not a struct or class literal type",
                                typeToString(ty).c_str()));
      return;
    }
    Decl *td = (Decl *)ty->decl;
    // collect expected fields in declaration order (with inheritance for classes)
    std::vector<std::pair<std::string, TypeExpr *>> fieldList;
    if (td->kind == Decl::Struct) {
      for (auto &f : ((DStruct *)td)->fields) fieldList.push_back({f.name, f.type});
    } else {
      DClass *c = (DClass *)td;
      std::vector<DClass *> chain;
      for (DClass *b = c; b;) {
        chain.push_back(b);
        ClassLayout *cl = layoutOf(b);
        b = cl ? cl->base : nullptr;
      }
      for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (auto &f : (*it)->fields) fieldList.push_back({f.name, f.type});
    }
    // types with an init() constructor build via init(): fields optional
    bool hasCtor = false;
    for (auto *mth : (td->kind == Decl::Class ? std::vector<DFunc *>(((DClass *)td)->methods)
                                              : std::vector<DFunc *>(((DStruct *)td)->methods)))
      if (mth->name == "init" && !mth->isStatic) hasCtor = true;
    bool isClassLit = ty->isClass() || hasCtor;
    if (!isClassLit && sl->fields.size() != fieldList.size()) {
      diag.error(e->loc, strfmt("'%s' literal requires all %zu fields, got %zu",
                                typeToString(ty).c_str(), fieldList.size(), sl->fields.size()));
      return;
    }
    std::set<std::string> seen;
    for (auto &[fname, fexpr] : sl->fields) {
      if (seen.count(fname)) {
        diag.error(e->loc, strfmt("duplicate field '%s' in struct literal", fname.c_str()));
        return;
      }
      seen.insert(fname);
      TypeExpr *fte = nullptr;
      for (auto &[n, t] : fieldList)
        if (n == fname) fte = t;
      if (!fte) {
        diag.error(e->loc, strfmt("'%s' has no field '%s'", typeToString(ty).c_str(), fname.c_str()));
        return;
      }
      checkExpr(fexpr);
      Type *ft = substituteFieldType(fte, ty);
      if (ft && fexpr->type && !typesAssignable(ft, fexpr->type, fexpr, fexpr->loc, "field value"))
        return;
    }
    sl->memberKind = MemberKind::Field; // reuse: marks resolved
    sl->target = td;
    e->type = ty;
    return;
  }
  case Expr::ArrayLit: {
    auto *al = (EArrayLit *)e;
    if (al->repeat) {
      checkExpr(al->elems[0]);
      bool ok = true;
      unsigned long long n = evalConstUint(al->repeat, ok);
      if (!ok) {
        diag.error(al->repeat->loc, "array repeat count must be a compile-time constant", "", 1);
        return;
      }
      Type *elem = al->elems[0]->type;
      e->type = tc.array(elem, (long long)n);
      al->memberIndex = -2; // mark repeat form
      return;
    }
    if (al->elems.empty()) {
      diag.error(e->loc, "empty array literals need a repeat form: `[zero_value; 0]` is invalid; use a type annotation",
                 "declare with an explicit type: `arr: [i32; 0] = []` is not allowed - arrays need at least one element in v1", 1);
      return;
    }
    for (auto *el : al->elems) checkExpr(el);
    Type *elem = al->elems[0]->type;
    bool allFit = true;
    for (size_t ei = 1; ei < al->elems.size(); ei++) {
      Type *et = al->elems[ei]->type;
      if (!et || !elem) { allFit = false; continue; }
      if (tc.same(elem, et)) continue;
      // least-upper-bound for (ptr-to-)class hierarchies: widen to the common base
      Type *widen = nullptr;
      bool viaPtr = elem->isPtr() && et->isPtr();
      Type *elemBase = viaPtr ? elem->pointee : elem;
      Type *etBase = viaPtr ? et->pointee : et;
      if (etBase->isClass() && elemBase->isClass()) {
        for (DClass *b = (DClass *)etBase->decl; b && !widen;) {
          Type *bt = tc.getClass(b, {});
          bool all = true;
          for (size_t ej = 0; ej < al->elems.size(); ej++) {
            Type *tj0 = al->elems[ej]->type;
            if (!tj0) { all = false; break; }
            Type *tj = viaPtr && tj0->isPtr() ? tj0->pointee : tj0;
            if (!tj || !(tc.same(tj, bt) || tj->isClass())) { all = false; break; }
            if (tc.same(tj, bt)) continue;
            bool up = false;
            for (DClass *b2 = (DClass *)tj->decl; b2;) {
              if (b2 == (DClass *)bt->decl) { up = true; break; }
              ClassLayout *bl = layoutOf(b2);
              b2 = bl ? bl->base : nullptr;
            }
            if (!up) { all = false; break; }
          }
          if (all) {
            widen = viaPtr ? tc.ptr(bt) : bt;
          } else {
            ClassLayout *bl = layoutOf(b);
            b = bl ? bl->base : nullptr;
          }
        }
      }
      if (widen) {
        elem = widen;
      } else if (!typesAssignable(elem, et, al->elems[ei], al->elems[ei]->loc, "array element")) {
        allFit = false;
        return;
      }
    }
    (void)allFit;
    e->type = tc.array(elem, (long long)al->elems.size());
    return;
  }
  case Expr::Lambda: {
    auto *lam = (ELambda *)e;
    std::vector<Type *> params;
    for (auto &p : lam->params) {
      if (!p.type) {
        diag.error(p.loc, strfmt("lambda parameter '%s' needs a type annotation", p.name.c_str()),
                   "write `func(x: i32) { ... }`", 1);
        return;
      }
      Type *pt = resolveType(p.type);
      if (!pt) return;
      params.push_back(pt);
    }
    Type *ret = lam->retType ? resolveType(lam->retType) : tc.prim(PRIM_void);
    // check body in nested scope; captures recorded on resolution via depth
    Scope lamScope;
    lamScope.parent = curScope;
    lamScope.depth = curScope ? curScope->depth + 1 : 1;
    Scope *saved = curScope;
    curScope = &lamScope;
    for (size_t pi = 0; pi < lam->params.size(); pi++) {
      LocalVar lv;
      lv.type = params[pi];
      lv.isMut = true;
      lv.declLoc = lam->params[pi].loc;
      lv.declScope = &lamScope;
      lv.declDepth = lamScope.depth;
      lamScope.vars[lam->params[pi].name] = lv;
    }
    DFunc pseudo(e->loc);
    pseudo.retType = lam->retType;
    DFunc *savedFunc = curFunc;
    Type *savedRet = curReturnType;
    curFunc = &pseudo;
    curReturnType = ret;
    lambdaStack.push_back({lam, lamScope.depth});
    checkBlock(lam->body);
    lambdaStack.pop_back();
    curFunc = savedFunc;
    curReturnType = savedRet;
    curScope = saved;

    lam->closureType = tc.func(ret, params);
    e->type = lam->closureType;
    return;
  }
  case Expr::Match: {
    e->type = checkMatch((EMatch *)e);
    return;
  }
  case Expr::UnsafeExpr: {
    auto *ue = (EUnsafeExpr *)e;
    unsafeDepth++;
    for (size_t si = 0; si < ue->stmts.size(); si++) {
      checkStmt(ue->stmts[si]);
      if (si + 1 == ue->stmts.size() && ue->stmts[si]->kind == Stmt::KExpr)
        e->type = ((SExpr *)ue->stmts[si])->e->type;
    }
    unsafeDepth--;
    if (!e->type) e->type = tc.prim(PRIM_void);
    return;
  }
  case Expr::Range: {
    diag.error(e->loc, "ranges are only valid in `for x in a..b` loops");
    return;
  }
  case Expr::Sizeof: case Expr::Alignof: {
    auto *sz = (ESizeof *)e;
    Type *ty = resolveType(sz->ty);
    if (!ty) return;
    if (ty->isNamedGeneric) return; // resolved post-substitution
    e->type = tc.prim(PRIM_usize);
    return;
  }
  }
}


// -------------------------------------------------------------- binary ops --
static bool isAssignOp(const std::string &op) {
  return op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
         op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=";
}

void Sema::checkBinary(EBinary *b) {
  checkExpr(b->lhs);
  checkExpr(b->rhs);
  Type *lt = b->lhs->type, *rt = b->rhs->type;
  if (!lt || !rt) return;
  const std::string &op = b->op;

  // never propagates
  if (lt->isNever()) { b->type = lt; b->binKind = (int)BinKind::None; return; }
  if (rt->isNever()) { b->type = rt; b->binKind = (int)BinKind::None; return; }

  // literal coercion toward the other operand's type
  auto tryCoerce = [&](Expr *lit, Type *other) {
    if (!other) return;
    if (lit->kind == Expr::IntLit && !lit->type && other->isInt()) {
      Expr *tmp = lit; tmp->type = nullptr;
      typesAssignable(other, lit->type, lit, lit->loc, "operand");
    }
    if (lit->kind == Expr::FloatLit && other->isFloat()) lit->type = other;
  };
  // note: IntLit always gets a default type in checkExpr, so coercion works via
  // re-typing: reset and re-fit
  auto refit = [&](Expr *lit, Type *other) -> bool {
    if (lit->kind == Expr::IntLit && other->isInt()) {
      lit->type = nullptr;
      // fits check via typesAssignable path with a fresh literal
      lit->type = other; // optimistic; validated below by range check
      unsigned bits = primBits(other->prim);
      unsigned long long v = ((EInt *)lit)->value;
      bool fits = other->prim == PRIM_i128 || other->prim == PRIM_u128 || bits >= 64 ||
                  ((v >> bits) == 0);
      if (primIsSigned(other->prim) && bits < 64) {
        long long sv = (long long)v;
        fits = sv >= -(1LL << (bits - 1)) && sv <= (1LL << (bits - 1)) - 1;
      }
      if (!fits) lit->type = tc.prim(PRIM_i32);
      return fits;
    }
    if (lit->kind == Expr::FloatLit && other->isFloat()) { lit->type = other; return true; }
    return false;
  };

  if (lt->isString() && rt->isString()) {
    if (op == "+") { b->binKind = (int)BinKind::StrConcat; b->type = lt; return; }
    if (op == "==") { b->binKind = (int)BinKind::StrCmp; b->type = tc.prim(PRIM_bool); return; }
    if (op == "!=") { b->binKind = (int)BinKind::StrCmp; b->type = tc.prim(PRIM_bool); return; }
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
      b->binKind = (int)BinKind::StrCmp;
      b->type = tc.prim(PRIM_bool);
      return;
    }
    diag.error(b->loc, strfmt("invalid operands to '%s' (`string` and `string`)", op.c_str()),
               "strings support + (concatenation) and comparisons", 1);
    return;
  }

  // logical
  if (op == "&&" || op == "and" || op == "||" || op == "or") {
    if (lt->isBool() && rt->isBool()) {
      b->binKind = (int)BinKind::ShortCircuit;
      b->type = tc.prim(PRIM_bool);
      return;
    }
    diag.error(b->loc, strfmt("'%s' requires bool operands, got '%s' and '%s'", op.c_str(),
                              typeToString(lt).c_str(), typeToString(rt).c_str()));
    b->type = tc.invalid();
    return;
  }

  // pointers
  if (lt->isPtr() || rt->isPtr()) {
    if (lt->isPtr() && rt->isPtr()) {
      if (op == "==") { b->binKind = (int)BinKind::PtrCmp; b->type = tc.prim(PRIM_bool); return; }
      if (op == "!=") { b->binKind = (int)BinKind::PtrCmp; b->type = tc.prim(PRIM_bool); return; }
      if (op == "-" ) { b->binKind = (int)BinKind::PtrDiff; b->type = tc.prim(PRIM_isize); return; }
      if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        b->binKind = (int)BinKind::PtrCmp; b->type = tc.prim(PRIM_bool); return;
      }
    } else if (lt->isPtr() && rt->isInt() && (op == "+" || op == "-")) {
      b->binKind = (int)BinKind::PtrArith; b->type = lt; return;
    } else if (rt->isPtr() && lt->isInt() && op == "+") {
      b->binKind = (int)BinKind::PtrArith; b->type = rt; return;
    }
    diag.error(b->loc, strfmt("invalid pointer operation '%s' between '%s' and '%s'", op.c_str(),
                              typeToString(lt).c_str(), typeToString(rt).c_str()),
               "pointer arithmetic: `p + i` scales by the pointee size; compare pointers with == !=", 1);
    b->type = tc.invalid();
    return;
  }

  // enums: equality only
  if (lt->isEnum() || rt->isEnum()) {
    if (lt->isEnum() && rt->isEnum() && lt->decl == rt->decl && (op == "==" || op == "!=")) {
      b->binKind = (int)BinKind::EnumCmp;
      b->type = tc.prim(PRIM_bool);
      return;
    }
    diag.error(b->loc, strfmt("cannot apply '%s' to enum values ('%s' and '%s'); cast to an integer "
                              "for bitwise/numeric operations", op.c_str(), typeToString(lt).c_str(),
                              typeToString(rt).c_str()));
    b->type = tc.invalid();
    return;
  }

  // vectors: lane-wise arithmetic
  if (lt->isVector() || rt->isVector()) {
    if (tc.same(lt, rt) && (op == "+" || op == "-" || op == "*" || op == "/")) {
      b->binKind = (int)BinKind::VecArith;
      b->type = lt;
      return;
    }
    diag.error(b->loc, strfmt("vector operation '%s' requires matching vector types, got '%s' and '%s'",
                              op.c_str(), typeToString(lt).c_str(), typeToString(rt).c_str()));
    b->type = tc.invalid();
    return;
  }

  // numeric
  bool lNum = lt->isInt() || lt->isFloat();
  bool rNum = rt->isInt() || rt->isFloat();
  if (lNum && rNum) {
    // literal refitting: make literal adopt the other side's type
    if (b->lhs->kind == Expr::IntLit || b->lhs->kind == Expr::FloatLit) refit(b->lhs, rt);
    else if (b->rhs->kind == Expr::IntLit || b->rhs->kind == Expr::FloatLit) refit(b->rhs, lt);
    lt = b->lhs->type;
    rt = b->rhs->type;
    bool bothInt = lt->isInt() && rt->isInt();
    bool bothFloat = lt->isFloat() && rt->isFloat();
    if (!tc.same(lt, rt) && !(bothInt || bothFloat)) {
      diag.error(b->loc, strfmt("cannot apply '%s' to '%s' and '%s'", op.c_str(),
                                typeToString(lt).c_str(), typeToString(rt).c_str()),
                 "Core does not implicitly mix integer widths or float/integer: use `as` to cast", 1);
      b->type = tc.invalid();
      return;
    }
    bool cmpOp = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    if (cmpOp) {
      if (!tc.same(lt, rt)) {
        diag.error(b->loc, strfmt("cannot compare '%s' and '%s'", typeToString(lt).c_str(),
                                  typeToString(rt).c_str()),
                   "use an explicit cast so both sides have the same type", 1);
        b->type = tc.invalid();
        return;
      }
      b->binKind = (int)BinKind::Cmp;
      b->type = tc.prim(PRIM_bool);
      return;
    }
    if (op == "<<" || op == ">>") {
      if (!rt->isInt()) {
        diag.error(b->loc, "shift amount must be an integer");
        b->type = tc.invalid();
        return;
      }
      b->binKind = (int)BinKind::Shift;
      b->type = lt;
      return;
    }
    if (op == "&" || op == "|" || op == "^" || op == "%" || op == "/" || op == "+" || op == "-") {
      if (!bothInt && (op == "&" || op == "|" || op == "^" || op == "%")) {
        diag.error(b->loc, strfmt("'%s' requires integer operands", op.c_str()));
        b->type = tc.invalid();
        return;
      }
      b->binKind = (int)BinKind::Arith;
      b->type = lt;
      return;
    }
    if (op == "*") { b->binKind = (int)BinKind::Arith; b->type = lt; return; }
  }

  // bool equality
  if (lt->isBool() && rt->isBool() && (op == "==" || op == "!=")) {
    b->binKind = (int)BinKind::Cmp;
    b->type = tc.prim(PRIM_bool);
    return;
  }

  diag.error(b->loc, strfmt("cannot apply '%s' to '%s' and '%s'", op.c_str(),
                            typeToString(lt).c_str(), typeToString(rt).c_str()),
             op == "+" && lt->isString()
                 ? "`+` supports string + string, but the right side has type " + typeToString(rt)
                 : "");
  b->type = tc.invalid();
}

void Sema::checkAssign(EAssign *a) {
  checkExpr(a->target, true);
  checkExpr(a->value);
  if (!isLValue(a->target)) {
    diag.error(a->loc, "left side of assignment is not assignable",
               "assignable: variables, *pointers, struct/class fields, array elements", 1);
    return;
  }
  // mutability
  if (a->target->kind == Expr::Ident) {
    auto *id = (EIdent *)a->target;
    if (id->idKind == IdKind::Local) {
      LocalVar lv;
      canReadVar(id->name, lv);
      if (!lv.isMut) {
        diag.error(a->loc, strfmt("cannot assign to immutable variable '%s'", id->name.c_str()),
                   "declare it with `mut " + id->name + " = ...`", 1);
        return;
      }
    } else if (id->idKind == IdKind::Global) {
      auto *g = (DGlobal *)id->target;
      if (!g->isMut) {
        diag.error(a->loc, strfmt("cannot assign to immutable global '%s'", g->name.c_str()),
                   "declare it with `mut " + g->name + ": " + typeToString(resolveType(g->type)) + "`", 1);
        return;
      }
    } else {
      diag.error(a->loc, "cannot assign to this expression");
      return;
    }
  }
  Type *tt = a->target->type, *vt = a->value->type;
  if (!tt || !vt) return;
  // plain assignment is op "="; compounds are "+=" etc.
  if (a->op != "=") {
    // build temporary binary node for rule checking
    EBinary tmp(a->loc, a->op.substr(0, a->op.size() - 1), a->target, a->value);
    checkBinary(&tmp);
    a->type = tt;
    if (tmp.binKind == (int)BinKind::Arith || tmp.binKind == (int)BinKind::Shift ||
        tmp.binKind == (int)BinKind::VecArith || tmp.binKind == (int)BinKind::PtrArith)
      return;
    diag.error(a->loc, strfmt("operator '%s' cannot be used for assignment", a->op.c_str()));
    return;
  }
  if (!typesAssignable(tt, vt, a->value, a->loc, "value")) return;
  a->type = tt;
}

// ------------------------------------------------------------------- casts --
void Sema::checkCast(ECast *c) {
  checkExpr(c->e);
  Type *dst = resolveType(c->ty);
  if (!dst) return;
  Type *src = c->e->type;
  if (!src) return;
  c->type = dst;
  auto set = [&](CastKind k) { c->castKind = (int)k; };
  if (tc.same(src, dst) || (src->isVoid() && dst->isPtr())) { set(CastKind::Identity); return; }
  if (src->isNever()) { set(CastKind::ToNever); return; }
  if (src->isInt() && dst->isInt()) { set(CastKind::IntToInt); return; }
  if (src->isInt() && dst->isFloat()) { set(CastKind::IntToFloat); return; }
  if (src->isFloat() && dst->isInt()) { set(CastKind::FloatToInt); return; }
  if (src->isFloat() && dst->isFloat()) { set(CastKind::FloatToFloat); return; }
  if (src->isBool() && dst->isInt()) { set(CastKind::BoolToInt); return; }
  if (src->isInt() && dst->isBool()) { set(CastKind::IntToBool); return; }
  if (src->isPtr() && dst->isPtr()) {
    if (tc.same(src->pointee, dst->pointee)) { set(CastKind::Identity); return; }
    if (unsafeDepth == 0) {
      diag.error(c->loc, strfmt("pointer cast from '%s' to '%s' requires an unsafe block",
                                typeToString(src).c_str(), typeToString(dst).c_str()),
                 "wrap in `unsafe { ... }` - casting between pointer types can break memory safety", 1);
      return;
    }
    set(CastKind::PtrToPtr);
    return;
  }
  if (src->isInt() && dst->isPtr()) {
    if (unsafeDepth == 0) {
      diag.error(c->loc, "integer-to-pointer casts require an unsafe block",
                 "wrap in `unsafe { ... }`", 1);
      return;
    }
    set(CastKind::IntToPtr);
    return;
  }
  if (src->isPtr() && dst->isInt()) {
    if (unsafeDepth == 0) {
      diag.error(c->loc, "pointer-to-integer casts require an unsafe block",
                 "wrap in `unsafe { ... }`", 1);
      return;
    }
    set(CastKind::PtrToInt);
    return;
  }
  if (src->isEnum() && dst->isInt()) { set(CastKind::EnumToInt); return; }
  if (src->isInt() && dst->isEnum()) { set(CastKind::IntToEnum); return; }
  if (src->isClass() && dst->isClass()) {
    // up or down
    for (DClass *b = (DClass *)src->decl; b;) {
      if (b == (DClass *)dst->decl) { set(CastKind::ClassUp); return; }
      ClassLayout *bl = layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
    if (unsafeDepth == 0) {
      diag.error(c->loc, strfmt("downcast from '%s' to '%s' requires an unsafe block (no runtime "
                                "type checks exist)", typeToString(src).c_str(), typeToString(dst).c_str()));
      return;
    }
    set(CastKind::ClassDown);
    return;
  }
  if (src->isClass() && dst->isInterface()) {
    ClassLayout *sl = layoutOf((DClass *)src->decl);
    DInterface *di = (DInterface *)dst->ifaceDecl;
    for (auto &[iff, _] : sl->interfaces)
      if (iff == di) { set(CastKind::IfaceWrap); return; }
    diag.error(c->loc, strfmt("class '%s' does not implement interface '%s'",
                              typeToString(src).c_str(), typeToString(dst).c_str()));
    return;
  }
  if (src->isInterface() && dst->isClass()) {
    if (unsafeDepth == 0) {
      diag.error(c->loc, "unwrapping an interface value to a concrete class requires an unsafe block",
                 "the runtime performs no type check", 1);
      return;
    }
    set(CastKind::IfaceUnwrap);
    return;
  }
  diag.error(c->loc, strfmt("cannot cast '%s' to '%s'", typeToString(src).c_str(),
                            typeToString(dst).c_str()));
}

// ----------------------------------------------------------------- members --
Type *Sema::checkMemberForRead(EMember *m) {
  checkExpr(m->obj);
  Expr *obj = m->obj;
  if (!obj->type) {
    // module/type-qualified handled for calls; value reads:
    if (obj->kind == Expr::Ident) {
      auto *id = (EIdent *)obj;
      if (id->idKind == IdKind::Module) {
        ModuleSema *mod = (ModuleSema *)id->target;
        auto git = mod->globals.find(m->name);
        if (git != mod->globals.end() && visible(git->second, curModule)) {
          m->memberKind = MemberKind::ModuleMember;
          m->viaModule = mod;
          m->target = git->second;
          return resolveType(git->second->type);
        }
        auto cit = mod->consts.find(m->name);
        if (cit != mod->consts.end() && visible(cit->second, curModule)) {
          m->memberKind = MemberKind::ModuleMember;
          m->viaModule = mod;
          m->target = cit->second;
          return resolveType(cit->second->type);
        }
        diag.error(m->loc, strfmt("module '%s' has no public member '%s'", id->name.c_str(),
                                  m->name.c_str()));
        return nullptr;
      }
      if (id->idKind == IdKind::TypeRef) {
        Decl *td = (Decl *)id->target;
        if (td->kind == Decl::Enum) {
          DEnum *en = (DEnum *)td;
          for (size_t vi = 0; vi < en->variants.size(); vi++) {
            if (en->variants[vi].name == m->name) {
              if (!en->variants[vi].payloadTypes.empty()) {
                diag.error(m->loc, strfmt("variant '%s' carries data; construct it: '%s.%s(...)'",
                                          m->name.c_str(), en->name.c_str(), m->name.c_str()));
                return nullptr;
              }
              m->memberKind = MemberKind::VariantOf;
              m->memberIndex = (int)vi;
              m->target = en;
              return tc.getEnum(en, {});
            }
          }
          diag.error(m->loc, strfmt("enum '%s' has no variant '%s'", en->name.c_str(), m->name.c_str()));
          return nullptr;
        }
        if (td->kind == Decl::Class || td->kind == Decl::Struct) {
          std::string tn = td->kind == Decl::Class ? ((DClass *)td)->name : ((DStruct *)td)->name;
          diag.error(m->loc, strfmt("type '%s' has no value member '%s'", tn.c_str(), m->name.c_str()),
                     "static methods are called: Type.method(...)", 1);
          return nullptr;
        }
      }
    }
    return nullptr;
  }

  Type *ot = obj->type;
  Type *derefTy = ot->isPtr() ? ot->pointee : ot;

  if (derefTy->isClass() || derefTy->isStruct()) {
    Type *ft = fieldTypeOf(derefTy, m->name);
    if (ft) {
      m->memberKind = MemberKind::Field;
      m->memberIndex = fieldIndexOf(derefTy, m->name);
      return ft;
    }
    // method reference without call? (methods must be called)
    std::string tn = derefTy->isClass() ? ((DClass *)derefTy->decl)->name : ((DStruct *)derefTy->decl)->name;
    diag.error(m->loc, strfmt("no field '%s' on %s '%s'", m->name.c_str(),
                              derefTy->isClass() ? "class" : "struct", tn.c_str()),
               "methods are called with (...): obj." + m->name + "(...)", 1);
    return nullptr;
  }
  if (derefTy->isInterface()) {
    diag.error(m->loc, "interface methods are called with (...)");
    return nullptr;
  }
  if (derefTy->isEnum()) {
    DEnum *en = (DEnum *)derefTy->decl;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
      if (en->variants[vi].name == m->name) {
        if (!en->variants[vi].payloadTypes.empty()) {
          diag.error(m->loc, strfmt("variant '%s' carries data; construct it: '%s(...)'",
                                    m->name.c_str(), m->name.c_str()));
          return nullptr;
        }
        m->memberKind = MemberKind::VariantOf;
        m->memberIndex = (int)vi;
        m->target = en;
        return derefTy;
      }
    }
    diag.error(m->loc, strfmt("enum '%s' has no variant '%s'", en->name.c_str(), m->name.c_str()),
               "use match to extract variant payloads", 1);
    return nullptr;
  }
  diag.error(m->loc, strfmt("value of type '%s' has no members", typeToString(ot).c_str()));
  return nullptr;
}

Type *Sema::resolveEnumArgs(DEnum *e, std::vector<Type *> args) {
  // check arg count for generic enums
  if (!e->genericParams.empty() && args.size() != e->genericParams.size()) {
    diag.error(e->loc, strfmt("enum '%s' expects %zu type arguments, got %zu", e->name.c_str(),
                              e->genericParams.size(), args.size()));
  }
  return tc.getEnum(e, args);
}

int Sema::fieldIndexOf(Type *structTy, const std::string &name) {
  if (structTy->isClass()) {
    DClass *c = (DClass *)structTy->decl;
    ClassLayout *cl = layoutOf(c);
    if (!cl) return -1;
    std::vector<DClass *> chain;
    for (DClass *b = c; b;) {
      chain.push_back(b);
      ClassLayout *bl = layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
    int idx = cl->polymorphic ? 1 : 0; // slot 0 is the vptr
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      for (auto &fld : (*it)->fields) {
        if (fld.name == name) return idx;
        idx++;
      }
    }
    return -1;
  }
  if (structTy->isStruct()) {
    DStruct *s = (DStruct *)structTy->decl;
    for (size_t i = 0; i < s->fields.size(); i++)
      if (s->fields[i].name == name) return (int)i;
  }
  return -1;
}

Type *Sema::fieldTypeOf(Type *structTy, const std::string &name) {
  if (structTy->isClass()) {
    DClass *c = (DClass *)structTy->decl;
    for (DClass *b = c; b;) {
      for (auto &fld : b->fields)
        if (fld.name == name) return substituteFieldType(fld.type, structTy);
      ClassLayout *bl = layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
    return nullptr;
  }
  if (structTy->isStruct()) {
    DStruct *s = (DStruct *)structTy->decl;
    for (auto &fld : s->fields)
      if (fld.name == name) return substituteFieldType(fld.type, structTy);
  }
  return nullptr;
}

Type *Sema::substituteFieldType(TypeExpr *te, Type *structTy) {
  Decl *d = (Decl *)structTy->decl;
  std::vector<std::string> gps;
  if (d->kind == Decl::Class) gps = ((DClass *)d)->genericParams;
  else if (d->kind == Decl::Struct) gps = ((DStruct *)d)->genericParams;
  if (gps.empty() || structTy->genericArgs.empty()) return resolveType(te);
  std::map<std::string, Type *> local;
  for (size_t i = 0; i < gps.size() && i < structTy->genericArgs.size(); i++)
    local[gps[i]] = structTy->genericArgs[i];
  auto *old = subst;
  subst = &local;
  Type *t = resolveType(te);
  subst = old;
  return t;
}

// -------------------------------------------------------------------- calls --
Type *Sema::checkCall(ECall *call) {
  Expr *callee = call->callee;

  // value callee: closure or function pointer value
  if (callee->kind != Expr::Ident && callee->kind != Expr::Member) {
    checkExpr(callee);
    if (!callee->type) return nullptr;
    if (callee->type->isFunc()) {
      Type *ft = callee->type;
      if (call->args.size() != ft->params.size()) {
        diag.error(call->loc, strfmt("function value expects %zu arguments, got %zu",
                                     ft->params.size(), call->args.size()));
        return nullptr;
      }
      for (size_t ai = 0; ai < call->args.size(); ai++) {
        checkExpr(call->args[ai]);
        if (!typesAssignable(ft->params[ai], call->args[ai]->type, call->args[ai],
                             call->args[ai]->loc, "argument"))
          return nullptr;
      }
      return ft->ret;
    }
    diag.error(call->loc, strfmt("value of type '%s' is not callable", typeToString(callee->type).c_str()));
    return nullptr;
  }

  // ---- builtins ---- (user-defined functions shadow compiler builtins)
  if (callee->kind == Expr::Ident) {
    auto *id = (EIdent *)callee;
    int b;
    if (builtinByName(id->name, b) && lookupFuncsVisible(id->name).empty()) {
      for (auto *a : call->args) checkExpr(a);
      id->idKind = IdKind::Builtin;
      id->builtin = b;
      return checkBuiltinCall(call, (Builtin)b, id->name);
    }
    // local variable of function type?
    LocalVar lv;
    if (canReadVar(id->name, lv) && lv.type->isFunc()) {
      id->idKind = IdKind::Local;
      id->scopeId = lv.declScope;
      id->type = lv.type;
      call->callee = id;
      // closure call: annotate callee type; args checked against signature
      Type *ft = lv.type;
      if (call->args.size() != ft->params.size()) {
        diag.error(call->loc, strfmt("function value expects %zu arguments, got %zu",
                                     ft->params.size(), call->args.size()));
        return nullptr;
      }
      for (size_t ai = 0; ai < call->args.size(); ai++) {
        checkExpr(call->args[ai]);
        if (!typesAssignable(ft->params[ai], call->args[ai]->type, call->args[ai],
                             call->args[ai]->loc, "argument"))
          return nullptr;
      }
      return ft->ret;
    }
  }

  std::string name;
  std::vector<DFunc *> candidates;
  bool viaModule = false;

  if (callee->kind == Expr::Ident) {
    auto *id = (EIdent *)callee;
    name = id->name;
    // explicit generic arguments: `alloc_array<i32>(5)`
    if (id->typeArgs) {
      for (auto *a : call->args) checkExpr(a);
      std::vector<DFunc *> gcands = lookupFuncsVisible(name);
      DFunc *gtmpl = nullptr;
      for (DFunc *g : gcands)
        if (!g->genericParams.empty()) gtmpl = g;
      if (!gtmpl) {
        diag.error(call->loc, strfmt("function '%s' is not generic but explicit type arguments "
                                     "were given", name.c_str()));
        return nullptr;
      }
      if (id->typeArgs->genericArgs.size() != gtmpl->genericParams.size()) {
        diag.error(call->loc, strfmt("'%s' expects %zu type arguments, got %zu", name.c_str(),
                                     gtmpl->genericParams.size(),
                                     id->typeArgs->genericArgs.size()));
        return nullptr;
      }
      std::vector<Type *> gargs;
      for (auto *gte : id->typeArgs->genericArgs) {
        Type *gt = resolveType(gte);
        if (!gt) return nullptr;
        gargs.push_back(gt);
      }
      // arity check
      if (call->args.size() != gtmpl->params.size()) {
        diag.error(call->loc, strfmt("'%s' expects %zu arguments, got %zu", name.c_str(),
                                     gtmpl->params.size(), call->args.size()));
        return nullptr;
      }
      // arg type check under substitution
      std::map<std::string, Type *> sub;
      for (size_t gi = 0; gi < gtmpl->genericParams.size(); gi++) sub[gtmpl->genericParams[gi]] = gargs[gi];
      auto *saved = subst;
      subst = &sub;
      for (size_t ai = 0; ai < call->args.size(); ai++) {
        Type *want = resolveType(gtmpl->params[ai].type);
        if (!typesAssignable(want, call->args[ai]->type, call->args[ai], call->args[ai]->loc, "argument")) {
          subst = saved;
          return nullptr;
        }
      }
      subst = saved;
      GenericInstance *gi = instantiateGeneric(gtmpl, gargs, call->loc);
      if (!gi) return nullptr;
      call->genInstance = gi;
      id->idKind = IdKind::Func;
      id->resolvedFunc = gtmpl;
      auto *saved2 = subst;
      subst = &sub;
      Type *ret = gtmpl->retType ? resolveType(gtmpl->retType) : tc.prim(PRIM_void);
      subst = saved2;
      return ret;
    }
    candidates = lookupFuncsVisible(name);
    if (candidates.empty()) {
      if (Decl *ed = lookupVariantCtor(name)) {
        return checkVariantCtor(call, (DEnum *)ed, name);
      }
      diag.error(id->loc, strfmt("unknown function '%s'", name.c_str()),
                 "functions must be defined in this module, imported, or part of the prelude", 1);
      return nullptr;
    }
  } else {
    auto *m = (EMember *)callee;
    // method call on a value
    checkExpr(m->obj);
    if (m->obj->type) {
      Type *ot = m->obj->type;
      Type *derefTy = ot->isPtr() ? ot->pointee : ot;
      if (derefTy->isEnum()) {
        return checkVariantCtor(call, (DEnum *)derefTy->decl, m->name, derefTy);
      }
      if (derefTy->isInterface()) {
        DInterface *iface = (DInterface *)derefTy->ifaceDecl;
        for (size_t mi = 0; mi < iface->methods.size(); mi++)
          if (iface->methods[mi]->name == m->name) { m->ifaceMethodIndex = (int)mi; break; }
        if (m->ifaceMethodIndex < 0) {
          diag.error(call->loc, strfmt("interface '%s' has no method '%s'", iface->name.c_str(),
                                       m->name.c_str()));
          return nullptr;
        }
        DFunc *proto = iface->methods[m->ifaceMethodIndex];
        if (call->args.size() != proto->params.size()) {
          diag.error(call->loc, strfmt("method '%s' expects %zu arguments, got %zu", m->name.c_str(),
                                       proto->params.size(), call->args.size()));
          return nullptr;
        }
        for (size_t ai = 0; ai < call->args.size(); ai++) {
          checkExpr(call->args[ai]);
          Type *want = resolveType(proto->params[ai].type);
          if (!typesAssignable(want, call->args[ai]->type, call->args[ai], call->args[ai]->loc, "argument"))
            return nullptr;
        }
        m->memberKind = MemberKind::IfaceMethod;
        m->target = iface;
        return proto->retType ? resolveType(proto->retType) : tc.prim(PRIM_void);
      }
      if (derefTy->isClass() || derefTy->isStruct()) {
        name = m->name;
        if (derefTy->isClass()) {
          // most-derived overload set wins: walk the chain and stop at the
          // first class that declares the name
          DClass *c = (DClass *)derefTy->decl;
          for (DClass *cc = c; cc && candidates.empty();) {
            for (DFunc *mth : cc->methods)
              if (mth->name == name) candidates.push_back(mth);
            ClassLayout *cl = layoutOf(cc);
            cc = cl ? cl->base : nullptr;
          }
        } else {
          for (DFunc *mth : ((DStruct *)derefTy->decl)->methods)
            if (mth->name == name) candidates.push_back(mth);
        }
        if (candidates.empty()) {
          diag.error(call->loc, strfmt("no method '%s' on %s '%s'", name.c_str(),
                                       derefTy->isClass() ? "class" : "struct",
                                       (derefTy->isClass() ? ((DClass *)derefTy->decl)->name
                                                           : ((DStruct *)derefTy->decl)->name).c_str()));
          return nullptr;
        }
        m->memberKind = MemberKind::Method;
      } else {
        diag.error(m->loc, strfmt("value of type '%s' has no methods", typeToString(ot).c_str()));
        return nullptr;
      }
    } else if (m->obj->kind == Expr::Ident) {
      auto *id = (EIdent *)m->obj;
      if (id->idKind == IdKind::Module) {
        ModuleSema *mod = (ModuleSema *)id->target;
        auto fit = mod->funcs.find(m->name);
        if (fit == mod->funcs.end()) {
          diag.error(call->loc, strfmt("module '%s' has no function '%s'", id->name.c_str(), m->name.c_str()));
          return nullptr;
        }
        for (auto *f : fit->second)
          if (!visible(f, curModule)) {
            diag.error(call->loc, strfmt("function '%s.%s' is not public", id->name.c_str(), m->name.c_str()),
                       "mark it `pub func %s(...)` in module " + id->name, 1);
            return nullptr;
          }
        candidates = fit->second;
        name = m->name;
        m->memberKind = MemberKind::ModuleMember;
        m->viaModule = mod;
        viaModule = true;
      } else if (id->idKind == IdKind::TypeRef) {
        Decl *td = (Decl *)id->target;
        if (td->kind == Decl::Enum) return checkVariantCtor(call, (DEnum *)td, m->name);
        if (td->kind == Decl::Class || td->kind == Decl::Struct) {
          std::string typeName = td->kind == Decl::Class ? ((DClass *)td)->name
                                                         : ((DStruct *)td)->name;
          std::vector<DFunc *> mths = td->kind == Decl::Class
                                          ? std::vector<DFunc *>(((DClass *)td)->methods)
                                          : std::vector<DFunc *>(((DStruct *)td)->methods);
          // static methods...
          for (DFunc *f : mths)
            if (f->name == m->name && f->isStatic) candidates.push_back(f);
          // ...or base-class method calls with implicit self (super-style)
          if (candidates.empty() && td->kind == Decl::Class && curFunc && curFunc->parent) {
            Decl *myType = curFunc->parent;
            bool derives = false;
            if (myType->kind == Decl::Class) {
              for (DClass *b = (DClass *)myType; b && !derives;) {
                if (b == (DClass *)td) derives = true;
                ClassLayout *bl = layoutOf(b);
                b = bl ? bl->base : nullptr;
              }
            }
            if (derives) {
              for (DFunc *f : mths)
                if (f->name == m->name && !f->isStatic) candidates.push_back(f);
              if (!candidates.empty()) {
                call->memberKind = MemberKind::Method;   // self comes from the caller
                m->memberKind = MemberKind::Method;
                m->name = m->name; // keep
                name = m->name;
                call->baseSelfCall = true;
                m->obj = new ESelf(call->loc); // self
                checkExpr(m->obj);
                m->memberKind = MemberKind::Method;
              }
            }
          }
          name = m->name;
          if (candidates.empty()) {
            diag.error(call->loc, strfmt("no static method '%s' on type '%s'", m->name.c_str(),
                                         typeName.c_str()),
                               td->kind == Decl::Class ? "constructors run through `Type { ... }` literals or a subclass init calling `Base.init(...)`" : "");
            return nullptr;
          }
        } else {
          diag.error(call->loc, "unsupported call target");
          return nullptr;
        }
      } else {
        diag.error(call->loc, strfmt("unknown call target for '%s'", m->name.c_str()));
        return nullptr;
      }
    } else {
      return nullptr;
    }
  }

  // check args BEFORE overload resolution
  for (auto *a : call->args) checkExpr(a);

  bool ok = false;
  DFunc *chosen = resolveOverload(candidates, call->args, call->loc, name, ok);
  if (!ok || !chosen) return nullptr;
  call->resolvedFunc = chosen;
  // finalize literal coercions against the chosen overload; constant-foldable
  // arguments (e.g. `4 * 10`) are accepted for in-range integer params (the
  // codegen casts them)
  quiet_++;
  for (size_t ai = 0; ai < call->args.size() && ai < chosen->params.size(); ai++) {
    Type *want = resolveType(chosen->params[ai].type);
    Type *got = call->args[ai]->type;
    if (!want || !got || tc.same(want, got)) continue;
    bool foldOk = true;
    unsigned long long fv = evalConstUint(call->args[ai], foldOk);
    if (foldOk && want->isInt() && got->isInt()) continue; // coerced in codegen
    typesAssignable(want, got, call->args[ai], call->args[ai]->loc, "argument");
  }
  quiet_--;
  // annotate the callee so codegen finds the implementation
  if (callee->kind == Expr::Ident) {
    auto *id = (EIdent *)callee;
    id->idKind = IdKind::Func;
    id->resolvedFunc = chosen;
  } else if (callee->kind == Expr::Member) {
    auto *m = (EMember *)callee;
    m->resolvedFunc = chosen;
  }

  // generic instantiation
  if (!chosen->genericParams.empty()) {
    std::map<std::string, Type *> vars;
    for (auto &gp : chosen->genericParams) vars[gp] = tc.genericVar(gp);
    auto *savedSubst = subst;
    subst = &vars;
    std::vector<Type *> gargs;
    bool inferOk = true;
    for (size_t pi = 0; pi < chosen->params.size() && pi < call->args.size(); pi++) {
      Type *want = resolveType(chosen->params[pi].type);
      Type *got = call->args[pi]->type;
      if (!unifyTypes(want, got, vars)) { inferOk = false; break; }
    }
    subst = savedSubst;
    if (!inferOk) return nullptr;
    gargs.clear();
    for (auto &gp : chosen->genericParams) {
      auto it = vars.find(gp);
      if (it == vars.end() || it->second->isNamedGeneric) {
        diag.error(call->loc, strfmt("cannot infer type parameter '%s' for '%s'; add a cast or "
                                     "explicit type annotation", gp.c_str(), name.c_str()));
        return nullptr;
      }
      gargs.push_back(it->second);
    }
    GenericInstance *gi = instantiateGeneric(chosen, gargs, call->loc);
    if (!gi) return nullptr;
    call->genInstance = gi;
    // result type: resolve under substitution
    auto *savedSubst2 = subst;
    std::map<std::string, Type *> sub;
    for (size_t i = 0; i < chosen->genericParams.size(); i++) sub[chosen->genericParams[i]] = gargs[i];
    subst = &sub;
    Type *ret = chosen->retType ? resolveType(chosen->retType) : tc.prim(PRIM_void);
    subst = savedSubst2;
    return ret;
  }

  // variadic extern check: extra args must be primitive
  if (chosen->isVariadic) {
    for (size_t ai = chosen->params.size(); ai < call->args.size(); ai++) {
      Type *t = call->args[ai]->type;
      if (!t || !(t->isInt() || t->isFloat() || t->isPtr() || t->isBool())) {
        diag.error(call->args[ai]->loc, strfmt("variadic argument %zu must be an integer, float, "
                                               "pointer or bool for the C ABI", ai + 1));
        return nullptr;
      }
    }
  }

  return chosen->retType ? resolveType(chosen->retType) : tc.prim(PRIM_void);
}

bool Sema::unifyTypes(Type *want, Type *got, std::map<std::string, Type *> &vars) {
  if (!want || !got) return false;
  if (want->isNamedGeneric) {
    auto it = vars.find(want->genericVarName);
    if (it != vars.end()) {
      if (it->second->isNamedGeneric) {
        if (got->isNamedGeneric) return it->second->genericVarName == got->genericVarName;
        it->second = got; // bind
        return true;
      }
      return tc.same(it->second, got);
    }
    return false;
  }
  if (tc.same(want, got)) return true;
  // literal fit
  if (got->kind == TypeKind::Prim && want->isInt()) return got->isInt();
  if (want->isPtr() && got->isPtr()) {
    // array decay: ptr<[T; N]> argument against ptr<T> binds T to the element
    if (got->pointee->isArray() && want->pointee->isNamedGeneric)
      return unifyTypes(want->pointee, got->pointee->elem, vars);
    return unifyTypes(want->pointee, got->pointee, vars);
  }
  if (want->kind == TypeKind::Struct && got->kind == TypeKind::Struct && want->decl == got->decl) {
    if (want->genericArgs.size() != got->genericArgs.size()) return false;
    for (size_t i = 0; i < want->genericArgs.size(); i++)
      if (!unifyTypes(want->genericArgs[i], got->genericArgs[i], vars)) return false;
    return true;
  }
  return false;
}

GenericInstance *Sema::instantiateGeneric(DFunc *tmpl, const std::vector<Type *> &args, SourceLoc loc) {
  std::string key = mangleFuncName(tmpl, args);
  auto kkey = std::make_pair(tmpl, key);
  auto it = instances.find(kkey);
  if (it != instances.end()) return it->second;

  static int depth = 0;
  if (++depth > 64) {
    diag.error(loc, "generic instantiation depth exceeded (64) - recursive generic expansion?");
    depth--;
    return nullptr;
  }

  auto *gi = new GenericInstance();
  gi->tmpl = tmpl;
  gi->args = args;
  gi->mangledName = key;
  instances[kkey] = gi;
  instanceOrder.push_back(gi);

  // clone the template body and check it under substitution
  static ASTContext cloneArena; // process-lifetime arena for instantiation clones
  ASTCloner cloner(cloneArena);
  DFunc *clone = cloneArena.make<DFunc>(tmpl->loc);
  clone->name = tmpl->name;
  clone->genericParams = tmpl->genericParams; // keeps subst active in codegen
  clone->parent = tmpl->parent;
  { // the clone belongs to the template's defining module
    auto fm = funcModule.find(tmpl);
    if (fm != funcModule.end()) funcModule[clone] = fm->second;
  }
  clone->isPub = tmpl->isPub;
  clone->isStatic = tmpl->isStatic;
  clone->isVariadic = tmpl->isVariadic;
  clone->linkName = tmpl->linkName;
  cloner.funcBody(tmpl, clone);

  std::map<std::string, Type *> sub;
  for (size_t i = 0; i < tmpl->genericParams.size() && i < args.size(); i++)
    sub[tmpl->genericParams[i]] = args[i];
  checkFuncDecl(clone, sub);

  gi->clonedFunc = clone;
  depth--;
  return gi;
}

Type *Sema::checkVariantCtor(ECall *call, DEnum *e, const std::string &vname, Type *knownType) {
  std::vector<Type *> knownArgs;
  if (knownType) knownArgs = knownType->genericArgs;
  for (size_t vi = 0; vi < e->variants.size(); vi++) {
    auto &v = e->variants[vi];
    if (v.name != vname) continue;
    if (v.payloadTypes.empty()) {
      // unit variant used as a call: allow (returns the enum value)
      if (call->args.empty()) {
        if (call->callee->kind == Expr::Ident) {
          auto *id = (EIdent *)call->callee;
          id->idKind = IdKind::EnumConst;
          id->target = e;
          id->enumTag = (int)vi;
        } else if (call->callee->kind == Expr::Member) {
          auto *m = (EMember *)call->callee;
          m->memberKind = MemberKind::VariantOf;
          m->memberIndex = (int)vi;
          m->target = e;
        }
        return tc.getEnum(e, knownArgs);
      }
      diag.error(call->loc, strfmt("variant '%s' has no payload; use '%s' without (...)",
                                   vname.c_str(), vname.c_str()));
      return nullptr;
    }
    if (call->args.size() != v.payloadTypes.size()) {
      diag.error(call->loc, strfmt("variant '%s' expects %zu payload values, got %zu", vname.c_str(),
                                   v.payloadTypes.size(), call->args.size()));
      return nullptr;
    }
    // resolve payload types under enum's generic args (unified from args)
    std::map<std::string, Type *> sub;
    // infer generic args from payload arguments
    if (!e->genericParams.empty()) {
      if (!knownArgs.empty()) {
        for (size_t gi = 0; gi < e->genericParams.size() && gi < knownArgs.size(); gi++)
          sub[e->genericParams[gi]] = knownArgs[gi];
      } else for (auto &gp : e->genericParams) sub[gp] = tc.genericVar(gp);
      auto *saved = subst;
      subst = &sub;
      for (size_t ai = 0; ai < call->args.size(); ai++) {
        checkExpr(call->args[ai]);
        Type *want = resolveType(v.payloadTypes[ai]);
        unifyTypes(want, call->args[ai]->type, sub);
      }
      subst = saved;
      for (auto &gp : e->genericParams) {
        if (sub[gp]->isNamedGeneric) {
          diag.error(call->loc, strfmt("cannot infer type parameter '%s' for enum '%s'",
                                       gp.c_str(), e->name.c_str()));
          return nullptr;
        }
      }
    } else {
      for (auto *a : call->args) checkExpr(a);
    }
    std::vector<Type *> gargs;
    for (auto &gp : e->genericParams) gargs.push_back(sub[gp]);
    auto *saved = subst;
    std::map<std::string, Type *> sub2;
    for (size_t i = 0; i < e->genericParams.size(); i++) sub2[e->genericParams[i]] = gargs[i];
    subst = &sub2;
    for (size_t ai = 0; ai < call->args.size(); ai++) {
      Type *want = resolveType(v.payloadTypes[ai]);
      if (!typesAssignable(want, call->args[ai]->type, call->args[ai], call->args[ai]->loc, "payload value")) {
        subst = saved;
        return nullptr;
      }
    }
    subst = saved;
    call->resolvedFunc = (DFunc *)nullptr;
    call->memberKind = MemberKind::VariantOf;
    call->memberIndex = (int)vi;
    call->target = e;
    call->genInstance = nullptr;
    // annotate callee as variant reference
    if (call->callee->kind == Expr::Ident) {
      auto *id = (EIdent *)call->callee;
      id->idKind = IdKind::EnumConst;
      id->target = e;
      id->enumTag = (int)vi;
    } else if (call->callee->kind == Expr::Member) {
      auto *m = (EMember *)call->callee;
      m->memberKind = MemberKind::VariantOf;
      m->memberIndex = (int)vi;
      m->target = e;
    }
    return tc.getEnum(e, gargs);
  }
  diag.error(call->loc, strfmt("enum has no variant '%s'", vname.c_str()));
  return nullptr;
}

// ---------------------------------------------------------------- builtins --
Type *Sema::checkBuiltinCall(ECall *call, Builtin b, const std::string &name) {
  auto &args = call->args;
  switch (b) {
  case Builtin::Len: {
    if (args.size() != 1) {
      diag.error(call->loc, "len() takes exactly one argument");
      return nullptr;
    }
    Type *t = args[0]->type;
    if (t && t->isArray()) return tc.prim(PRIM_usize);
    if (t && t->isString()) return tc.prim(PRIM_usize);
    diag.error(args[0]->loc, strfmt("len() expects an array or string, got '%s'",
                                    t ? typeToString(t).c_str() : "?" ));
    return nullptr;
  }
  case Builtin::SourceFile:
    return tc.prim(PRIM_string);
  case Builtin::SourceLine:
    return tc.prim(PRIM_u64);
  case Builtin::AtomicLoad: {
    if (args.size() != 1 || !args[0]->type || !args[0]->type->isPtr()) {
      diag.error(call->loc, "atomic_load(ptr<T>) takes one pointer argument");
      return nullptr;
    }
    Type *pt = args[0]->type->pointee;
    if (!(pt->isInt() || pt->isBool())) {
      diag.error(call->loc, "atomic_load supports integer and bool pointees");
      return nullptr;
    }
    return pt;
  }
  case Builtin::AtomicStore: {
    if (args.size() != 2 || !args[0]->type || !args[0]->type->isPtr()) {
      diag.error(call->loc, "atomic_store(ptr<T>, value) takes a pointer and a value");
      return nullptr;
    }
    Type *pt = args[0]->type->pointee;
    if (!typesAssignable(pt, args[1]->type, args[1], args[1]->loc, "atomic value")) return nullptr;
    return tc.prim(PRIM_void);
  }
  case Builtin::AtomicAdd: case Builtin::AtomicSub: case Builtin::AtomicSwap: case Builtin::AtomicCas: {
    size_t want = b == Builtin::AtomicCas ? 3 : 2;
    if (args.size() != want || !args[0]->type || !args[0]->type->isPtr()) {
      diag.error(call->loc, strfmt("atomic builtin expects %zu arguments starting with a pointer", want));
      return nullptr;
    }
    Type *pt = args[0]->type->pointee;
    if (!(pt->isInt() || pt->isBool())) {
      diag.error(call->loc, "atomic arithmetic supports integer and bool pointees");
      return nullptr;
    }
    for (size_t ai = 1; ai < args.size(); ai++)
      if (!typesAssignable(pt, args[ai]->type, args[ai], args[ai]->loc, "atomic value")) return nullptr;
    return pt;
  }
  case Builtin::AtomicFence:
    if (!args.empty()) {
      diag.error(call->loc, "atomic_fence() takes no arguments");
      return nullptr;
    }
    return tc.prim(PRIM_void);
  case Builtin::VolatileLoad: {
    if (args.size() != 1 || !args[0]->type || !args[0]->type->isPtr()) {
      diag.error(call->loc, "volatile_load(ptr<T>) takes one pointer argument");
      return nullptr;
    }
    if (unsafeDepth == 0) {
      diag.error(call->loc, "volatile_load requires an unsafe block",
                 "volatile access is for memory-mapped hardware - see docs/language/memory-model.md", 1);
      return nullptr;
    }
    return args[0]->type->pointee;
  }
  case Builtin::VolatileStore: {
    if (args.size() != 2 || !args[0]->type || !args[0]->type->isPtr()) {
      diag.error(call->loc, "volatile_store(ptr<T>, value) takes a pointer and a value");
      return nullptr;
    }
    if (unsafeDepth == 0) {
      diag.error(call->loc, "volatile_store requires an unsafe block",
                 "volatile access is for memory-mapped hardware - see docs/language/memory-model.md", 1);
      return nullptr;
    }
    if (!typesAssignable(args[0]->type->pointee, args[1]->type, args[1], args[1]->loc, "volatile value"))
      return nullptr;
    return tc.prim(PRIM_void);
  }
  case Builtin::Asm: case Builtin::AsmVolatile: {
    if (unsafeDepth == 0) {
      diag.error(call->loc, strfmt("'%s' requires an unsafe block", name.c_str()),
                 "inline assembly is inherently unsafe", 1);
      return nullptr;
    }
    if (args.size() < 2) {
      diag.error(call->loc, strfmt("%s(asm: string, constraints: string, args...) - at least the "
                                   "template and constraint strings are required", name.c_str()));
      return nullptr;
    }
    if (args[0]->type && !args[0]->type->isString()) {
      diag.error(args[0]->loc, "the first inline asm argument must be the template string");
      return nullptr;
    }
    if (args[1]->type && !args[1]->type->isString()) {
      diag.error(args[1]->loc, "the second inline asm argument must be the constraint string");
      return nullptr;
    }
    for (size_t ai = 2; ai < args.size(); ai++) {
      Type *t = args[ai]->type;
      if (!t || !(t->isInt() || t->isPtr() || t->isBool() || t->isChar())) {
        diag.error(args[ai]->loc, "inline asm arguments must be integers, pointers or bools");
        return nullptr;
      }
    }
    return tc.prim(PRIM_u64);
  }
  case Builtin::Splat: {
    // splat_<vectorname>(scalar) -> vector; scalar must be the element type
    std::string vec = name.substr(6); // e.g. "f32x4"
    int vk = primKindByName(vec);
    std::string elemName = vec.substr(0, vec.find('x'));
    int ek = primKindByName(elemName);
    if (vk < 0 || ek < 0 || args.size() != 1 || !args[0]->type ||
        args[0]->type->prim != ek) {
      diag.error(call->loc, strfmt("splat_%s expects one '%s' argument", vec.c_str(), elemName.c_str()));
      return nullptr;
    }
    return tc.prim(vk);
  }
  case Builtin::SimdExtract: {
    std::string prim = name.substr(13); // after "simd_extract_"
    int pk = primKindByName(prim);
    if (pk < 0 || args.size() != 2) {
      diag.error(call->loc, strfmt("simd_extract_%s(vector, index)", prim.c_str()));
      return nullptr;
    }
    // return the element type: strip 'x<lanes>'
    std::string elem = prim.substr(0, prim.find('x'));
    int ek = primKindByName(elem);
    return tc.prim(ek);
  }
  case Builtin::SimdReplace: {
    std::string prim = name.substr(13);
    int pk = primKindByName(prim);
    if (pk < 0 || args.size() != 3) {
      diag.error(call->loc, strfmt("simd_replace_%s(vector, index, value)", prim.c_str()));
      return nullptr;
    }
    return tc.prim(pk);
  }
  default:
    return nullptr;
  }
}

// ------------------------------------------------------------------ match ---
Type *Sema::checkMatch(EMatch *m) {
  checkExpr(m->scrutinee);
  Type *st = m->scrutinee ? m->scrutinee->type : nullptr;
  if (!st) return nullptr;
  DEnum *scrutEnum = enumOf(st);

  Type *resultTy = nullptr;
  bool allVoid = true;
  std::set<int> coveredTags;
  bool hasWildcard = false;

  for (auto &arm : m->arms) {
    std::vector<std::pair<std::string, Type *>> binds;
    checkPattern(arm.pattern, st, binds);
    if (arm.pattern->kind == Pattern::Wild) hasWildcard = true;
    if (arm.pattern->kind == Pattern::Variant && scrutEnum)
      coveredTags.insert(arm.pattern->variantTag);
    if (arm.pattern->kind == Pattern::Lit && scrutEnum) {
      // const pattern on enum: cannot verify tag; treat as covering nothing
    }
    // check body with bindings in scope
    Scope armScope;
    armScope.parent = curScope;
    Scope *saved = curScope;
    curScope = &armScope;
    for (auto &[bn, bt] : binds) {
      LocalVar lv;
      lv.type = bt;
      lv.isMut = false;
      lv.declLoc = arm.pattern->loc;
      lv.declScope = &armScope;
      armScope.vars[bn] = lv;
    }
    checkBlock(arm.body);
    curScope = saved;

    // arm value type = last expression statement (if any)
    Type *armTy = nullptr;
    if (arm.body && arm.body->kind == Stmt::KBlock) {
      auto *blk = (SBlock *)arm.body;
      if (!blk->stmts.empty()) {
        Stmt *last = blk->stmts.back();
        if (last->kind == Stmt::KExpr) armTy = ((SExpr *)last)->e->type;
      }
    }
    if (armTy && !armTy->isVoid()) {
      allVoid = false;
      if (!resultTy) resultTy = armTy;
      else if (!tc.same(resultTy, armTy)) {
        // literal refit
        bool ok = typesAssignable(resultTy, armTy, nullptr, arm.body->loc, "match arm value");
        if (!ok) return nullptr;
      }
    }
  }

  if (scrutEnum && !hasWildcard) {
    // exhaustiveness
    std::string missing;
    for (size_t vi = 0; vi < scrutEnum->variants.size(); vi++) {
      if (!coveredTags.count((int)vi)) {
        if (!missing.empty()) missing += ", ";
        missing += scrutEnum->variants[vi].name;
      }
    }
    if (!missing.empty()) {
      diag.error(m->loc, strfmt("match on enum '%s' is not exhaustive; missing: %s",
                                scrutEnum->name.c_str(), missing.c_str()),
                 "add the missing variants or a `_ => ...` wildcard arm", 1);
    }
  }
  (void)hasWildcard;
  return allVoid ? tc.prim(PRIM_void) : resultTy;
}

void Sema::checkPattern(Pattern *p, Type *scrutinee, std::vector<std::pair<std::string, Type *>> &binds) {
  if (!p) return;
  switch (p->kind) {
  case Pattern::Wild:
    return;
  case Pattern::Var: {
    // capitalized names in patterns are variant names and must resolve
    if (!scrutinee->isEnum() && !p->name.empty() && isupper(p->name[0])) {
      diag.error(p->loc, strfmt("pattern '%s' looks like a variant but the match scrutinee is '%s'",
                                p->name.c_str(), typeToString(scrutinee).c_str()),
                 "variant patterns require an enum; use a literal or `_` otherwise", 1);
      return;
    }
    if (scrutinee->isEnum()) {
      DEnum *en = (DEnum *)scrutinee->decl;
      for (size_t vi = 0; vi < en->variants.size(); vi++) {
        if (en->variants[vi].name == p->name) {
          if (!en->variants[vi].payloadTypes.empty()) {
            diag.error(p->loc, strfmt("variant '%s' carries data; use '%s(binding)'",
                                      p->name.c_str(), p->name.c_str()));
            return;
          }
          p->kind = Pattern::Variant;
          p->enumDecl = en;
          p->variantTag = (int)vi;
          return;
        }
      }
    }
    p->bindType = scrutinee;
    binds.push_back({p->name, scrutinee});
    return;
  }
  case Pattern::Variant: {
    if (!scrutinee->isEnum()) {
      diag.error(p->loc, strfmt("variant pattern '%s' requires an enum scrutinee, got '%s'",
                                p->name.c_str(), typeToString(scrutinee).c_str()));
      return;
    }
    DEnum *en = (DEnum *)scrutinee->decl;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
      if (en->variants[vi].name == p->name) {
        p->enumDecl = en;
        p->variantTag = (int)vi;
        auto &payloads = en->variants[vi].payloadTypes;
        if (p->subs.size() != payloads.size()) {
          diag.error(p->loc, strfmt("variant '%s' has %zu payload values", p->name.c_str(),
                                    payloads.size()));
          return;
        }
        // resolve payload types under enum generics
        std::map<std::string, Type *> sub;
        for (size_t gi = 0; gi < en->genericParams.size() && gi < scrutinee->genericArgs.size(); gi++)
          sub[en->genericParams[gi]] = scrutinee->genericArgs[gi];
        auto *saved = subst;
        subst = sub.empty() ? nullptr : &sub;
        for (size_t si = 0; si < p->subs.size(); si++) {
          Pattern *sp = p->subs[si];
          Type *pt = resolveType(payloads[si]);
          p->payloadTypes.push_back(pt);
          if (sp->kind == Pattern::Var) {
            sp->bindType = pt;
            binds.push_back({sp->name, pt});
          }
        }
        subst = saved;
        return;
      }
    }
    diag.error(p->loc, strfmt("enum '%s' has no variant '%s'", en->name.c_str(), p->name.c_str()));
    return;
  }
  case Pattern::Lit: {
    checkExpr(p->litExpr);
    if (p->litExpr->type && !typesAssignable(scrutinee, p->litExpr->type, p->litExpr, p->loc,
                                            "match pattern")) return;
    return;
  }
  }
}

// ---------------------------------------------------------- termination -----
bool Sema::containsBreak(Stmt *s) {
  if (!s) return false;
  switch (s->kind) {
  case Stmt::KBreak: return true;
  case Stmt::KBlock:
    for (auto *x : ((SBlock *)s)->stmts) if (containsBreak(x)) return true;
    return false;
  case Stmt::KUnsafe:
    for (auto *x : ((SUnsafe *)s)->stmts) if (containsBreak(x)) return true;
    return false;
  case Stmt::KIf: {
    auto *i = (SIf *)s;
    return containsBreak(i->thenBlock) || containsBreak(i->elseBlock);
  }
  case Stmt::KSwitch: {
    auto *sw = (SSwitch *)s;
    for (auto &c : sw->cases) if (containsBreak(c.body)) return true;
    return containsBreak(sw->defaultBody);
  }
  case Stmt::KFor: return containsBreak(((SFor *)s)->body);
  case Stmt::KForIn: return containsBreak(((SForIn *)s)->body);
  default: return false;
  }
}

bool Sema::terminates(Stmt *s) {
  if (!s) return false;
  switch (s->kind) {
  case Stmt::KReturn: return true;
  case Stmt::KBreak: return false; // exits the loop, not the function
  case Stmt::KContinue: return false;
  case Stmt::KIf: {
    auto *i = (SIf *)s;
    return i->elseBlock && terminates(i->thenBlock) &&
           (i->elseBlock->kind == Stmt::KIf || i->elseBlock->kind == Stmt::KBlock) &&
           terminates(i->elseBlock);
  }
  case Stmt::KWhile: {
    auto *w = (SWhile *)s;
    return w->cond->kind == Expr::BoolLit && ((EBool *)w->cond)->value && !containsBreak(w->body);
  }
  case Stmt::KFor: return false;
  case Stmt::KForIn: return false;
  case Stmt::KSwitch: {
    auto *sw = (SSwitch *)s;
    if (!sw->defaultBody) return false;
    for (auto &c : sw->cases)
      if (!terminates(c.body)) return false;
    return terminates(sw->defaultBody);
  }
  case Stmt::KBlock: {
    for (auto *x : ((SBlock *)s)->stmts)
      if (terminates(x)) return true;
    return false;
  }
  case Stmt::KUnsafe: {
    for (auto *x : ((SUnsafe *)s)->stmts)
      if (terminates(x)) return true;
    return false;
  }
  case Stmt::KExpr: {
    Expr *e = ((SExpr *)s)->e;
    if (e->kind == Expr::Call && e->type && e->type->isNever()) return true;
    return false;
  }
  }
  return false;
}


DFunc *Sema::resolveOverload(const std::vector<DFunc *> &cands, const std::vector<Expr *> &args,
                             SourceLoc loc, const std::string &name, bool &ok) {
  ok = false;
  DFunc *best = nullptr;
  int bestScore = -1;
  bool tie = false;
  for (DFunc *f : cands) {
    if (args.size() > f->params.size() && !f->isVariadic) continue;
    bool arityOk = true;
    for (size_t pi = args.size(); pi < f->params.size(); pi++)
      if (!f->params[pi].defVal) { arityOk = false; break; }
    if (!arityOk) continue;
    if (!f->isVariadic && args.size() != f->params.size()) {
      // allow a fully-defaulted tail (default arguments)
      bool tailDefaults = f->params.size() > args.size();
      for (size_t pi = args.size(); pi < f->params.size(); pi++)
        if (!f->params[pi].defVal) tailDefaults = false;
      if (!tailDefaults) continue;
    }
    int score = 2;
    bool matches = true;
    quiet_++;
    // generic candidate: unify parameter types against the arguments
    std::map<std::string, Type *> vars;
    auto *savedSubstRO = subst;
    if (!f->genericParams.empty()) {
      for (auto &gp : f->genericParams) vars[gp] = tc.genericVar(gp);
      subst = &vars;
    }
    for (size_t ai = 0; ai < args.size() && ai < f->params.size(); ai++) {
      Type *want = resolveType(f->params[ai].type);
      Type *got = args[ai]->type;
      if (!want || !got) { matches = false; break; }
      if (tc.same(want, got)) continue;
      if (!f->genericParams.empty() && want && got && unifyTypes(want, got, vars)) {
        score = std::min(score, 1);
        continue;
      }
      // constant-foldable argument (e.g. `4 * 10`) fits any in-range integer type
      bool foldOk = true;
      unsigned long long fv = evalConstUint(args[ai], foldOk);
      if (foldOk && want && want->isInt() && args[ai]->kind != Expr::IntLit) {
        unsigned bits = primBits(want->prim);
        bool fits = want->prim == PRIM_i128 || want->prim == PRIM_u128 || bits >= 64 ||
                    (fv >> bits) == 0;
        if (primIsSigned(want->prim) && bits < 64) {
          long long sv = (long long)fv;
          fits = sv >= -(1LL << (bits - 1)) && sv <= (1LL << (bits - 1)) - 1;
        }
        if (fits) { score = std::min(score, 1); continue; }
      }
      Expr *lit = stripNeg(args[ai]);
      if (lit && lit->kind == Expr::IntLit && want && want->isInt()) {
        unsigned bits = primBits(want->prim);
        unsigned long long v = ((EInt *)lit)->value;
        bool fits = want->prim == PRIM_i128 || want->prim == PRIM_u128 || bits >= 64 ||
                    (v >> bits) == 0;
        if (primIsSigned(want->prim) && bits < 64) {
          long long sv = (long long)v;
          fits = sv >= -(1LL << (bits - 1)) && sv <= (1LL << (bits - 1)) - 1;
        }
        if (fits) { score = std::min(score, 1); continue; }
      }
      if (lit && lit->kind == Expr::FloatLit && want->isFloat()) { score = std::min(score, 1); continue; }
      if (got->isClass() && want->isClass()) {
        bool up = false;
        for (DClass *b = (DClass *)got->decl; b;) {
          if (b == (DClass *)want->decl) { up = true; break; }
          ClassLayout *bl = layoutOf(b);
          b = bl ? bl->base : nullptr;
        }
        if (up) { score = std::min(score, 1); continue; }
      }
      if (got->isClass() && want->isInterface()) {
        ClassLayout *sl = layoutOf((DClass *)got->decl);
        bool impl = false;
        for (auto &[iff, _] : sl->interfaces)
          if (iff == (DInterface *)want->ifaceDecl) impl = true;
        if (impl) { score = std::min(score, 1); continue; }
      }
      // ptr<[T; N]> -> ptr<T> (array decay)
      if (got->isPtr() && got->pointee->isArray() && want->isPtr() &&
          tc.same(got->pointee->elem, want->pointee)) {
        score = std::min(score, 1);
        continue;
      }
      // FFI: string -> ptr<char>
      if (got->isString() && want->isPtr() && want->pointee->isPrim() &&
          want->pointee->prim == PRIM_char) {
        score = std::min(score, 1);
        continue;
      }
      // ptr<Derived> -> ptr<Base>
      if (got->isPtr() && want->isPtr() && got->pointee->isClass() && want->pointee->isClass()) {
        bool up = false;
        for (DClass *b = (DClass *)got->pointee->decl; b;) {
          if (b == (DClass *)want->pointee->decl) { up = true; break; }
          ClassLayout *bl = layoutOf(b);
          b = bl ? bl->base : nullptr;
        }
        if (up) { score = std::min(score, 1); continue; }
      }
      matches = false;
      break;
    }
    subst = savedSubstRO;
    quiet_--;
    if (!matches) continue;
    if (score > bestScore) { bestScore = score; best = f; tie = false; }
    else if (score == bestScore && best != nullptr) tie = true;
  }
  if (!best) {
    if (quiet_ == 0) {
      std::string argTys;
      for (size_t ai = 0; ai < args.size(); ai++) {
        if (ai) argTys += ", ";
        argTys += args[ai]->type ? typeToString(args[ai]->type) : std::string("?");
      }
      std::string candStr;
      for (size_t ci = 0; ci < cands.size(); ci++) {
        if (ci) candStr += " | ";
        candStr += cands[ci]->name + "(";
        quiet_++;
        for (size_t pi = 0; pi < cands[ci]->params.size(); pi++) {
          if (pi) candStr += ", ";
          Type *pt = resolveType(cands[ci]->params[pi].type);
          candStr += pt ? typeToString(pt) : std::string(cands[ci]->params[pi].type ? "?" : "void");
        }
        quiet_--;
        candStr += ")";
      }
      diag.error(loc, strfmt("no matching function '%s' for arguments (%s)", name.c_str(), argTys.c_str()),
                 cands.empty() ? "" : strfmt("candidates take: %s", candStr.c_str()));
    }
    return nullptr;
  }
  if (tie) {
    if (quiet_ == 0)
      diag.error(loc, strfmt("call to '%s' is ambiguous", name.c_str()),
                 "add explicit casts to clarify argument types");
    return nullptr;
  }
  ok = true;
  return best;
}

} // namespace core
