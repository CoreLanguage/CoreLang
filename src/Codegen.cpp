#include "Codegen.h"

#include <llvm/IR/DIBuilder.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/Support/Alignment.h>
#include <llvm/TargetParser/Host.h>

using namespace llvm;

namespace core {

Codegen::Codegen(Sema &sema, TypeContext &tc, Diagnostics &diag, const CodegenOptions &opts)
    : sema(sema), tc(tc), diag(diag), opts(opts), builder(ctx) {}

// ================================================================ types =====
llvm::StructType *Codegen::stringType() {
  if (stringTy) return stringTy;
  stringTy = StructType::create(ctx, {PointerType::get(ctx, 0), builder.getInt64Ty()},
                                "core.string");
  return stringTy;
}

llvm::Type *Codegen::llvmType(Type *t) {
  if (!t) return builder.getVoidTy();
  switch (t->kind) {
  case TypeKind::Prim:
    switch (t->prim) {
    case PRIM_void: return builder.getVoidTy();
    case PRIM_never: return builder.getVoidTy();
    case PRIM_bool: return builder.getInt1Ty();
    case PRIM_char: return builder.getInt8Ty();
    case PRIM_string: return stringType();
    case PRIM_i8: return builder.getInt8Ty();
    case PRIM_i16: return builder.getInt16Ty();
    case PRIM_i32: return builder.getInt32Ty();
    case PRIM_i64: return builder.getInt64Ty();
    case PRIM_i128: return builder.getInt128Ty();
    case PRIM_u8: return builder.getInt8Ty();
    case PRIM_u16: return builder.getInt16Ty();
    case PRIM_u32: return builder.getInt32Ty();
    case PRIM_u64: return builder.getInt64Ty();
    case PRIM_u128: return builder.getInt128Ty();
    case PRIM_f32: return builder.getFloatTy();
    case PRIM_f64: return builder.getDoubleTy();
    case PRIM_usize: case PRIM_isize:
      return builder.getInt64Ty();
    case PRIM_f32x4: return FixedVectorType::get(builder.getFloatTy(), 4);
    case PRIM_f64x2: return FixedVectorType::get(builder.getDoubleTy(), 2);
    case PRIM_i32x4: return FixedVectorType::get(builder.getInt32Ty(), 4);
    case PRIM_i64x2: return FixedVectorType::get(builder.getInt64Ty(), 2);
    case PRIM_i8x16: return FixedVectorType::get(builder.getInt8Ty(), 16);
    case PRIM_i16x8: return FixedVectorType::get(builder.getInt16Ty(), 8);
    case PRIM_u8x16: return FixedVectorType::get(builder.getInt8Ty(), 16);
    case PRIM_u16x8: return FixedVectorType::get(builder.getInt16Ty(), 8);
    case PRIM_u32x4: return FixedVectorType::get(builder.getInt32Ty(), 4);
    case PRIM_u64x2: return FixedVectorType::get(builder.getInt64Ty(), 2);
    default: return builder.getInt32Ty();
    }
  case TypeKind::Never:
    return builder.getVoidTy();
  case TypeKind::Ptr:
    return PointerType::get(ctx, 0);
  case TypeKind::Array:
    return ArrayType::get(llvmType(t->elem), (unsigned)t->arrayLen);
  case TypeKind::Struct: case TypeKind::Class:
    return structTypeFor(t);
  case TypeKind::Interface: {
    // fat pointer {ptr, itable}
    return StructType::get(PointerType::get(ctx, 0), PointerType::get(ctx, 0));
  }
  case TypeKind::Enum: {
    DEnum *en = (DEnum *)t->decl;
    bool hasPayload = false;
    for (auto &v : en->variants)
      if (!v.payloadTypes.empty()) hasPayload = true;
    if (!hasPayload) return builder.getInt32Ty(); // simple enums are i32
    return enumStorageType(t, nullptr, nullptr, nullptr);
  }
  case TypeKind::Func: {
    // closure pair {fn ptr, env ptr}
    return StructType::get(PointerType::get(ctx, 0), PointerType::get(ctx, 0));
  }
  default:
    return builder.getInt32Ty();
  }
}

llvm::StructType *Codegen::structTypeFor(Type *t) {
  std::string name = std::string("core.") + (t->isClass() ? "class." : "struct.");
  Decl *d = (Decl *)t->decl;
  std::string dname = d->kind == Decl::Class ? ((DClass *)d)->name : ((DStruct *)d)->name;
  name += dname;
  for (auto *a : t->genericArgs) name += "." + sema.mangleTypeForName(a);
  if (auto *st = StructType::getTypeByName(ctx, name)) return st;

  std::vector<llvm::Type *> fields;
  if (t->isClass()) {
    ClassLayout *cl = sema.layoutOf(d);
    if (cl && cl->polymorphic) fields.push_back(PointerType::get(ctx, 0)); // vptr
  }
  auto addFields = [&](const std::vector<Field> &fl) {
    for (auto &f : fl) {
      // resolve field type under generic substitution
      Type *ft = nullptr;
      if (t->isClass()) ft = sema.fieldTypeOf(tc.getClass(d, t->genericArgs), f.name);
      else {
        DStruct *sd = (DStruct *)d;
        if (sd->genericParams.empty() || t->genericArgs.empty()) {
          // resolve directly (non-generic)
          std::map<std::string, Type *> empty;
          auto *saved = sema.subst;
          sema.subst = &empty;
          ft = sema.resolveType(f.type);
          sema.subst = saved;
        } else {
          ft = sema.fieldTypeOf(tc.getStruct(d, t->genericArgs), f.name);
        }
      }
      fields.push_back(ft ? llvmType(ft) : builder.getInt32Ty());
    }
  };
  if (t->isClass()) {
    DClass *c = (DClass *)d;
    ClassLayout *cl = sema.layoutOf(c);
    std::vector<DClass *> chain;
    for (DClass *b = c; b;) {
      chain.push_back(b);
      ClassLayout *bl = sema.layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) addFields((*it)->fields);
  } else {
    addFields(((DStruct *)d)->fields);
  }
  bool packed = d->kind == Decl::Struct ? ((DStruct *)d)->isPacked : false;
  llvm::StructType *st = StructType::create(ctx, fields, name, packed);
  return st;
}

unsigned Codegen::fieldGEPIndex(Type *aggTy, int memberIndex) {
  // sema indexes classes with vptr at 0; LLVM struct field 0 is the vptr too.
  return (unsigned)memberIndex;
}

// Compute the byte extent of one variant's payload area: lays out each payload
// field with proper alignment and returns (payloadAlign, payloadBytes).
static std::pair<unsigned, unsigned> variantExtent(Codegen *cg, llvm::Module *mod, DEnum *e,
                                                   const std::vector<Type *> &genericArgs,
                                                   const std::vector<TypeExpr *> &payloads,
                                                   Sema *sema, TypeContext &tc) {
  unsigned maxAlign = 1, cursor = 0;
  auto *saved = sema->subst;
  std::map<std::string, Type *> sub;
  for (size_t gi = 0; gi < e->genericParams.size() && gi < genericArgs.size(); gi++)
    sub[e->genericParams[gi]] = genericArgs[gi];
  sema->subst = sub.empty() ? nullptr : &sub;
  for (auto *pte : payloads) {
    Type *rt = sema->resolveType(pte);
    if (!rt) continue;
    llvm::Type *lt = cg->llvmTypeFor(rt);
    unsigned al = mod->getDataLayout().getABITypeAlign(lt).value();
    unsigned sz = (unsigned)mod->getDataLayout().getTypeAllocSize(lt);
    maxAlign = std::max(maxAlign, al);
    cursor = (cursor + al - 1) / al * al;
    cursor += sz;
  }
  sema->subst = saved;
  return {maxAlign, cursor};
}

llvm::Type *Codegen::enumStorageType(Type *t, unsigned *payloadOffset, unsigned *totalSize,
                                     unsigned *alignOut) {
  DEnum *e = (DEnum *)t->decl;
  std::string name = "core.enum." + e->name;
  for (auto *a : t->genericArgs) name += "." + sema.mangleTypeForName(a);
  bool hasPayload = false;
  unsigned maxAlign = 1, maxExtent = 0;
  for (auto &v : e->variants) {
    if (v.payloadTypes.empty()) continue;
    hasPayload = true;
    auto [al, ex] = variantExtent(this, mod, e, t->genericArgs, v.payloadTypes, &sema, tc);
    maxAlign = std::max(maxAlign, al);
    maxExtent = std::max(maxExtent, ex);
  }
  unsigned off = 4;
  if (maxAlign > 4) off = (4 + maxAlign - 1) / maxAlign * maxAlign;
  unsigned total = hasPayload ? off + maxExtent : 4;
  if (payloadOffset) *payloadOffset = hasPayload ? off : 0;
  if (totalSize) *totalSize = total;
  if (alignOut) *alignOut = std::max(4u, maxAlign);
  llvm::StructType *st = StructType::getTypeByName(ctx, name);
  if (!st)
    st = StructType::create(ctx, ArrayType::get(builder.getInt8Ty(), total), name);
  return st;
}

// ============================================================ declarations ==
std::string Codegen::funcSymbol(DFunc *f, const std::vector<Type *> &genericArgs) {
  sema.curModule = curModule; // mangling reads the defining module
  return sema.mangleFuncName(f, genericArgs);
}

llvm::FunctionCallee Codegen::rtFunc(const std::string &name, llvm::Type *ret,
                                     std::vector<llvm::Type *> params, bool varargs) {
  FunctionType *ft = FunctionType::get(ret, params, varargs);
  return mod->getOrInsertFunction(name, ft);
}

llvm::Function *Codegen::declareFunc(DFunc *f, const std::vector<Type *> &genericArgs) {
  std::string sym = funcSymbol(f, genericArgs);
  if (auto *existing = mod->getFunction(sym)) return existing;

  // generic instances resolve template types under their substitution
  std::map<std::string, Type *> instanceSubst;
  auto *savedSubst = sema.subst;
  if (!f->genericParams.empty() && !genericArgs.empty()) {
    for (size_t gi = 0; gi < f->genericParams.size() && gi < genericArgs.size(); gi++)
      instanceSubst[f->genericParams[gi]] = genericArgs[gi];
    sema.subst = &instanceSubst;
  }
  std::vector<Type *> paramTypes;
  Type *selfTy = sema.selfTypeOf(f);
  bool hasSelf = selfTy != nullptr && !f->isStatic;
  if (hasSelf) paramTypes.push_back(selfTy);
  for (auto &p : f->params) paramTypes.push_back(sema.resolveType(p.type));

  Type *retTy = f->retType ? sema.resolveType(f->retType) : tc.prim(PRIM_void);
  sema.subst = savedSubst;
  if (retTy && retTy->isNever()) retTy = nullptr; // void in LLVM

  std::vector<llvm::Type *> lparams;
  for (auto *pt : paramTypes) {
    llvm::Type *lt = pt ? llvmType(pt) : nullptr;
    if (!lt) {
      fprintf(stderr, "internal: param of '%s' maps to null LLVM type\n", f->name.c_str());
      lt = builder.getInt32Ty();
    }
    lparams.push_back(lt);
  }
  llvm::Type *lret = retTy ? llvmType(retTy) : builder.getVoidTy();
  bool isEntryMain = f->name == "main" && f->parent == nullptr && !f->isExtern;
  if (isEntryMain && lret->isVoidTy()) lret = builder.getInt32Ty(); // C main ABI
  FunctionType *ft = FunctionType::get(lret, lparams, f->isVariadic);

  llvm::Function *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, sym, mod);
  if (!f->isExtern && !(f->name == "main" && f->parent == nullptr)) {
    // internal-ish: allow DCE; keep external linkage for simplicity of vtables
  }
  return fn;
}

llvm::GlobalVariable *Codegen::globalFor(DGlobal *g) {
  std::string sym = "_CG_" + (curModule ? curModule->name : "") + "_" + g->name;
  for (auto *m : sema.modules) {
    // mangle with defining module
  }
  std::string mangled = "_CG";
  std::string modName = curModule ? curModule->name : "";
  mangled += std::to_string(modName.size()) + modName + std::to_string(g->name.size()) + g->name;
  if (auto *gv = mod->getGlobalVariable(mangled)) return gv;
  Type *t = sema.resolveType(g->type);
  llvm::Type *lt = t ? llvmType(t) : builder.getInt32Ty();
  llvm::Constant *init = Constant::getNullValue(lt);
  if (g->init) {
    init = evalConst(g->init);
    if (!init) {
      diag.error(g->loc, strfmt("global '%s' requires a compile-time constant initializer",
                                g->name.c_str()),
                 "globals are initialized before main; only constants can initialize them", 1);
      init = Constant::getNullValue(lt);
    } else if (g->init->type && (g->init->type->isPtr()) && g->init->kind == Expr::NullLit) {
      init = Constant::getNullValue(lt);
    }
  }
  auto *gv = new GlobalVariable(*mod, lt, false, GlobalValue::ExternalLinkage, init, mangled);
  if (g->isTLS) gv->setThreadLocal(true);
  return gv;
}

llvm::Value *Codegen::vtableFor(DClass *c) {
  ClassLayout *cl = sema.layoutOf(c);
  std::string name = "_CV" + sema.mangleTypeForName(tc.getClass(c, {}));
  if (auto *gv = mod->getGlobalVariable(name)) return gv;
  std::vector<llvm::Constant *> slots;
  for (DFunc *m : cl->vtableOrder) {
    llvm::Function *impl = declareFunc(m, {});
    slots.push_back(impl);
  }
  ArrayType *vt = ArrayType::get(PointerType::get(ctx, 0), slots.size() ? slots.size() : 1);
  Constant *init = ConstantArray::get(vt, slots.empty()
      ? std::vector<Constant *>{ConstantPointerNull::get(PointerType::get(ctx, 0))}
      : slots);
  auto *gv = new GlobalVariable(*mod, vt, true, GlobalValue::LinkOnceODRLinkage, init, name);
  return gv;
}

