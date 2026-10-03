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
  std::vector<std::pair<std::string, ModuleSema *>> imports; // name -> module (alias or first part)
};

// A monomorphized generic instance (function or method).
struct GenericInstance {
  DFunc *tmpl = nullptr;
  DFunc *clonedFunc = nullptr; // cloned+checked body (per instance)
  std::vector<Type *> args;
  std::string mangledName; // filled by sema
};

// Resolved class layout: fields in memory order (vptr first if polymorphic).
struct ClassLayout {
  DClass *cls = nullptr;
  bool polymorphic = false;         // has vptr
  DClass *base = nullptr;
  std::vector<std::pair<std::string, Type *>> allFields; // name, type (own fields only for struct)
  std::vector<DFunc *> vtableOrder;                 // virtual methods in slot order
  std::map<std::string, int> vtableSlots;
  std::vector<std::pair<DInterface *, std::string>> interfaces; // implemented ifaces (decl, mangled iface name)
};

// Builtin function ids for calls the compiler itself implements.
enum class Builtin {
  None, Len, SourceFile, SourceLine,
  AtomicLoad, AtomicStore, AtomicAdd, AtomicSub, AtomicSwap, AtomicCas, AtomicFence,
  VolatileLoad, VolatileStore,
  Asm, AsmVolatile,
  Splat, SimdExtract, SimdReplace, // suffixed variants resolved by name
};

class Sema {
public:
  Sema(TypeContext &tc, Diagnostics &diag) : tc(tc), diag(diag) {}

  // Phase 1: register all toplevel decls of every module (in dependency order,
  // prelude first). Returns false on errors.
  bool registerModules(std::vector<ModuleSema *> &modules);
  // Phase 2: type check every function body. Returns false on errors.
  bool checkAll();
  // Phase 3: main entry checks.
  bool checkEntry(ModuleSema *m);

  TypeContext &tc;
  Diagnostics &diag;
  ASTContext ctx; // node arena for generic instantiations
  std::vector<ModuleSema *> modules; // dependency order, prelude first
  std::map<std::string, ModuleSema *> byPath;
  std::map<DFunc *, ModuleSema *> funcModule; // defining module per function
  std::map<void *, ModuleSema *> declModule;  // defining module per type decl
  std::map<Decl *, ClassLayout *> layouts;   // DClass* -> layout (also structs)
  std::map<std::pair<DFunc *, std::string>, GenericInstance *> instances;
  std::vector<GenericInstance *> instanceOrder; // deterministic emission order
  ModuleSema *prelude = nullptr;

  // ---- helper API used by codegen ----
  ClassLayout *layoutOf(Decl *structOrClass);
  Type *typeOf(Expr *e);             // annotated type (after checkAll)
  std::string mangleFuncName(DFunc *f, const std::vector<Type *> &genericArgs);
  GenericInstance *findInstance(DFunc *tmpl, const std::vector<Type *> &args);
  const std::vector<DFunc *> &virtualSlots(DClass *c); // for base-chain lookup
  std::string mangleTypeForName(Type *t);
  int quiet_ = 0;

  // active checking context (public: Codegen reads substitution/layout state)
  ModuleSema *curModule = nullptr;
  DFunc *curFunc = nullptr;
  Type *curReturnType = nullptr;
  std::map<std::string, Type *> *subst = nullptr; // generic substitution
  int unsafeDepth = 0;
  int loopDepth = 0;

  struct LocalVar {
    Type *type = nullptr;
    bool isMut = false;
    bool isConst = false;
    SourceLoc declLoc;
    void *declScope = nullptr;
  };
  struct Scope {
    Scope *parent = nullptr;
    std::map<std::string, LocalVar> vars;
  };
  ModuleSema *entryModule = nullptr;
  Scope *curScope = nullptr;

  // type resolution
  Type *resolveType(TypeExpr *te);
  Type *resolveNamedType(TypeExpr *te);
  unsigned long long constUint(Expr *e, bool &ok);

  // statements/expressions
  void checkFuncDecl(DFunc *f, std::map<std::string, Type *> genericSubst);
  void checkBlock(Stmt *block);
  void checkStmt(Stmt *s);
  void checkExpr(Expr *e, bool lvalue = false);
  void checkBinary(EBinary *b);
  void checkAssign(EAssign *a);
  void checkCast(ECast *c);
  Type *checkMatch(EMatch *m);
  void collectCaptures(Stmt *s, std::set<void *> &ownScopes,
                       std::vector<ELambda::Capture> &caps, std::set<std::string> &seen);
  void collectCapturesExpr(Expr *e, std::set<void *> &ownScopes,
                           std::vector<ELambda::Capture> &caps, std::set<std::string> &seen);
  Type *selfTypeOf(DFunc *f);
  bool unifyTypes(Type *want, Type *got, std::map<std::string, Type *> &vars);
  Type *checkMemberForRead(EMember *m);
  bool canReadVar(const std::string &name, LocalVar &out);

  // symbol lookup
  Decl *lookupTypeOwn(const std::string &name);
  Decl *lookupTypeVisible(const std::string &name, ModuleSema **via = nullptr);
  std::vector<DFunc *> lookupFuncsVisible(const std::string &name, ModuleSema **via = nullptr);
  bool visible(Decl *d, ModuleSema *from);
  ModuleSema *lookupModuleRef(const std::string &name);

  // helpers
  Type *defaultValueType(Type *t);
  bool terminates(Stmt *s);
  void checkVarInit(Expr *init);
  Expr *constFold(Expr *e); // returns folded literal or original
  unsigned long long evalConstUint(Expr *e, bool &ok);

  // class layout construction
  void buildLayout(DClass *c);
  void buildStructLayout(DStruct *s);
  Type *checkCall(ECall *call);
  Type *checkBuiltinCall(ECall *call, Builtin b, const std::string &name);
  Decl *lookupVariantCtor(const std::string &name, Type **outEnumTy = nullptr);
  Type *checkVariantCtor(ECall *call, DEnum *e, const std::string &vname);
  void checkPattern(Pattern *p, Type *scrutinee, std::vector<std::pair<std::string, Type *>> &binds);
  bool containsBreak(Stmt *s);
  DFunc *resolveOverload(const std::vector<DFunc *> &cands, const std::vector<Expr *> &args,
                         SourceLoc loc, const std::string &name, bool &ok);
  bool typesAssignable(Type *dst, Type *src, Expr *srcExpr, SourceLoc loc, const std::string &what);
  bool isLValue(Expr *e);
  DEnum *enumOf(Type *t) { return t && t->isEnum() ? (DEnum *)t->decl : nullptr; }
  int fieldIndexOf(Type *structTy, const std::string &name);
  Type *fieldTypeOf(Type *structTy, const std::string &name);
  Type *substituteFieldType(TypeExpr *te, Type *structTy);
  Type *resolveEnumArgs(DEnum *e, std::vector<Type *> args);
  GenericInstance *instantiateGeneric(DFunc *tmpl, const std::vector<Type *> &args, SourceLoc loc);
};

} // namespace core
#endif
