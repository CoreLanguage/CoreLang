// Core compiler - semantic analysis: name resolution, overload resolution,
// type checking, const evaluation, access control, and the annotations that
// drive code generation.
#ifndef CORE_SEMA_H
#define CORE_SEMA_H

#include "AST.h"
#include "Diag.h"
#include "Type.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace core {

struct ModuleSema;

// One resolved .cr module (file).
struct ModuleSema {
  std::string name;      // dotted module name, e.g. "utils.math"
  std::string path;      // canonical file path
  SourceUnit *unit = nullptr;
  bool isPrelude = false;

  std::map<std::string, Decl *> types;                 // struct/class/interface/trait/enum
  std::map<std::string, std::vector<DFunc *>> funcs;   // overload sets
  std::map<std::string, DGlobal *> globals;
  std::map<std::string, DConst *> consts;
  std::vector<std::pair<std::string, ModuleSema *>> imports; // name -> module
};

// A monomorphized generic instance (function or method).
struct GenericInstance {
  DFunc *tmpl = nullptr;
  DFunc *clonedFunc = nullptr; // cloned+checked body (per instance)
  std::vector<Type *> args;
  std::string mangledName;
};

// Resolved class layout: fields in memory order (vptr first if polymorphic).
struct ClassLayout {
  DClass *cls = nullptr;
  bool polymorphic = false;         // has vptr
  DClass *base = nullptr;
  std::vector<std::pair<std::string, Type *>> allFields;
  std::vector<DFunc *> vtableOrder;                 // virtual methods in slot order
  std::map<std::string, int> vtableSlots;
  std::vector<std::pair<DInterface *, std::string>> interfaces;
};

// Builtin function ids for calls the compiler itself implements.
enum class Builtin {
  None, Len, SourceFile, SourceLine,
  AtomicLoad, AtomicStore, AtomicAdd, AtomicSub, AtomicSwap, AtomicCas, AtomicFence,
  VolatileLoad, VolatileStore,
  Asm, AsmVolatile,
  Splat, SimdExtract, SimdReplace,
};

class Sema {
public:
  Sema(TypeContext &tc, Diagnostics &diag) : tc(tc), diag(diag) {}

  bool registerModules(std::vector<ModuleSema *> &mods);
  bool checkAll();
  bool checkEntry(ModuleSema *m);

  TypeContext &tc;
  Diagnostics &diag;
  std::vector<ModuleSema *> modules; // dependency order, prelude first
  std::map<std::string, ModuleSema *> byPath;
  std::map<DFunc *, ModuleSema *> funcModule; // defining module per function
  std::map<void *, ModuleSema *> declModule;  // defining module per type decl
  std::map<Decl *, ClassLayout *> layouts;
  std::map<std::pair<DFunc *, std::string>, GenericInstance *> instances;
  std::vector<GenericInstance *> instanceOrder;
  ModuleSema *prelude = nullptr;

  // helper API used by codegen
  ClassLayout *layoutOf(Decl *structOrClass);
  Type *typeOf(Expr *e);
  std::string mangleFuncName(DFunc *f, const std::vector<Type *> &genericArgs);
  GenericInstance *findInstance(DFunc *tmpl, const std::vector<Type *> &args);
  std::string mangleTypeForName(Type *t);

  // active checking context (public: Codegen reads substitution state)
  ModuleSema *curModule = nullptr;
  std::vector<std::pair<ELambda *, int>> lambdaStack; // (lambda, base depth)
  void recordCapture(const std::string &name, Type *type, void *declScope);
  DFunc *curFunc = nullptr;
  Type *curReturnType = nullptr;
  std::map<std::string, Type *> *subst = nullptr;
  int unsafeDepth = 0;
  int loopDepth = 0;
  int quiet_ = 0;
  int noStructLit_ = 0;

  struct LocalVar {
    Type *type = nullptr;
    bool isMut = false;
    bool isConst = false;
    SourceLoc declLoc;
    void *declScope = nullptr;
    int declDepth = -1;
  };
  struct Scope {
    Scope *parent = nullptr;
    int depth = 0;
    std::map<std::string, LocalVar> vars;
  };
  Scope *curScope = nullptr;
  ModuleSema *entryModule = nullptr;

  // type resolution
  Type *resolveType(TypeExpr *te);
  Type *resolveNamedType(TypeExpr *te);

  // statements/expressions
  void checkFuncDecl(DFunc *f, std::map<std::string, Type *> genericSubst);
  void checkBlock(Stmt *block);
  void checkStmt(Stmt *s);
  void checkExpr(Expr *e, bool lvalue = false);
  void checkBinary(EBinary *b);
  void checkAssign(EAssign *a);
  void checkCast(ECast *c);
  Type *checkMatch(EMatch *m);
  Type *checkCall(ECall *call);
  Type *checkBuiltinCall(ECall *call, Builtin b, const std::string &name);
  Decl *lookupVariantCtor(const std::string &name, Type **outEnumTy = nullptr);
  Type *checkVariantCtor(ECall *call, DEnum *e, const std::string &vname, Type *knownType = nullptr);
  void checkPattern(Pattern *p, Type *scrutinee, std::vector<std::pair<std::string, Type *>> &binds);
  Expr *constFold(Expr *e);
  unsigned long long evalConstUint(Expr *e, bool &ok);
  bool terminates(Stmt *s);
  bool containsBreak(Stmt *s);
  DFunc *resolveOverload(const std::vector<DFunc *> &cands, const std::vector<Expr *> &args,
                         SourceLoc loc, const std::string &name, bool &ok);
  bool typesAssignable(Type *dst, Type *src, Expr *srcExpr, SourceLoc loc, const std::string &what);
  bool isLValue(Expr *e);
  GenericInstance *instantiateGeneric(DFunc *tmpl, const std::vector<Type *> &args, SourceLoc loc);
  Type *checkMemberForRead(EMember *m);
  Type *resolveEnumArgs(DEnum *e, std::vector<Type *> args);
  DEnum *enumOf(Type *t) { return t && t->isEnum() ? (DEnum *)t->decl : nullptr; }
  int fieldIndexOf(Type *structTy, const std::string &name);
  Type *fieldTypeOf(Type *structTy, const std::string &name);
  Type *substituteFieldType(TypeExpr *te, Type *structTy);
  Type *selfTypeOf(DFunc *f);
  bool unifyTypes(Type *want, Type *got, std::map<std::string, Type *> &vars);

  // symbol lookup
  Decl *lookupTypeOwn(const std::string &name);
  Decl *lookupTypeVisible(const std::string &name, ModuleSema **via = nullptr);
  std::vector<DFunc *> lookupFuncsVisible(const std::string &name, ModuleSema **via = nullptr);
  bool visible(Decl *d, ModuleSema *from);
  ModuleSema *lookupModuleRef(const std::string &name);
  bool canReadVar(const std::string &name, LocalVar &out);

  // class layout construction
  void buildLayout(DClass *c);
  void buildStructLayout(DStruct *s);
};

} // namespace core
#endif