llvm::Value *Codegen::itableFor(DClass *c, DInterface *iface) {
  std::string name = "_CI" + sema.mangleTypeForName(tc.getClass(c, {})) + "_" +
                     sema.mangleTypeForName(tc.getInterface(iface));
  if (auto *gv = mod->getGlobalVariable(name)) return gv;
  std::vector<llvm::Constant *> slots;
  for (DFunc *proto : iface->methods) {
    // find implementation in class chain
    DFunc *impl = nullptr;
    for (DClass *b = c; b && !impl;) {
      for (DFunc *mth : b->methods)
        if (mth->name == proto->name && !mth->isStatic) { impl = mth; break; }
      ClassLayout *bl = sema.layoutOf(b);
      b = bl ? bl->base : nullptr;
    }
    if (!impl) {
      // trait default body
      impl = proto;
    }
    slots.push_back(declareFunc(impl, {}));
  }
  ArrayType *it = ArrayType::get(PointerType::get(ctx, 0), slots.size() ? slots.size() : 1);
  Constant *init = ConstantArray::get(it, slots.empty()
      ? std::vector<Constant *>{ConstantPointerNull::get(PointerType::get(ctx, 0))}
      : slots);
  auto *gv = new GlobalVariable(*mod, it, true, GlobalValue::LinkOnceODRLinkage, init, name);
  return gv;
}

// ============================================================== constants ===
// ============================================================== constants ===
llvm::Constant *Codegen::evalConst(Expr *e) {
  if (!e) return nullptr;
  switch (e->kind) {
  case Expr::IntLit: {
    auto *ie = (EInt *)e;
    Type *t = e->type ? e->type : tc.prim(PRIM_i32);
    unsigned bits = t->isInt() ? primBits(t->prim) : 32;
    if (t->prim == PRIM_i128 || t->prim == PRIM_u128)
      return ConstantInt::get(ctx, APInt(bits, ie->digits, 10));
    bool isSigned = primIsSigned(t->prim);
    return ConstantInt::get(ctx, APInt(bits, isSigned ? (int64_t)ie->value : ie->value));
  }
  case Expr::FloatLit: {
    Type *t = e->type ? e->type : tc.prim(PRIM_f64);
    return ConstantFP::get(t->prim == PRIM_f32 ? builder.getFloatTy() : builder.getDoubleTy(),
                           ((EFloat *)e)->value);
  }
  case Expr::BoolLit:
    return ConstantInt::get(builder.getInt1Ty(), ((EBool *)e)->value ? 1 : 0);
  case Expr::CharLit:
    return ConstantInt::get(builder.getInt8Ty(), ((EChar *)e)->value);
  case Expr::StringLit: {
    auto *sl = (EString *)e;
    std::string bytes = sl->value;
    ArrayType *at = ArrayType::get(builder.getInt8Ty(), bytes.size() + 1);
    auto *gv = new GlobalVariable(*mod, at, true, GlobalValue::PrivateLinkage,
                                  ConstantDataArray::getString(ctx, bytes, true), ".str");
    llvm::Constant *ptr = ConstantExpr::getBitCast(gv, PointerType::get(ctx, 0));
    StructType *st = stringType();
    return ConstantStruct::get(st, {ptr, ConstantInt::get(builder.getInt64Ty(), bytes.size())});
  }
  case Expr::NullLit: {
    return ConstantPointerNull::get(PointerType::get(ctx, 0));
  }
  case Expr::Ident: {
    auto *id = (EIdent *)e;
    if (id->idKind == IdKind::ConstVal) {
      auto *c = (DConst *)id->target;
      if (c->folded) return (llvm::Constant *)c->folded;
      llvm::Constant *v = evalConst(c->init);
      c->folded = v;
      return v;
    }
    if (id->idKind == IdKind::EnumConst) {
      return ConstantInt::get(builder.getInt32Ty(), id->enumTag);
    }
    return nullptr;
  }
  case Expr::Unary: {
    auto *u = (EUnary *)e;
    if (u->op == "-") {
      if (llvm::Constant *v = evalConst(u->operand)) {
        if (auto *ci = dyn_cast<ConstantInt>(v))
          return ConstantInt::get(ctx, -ci->getValue());
        if (auto *cf = dyn_cast<ConstantFP>(v))
          return ConstantFP::get(cf->getType(), -cf->getValueAPF());
      }
    }
    return nullptr;
  }
  case Expr::Binary: {
    auto *b = (EBinary *)e;
    llvm::Constant *l = evalConst(b->lhs);
    llvm::Constant *r = evalConst(b->rhs);
    if (!l || !r) return nullptr;
    auto *li = dyn_cast<ConstantInt>(l);
    auto *ri = dyn_cast<ConstantInt>(r);
    if (li && ri) {
      uint64_t a = li->getValue().getZExtValue(), c = ri->getValue().getZExtValue();
      const std::string &op = b->op;
      uint64_t v = 0;
      if (op == "+") v = a + c;
      else if (op == "-") v = a - c;
      else if (op == "*") v = a * c;
      else if (op == "/") { if (!c) return nullptr; v = a / c; }
      else if (op == "%") { if (!c) return nullptr; v = a % c; }
      else if (op == "&") v = a & c;
      else if (op == "|") v = a | c;
      else if (op == "^") v = a ^ c;
      else if (op == "<<") v = c < 64 ? a << c : 0;
      else if (op == ">>") v = c < 64 ? a >> c : 0;
      else return nullptr;
      return ConstantInt::get(li->getType(), v);
    }
    return nullptr;
  }
  case Expr::Sizeof: case Expr::Alignof: {
    TypeExpr *te = e->kind == Expr::Sizeof ? ((ESizeof *)e)->ty : ((EAlignof *)e)->ty;
    Type *t = sema.resolveType(te);
    if (!t) return nullptr;
    llvm::Type *lt = llvmType(t);
    unsigned long long v = e->kind == Expr::Sizeof
        ? mod->getDataLayout().getTypeAllocSize(lt)
        : mod->getDataLayout().getABITypeAlign(lt).value();
    return ConstantInt::get(builder.getInt64Ty(), v);
  }
  case Expr::Cast: {
    auto *c = (ECast *)e;
    llvm::Constant *v = evalConst(c->e);
    if (!v) return nullptr;
    Type *dst = e->type;
    if (!dst) return nullptr;
    if (auto *ci = dyn_cast<ConstantInt>(v)) {
      unsigned bits = dst->isInt() ? primBits(dst->prim) : 64;
      return ConstantInt::get(ctx, ci->getValue().zextOrTrunc(bits));
    }
    return nullptr;
  }
  default:
    return nullptr;
  }
}

// ============================================================= generation ===
bool Codegen::generate(llvm::Module &module, ModuleSema *entryModule) {
  mod = &module;
  { // set the data layout up front so sizeof/alignof see target-true values
    std::string tripleStr = opts.targetTriple.empty()
                                ? std::string(llvm::sys::getDefaultTargetTriple())
                                : opts.targetTriple;
    std::string err;
    if (const llvm::Target *t = llvm::TargetRegistry::lookupTarget(tripleStr, err)) {
      llvm::TargetOptions topts;
      std::unique_ptr<llvm::TargetMachine> tm(
          t->createTargetMachine(tripleStr, "generic", "", topts, llvm::Reloc::PIC_));
      module.setTargetTriple(tripleStr);
      module.setDataLayout(tm->createDataLayout());
    }
  }
  mod->setTargetTriple(llvm::Triple::normalize(
      opts.targetTriple.empty() ? std::string(llvm::sys::getDefaultTargetTriple()) : opts.targetTriple));

  // debug info setup
  std::unique_ptr<llvm::DIBuilder> dib;
  if (opts.debugInfo) {
    dib = std::make_unique<llvm::DIBuilder>(*mod);
    dbg = dib.get();
    dbgEnabled = true;
    // one compile unit per source file
    for (size_t fi = 0; fi < sema.diag.sm.fileCount(); fi++) {
      std::string path = sema.diag.sm.fileName((unsigned)fi);
      std::string dir = ".", name = path;
      size_t slash = path.find_last_of('/');
      if (slash != std::string::npos) {
        dir = path.substr(0, slash);
        name = path.substr(slash + 1);
      }
      llvm::DIFile *dif = dbg->createFile(name, dir);
      llvm::DICompileUnit *cu = dbg->createCompileUnit(llvm::dwarf::DW_LANG_C, dif, "core", false, "", 0);
      debugCUs()[(unsigned)fi] = cu;
    }
  }

  // 1. declare everything: globals, functions, methods (prelude first)
  auto declareDecl = [&](Decl *d) {
    if (d->kind == Decl::Global) globalFor((DGlobal *)d);
    else if (d->kind == Decl::Func) {
      auto *f = (DFunc *)d;
      if (f->genericParams.empty()) declareFunc(f, {});
    } else if (d->kind == Decl::Extern) {
      declareFunc(((DExtern *)d)->proto, {});
    } else if (d->kind == Decl::Class) {
      for (DFunc *mth : ((DClass *)d)->methods)
        if (mth->genericParams.empty()) declareFunc(mth, {});
    } else if (d->kind == Decl::Struct) {
      for (DFunc *mth : ((DStruct *)d)->methods)
        if (mth->genericParams.empty()) declareFunc(mth, {});
    } else if (d->kind == Decl::Interface || d->kind == Decl::Trait) {
      for (DFunc *mth : ((DInterface *)d)->methods)
        if (mth->genericParams.empty() && mth->body) declareFunc(mth, {}); // trait defaults
    }
  };
  for (auto *m : sema.modules) {
    curModule = m;
    for (Decl *d : m->unit->decls) declareDecl(d);
  }
  // generic instances (instantiation may create more while emitting)
  for (size_t i = 0; i < sema.instanceOrder.size(); i++) {
    GenericInstance *gi = sema.instanceOrder[i];
    declareFunc(gi->clonedFunc, gi->args);
  }

  // 2. emit function bodies
  auto emitAll = [&]() {
    for (auto *m : sema.modules) {
      curModule = m;
      for (Decl *d : m->unit->decls) {
        if (d->kind == Decl::Func) {
          auto *f = (DFunc *)d;
          if (f->genericParams.empty()) emitFuncBody(f, {});
        } else if (d->kind == Decl::Class) {
          for (DFunc *mth : ((DClass *)d)->methods)
            if (mth->genericParams.empty()) emitFuncBody(mth, {});
        } else if (d->kind == Decl::Struct) {
          for (DFunc *mth : ((DStruct *)d)->methods)
            if (mth->genericParams.empty()) emitFuncBody(mth, {});
        } else if (d->kind == Decl::Interface || d->kind == Decl::Trait) {
          for (DFunc *mth : ((DInterface *)d)->methods)
            if (mth->genericParams.empty() && mth->body) emitFuncBody(mth, {});
        }
      }
    }
    for (auto *gi : sema.instanceOrder) emitFuncBody(gi->clonedFunc, gi->args);
  };
  emitAll();

  // 3. debug info finalize
  if (dib) {
    dib->finalize();
    dbg = nullptr;
    dbgEnabled = false;
  }
  return true;
}

void Codegen::emitDebugLoc(SourceLoc loc) {
  if (dbgEnabled && loc.valid && dbg)
    builder.SetCurrentDebugLocation(
        llvm::DebugLoc(llvm::DILocation::get(ctx, loc.line, loc.col, curDebugScope())));
}

llvm::DIScope *Codegen::curDebugScope() {
  if (curSP) return curSP;
  return nullptr;
}

void Codegen::emitFuncBody(DFunc *f, const std::vector<Type *> &genericArgs) {
  if (f->isExtern || !f->body) return;
  std::string sym = funcSymbol(f, genericArgs);
  llvm::Function *fnp = mod->getFunction(sym);
  if (!fnp) fnp = declareFunc(f, genericArgs);
  if (!fnp->empty()) return; // already emitted
  fn = fnp;                  // member: used by all statement emission

  // generic instances resolve template types under their substitution
  std::map<std::string, Type *> instanceSubst;
  auto *savedSemaSubst = sema.subst;
  if (!f->genericParams.empty() && !genericArgs.empty()) {
    for (size_t gi = 0; gi < f->genericParams.size() && gi < genericArgs.size(); gi++)
      instanceSubst[f->genericParams[gi]] = genericArgs[gi];
    sema.subst = &instanceSubst;
  }

  DFunc *savedDecl = curFuncDecl;
  ModuleSema *savedModule = curModule;
  curFuncDecl = f;
  // defining module of the parent type
  if (f->parent) {
    for (auto *m : sema.modules)
      for (auto &[tn, td] : m->types)
        if (td == f->parent) curModule = m;
  }

  // debug info: subprogram (function-level + line tables; variables come later)
  llvm::DISubprogram *savedSP = curSP;
  if (dbgEnabled) {
    unsigned fid = f->loc.valid ? f->loc.file : 0;
    auto &cuMap = debugCUs();
    llvm::DICompileUnit *cu = nullptr;
    if (cuMap.count(fid)) cu = (llvm::DICompileUnit *)cuMap[fid];
    if (!cu) cu = (llvm::DICompileUnit *)cuMap[0];
    llvm::DISubroutineType *fty = dbg->createSubroutineType(dbg->getOrCreateTypeArray({}));
    curSP = dbg->createFunction(cu, f->name, sym, cu->getFile(),
                                (unsigned)(f->loc.line ? f->loc.line : 1), fty,
                                0, llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
    fnp->setSubprogram(curSP);
  }

  llvm::BasicBlock *entry = llvm::BasicBlock::Create(ctx, "entry", fnp);
  builder.SetCurrentDebugLocation(llvm::DebugLoc()); // clear stale locations
  builder.SetInsertPoint(entry);

  // bind params to allocas
  localSlots.clear();
  {
    unsigned argIdx = 0;
    bool hasSelf = sema.selfTypeOf(f) != nullptr && !f->isStatic;
    if (hasSelf) {
      llvm::Argument &selfArg = *fnp->args().begin();
      Type *selfTy = sema.selfTypeOf(f);
      auto *slot = builder.CreateAlloca(llvmType(selfTy), nullptr, "self");
      builder.CreateStore(&selfArg, slot);
      localSlots["self"] = slot;
      argIdx++;
    }
    for (auto &p : f->params) {
      llvm::Argument &arg = *std::next(fnp->args().begin(), argIdx);
      arg.setName(p.name);
      auto *slot = builder.CreateAlloca(arg.getType(), nullptr, p.name + ".addr");
      builder.CreateStore(&arg, slot);
      localSlots[p.name] = slot;
      argIdx++;
    }
  }

  emitBlock(f->body);

  // epilogue: default return value if the block can fall through
  Type *retTy = f->retType ? sema.resolveType(f->retType) : tc.prim(PRIM_void);
  sema.subst = savedSemaSubst;
  bool returnsNothing = !retTy || retTy->isVoid() || retTy->isNever();
  bool isEntryMain = f->name == "main" && f->parent == nullptr && !f->isExtern;
  if (returnsNothing) {
    if (isEntryMain) builder.CreateRet(ConstantInt::get(builder.getInt32Ty(), 0));
    else { emitVptrStoreIfInit(f); builder.CreateRetVoid(); }
  } else {
    emitVptrStoreIfInit(f);
    llvm::Value *dv = emitDefaultValue(retTy);
    if (!dv) {
      fprintf(stderr, "internal: default value is null for type '%s' in '%s'\n",
              typeToString(retTy).c_str(), f->name.c_str());
      dv = Constant::getNullValue(builder.getInt32Ty());
    }
    builder.CreateRet(dv);
  }
  fn = nullptr;

  curSP = savedSP;
  curFuncDecl = savedDecl;
  curModule = savedModule;
}

// Constructors store their own class's vtable on every exit; because a
// derived init calls the base init and runs LAST, the most-derived vtable
// wins (documented in docs/language/classes.md).
void Codegen::emitVptrStoreIfInit(DFunc *f) {
  if (f->name != "init" || !f->parent || f->parent->kind != Decl::Class || f->isStatic) return;
  DClass *c = (DClass *)f->parent;
  ClassLayout *cl = sema.layoutOf(c);
  if (!cl || !cl->polymorphic) return;
  auto it = localSlots.find("self");
  if (it == localSlots.end()) return;
  llvm::Value *selfPtr = builder.CreateLoad(PointerType::get(ctx, 0), it->second);
  builder.CreateStore(vtableFor(c), selfPtr);
}

llvm::Value *Codegen::emitDefaultValue(Type *t) {
  llvm::Type *lt = llvmType(t);
  if (t->isString()) {
    StructType *st = stringType();
    return ConstantStruct::get(st, {ConstantPointerNull::get(PointerType::get(ctx, 0)),
                                    ConstantInt::get(builder.getInt64Ty(), 0)});
  }
  if (t->isInterface() || t->isFunc()) {
    return Constant::getNullValue(lt);
  }
  if (t->isEnum()) {
    // variant 0
    unsigned off, total, align;
    llvm::Type *st = enumStorageType(t, &off, &total, &align);
    if (auto *sty = dyn_cast<StructType>(st)) {
      if (sty->getNumElements() == 1 && isa<ArrayType>(sty->getElementType(0))) {
        auto *at = cast<ArrayType>(sty->getElementType(0));
        return ConstantAggregateZero::get(at);
      }
    }
    return ConstantInt::get(builder.getInt32Ty(), 0);
  }
  return Constant::getNullValue(lt);
}

// ============================================================= expressions ==
llvm::Value *Codegen::emitLValue(Expr *e) {
  switch (e->kind) {
  case Expr::Ident: {
    auto *id = (EIdent *)e;
    if (id->idKind == IdKind::Local) {
      auto it = localSlots.find(id->name);
      if (it != localSlots.end()) return it->second;
      // lambda capture: reads come from env; loads handled in emitExpr
      return nullptr;
    }
    if (id->idKind == IdKind::Global) {
      return globalFor((DGlobal *)id->target);
    }
    return nullptr;
  }
  case Expr::Self: {
    auto it = localSlots.find("self");
    return it != localSlots.end() ? it->second : nullptr;
  }
  case Expr::Unary: {
    auto *u = (EUnary *)e;
    if (u->op == "*") return emitExpr(u->operand); // deref: address is the value
    return nullptr;
  }
  case Expr::Member: {
    auto *m = (EMember *)e;
    if (m->memberKind != MemberKind::Field) return nullptr;
    if (m->obj->type && m->obj->type->isPtr()) {
      // auto-deref through the pointer, then GEP to the field
      llvm::Value *objPtr = emitExpr(m->obj);
      unsigned idx = fieldGEPIndex(m->obj->type->pointee, m->memberIndex);
      return builder.CreateStructGEP(structTypeFor(m->obj->type->pointee), objPtr, idx);
    }
    llvm::Value *objAddr = emitLValue(m->obj);
    if (!objAddr) return nullptr;
    Type *objTy = m->obj->type;
    unsigned idx = fieldGEPIndex(objTy, m->memberIndex);
    return builder.CreateStructGEP(structTypeFor(objTy), objAddr, idx);
  }
  case Expr::Index: {
    auto *ix = (EIndex *)e;
    Type *bt = ix->base->type;
    if (bt->isArray()) {
      llvm::Value *base = emitLValue(ix->base);
      if (ix->checkBounds) {
        long long len = bt->arrayLen;
        emitBoundsCheck(emitExpr(ix->index), len, e->loc);
      }
      llvm::Value *idx = emitExpr(ix->index);
      return builder.CreateGEP(cast<llvm::ArrayType>(llvmType(bt)), base,
                               {ConstantInt::get(builder.getInt64Ty(), 0), idx}, "elemptr");
    }
    if (bt->isString()) {
      llvm::Value *sv = emitExpr(ix->base);
      llvm::Value *dataPtr = builder.CreateExtractValue(sv, {0});
      llvm::Value *slen = builder.CreateExtractValue(sv, {1});
      llvm::Value *idx = emitExpr(ix->index);
      if (ix->checkBounds) {
        // dynamic bounds check against the string length
        llvm::Function *f = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *okBB = llvm::BasicBlock::Create(ctx, "idx.ok", f);
        llvm::BasicBlock *failBB = llvm::BasicBlock::Create(ctx, "idx.fail", f);
        llvm::Value *cond = builder.CreateICmpULT(
            builder.CreateZExtOrBitCast(idx, builder.getInt64Ty()), slen);
        builder.CreateCondBr(cond, okBB, failBB);
        builder.SetInsertPoint(failBB);
        std::string file = diag.sm.fileName(e->loc.file);
        builder.CreateCall(
            rtFunc("core_rt_index_failed", builder.getVoidTy(),
                   {stringType(), builder.getInt64Ty(), builder.getInt64Ty(), builder.getInt64Ty()}),
            {makeStringConst(file), ConstantInt::get(builder.getInt64Ty(), e->loc.line),
             builder.CreateSExt(idx, builder.getInt64Ty()), slen});
        builder.CreateUnreachable();
        builder.SetInsertPoint(okBB);
      }
      return builder.CreateGEP(builder.getInt8Ty(), dataPtr, idx, "strchar");
    }
    if (bt->isPtr()) {
      // p[i] == *(p + i)
      llvm::Value *base = emitExpr(ix->base);
      llvm::Value *idx = emitExpr(ix->index);
      return builder.CreateGEP(llvmType(bt->pointee), base, idx, "ptridx");
    }
    return nullptr;
  }
  default:
    return nullptr;
  }
}

void Codegen::emitBoundsCheck(llvm::Value *idx, long long len, SourceLoc loc) {
  using namespace std::string_literals;
  llvm::Function *f = builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *okBB = llvm::BasicBlock::Create(ctx, "idx.ok", f);
  llvm::BasicBlock *failBB = llvm::BasicBlock::Create(ctx, "idx.fail", f);
  llvm::Value *cond;
  if (len >= 0) {
    cond = builder.CreateICmpULT(idx, ConstantInt::get(cast<IntegerType>(idx->getType()), len));
  } else {
    // string: dynamic length
    // handled by caller (passing length); unreachable here
    cond = ConstantInt::get(builder.getInt1Ty(), 1);
  }
  builder.CreateCondBr(cond, okBB, failBB);
  builder.SetInsertPoint(failBB);
  // panic: index out of bounds
  std::string file = diag.sm.fileName(loc.file);
  builder.CreateCall(rtFunc("core_rt_index_failed", builder.getVoidTy(),
                            {stringType(), builder.getInt64Ty(), builder.getInt64Ty(), builder.getInt64Ty()}),
                     {makeStringConst(file), ConstantInt::get(builder.getInt64Ty(), loc.line),
                      builder.CreateSExt(idx, builder.getInt64Ty()),
                      ConstantInt::get(builder.getInt64Ty(), len >= 0 ? len : 0)});
  builder.CreateUnreachable();
  builder.SetInsertPoint(okBB);
}

llvm::CallInst *Codegen::ccall(llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value *> args,
                               const std::string &name) {
  llvm::FunctionType *fty = callee.getFunctionType();
  if (fty->getReturnType()->isVoidTy())
    return builder.CreateCall(callee, args);
  return builder.CreateCall(callee, args, name);
}

llvm::CallInst *Codegen::ccall(llvm::FunctionType *fty, llvm::Value *callee,
                               llvm::ArrayRef<llvm::Value *> args, const std::string &name) {
  if (fty->getReturnType()->isVoidTy())
    return builder.CreateCall(fty, callee, args);
  return builder.CreateCall(fty, callee, args, name);
}

llvm::Value *Codegen::emitExpr(Expr *e) {
  if (!e) return nullptr;
  switch (e->kind) {
  case Expr::IntLit: case Expr::FloatLit: case Expr::BoolLit: case Expr::CharLit:
  case Expr::StringLit: case Expr::NullLit: {
    if (llvm::Constant *c = evalConst(e)) return c;
    return Constant::getNullValue(llvmType(e->type));
  }
  case Expr::Ident: {
    auto *id = (EIdent *)e;
    if (id->idKind == IdKind::Local) {
      auto it = localSlots.find(id->name);
      if (it != localSlots.end()) {
        // loads of aggregates copy the value; scalars load directly
        return builder.CreateLoad(llvmType(id->type), it->second, id->name);
      }
      return Constant::getNullValue(llvmType(id->type));
    }
    if (id->idKind == IdKind::Global) {
      auto *gv = globalFor((DGlobal *)id->target);
      return builder.CreateLoad(gv->getValueType(), gv, id->name);
    }
    if (id->idKind == IdKind::ConstVal) {
      auto *c = (DConst *)id->target;
      if (c && c->folded) return (llvm::Constant *)c->folded;
      return evalConst(c->init);
    }
    if (id->idKind == IdKind::EnumConst) {
      return ConstantInt::get(builder.getInt32Ty(), id->enumTag);
    }
    if (id->idKind == IdKind::Func) {
      // function referenced as value: wrap in a closure trampoline
      DFunc *f = (DFunc *)id->resolvedFunc;
      return makeClosureForFunction(f);
    }
    return Constant::getNullValue(llvmType(e->type));
  }
  case Expr::Self: {
    auto it = localSlots.find("self");
    if (it != localSlots.end())
      return builder.CreateLoad(llvmType(e->type), it->second, "self");
    return Constant::getNullValue(llvmType(e->type));
  }
  case Expr::Unary: {
    auto *u = (EUnary *)e;
    UnKind k = (UnKind)u->unKind;
    if (k == UnKind::Deref) {
      llvm::Value *ptr = emitExpr(u->operand);
      Type *pt = u->operand->type->pointee;
      return builder.CreateLoad(llvmType(pt), ptr, "deref");
    }
    if (k == UnKind::Ref) {
      // &funcname handled in Ident; here: lvalue address
      if (u->operand->kind == Expr::Ident && ((EIdent *)u->operand)->idKind == IdKind::Func) {
        return emitExpr(u->operand); // closure value
      }
      return emitLValue(u->operand);
    }
    llvm::Value *v = emitExpr(u->operand);
    if (k == UnKind::Neg) {
      if (u->operand->type->isFloat()) return builder.CreateFNeg(v, "neg");
      return builder.CreateNeg(v, "neg");
    }
    if (k == UnKind::Not) {
      return builder.CreateNot(v, "not");
    }
    if (k == UnKind::BitNot) {
      return builder.CreateNot(v, "bnot");
    }
    return v;
  }
  case Expr::Binary:
    return emitBinary((EBinary *)e);
  case Expr::Assign: {
    auto *a = (EAssign *)e;
    llvm::Value *addr = emitLValue(a->target);
    llvm::Value *val = emitExpr(a->value);
    if (!addr) return val;
    llvm::Type *lt = llvmType(a->target->type);
    if (a->op.empty() || a->op == "=") {
      builder.CreateStore(val, addr);
      return val;
    }
    // compound
    llvm::Value *cur = builder.CreateLoad(lt, addr);
    std::string base = a->op.substr(0, a->op.size() - 1);
    llvm::Value *result = nullptr;
    if (a->target->type->isPtr() && base == "+") {
      result = builder.CreateGEP(llvmType(a->target->type->pointee), cur, val);
    } else if (a->target->type->isFloat()) {
      if (base == "+") result = builder.CreateFAdd(cur, val);
      else if (base == "-") result = builder.CreateFSub(cur, val);
      else if (base == "*") result = builder.CreateFMul(cur, val);
      else if (base == "/") result = builder.CreateFDiv(cur, val);
    } else if (base == "<<") result = builder.CreateShl(cur, val);
    else if (base == ">>") result = builder.CreateLShr(cur, val);
    else if (base == "+") result = builder.CreateAdd(cur, val);
    else if (base == "-") result = builder.CreateSub(cur, val);
    else if (base == "*") result = builder.CreateMul(cur, val);
    else if (base == "/") result = a->target->type->isInt() && primIsSigned(a->target->type->prim)
                                       ? builder.CreateSDiv(cur, val)
                                       : builder.CreateUDiv(cur, val);
    else if (base == "%") result = a->target->type->isInt() && primIsSigned(a->target->type->prim)
                                       ? builder.CreateSRem(cur, val)
                                       : builder.CreateURem(cur, val);
    else if (base == "&") result = builder.CreateAnd(cur, val);
    else if (base == "|") result = builder.CreateOr(cur, val);
    else if (base == "^") result = builder.CreateXor(cur, val);
    if (result) builder.CreateStore(result, addr);
    return result ? result : val;
  }
  case Expr::Cast:
    return emitCast((ECast *)e);
  case Expr::Call:
    return emitCall((ECall *)e);
  case Expr::Member: {
    auto *m = (EMember *)e;
    if (m->memberKind == MemberKind::VariantOf) {
      // unit variant value: tag constant (payload-less enums are i32)
      DEnum *en = (DEnum *)m->target;
      Type *et = e->type && e->type->isEnum() ? e->type : tc.getEnum(en, {});
      if (et && enumHasPayloads(en)) {
        return emitVariantValue(en, (unsigned)m->memberIndex, et);
      }
      return ConstantInt::get(builder.getInt32Ty(), m->memberIndex);
    }
    llvm::Value *addr = emitLValue(e);
    if (addr) {
      Type *t = e->type;
      if (m->obj->type && m->obj->type->isPtr()) {
        // through-pointer field: address is the object pointer
        llvm::Value *objPtr = emitExpr(m->obj);
        unsigned idx = fieldGEPIndex(m->obj->type->pointee, m->memberIndex);
        llvm::Value *fp = builder.CreateStructGEP(structTypeFor(m->obj->type->pointee), objPtr, idx);
        return builder.CreateLoad(llvmType(t), fp, "field");
      }
      return builder.CreateLoad(llvmType(t), addr, "field");
    }
    if (m->memberKind == MemberKind::ModuleMember && m->target &&
        ((Decl *)m->target)->kind == Decl::Global) {
      auto *gv = globalFor((DGlobal *)m->target);
      return builder.CreateLoad(gv->getValueType(), gv);
    }
    return Constant::getNullValue(llvmType(e->type));
  }
  case Expr::Index: {
    llvm::Value *addr = emitLValue(e);
    if (!addr) return Constant::getNullValue(llvmType(e->type));
    return builder.CreateLoad(llvmType(e->type), addr, "elem");
  }
  case Expr::StructLit:
    return emitStructLit((EStructLit *)e);
  case Expr::ArrayLit:
    return emitArrayLit((EArrayLit *)e);
  case Expr::Lambda:
    return emitLambda((ELambda *)e);
  case Expr::Match:
    return emitMatch((EMatch *)e);
  case Expr::UnsafeExpr: {
    auto *ue = (EUnsafeExpr *)e;
    llvm::Value *last = nullptr;
    for (size_t si = 0; si < ue->stmts.size(); si++) {
      if (si + 1 == ue->stmts.size() && ue->stmts[si]->kind == Stmt::KExpr) {
        last = emitExpr(((SExpr *)ue->stmts[si])->e);
        continue;
      }
      emitStmt(ue->stmts[si]);
    }
    return last ? last : Constant::getNullValue(builder.getInt32Ty());
  }
  case Expr::Sizeof: case Expr::Alignof: {
    if (llvm::Constant *c = evalConst(e)) return c;
    return Constant::getNullValue(builder.getInt64Ty());
  }
  default:
    return Constant::getNullValue(llvmType(e->type ? e->type : tc.prim(PRIM_i32)));
  }
}

// Pull the byte content out of a constant Core string (struct {ptr, len}).
static std::string stringConstantBytes(llvm::Constant *c) {
  if (!c) return "";
  if (auto *cs = dyn_cast<llvm::ConstantStruct>(c)) {
    llvm::Constant *p = cs->getOperand(0);
    if (auto *ce = dyn_cast<llvm::ConstantExpr>(p)) {
      if (ce->getOpcode() == llvm::Instruction::BitCast)
        p = ce->getOperand(0);
    }
    if (auto *g = dyn_cast<llvm::GlobalVariable>(p)) {
      if (auto *cda = dyn_cast<llvm::ConstantDataArray>(g->getInitializer()))
        return cda->getAsString().str();
    }
  }
  return "";
}

bool Codegen::enumHasPayloads(DEnum *e) {
  for (auto &v : e->variants)
    if (!v.payloadTypes.empty()) return true;
  return false;
}

// Build an enum value with the given tag (payload zeroed).
llvm::Value *Codegen::emitVariantValue(DEnum *en, unsigned tag, Type *enumTy) {
  unsigned off, total, align;
  llvm::Type *storage = enumStorageType(enumTy, &off, &total, &align);
  llvm::Value *slot = builder.CreateAlloca(storage, nullptr, "variant");
  builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), tag),
                      builder.CreateBitCast(slot, PointerType::get(ctx, 0)));
  return builder.CreateLoad(storage, slot);
}

// ------------------------------------------------------------- closures -----
// Every function VALUE uses the uniform closure ABI: {fn ptr, env ptr}.
// The callee's first parameter is the env pointer (ignored by plain
// functions, holding captured variables for lambdas).
llvm::Value *Codegen::makeClosureForFunction(DFunc *f) {
  llvm::Function *impl = declareFunc(f, {});
  std::string tname = impl->getName().str() + ".closure";
  if (llvm::Function *tf = mod->getFunction(tname))
    return makeClosureValue(tf, ConstantPointerNull::get(PointerType::get(ctx, 0)));

  std::vector<llvm::Type *> ps = impl->getFunctionType()->params();
  llvm::FunctionType *ft = llvm::FunctionType::get(impl->getReturnType(), ps, false);
  llvm::Function *tramp = llvm::Function::Create(ft, llvm::Function::InternalLinkage, tname, mod);
  llvm::BasicBlock *saved = builder.GetInsertBlock();
  llvm::BasicBlock *bb = llvm::BasicBlock::Create(ctx, "entry", tramp);
  builder.SetInsertPoint(bb);
  std::vector<llvm::Value *> args;
  for (size_t i = 1; i < tramp->arg_size(); i++) args.push_back(tramp->getArg(i));
  llvm::Value *r = builder.CreateCall(impl, args);
  if (impl->getReturnType()->isVoidTy()) builder.CreateRetVoid();
  else builder.CreateRet(r);
  if (saved) builder.SetInsertPoint(saved);
  return makeClosureValue(tramp, ConstantPointerNull::get(PointerType::get(ctx, 0)));
}

llvm::Value *Codegen::makeClosureValue(llvm::Function *fnPtr, llvm::Value *env) {
  llvm::Value *v = llvm::UndefValue::get(llvmType(tc.func(nullptr, {})));
  v = builder.CreateInsertValue(v, fnPtr, 0);
  v = builder.CreateInsertValue(v, env, 1);
  return v;
}

// --------------------------------------------------------------- binary -----
llvm::Value *Codegen::emitBinary(EBinary *b) {
  BinKind kind = (BinKind)b->binKind;
  const std::string &op = b->op;
  if (kind == BinKind::ShortCircuit) {
    bool isAnd = op == "&&" || op == "and";
    llvm::Function *f = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *rhsBB = llvm::BasicBlock::Create(ctx, isAnd ? "and.rhs" : "or.rhs", f);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, isAnd ? "and.end" : "or.end", f);
    llvm::Value *l = emitExpr(b->lhs);
    llvm::BasicBlock *lhsEnd = builder.GetInsertBlock();
    builder.CreateCondBr(l, rhsBB, endBB);
    builder.SetInsertPoint(rhsBB);
    llvm::Value *r = emitExpr(b->rhs);
    llvm::BasicBlock *rhsEnd = builder.GetInsertBlock();
    builder.CreateBr(endBB);
    builder.SetInsertPoint(endBB);
    llvm::PHINode *phi = builder.CreatePHI(builder.getInt1Ty(), 2);
    phi->addIncoming(isAnd ? ConstantInt::getFalse(ctx) : r, lhsEnd);
    phi->addIncoming(r, rhsEnd);
    return phi;
  }

  if (kind == BinKind::StrConcat) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    return builder.CreateCall(
        rtFunc("core_rt_str_concat", stringType(), {stringType(), stringType()}), {l, r});
  }
  if (kind == BinKind::StrCmp) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    llvm::Value *c = builder.CreateCall(
        rtFunc("core_rt_str_cmp", builder.getInt32Ty(), {stringType(), stringType()}), {l, r});
    llvm::Value *eq = nullptr;
    if (op == "==") {
      return builder.CreateCall(
          rtFunc("core_rt_str_eq", builder.getInt1Ty(), {stringType(), stringType()}), {l, r});
    }
    if (op == "!=") {
      eq = builder.CreateCall(
          rtFunc("core_rt_str_eq", builder.getInt1Ty(), {stringType(), stringType()}), {l, r});
      return builder.CreateNot(eq);
    }
    if (op == "<") return builder.CreateICmpSLT(c, ConstantInt::get(builder.getInt32Ty(), 0));
    if (op == "<=") return builder.CreateICmpSLE(c, ConstantInt::get(builder.getInt32Ty(), 0));
    if (op == ">") return builder.CreateICmpSGT(c, ConstantInt::get(builder.getInt32Ty(), 0));
    if (op == ">=") return builder.CreateICmpSGE(c, ConstantInt::get(builder.getInt32Ty(), 0));
  }
  if (kind == BinKind::PtrArith) {
    bool lIsPtr = b->lhs->type->isPtr();
    llvm::Value *p = emitExpr(lIsPtr ? b->lhs : b->rhs);
    llvm::Value *i = emitExpr(lIsPtr ? b->rhs : b->lhs);
    Type *ptee = (lIsPtr ? b->lhs : b->rhs)->type->pointee;
    return builder.CreateGEP(llvmType(ptee), p, i, "ptr.arith");
  }
  if (kind == BinKind::PtrDiff) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    llvm::Value *li = builder.CreatePtrToInt(l, builder.getInt64Ty());
    llvm::Value *ri = builder.CreatePtrToInt(r, builder.getInt64Ty());
    llvm::Value *diff = builder.CreateSub(li, ri);
    unsigned sz = (unsigned)mod->getDataLayout().getTypeAllocSize(
        llvmType(b->lhs->type->pointee));
    return builder.CreateSDiv(diff, ConstantInt::get(builder.getInt64Ty(), sz), "ptr.diff");
  }
  if (kind == BinKind::PtrCmp) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    if (op == "==") return builder.CreateICmpEQ(l, r);
    if (op == "!=") return builder.CreateICmpNE(l, r);
    if (op == "<") return builder.CreateICmpULT(l, r);
    if (op == "<=") return builder.CreateICmpULE(l, r);
    if (op == ">") return builder.CreateICmpUGT(l, r);
    if (op == ">=") return builder.CreateICmpUGE(l, r);
  }
  if (kind == BinKind::EnumCmp) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    return op == "==" ? builder.CreateICmpEQ(l, r) : builder.CreateICmpNE(l, r);
  }
  if (kind == BinKind::VecArith) {
    llvm::Value *l = emitExpr(b->lhs);
    llvm::Value *r = emitExpr(b->rhs);
    bool isFloat = b->lhs->type->isFloat();
    if (op == "+") return isFloat ? builder.CreateFAdd(l, r) : builder.CreateAdd(l, r);
    if (op == "-") return isFloat ? builder.CreateFSub(l, r) : builder.CreateSub(l, r);
    if (op == "*") return isFloat ? builder.CreateFMul(l, r) : builder.CreateMul(l, r);
    if (op == "/") return isFloat ? builder.CreateFDiv(l, r) : builder.CreateUDiv(l, r);
  }

  llvm::Value *l = emitExpr(b->lhs);
  llvm::Value *r = emitExpr(b->rhs);
  Type *lt = b->lhs->type;
  bool isFloat = lt && lt->isFloat();
  bool isSigned = lt && lt->isInt() && primIsSigned(lt->prim);

  if (kind == BinKind::Cmp) {
    if (isFloat) {
      if (op == "==") return builder.CreateFCmpOEQ(l, r);
      if (op == "!=") return builder.CreateFCmpUNE(l, r);
      if (op == "<") return builder.CreateFCmpOLT(l, r);
      if (op == "<=") return builder.CreateFCmpOLE(l, r);
      if (op == ">") return builder.CreateFCmpOGT(l, r);
      if (op == ">=") return builder.CreateFCmpOGE(l, r);
    }
    if (op == "==") return isSigned ? builder.CreateICmpEQ(l, r) : builder.CreateICmpEQ(l, r);
    if (op == "!=") return builder.CreateICmpNE(l, r);
    if (op == "<") return isSigned ? builder.CreateICmpSLT(l, r) : builder.CreateICmpULT(l, r);
    if (op == "<=") return isSigned ? builder.CreateICmpSLE(l, r) : builder.CreateICmpULE(l, r);
    if (op == ">") return isSigned ? builder.CreateICmpSGT(l, r) : builder.CreateICmpUGT(l, r);
    if (op == ">=") return isSigned ? builder.CreateICmpSGE(l, r) : builder.CreateICmpUGE(l, r);
  }
  if (kind == BinKind::Arith) {
    if (isFloat) {
      if (op == "+") return builder.CreateFAdd(l, r);
      if (op == "-") return builder.CreateFSub(l, r);
      if (op == "*") return builder.CreateFMul(l, r);
      if (op == "/") return builder.CreateFDiv(l, r);
    }
    if (op == "+") return builder.CreateAdd(l, r);
    if (op == "-") return builder.CreateSub(l, r);
    if (op == "*") return builder.CreateMul(l, r);
    if (op == "/") return isSigned ? builder.CreateSDiv(l, r) : builder.CreateUDiv(l, r);
    if (op == "%") return isSigned ? builder.CreateSRem(l, r) : builder.CreateURem(l, r);
    if (op == "&") return builder.CreateAnd(l, r);
    if (op == "|") return builder.CreateOr(l, r);
    if (op == "^") return builder.CreateXor(l, r);
  }
  if (kind == BinKind::Shift) {
    // shift amount type may differ from value type
    if (r->getType() != l->getType())
      r = builder.CreateIntCast(r, l->getType(), false);
    if (op == "<<") return builder.CreateShl(l, r);
    if (op == ">>") return (lt && primIsSigned(lt->prim)) ? builder.CreateAShr(l, r)
                                                          : builder.CreateLShr(l, r);
  }
  return Constant::getNullValue(llvmType(b->type));
}

// ----------------------------------------------------------------- calls ----
llvm::Value *Codegen::emitCall(ECall *c) {
  emitDebugLoc(c->loc);
  Expr *callee = c->callee;

  // builtin?
  if (callee->kind == Expr::Ident && ((EIdent *)callee)->idKind == IdKind::Builtin) {
    return emitBuiltinCall(c, (Builtin)((EIdent *)callee)->builtin,
                           ((EIdent *)callee)->name);
  }

  // variant constructor?
  if (callee->kind == Expr::Member && ((EMember *)callee)->memberKind == MemberKind::VariantOf) {
    return emitVariantCtor(c);
  }
  if (callee->kind == Expr::Ident && ((EIdent *)callee)->idKind == IdKind::EnumConst) {
    return emitVariantCtor(c);
  }

  // value callee: closure call
  if (callee->kind != Expr::Member && callee->kind != Expr::Ident) {
    llvm::Value *closure = emitExpr(callee);
    llvm::Value *fnPtr = builder.CreateExtractValue(closure, {0});
    llvm::Value *env = builder.CreateExtractValue(closure, {1});
    std::vector<llvm::Value *> args{env};
    for (auto *a : c->args) args.push_back(emitExpr(a));
    // signature: env + params
    Type *ft = callee->type;
    std::vector<llvm::Type *> pts;
    pts.push_back(PointerType::get(ctx, 0));
    for (auto *p : ft->params) pts.push_back(llvmType(p));
    FunctionType *fty = FunctionType::get(llvmType(ft->ret), pts, false);
    return ccall(fty, fnPtr, args, "call.closure");
  }

  // member callee: method / static / module function / interface
  if (callee->kind == Expr::Member) {
    auto *m = (EMember *)callee;
    if (m->memberKind == MemberKind::IfaceMethod) {
      // fat pointer: {data ptr, itable ptr}
      llvm::Value *fat = emitExpr(m->obj);
      llvm::Value *data = builder.CreateExtractValue(fat, {0});
      llvm::Value *itable = builder.CreateExtractValue(fat, {1});
      unsigned slot = (unsigned)m->ifaceMethodIndex;
      llvm::Value *fnPtr = builder.CreateLoad(
          PointerType::get(ctx, 0),
          builder.CreateGEP(PointerType::get(ctx, 0), itable,
                            ConstantInt::get(builder.getInt64Ty(), slot)));
      DInterface *iface = (DInterface *)m->target;
      DFunc *proto = iface->methods[m->ifaceMethodIndex];
      std::vector<llvm::Type *> pts{PointerType::get(ctx, 0)};
      for (auto &p : proto->params) pts.push_back(llvmType(sema.resolveType(p.type)));
      FunctionType *fty = FunctionType::get(
          proto->retType ? llvmType(sema.resolveType(proto->retType)) : builder.getVoidTy(), pts, false);
      std::vector<llvm::Value *> args{data};
      for (auto *a : c->args) args.push_back(emitExpr(a));
      return ccall(fty, fnPtr, args, "call.iface");
    }
    if (m->memberKind == MemberKind::Method && m->resolvedFunc) {
      DFunc *f = (DFunc *)m->resolvedFunc;
      if (c->baseSelfCall) {
        // BaseName.method(args): direct call with the CURRENT self
        llvm::Function *impl = declareFunc(f, {});
        auto it = localSlots.find("self");
        llvm::Value *self = nullptr;
        if (it != localSlots.end())
          self = builder.CreateLoad(llvmType(sema.selfTypeOf(f)), it->second);
        else
          self = Constant::getNullValue(PointerType::get(ctx, 0));
        std::vector<llvm::Value *> args{self};
        for (size_t ai = 0; ai < f->params.size(); ai++) {
          if (ai < c->args.size()) args.push_back(emitExpr(c->args[ai]));
          else if (f->params[ai].defVal) {
            Type *want = sema.resolveType(f->params[ai].type);
            args.push_back(coerceValue(emitExpr(f->params[ai].defVal), want,
                                       f->params[ai].defVal->type));
          }
        }
        return ccall(impl, args, "call.base");
      }
      // generic method?
      if (!f->genericParams.empty() && c->genInstance) {
        GenericInstance *gi = (GenericInstance *)c->genInstance;
        llvm::Function *impl = declareFunc(gi->clonedFunc, gi->args);
        llvm::Value *self = emitSelfArg(m->obj);
        std::vector<llvm::Value *> args{self};
        for (size_t ai = 0; ai < gi->clonedFunc->params.size(); ai++) {
          if (ai < c->args.size()) args.push_back(emitExpr(c->args[ai]));
          else if (gi->clonedFunc->params[ai].defVal) args.push_back(emitExpr(gi->clonedFunc->params[ai].defVal));
        }
        return ccall(impl, args, "call.method");
      }
      llvm::Function *impl = declareFunc(f, {});
      bool virtualCall = false;
      if (m->obj->type) {
        Type *ot = m->obj->type;
        Type *derefTy = ot->isPtr() ? ot->pointee : ot;
        if (derefTy->isClass()) {
          DClass *dc = (DClass *)derefTy->decl;
          ClassLayout *cl = sema.layoutOf(dc);
          // virtual when the STATIC type's vtable contains the method
          if (cl && cl->polymorphic && cl->vtableSlots.count(f->name)) virtualCall = true;
        }
      }
      llvm::Value *self = emitSelfArg(m->obj);
      std::vector<llvm::Value *> args = emitCallArgs(f, c);
      if (!f->isStatic) args.insert(args.begin(), self);

      if (virtualCall && !f->isStatic) {
        Type *derefTy = m->obj->type->isPtr() ? m->obj->type->pointee : m->obj->type;
        DClass *dc = (DClass *)derefTy->decl;
        ClassLayout *cl = sema.layoutOf(dc);
        int slot = cl->vtableSlots.at(f->name);
        llvm::Value *selfPtr = args[0];
        llvm::Value *vt = builder.CreateLoad(PointerType::get(ctx, 0), selfPtr, "vptr");
        llvm::Value *fnPtr = builder.CreateLoad(
            PointerType::get(ctx, 0),
            builder.CreateGEP(PointerType::get(ctx, 0), vt,
                              ConstantInt::get(builder.getInt64Ty(), slot)));
        std::vector<llvm::Type *> pts;
        Type *selfTy = sema.selfTypeOf(f);
        pts.push_back(selfTy ? llvmType(selfTy) : PointerType::get(ctx, 0));
        for (auto &p : f->params) pts.push_back(llvmType(sema.resolveType(p.type)));
        FunctionType *fty = FunctionType::get(
            f->retType ? llvmType(sema.resolveType(f->retType)) : builder.getVoidTy(), pts, false);
        return ccall(fty, fnPtr, args, "call.virtual");
      }
      return ccall(impl, args, "call.method");
    }
    if (m->memberKind == MemberKind::StaticMethod && m->resolvedFunc) {
      DFunc *f = (DFunc *)m->resolvedFunc;
      if (!f->genericParams.empty() && c->genInstance) {
        GenericInstance *gi = (GenericInstance *)c->genInstance;
        llvm::Function *impl = declareFunc(gi->clonedFunc, gi->args);
        std::vector<llvm::Value *> args;
        for (auto *a : c->args) args.push_back(emitExpr(a));
        return ccall(impl, args, "call.static");
      }
      llvm::Function *impl = declareFunc(f, {});
      std::vector<llvm::Value *> args;
      for (auto *a : c->args) args.push_back(emitExpr(a));
      return ccall(impl, args, "call.static");
    }
    if (m->memberKind == MemberKind::ModuleMember && m->resolvedFunc) {
      DFunc *f = (DFunc *)m->resolvedFunc;
      if (!f->genericParams.empty() && c->genInstance) {
        GenericInstance *gi = (GenericInstance *)c->genInstance;
        llvm::Function *impl = declareFunc(gi->clonedFunc, gi->args);
        std::vector<llvm::Value *> args;
        for (auto *a : c->args) args.push_back(emitExpr(a));
        return ccall(impl, args, "call");
      }
      llvm::Function *impl = declareFunc(f, {});
      std::vector<llvm::Value *> args;
      for (auto *a : c->args) args.push_back(emitExpr(a));
      return ccall(impl, args, "call");
    }
  }

  // plain identifier callee (function or generic)
  if (callee->kind == Expr::Ident) {
    auto *id = (EIdent *)callee;
    // generic instantiation?
    if (c->genInstance) {
      GenericInstance *gi = (GenericInstance *)c->genInstance;
      llvm::Function *impl = declareFunc(gi->clonedFunc, gi->args);
      std::vector<llvm::Value *> args;
      for (auto *a : c->args) args.push_back(emitExpr(a));
      return ccall(impl, args, "call");
    }
    if (id->idKind == IdKind::Local) {
      // variable of function type
      llvm::Value *closure = emitExpr(callee);
      llvm::Value *fnPtr = builder.CreateExtractValue(closure, {0});
      llvm::Value *env = builder.CreateExtractValue(closure, {1});
      Type *ft = callee->type;
      std::vector<llvm::Type *> pts{PointerType::get(ctx, 0)};
      for (auto *p : ft->params) pts.push_back(llvmType(p));
      FunctionType *fty = FunctionType::get(llvmType(ft->ret), pts, false);
      std::vector<llvm::Value *> args{env};
      for (auto *a : c->args) args.push_back(emitExpr(a));
      return ccall(fty, fnPtr, args, "call");
    }
    DFunc *f = id->resolvedFunc ? (DFunc *)id->resolvedFunc : nullptr;
    if (f) {
      llvm::Function *impl = declareFunc(f, {});
      std::vector<llvm::Value *> args = emitCallArgs(f, c);
      return ccall(impl, args, "call");
    }
  }
  emitPanic("internal: unresolved call", c->loc);
  return Constant::getNullValue(builder.getInt32Ty());
}

llvm::Value *Codegen::coerceValue(llvm::Value *v, Type *want, Type *got) {
  if (!want || !got) return v;
  if (want->isString() && got->isString()) return v;
  // string -> ptr<char>: pass the data pointer
  if (got->isString() && want->isPtr() && want->pointee->isPrim() &&
      want->pointee->prim == PRIM_char) {
    return builder.CreateExtractValue(v, {0});
  }
  // f32 -> f64 for C varargs
  if (got->isFloat() && got->prim == PRIM_f32 && want->isFloat() && want->prim == PRIM_f64)
    return builder.CreateFPExt(v, builder.getDoubleTy());
  if (got->isBool() && want->isInt()) return builder.CreateZExt(v, llvmType(want));
  // integer widening/narrowing for default-arg literals
  if (got->isInt() && want->isInt() && !tc.same(want, got)) {
    llvm::Type *lt = llvmType(want);
    unsigned sb = v->getType()->getIntegerBitWidth();
    unsigned db = lt->getIntegerBitWidth();
    if (db < sb) return builder.CreateTrunc(v, lt);
    if (db > sb)
      return primIsSigned(got->prim) ? builder.CreateSExt(v, lt) : builder.CreateZExt(v, lt);
    return v;
  }
  return v;
}

std::vector<llvm::Value *> Codegen::emitCallArgs(DFunc *f, ECall *c) {
  std::vector<llvm::Value *> args;
  for (size_t ai = 0; ai < f->params.size(); ai++) {
    if (ai < c->args.size()) {
      llvm::Value *v = emitExpr(c->args[ai]);
      Type *want = sema.resolveType(f->params[ai].type);
      args.push_back(coerceValue(v, want, c->args[ai]->type));
    } else if (f->params[ai].defVal) {
      Type *want = sema.resolveType(f->params[ai].type);
      args.push_back(coerceValue(emitExpr(f->params[ai].defVal), want, f->params[ai].defVal->type));
    }
  }
  // variadic extras
  for (size_t ai = f->params.size(); ai < c->args.size(); ai++) {
    llvm::Value *v = emitExpr(c->args[ai]);
    Type *at = c->args[ai]->type;
    // C varargs promotions
    if (at->isFloat() && at->prim == PRIM_f32) v = builder.CreateFPExt(v, builder.getDoubleTy());
    if (at->isBool()) v = builder.CreateZExt(v, builder.getInt32Ty());
    args.push_back(v);
  }
  return args;
}

llvm::Value *Codegen::emitSelfArg(Expr *obj) {
  // `self` for a method call: pointer to the object
  if (obj->type && obj->type->isPtr()) return emitExpr(obj);
  llvm::Value *addr = emitLValue(obj);
  if (addr) return addr;
  return emitExpr(obj);
}

// ---------------------------------------------------- call argument helpers --
llvm::Constant *Codegen::makeStringConst(const std::string &bytes) {
  ArrayType *at = ArrayType::get(builder.getInt8Ty(), bytes.size() + 1);
  auto *gv = new GlobalVariable(*mod, at, true, GlobalValue::PrivateLinkage,
                                ConstantDataArray::getString(ctx, bytes, true), ".str");
  llvm::Constant *ptr = ConstantExpr::getBitCast(gv, PointerType::get(ctx, 0));
  return ConstantStruct::get(stringType(), {ptr, ConstantInt::get(builder.getInt64Ty(), bytes.size())});
}

void Codegen::emitPanic(const std::string &msg, SourceLoc loc) {
  builder.CreateCall(rtFunc("core_rt_panic", builder.getVoidTy(), {stringType()}),
                     {makeStringConst(msg)});
  builder.CreateUnreachable();
}

// ----------------------------------------------------------------- casts ----
llvm::Value *Codegen::emitCast(ECast *c) {
  CastKind kind = (CastKind)c->castKind;
  llvm::Value *v = emitExpr(c->e);
  if (kind == CastKind::Identity || kind == CastKind::ToNever) return v;
  llvm::Type *dst = llvmType(c->type);
  if (!dst) return v;
  switch (kind) {
  case CastKind::IntToInt: {
    unsigned sb = v->getType()->getIntegerBitWidth();
    unsigned db = dst->getIntegerBitWidth();
    bool sSigned = c->e->type && primIsSigned(c->e->type->prim);
    if (db < sb) return builder.CreateTrunc(v, dst);
    if (db > sb) return sSigned ? builder.CreateSExt(v, dst) : builder.CreateZExt(v, dst);
    return v;
  }
  case CastKind::IntToFloat:
    return (c->e->type && primIsSigned(c->e->type->prim)) ? builder.CreateSIToFP(v, dst)
                                                          : builder.CreateUIToFP(v, dst);
  case CastKind::FloatToInt:
    return (c->type->isInt() && primIsSigned(c->type->prim)) ? builder.CreateFPToSI(v, dst)
                                                             : builder.CreateFPToUI(v, dst);
  case CastKind::FloatToFloat: {
    if (dst->getPrimitiveSizeInBits() > v->getType()->getPrimitiveSizeInBits())
      return builder.CreateFPExt(v, dst);
    return builder.CreateFPTrunc(v, dst);
  }
  case CastKind::PtrToPtr:
  case CastKind::ClassUp:
  case CastKind::ClassDown:
  case CastKind::IntToPtr:
    return builder.CreateBitOrPointerCast(v, dst);
  case CastKind::PtrToInt:
    return builder.CreatePtrToInt(v, dst);
  case CastKind::EnumToInt: {
    // storage is i32 or byte array; ints pass through
    if (v->getType()->isIntegerTy()) return v;
    return builder.CreateLoad(builder.getInt32Ty(), emitLValue(c->e));
  }
  case CastKind::IntToEnum: {
    unsigned db = dst->getIntegerBitWidth();
    if (db == v->getType()->getIntegerBitWidth()) return v;
    return builder.CreateIntCast(v, dst, false);
  }
  case CastKind::BoolToInt:
    return builder.CreateZExt(v, dst);
  case CastKind::IntToBool:
    return builder.CreateICmpNE(v, Constant::getNullValue(v->getType()));
  case CastKind::IfaceWrap: {
    // class -> interface fat pointer: {object address, itable}
    DInterface *iface = (DInterface *)c->type->ifaceDecl;
    DClass *cls = (DClass *)c->e->type->decl;
    llvm::Value *obj = c->e->type->isPtr() ? emitExpr(c->e) : emitLValue(c->e);
    if (!obj) obj = emitExpr(c->e);
    llvm::Value *it = itableFor(cls, iface);
    llvm::Value *fat = llvm::UndefValue::get(llvmType(c->type));
    fat = builder.CreateInsertValue(fat, obj, 0);
    fat = builder.CreateInsertValue(fat, it, 1);
    return fat;
  }
  case CastKind::IfaceUnwrap: {
    return builder.CreateExtractValue(v, {0});
  }
  default:
    return v;
  }
}

// ---------------------------------------------------------------- builtins --
llvm::Value *Codegen::emitBuiltinCall(ECall *c, Builtin b, const std::string &name) {
  auto &args = c->args;
  switch (b) {
  case Builtin::Len: {
    Type *t = args[0]->type;
    llvm::Value *v = emitExpr(args[0]);
    if (t->isArray()) return ConstantInt::get(builder.getInt64Ty(), t->arrayLen);
    if (t->isString()) return builder.CreateExtractValue(v, {1});
    return ConstantInt::get(builder.getInt64Ty(), 0);
  }
  case Builtin::SourceFile: {
    std::string file = diag.sm.fileName(c->loc.file);
    return makeStringConst(file);
  }
  case Builtin::SourceLine:
    return ConstantInt::get(builder.getInt64Ty(), c->loc.line);
  case Builtin::AtomicLoad: {
    Type *pt = args[0]->type->pointee;
    if (pt->isBool()) {
      // LLVM requires byte-sized atomics: bool uses an i8 cell
      llvm::LoadInst *l = builder.CreateLoad(builder.getInt8Ty(), emitExpr(args[0]));
      l->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
      return builder.CreateICmpNE(l, ConstantInt::get(builder.getInt8Ty(), 0));
    }
    llvm::LoadInst *l = builder.CreateLoad(llvmType(pt), emitExpr(args[0]));
    l->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
    return l;
  }
  case Builtin::AtomicStore: {
    Type *pt = args[0]->type->pointee;
    if (pt->isBool()) {
      llvm::Value *b = builder.CreateZExt(emitExpr(args[1]), builder.getInt8Ty());
      llvm::StoreInst *s = builder.CreateStore(b, emitExpr(args[0]));
      s->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
      return nullptr;
    }
    llvm::StoreInst *s = builder.CreateStore(emitExpr(args[1]), emitExpr(args[0]));
    s->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
    return nullptr;
  }
  case Builtin::AtomicAdd: case Builtin::AtomicSub: case Builtin::AtomicSwap: {
    llvm::AtomicRMWInst::BinOp op = b == Builtin::AtomicAdd ? llvm::AtomicRMWInst::Add
                                  : b == Builtin::AtomicSub ? llvm::AtomicRMWInst::Sub
                                                            : llvm::AtomicRMWInst::Xchg;
    Type *pt = args[0]->type->pointee;
    if (pt->isBool()) {
      llvm::Value *b = builder.CreateZExt(emitExpr(args[1]), builder.getInt8Ty());
      return builder.CreateICmpNE(
          builder.CreateAtomicRMW(op, emitExpr(args[0]), b, llvm::MaybeAlign(),
                                  llvm::AtomicOrdering::SequentiallyConsistent),
          ConstantInt::get(builder.getInt8Ty(), 0));
    }
    return builder.CreateAtomicRMW(op, emitExpr(args[0]), emitExpr(args[1]),
                                   llvm::MaybeAlign(), llvm::AtomicOrdering::SequentiallyConsistent);
  }
  case Builtin::AtomicCas: {
    llvm::Value *ptr = emitExpr(args[0]);
    llvm::Value *cmp = emitExpr(args[1]);
    llvm::Value *nw = emitExpr(args[2]);
    Type *pt = args[0]->type->pointee;
    if (pt->isBool()) {
      cmp = builder.CreateZExt(cmp, builder.getInt8Ty());
      nw = builder.CreateZExt(nw, builder.getInt8Ty());
      llvm::AtomicCmpXchgInst *cx = builder.CreateAtomicCmpXchg(
          ptr, cmp, nw, llvm::MaybeAlign(), llvm::AtomicOrdering::SequentiallyConsistent,
          llvm::AtomicOrdering::SequentiallyConsistent);
      return builder.CreateICmpNE(builder.CreateExtractValue(cx, {0}),
                                  ConstantInt::get(builder.getInt8Ty(), 0));
    }
    llvm::AtomicCmpXchgInst *cx = builder.CreateAtomicCmpXchg(
        ptr, cmp, nw, llvm::MaybeAlign(), llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering::SequentiallyConsistent);
    // returns {old, succeeded}: extract old
    return builder.CreateExtractValue(cx, {0});
  }
  case Builtin::AtomicFence:
    builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
    return nullptr;
  case Builtin::VolatileLoad: {
    Type *pt = args[0]->type->pointee;
    llvm::LoadInst *l = builder.CreateLoad(llvmType(pt), emitExpr(args[0]));
    l->setVolatile(true);
    return l;
  }
  case Builtin::VolatileStore: {
    llvm::StoreInst *s = builder.CreateStore(emitExpr(args[1]), emitExpr(args[0]));
    s->setVolatile(true);
    return nullptr;
  }
  case Builtin::Asm: case Builtin::AsmVolatile: {
    // asm(template, constraints, args...) -> u64
    llvm::Value *tmpl = emitExpr(args[0]);
    llvm::Value *cons = emitExpr(args[1]);
    std::string tmplStr, consStr;
    tmplStr = stringConstantBytes(dyn_cast<llvm::Constant>(tmpl));
    consStr = stringConstantBytes(dyn_cast<llvm::Constant>(cons));
    if (getenv("CORE_DBG"))
      fprintf(stderr, "[asm] tmpl kind=%d empty=%d\n", (int)tmpl->getValueID(), tmplStr.empty());
    if (tmplStr.empty()) {
      diag.error(c->loc, "inline asm template and constraints must be string literals");
      return Constant::getNullValue(builder.getInt64Ty());
    }
    std::vector<llvm::Type *> pts;
    std::vector<llvm::Value *> vals;
    for (size_t i = 2; i < args.size(); i++) {
      llvm::Value *v = emitExpr(args[i]);
      if (v->getType()->isIntOrIntVectorTy() && v->getType()->getIntegerBitWidth() < 64) {
        // widen small ints to i64 for the asm ABI
        v = builder.CreateZExt(v, builder.getInt64Ty());
      } else if (v->getType()->isPointerTy()) {
        v = builder.CreatePtrToInt(v, builder.getInt64Ty());
      }
      pts.push_back(builder.getInt64Ty());
      vals.push_back(v);
    }
    FunctionType *fty = FunctionType::get(builder.getInt64Ty(), pts, false);
    InlineAsm *ia = InlineAsm::get(fty, tmplStr, consStr, b == Builtin::AsmVolatile);
    return builder.CreateCall(ia, vals);
  }
  case Builtin::Splat: {
    std::string vec = name.substr(6); // e.g. "f32x4"
    int vk = primKindByName(vec);
    llvm::Type *lt = llvmType(tc.prim(vk));
    llvm::Value *scalar = emitExpr(args[0]);
    llvm::VectorType *vt = dyn_cast<llvm::VectorType>(lt);
    if (!vt) return scalar;
    llvm::Value *elem = scalar;
    if (elem->getType() != vt->getElementType()) {
      if (elem->getType()->isIntegerTy() && vt->getElementType()->isIntegerTy())
        elem = builder.CreateIntCast(elem, vt->getElementType(), false);
      else if (elem->getType()->isIntegerTy())
        elem = builder.CreateSIToFP(elem, vt->getElementType());
    }
    return builder.CreateVectorSplat(vt->getElementCount(), elem);
  }
  case Builtin::SimdExtract: {
    std::string prim = name.substr(13);
    std::string elem = prim.substr(0, prim.find('x'));
    int ek = primKindByName(elem);
    llvm::Value *v = emitExpr(args[0]);
    llvm::Value *idx = emitExpr(args[1]);
    return builder.CreateExtractElement(v, idx);
  }
  case Builtin::SimdReplace: {
    std::string prim = name.substr(13);
    int pk = primKindByName(prim);
    llvm::Value *v = emitExpr(args[0]);
    llvm::Value *idx = emitExpr(args[1]);
    llvm::Value *x = emitExpr(args[2]);
    llvm::VectorType *vt = dyn_cast<llvm::VectorType>(llvmType(tc.prim(pk)));
    if (x->getType() != vt->getElementType())
      x = builder.CreateBitOrPointerCast(x, vt->getElementType());
    return builder.CreateInsertElement(v, x, idx);
  }
  default:
    return Constant::getNullValue(builder.getInt32Ty());
  }
}

// --------------------------------------------------------- variant ctor -----
llvm::Value *Codegen::emitVariantCtor(ECall *c) {
  DEnum *e = nullptr;
  int tag = -1;
  if (c->callee->kind == Expr::Member) {
    auto *m = (EMember *)c->callee;
    e = (DEnum *)m->target;
    tag = m->memberIndex;
  } else {
    auto *id = (EIdent *)c->callee;
    e = (DEnum *)id->target;
    tag = id->enumTag;
  }
  Type *enumTy = c->type;
  unsigned off, total, align;
  llvm::Type *storage = enumStorageType(enumTy, &off, &total, &align);

  if (!e || e->variants[tag].payloadTypes.empty()) {
    return ConstantInt::get(builder.getInt32Ty(), tag);
  }
  // build byte-array constant or runtime stores: use stack alloc + stores
  llvm::Value *slot = builder.CreateAlloca(storage, nullptr, "variant");
  // store tag
  llvm::Value *tagPtr = builder.CreateBitCast(slot, PointerType::get(ctx, 0));
  builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), tag), tagPtr);
  // store payloads at ABI-aligned offsets
  unsigned cursor = off;
  for (size_t ai = 0; ai < c->args.size(); ai++) {
    Type *pt = c->args[ai]->type;
    llvm::Value *val = emitExpr(c->args[ai]);
    unsigned psz = (unsigned)mod->getDataLayout().getTypeAllocSize(llvmType(pt));
    unsigned pal = mod->getDataLayout().getABITypeAlign(llvmType(pt)).value();
    cursor = (cursor + pal - 1) / pal * pal;
    llvm::Value *base = builder.CreateBitCast(slot, PointerType::get(ctx, 0));
    llvm::Value *payloadPtr = builder.CreateGEP(builder.getInt8Ty(), base,
                                                ConstantInt::get(builder.getInt64Ty(), cursor));
    builder.CreateStore(val, builder.CreateBitCast(payloadPtr, PointerType::get(ctx, 0)));
    cursor += psz;
  }
  return builder.CreateLoad(storage, slot);
}

// ----------------------------------------------------------------- lambda ---
llvm::Value *Codegen::emitLambda(ELambda *lam) {
  // synthesize: ret @lambda(env: ptr, params...) -> ret
  static int lambdaId = 0;
  int id = lambdaId++;
  std::string base = curFuncDecl ? funcSymbol(curFuncDecl, {}) : "anon";
  std::string sym = base + ".lambda" + std::to_string(id);

  std::vector<llvm::Type *> pts{PointerType::get(ctx, 0)}; // env
  std::vector<Type *> paramTypes;
  for (auto &p : lam->params) paramTypes.push_back(sema.resolveType(p.type));
  for (auto *pt : paramTypes) pts.push_back(llvmType(pt));
  Type *retTy = lam->retType ? sema.resolveType(lam->retType) : tc.prim(PRIM_void);
  FunctionType *fty = FunctionType::get(llvmType(retTy), pts, false);
  llvm::Function *lfn = llvm::Function::Create(fty, llvm::Function::InternalLinkage, sym, mod);

  // env struct type from captures
  std::vector<llvm::Type *> capTys;
  for (auto &cap : lam->captures) capTys.push_back(llvmType(cap.type));
  llvm::StructType *envTy = capTys.empty() ? nullptr : StructType::create(ctx, capTys, sym + ".env");

  // emit the lambda body
  llvm::BasicBlock *saved = builder.GetInsertBlock();
  llvm::DISubprogram *savedSP = curSP;
  llvm::Function *savedFn = fn;
  DFunc *savedDecl = curFuncDecl;
  fn = lfn;
  auto savedSlots = localSlots;

  llvm::BasicBlock *entry = llvm::BasicBlock::Create(ctx, "entry", lfn);
  builder.SetInsertPoint(entry);
  localSlots.clear();
  // env param
  llvm::Argument &envArg = *lfn->args().begin();
  envArg.setName("env");
  unsigned capIdx = 0;
  for (auto &cap : lam->captures) {
    llvm::Value *capAddr = builder.CreateStructGEP(envTy, &envArg, capIdx);
    localSlots[cap.name] = capAddr; // address of the captured copy
    capIdx++;
  }
  // params
  for (size_t pi = 0; pi < lam->params.size(); pi++) {
    llvm::Argument &arg = *lfn->getArg((unsigned)(pi + 1));
    arg.setName(lam->params[pi].name);
    auto *slot = builder.CreateAlloca(arg.getType(), nullptr, lam->params[pi].name + ".addr");
    builder.CreateStore(&arg, slot);
    localSlots[lam->params[pi].name] = slot;
  }
  emitBlock(lam->body);
  Type *rt = lam->retType ? sema.resolveType(lam->retType) : tc.prim(PRIM_void);
  if (rt->isVoid() || rt->isNever()) builder.CreateRetVoid();
  else builder.CreateRet(emitDefaultValue(rt));

  fn = savedFn;
  localSlots = savedSlots;
  curSP = savedSP;
  builder.SetInsertPoint(saved);

  // creation site: allocate + fill env
  if (capTys.empty()) {
    return makeClosureValue(lfn, ConstantPointerNull::get(PointerType::get(ctx, 0)));
  }
  llvm::Value *envMem = builder.CreateCall(
      rtFunc("core_rt_alloc", PointerType::get(ctx, 0), {builder.getInt64Ty()}),
      ConstantInt::get(builder.getInt64Ty(), mod->getDataLayout().getTypeAllocSize(envTy)));
  llvm::Value *envPtr = builder.CreateBitCast(envMem, PointerType::get(ctx, 0));
  unsigned ci = 0;
  for (auto &cap : lam->captures) {
    llvm::Value *srcAddr = nullptr;
    auto it = savedSlots.find(cap.name); // creator's slot for the captured variable
    if (it != savedSlots.end()) srcAddr = it->second;
    if (srcAddr) {
      llvm::Value *val = builder.CreateLoad(llvmType(cap.type), srcAddr);
      builder.CreateStore(val, builder.CreateStructGEP(envTy, envPtr, ci));
    }
    ci++;
  }
  return makeClosureValue(lfn, envPtr);
}

// ------------------------------------------------------------- literals -----
llvm::Value *Codegen::emitStructLit(EStructLit *sl) {
  Type *ty = sl->type;
  llvm::StructType *st = structTypeFor(ty);
  llvm::Value *slot = builder.CreateAlloca(st, nullptr, "tmpobj");
  // polymorphic class literal: set vptr before init
  if (ty->isClass()) {
    DClass *c = (DClass *)ty->decl;
    ClassLayout *cl = sema.layoutOf(c);
    if (cl && cl->polymorphic) {
      builder.CreateStore(vtableFor(c), slot);
    }
  }
  Decl *td = (Decl *)ty->decl;
  for (auto &[fname, fexpr] : sl->fields) {
    int idx = sema.fieldIndexOf(ty, fname);
    if (idx < 0) continue;
    llvm::Value *fptr = builder.CreateStructGEP(st, slot, idx);
    Type *ft = sema.fieldTypeOf(ty, fname);
    llvm::Value *val = emitExpr(fexpr);
    builder.CreateStore(val, fptr);
    (void)ft;
  }
  // classes/structs with constructors: run init after field initialization
  if (ty->isClass() || ty->isStruct()) {
    Decl *cd = (Decl *)ty->decl;
    std::vector<DFunc *> cm = cd->kind == Decl::Class ? ((DClass *)cd)->methods
                                                      : ((DStruct *)cd)->methods;
    for (DFunc *mth : cm) {
      if (mth->name == "init" && !mth->isStatic) {
        // only when all constructor params have defaults (no args available here)
        bool callable = true;
        for (auto &p : mth->params)
          if (!p.defVal) callable = false;
        if (callable) {
          llvm::Function *impl = declareFunc(mth, {});
          std::vector<llvm::Value *> args{slot};
          for (auto &p : mth->params) {
            if (p.defVal) {
              Type *want = sema.resolveType(p.type);
              args.push_back(coerceValue(emitExpr(p.defVal), want, p.defVal->type));
            }
          }
          ccall(impl, args, "call.init");
        } else {
          std::string tn = cd->kind == Decl::Class ? ((DClass *)cd)->name
                                                   : ((DStruct *)cd)->name;
          diag.error(sl->loc, strfmt("constructor of '%s' requires arguments", tn.c_str()),
                     "allocate with alloc<T>() and call init(...) explicitly", 1);
        }
        break;
      }
    }
  }
  return builder.CreateLoad(st, slot, "obj");
}

llvm::Value *Codegen::emitArrayLit(EArrayLit *al) {
  Type *arrTy = al->type;
  llvm::ArrayType *at = cast<llvm::ArrayType>(llvmType(arrTy));
  if (al->repeat) {
    llvm::Value *val = emitExpr(al->elems[0]);
    llvm::Value *slot = builder.CreateAlloca(at, nullptr, "tmparr");
    unsigned n = (unsigned)arrTy->arrayLen;
    for (unsigned i = 0; i < n; i++) {
      llvm::Value *ep = builder.CreateGEP(at, slot, {ConstantInt::get(builder.getInt64Ty(), 0),
                                                     ConstantInt::get(builder.getInt64Ty(), i)});
      builder.CreateStore(val, ep);
    }
    return builder.CreateLoad(at, slot);
  }
  llvm::Value *slot = builder.CreateAlloca(at, nullptr, "tmparr");
  for (size_t i = 0; i < al->elems.size(); i++) {
    llvm::Value *val = emitExpr(al->elems[i]);
    llvm::Value *ep = builder.CreateGEP(at, slot, {ConstantInt::get(builder.getInt64Ty(), 0),
                                                   ConstantInt::get(builder.getInt64Ty(), i)});
    builder.CreateStore(val, ep);
  }
  return builder.CreateLoad(at, slot);
}

// ----------------------------------------------------------------- match ----
llvm::Value *Codegen::emitMatch(EMatch *m) {
  Type *st = m->scrutinee->type;
  if (getenv("CORE_DBG")) fprintf(stderr, "[match] scrutinee type kind=%d\n", st ? (int)st->kind : -1);
  llvm::Value *scrut = emitExpr(m->scrutinee);
  if (!builder.GetInsertBlock()) {
    fprintf(stderr, "internal: no insert point after scrutinee in match\n");
    return Constant::getNullValue(builder.getInt32Ty());
  }
  llvm::Function *f = builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "match.end", f);

  bool isEnum = st->isEnum();
  DEnum *en = isEnum ? (DEnum *)st->decl : nullptr;
  llvm::Value *tag = scrut;
  llvm::Value *scrutAddr = nullptr;
  if (isEnum && en) {
    bool hasPayload = false;
    for (auto &v : en->variants)
      if (!v.payloadTypes.empty()) hasPayload = true;
    if (hasPayload) {
      scrutAddr = builder.CreateAlloca(llvmType(st), nullptr, "match.tmp");
      builder.CreateStore(scrut, scrutAddr);
      tag = builder.CreateLoad(builder.getInt32Ty(),
                               builder.CreateBitCast(scrutAddr, PointerType::get(ctx, 0)));
    }
  }

  bool matchHasValue = m->type && !m->type->isVoid() && !m->type->isNever() &&
                       m->type->kind != TypeKind::Invalid;
  llvm::Value *resultSlot =
      matchHasValue ? builder.CreateAlloca(llvmType(m->type), nullptr, "match.res") : nullptr;
  // build blocks
  std::vector<std::pair<llvm::BasicBlock *, MatchArm *>> armBlocks;
  for (auto &arm : m->arms) {
    armBlocks.push_back({llvm::BasicBlock::Create(ctx, "arm", f), &arm});
  }
  llvm::BasicBlock *defaultBB = endBB; // no match: skip (void match) — exhausted matches never fall through
  // dispatch chain
  if (!isEnum) {
    // literal patterns: chain of icmp
    llvm::BasicBlock *cur = llvm::BasicBlock::Create(ctx, "match.dispatch", f);
    builder.CreateBr(cur);
    builder.SetInsertPoint(cur);
    for (size_t ai = 0; ai < m->arms.size(); ai++) {
      auto &arm = m->arms[ai];
      if (arm.pattern->kind == Pattern::Lit) {
        llvm::Value *pv = emitExpr(arm.pattern->litExpr);
        llvm::Value *cond = builder.CreateICmpEQ(scrut, pv);
        llvm::BasicBlock *next = ai + 1 < m->arms.size()
                                     ? llvm::BasicBlock::Create(ctx, "match.next", f)
                                     : defaultBB;
        builder.CreateCondBr(cond, armBlocks[ai].first, next);
        builder.SetInsertPoint(next);
        if (ai + 1 >= m->arms.size()) break;
        continue;
      }
      if (arm.pattern->kind == Pattern::Wild) {
        builder.CreateBr(armBlocks[ai].first);
        builder.SetInsertPoint(defaultBB);
        break;
      }
      builder.CreateBr(armBlocks[ai].first);
      builder.SetInsertPoint(defaultBB);
      break;
    }
  } else {
    // enum tag dispatch
    llvm::BasicBlock *cur = llvm::BasicBlock::Create(ctx, "match.dispatch", f);
    builder.CreateBr(cur);
    builder.SetInsertPoint(cur);
    for (size_t ai = 0; ai < m->arms.size(); ai++) {
      auto &arm = m->arms[ai];
      bool isDefault = arm.pattern->kind == Pattern::Wild ||
                       (arm.pattern->kind == Pattern::Var && !arm.pattern->enumDecl);
      if (!isDefault && arm.pattern->kind == Pattern::Variant) {
        llvm::Value *cond = builder.CreateICmpEQ(tag, ConstantInt::get(builder.getInt32Ty(),
                                                                       arm.pattern->variantTag));
        llvm::BasicBlock *next = ai + 1 < m->arms.size()
                                     ? llvm::BasicBlock::Create(ctx, "match.next", f)
                                     : defaultBB;
        builder.CreateCondBr(cond, armBlocks[ai].first, next);
        builder.SetInsertPoint(next);
        continue;
      }
      // wildcard/default arm
      builder.CreateBr(armBlocks[ai].first);
      builder.SetInsertPoint(defaultBB);
      break;
    }
  }

  // emit arm bodies
  for (size_t ai = 0; ai < armBlocks.size(); ai++) {
    llvm::BasicBlock *ab = armBlocks[ai].first;
    auto &arm = *armBlocks[ai].second;
    builder.SetInsertPoint(ab);
    // bindings
    if (arm.pattern->kind == Pattern::Variant && scrutAddr) {
      // bind payload values
      unsigned off, total, align;
      enumStorageType(st, &off, &total, &align);
      unsigned cursor = off;
      for (size_t si = 0; si < arm.pattern->subs.size(); si++) {
        Pattern *sp = arm.pattern->subs[si];
        Type *pt = arm.pattern->payloadTypes[si];
        unsigned psz = (unsigned)mod->getDataLayout().getTypeAllocSize(llvmType(pt));
        unsigned pal = mod->getDataLayout().getABITypeAlign(llvmType(pt)).value();
        cursor = (cursor + pal - 1) / pal * pal;
        if (sp->kind == Pattern::Var) {
          llvm::Value *base = builder.CreateBitCast(scrutAddr, PointerType::get(ctx, 0));
          llvm::Value *pp = builder.CreateGEP(builder.getInt8Ty(), base,
                                              ConstantInt::get(builder.getInt64Ty(), cursor));
          llvm::Value *val = builder.CreateLoad(llvmType(pt),
                                                builder.CreateBitCast(pp, PointerType::get(ctx, 0)));
          auto *slot = builder.CreateAlloca(llvmType(pt), nullptr, sp->name);
          builder.CreateStore(val, slot);
          localSlots[sp->name] = slot;
        }
        cursor += psz;
      }
    } else if (arm.pattern->kind == Pattern::Var && !arm.pattern->enumDecl) {
      // whole-value binding
      auto *slot = builder.CreateAlloca(llvmType(st), nullptr, arm.pattern->name);
      builder.CreateStore(scrut, slot);
      localSlots[arm.pattern->name] = slot;
    }
    // arm value: the last statement's expression result goes to the result slot
    armResultSlot = m->type && !m->type->isVoid() ? resultSlot : nullptr;
    emitBlock(arm.body);
    armResultSlot = nullptr;
    builder.CreateBr(endBB);
  }
  builder.SetInsertPoint(endBB);
  if (resultSlot) return builder.CreateLoad(llvmType(m->type), resultSlot);
  return Constant::getNullValue(builder.getInt32Ty()); // void match; unused
}

// ============================================================= statements ===
void Codegen::emitBlock(Stmt *block) {
  auto *b = (SBlock *)block;
  for (size_t i = 0; i < b->stmts.size(); i++) {
    bool isLast = i + 1 == b->stmts.size();
    Stmt *s = b->stmts[i];
    if (armResultSlot && isLast && s->kind == Stmt::KExpr) {
      llvm::Value *v = emitExpr(((SExpr *)s)->e);
      if (v && armResultSlot) builder.CreateStore(v, armResultSlot);
      continue;
    }
    emitStmt(s);
  }
}

void Codegen::emitStmt(Stmt *s) {
  if (!s) return;
  switch (s->kind) {
  case Stmt::KExpr: {
    emitDebugLoc(s->loc);
    emitExpr(((SExpr *)s)->e);
    break;
  }
  case Stmt::KLet: {
    auto *l = (SLet *)s;
    emitDebugLoc(s->loc);
    if (l->isAssignExisting) {
      // assignment to existing local or global
      if (l->assignGlobal) {
        auto *gv = globalFor((DGlobal *)l->assignGlobal);
        builder.CreateStore(emitExpr(l->init), gv);
      } else {
        auto it = localSlots.find(l->name);
        if (it != localSlots.end()) builder.CreateStore(emitExpr(l->init), it->second);
      }
      break;
    }
    llvm::Type *lt = llvmType(l->resolvedType);
    auto *slot = builder.CreateAlloca(lt, nullptr, l->name);
    if (l->init) {
      builder.CreateStore(emitExpr(l->init), slot);
    } else {
      builder.CreateStore(emitDefaultValue(l->resolvedType), slot);
    }
    localSlots[l->name] = slot;
    break;
  }
  case Stmt::KReturn: {
    emitDebugLoc(s->loc);
    auto *r = (SReturn *)s;
    if (r->e) {
      llvm::Value *v = emitExpr(r->e);
      emitVptrStoreIfInit(curFuncDecl);
      builder.CreateRet(v);
    } else if (fn->getReturnType()->isVoidTy()) {
      emitVptrStoreIfInit(curFuncDecl);
      builder.CreateRetVoid();
    } else {
      emitVptrStoreIfInit(curFuncDecl);
      builder.CreateRet(Constant::getNullValue(fn->getReturnType()));
    }
    // unreachable continuation block for subsequent (dead) code
    llvm::BasicBlock *dead = llvm::BasicBlock::Create(ctx, "post.ret", fn);
    builder.SetInsertPoint(dead);
    break;
  }
  case Stmt::KIf: {
    emitDebugLoc(s->loc);
    auto *i = (SIf *)s;
    llvm::Value *cond = emitExpr(i->cond);
    llvm::Function *f = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(ctx, "if.then", f);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "if.end", f);
    llvm::BasicBlock *elseBB = i->elseBlock ? llvm::BasicBlock::Create(ctx, "if.else", f) : endBB;
    builder.CreateCondBr(cond, thenBB, elseBB);
    builder.SetInsertPoint(thenBB);
    emitBlock(i->thenBlock);
    // fall through only if the block is not terminated
    if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(endBB);
    if (i->elseBlock) {
      builder.SetInsertPoint(elseBB);
      if (i->elseBlock->kind == Stmt::KBlock) emitBlock(i->elseBlock);
      else emitStmt(i->elseBlock);
      if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(endBB);
    }
    builder.SetInsertPoint(endBB);
    break;
  }
  case Stmt::KWhile: {
    emitDebugLoc(s->loc);
    auto *w = (SWhile *)s;
    llvm::Function *f = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx, "while.cond", f);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx, "while.body", f);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "while.end", f);
    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    llvm::Value *cond = emitExpr(w->cond);
    builder.CreateCondBr(cond, bodyBB, endBB);
    builder.SetInsertPoint(bodyBB);
    breakStack.push_back({endBB, condBB});
    continueStack.push_back({endBB, condBB});
    emitBlock(w->body);
    breakStack.pop_back();
    continueStack.pop_back();
    if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(condBB);
    builder.SetInsertPoint(endBB);
    break;
  }
  case Stmt::KFor: {
    auto *f = (SFor *)s;
    if (f->init) emitStmt(f->init);
    llvm::Function *lf = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx, "for.cond", lf);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx, "for.body", lf);
    llvm::BasicBlock *stepBB = llvm::BasicBlock::Create(ctx, "for.step", lf);
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "for.end", lf);
    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    if (f->cond) {
      llvm::Value *cond = emitExpr(f->cond);
      builder.CreateCondBr(cond, bodyBB, endBB);
    } else {
      builder.CreateBr(bodyBB);
    }
    builder.SetInsertPoint(bodyBB);
    breakStack.push_back({endBB, stepBB});
    continueStack.push_back({endBB, stepBB});
    emitBlock(f->body);
    breakStack.pop_back();
    continueStack.pop_back();
    if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(stepBB);
    builder.SetInsertPoint(stepBB);
    if (f->step) emitStmt(f->step);
    if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(condBB);
    builder.SetInsertPoint(endBB);
    break;
  }
  case Stmt::KForIn: {
    auto *fi = (SForIn *)s;
    if (fi->iterable->kind == Expr::Range) {
      auto *r = (ERange *)fi->iterable;
      emitDebugLoc(s->loc);
      llvm::Function *lf = builder.GetInsertBlock()->getParent();
      Type *itTy = r->lo->type;
      llvm::Value *start = emitExpr(r->lo);
      llvm::Value *end = emitExpr(r->hi);
      bool up = r->inclusive;
      auto *ivar = builder.CreateAlloca(llvmType(itTy), nullptr, fi->varName);
      builder.CreateStore(start, ivar);
      localSlots[fi->varName] = ivar;
      llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx, "forin.cond", lf);
      llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx, "forin.body", lf);
      llvm::BasicBlock *stepBB = llvm::BasicBlock::Create(ctx, "forin.step", lf);
      llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "forin.end", lf);
      builder.CreateBr(condBB);
      builder.SetInsertPoint(condBB);
      llvm::Value *cur = builder.CreateLoad(llvmType(itTy), ivar);
      llvm::Value *cond = up ? builder.CreateICmpSLE(cur, end) : builder.CreateICmpSLT(cur, end);
      builder.CreateCondBr(cond, bodyBB, endBB);
      builder.SetInsertPoint(bodyBB);
      breakStack.push_back({endBB, stepBB});
      continueStack.push_back({endBB, stepBB});
      emitBlock(fi->body);
      breakStack.pop_back();
      continueStack.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(stepBB);
      builder.SetInsertPoint(stepBB);
      llvm::Value *one = ConstantInt::get(cast<IntegerType>(llvmType(itTy)), 1);
      builder.CreateStore(builder.CreateAdd(builder.CreateLoad(llvmType(itTy), ivar), one), ivar);
      builder.CreateBr(condBB);
      builder.SetInsertPoint(endBB);
    } else {
      // array iteration
      Type *arrTy = fi->iterable->type;
      llvm::Value *arr = emitExpr(fi->iterable);
      emitDebugLoc(s->loc);
      llvm::Function *lf = builder.GetInsertBlock()->getParent();
      Type *elemTy = arrTy->elem;
      auto *evar = builder.CreateAlloca(llvmType(elemTy), nullptr, fi->varName);
      auto *ivar = builder.CreateAlloca(builder.getInt64Ty(), nullptr, fi->varName + ".i");
      builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), ivar);
      localSlots[fi->varName] = evar;
      llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx, "forarr.cond", lf);
      llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx, "forarr.body", lf);
      llvm::BasicBlock *stepBB = llvm::BasicBlock::Create(ctx, "forarr.step", lf);
      llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "forarr.end", lf);
      builder.CreateBr(condBB);
      builder.SetInsertPoint(condBB);
      llvm::Value *i = builder.CreateLoad(builder.getInt64Ty(), ivar);
      builder.CreateCondBr(builder.CreateICmpULT(i, ConstantInt::get(builder.getInt64Ty(),
                                                                    arrTy->arrayLen)), bodyBB, endBB);
      builder.SetInsertPoint(bodyBB);
      i = builder.CreateLoad(builder.getInt64Ty(), ivar);
      llvm::Value *arrAddr = emitLValue(fi->iterable);
      if (!arrAddr) {
        auto *tmp = builder.CreateAlloca(llvmType(arrTy), nullptr, "forarr.tmp");
        builder.CreateStore(arr, tmp);
        arrAddr = tmp;
      }
      llvm::Value *elemPtr = builder.CreateGEP(cast<llvm::ArrayType>(llvmType(arrTy)), arrAddr,
                                               {ConstantInt::get(builder.getInt64Ty(), 0), i});
      builder.CreateStore(builder.CreateLoad(llvmType(elemTy), elemPtr), evar);
      breakStack.push_back({endBB, stepBB});
      continueStack.push_back({endBB, stepBB});
      emitBlock(fi->body);
      breakStack.pop_back();
      continueStack.pop_back();
      if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(stepBB);
      builder.SetInsertPoint(stepBB);
      builder.CreateStore(builder.CreateAdd(builder.CreateLoad(builder.getInt64Ty(), ivar),
                                            ConstantInt::get(builder.getInt64Ty(), 1)), ivar);
      builder.CreateBr(condBB);
      builder.SetInsertPoint(endBB);
    }
    break;
  }
  case Stmt::KBreak: {
    if (!breakStack.empty()) {
      // switch: break is implicit (cases never fall through); loops use it
      builder.CreateBr(breakStack.back().first);
      llvm::BasicBlock *dead = llvm::BasicBlock::Create(ctx, "post.break", fn);
      builder.SetInsertPoint(dead);
    }
    break;
  }
  case Stmt::KContinue: {
    if (!continueStack.empty()) {
      builder.CreateBr(continueStack.back().second);
      llvm::BasicBlock *dead = llvm::BasicBlock::Create(ctx, "post.cont", fn);
      builder.SetInsertPoint(dead);
    }
    break;
  }
  case Stmt::KSwitch: {
    auto *sw = (SSwitch *)s;
    emitDebugLoc(s->loc);
    llvm::Value *scrut = emitExpr(sw->scrutinee);
    if (!scrut) {
      fprintf(stderr, "internal: switch scrutinee emitted null (kind=%d)\n", (int)sw->scrutinee->kind);
      scrut = Constant::getNullValue(builder.getInt32Ty());
    }
    llvm::Function *f = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx, "switch.end", f);
    // case bodies
    std::vector<std::pair<llvm::BasicBlock *, Stmt *>> bodies;
    for (auto &c : sw->cases) {
      bodies.push_back({llvm::BasicBlock::Create(ctx, "case", f), c.body});
    }
    llvm::BasicBlock *defBB = sw->defaultBody ? llvm::BasicBlock::Create(ctx, "default", f) : endBB;
    llvm::Value *iv = scrut->getType()->isIntegerTy(1)
                          ? builder.CreateZExt(scrut, builder.getInt32Ty())
                          : builder.CreateTrunc(scrut, builder.getInt32Ty());
    SwitchInst *si = builder.CreateSwitch(iv, defBB, (unsigned)sw->cases.size());
    for (size_t ci = 0; ci < sw->cases.size(); ci++) {
      for (Expr *v : sw->cases[ci].values) {
        llvm::Value *cv = emitExpr(v);
        if (cv->getType()->isIntegerTy(1)) cv = builder.CreateZExt(cv, builder.getInt32Ty());
        cv = builder.CreateTrunc(cv, builder.getInt32Ty());
        si->addCase(cast<ConstantInt>(cv), bodies[ci].first);
      }
    }
    for (auto &[bb, body] : bodies) {
      builder.SetInsertPoint(bb);
      emitBlock(body);
      if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(endBB);
    }
    builder.SetInsertPoint(defBB);
    if (sw->defaultBody) {
      emitBlock(sw->defaultBody);
      if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(endBB);
    }
    builder.SetInsertPoint(endBB);
    break;
  }
  case Stmt::KBlock: {
    emitBlock(s);
    break;
  }
  case Stmt::KUnsafe: {
    for (auto *us : ((SUnsafe *)s)->stmts) emitStmt(us);
    break;
  }
  }
}
} // namespace core
