// Core compiler - AST definitions.
// Every node is allocated from an ASTContext (bump arena, process lifetime).
#ifndef CORE_AST_H
#define CORE_AST_H

#include "Common.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace core {

struct Type;      // defined in Type.h (interned types)
struct Expr;
struct Stmt;
struct Decl;
struct TypeExpr;

// ------------------------------------------------------------ type syntax ---
// Syntactic types (before semantic resolution).
struct TypeExpr {
  enum Kind { Prim, Named, Array, Func } kind = Prim;
  // Prim: prim index into PrimKind; Named: dotted name + generic args
  int prim = 0;
  std::vector<std::string> nameParts;   // Named
  std::vector<TypeExpr *> genericArgs;  // Named
  TypeExpr *elem = nullptr;             // Array
  Expr *arraySize = nullptr;            // Array (may be null => error)
  std::vector<TypeExpr *> paramTypes;   // Func
  TypeExpr *retType = nullptr;          // Func
  SourceLoc loc;
};

// ---------------------------------------------------------------- patterns --
struct Pattern {
  enum Kind { Wild, Var, Variant, Lit } kind = Wild;
  std::string name;              // Var binding / Variant name
  std::vector<Pattern *> subs;   // Variant payload bindings
  Expr *litExpr = nullptr;       // Lit
  SourceLoc loc;
  // sema annotations
  void *enumDecl = nullptr;      // DEnum* for Variant
  int variantTag = -1;
  Type *bindType = nullptr;      // type bound by Var
  std::vector<Type *> payloadTypes; // resolved payload types for Variant
};

// --------------------------------------------------------------- expressions --
struct Expr;
struct Stmt;

// Sema resolution annotations (filled during type checking).
enum class IdKind {
  Unresolved, Local, Global, ConstVal, Func, EnumConst, Module, TypeRef, Builtin,
};
enum class MemberKind {
  Unresolved, Field, Method, StaticMethod, ModuleMember, IfaceMethod, VariantOf,
};
enum class CastKind { None, Identity, IntToInt, IntToFloat, FloatToInt, FloatToFloat,
                       PtrToPtr, IntToPtr, PtrToInt, EnumToInt, IntToEnum, BoolToInt,
                       IntToBool, ClassUp, ClassDown, IfaceWrap, IfaceUnwrap, ToNever };
enum class BinKind { None, Arith, Cmp, StrConcat, StrCmp, PtrArith, PtrDiff, PtrCmp,
                      ShortCircuit, Bitwise, Shift, EnumCmp, VecArith };
enum class UnKind { None, Neg, Not, BitNot, Deref, Ref };

struct Expr {
  enum Kind {
    IntLit, FloatLit, BoolLit, CharLit, StringLit, NullLit,
    Ident, Self, Unary, Binary, Assign, Cast, Call, Member, Index,
    StructLit, ArrayLit, Lambda, Match, Range, Sizeof, Alignof, UnsafeExpr,
  };
  Kind kind;
  SourceLoc loc;
  Type *type = nullptr;           // resolved type (Type.h); set by Sema
  // --- sema annotations ---
  IdKind idKind = IdKind::Unresolved;
  void *target = nullptr;         // DGlobal*/DConst*/DFunc*/ModuleSema*/Decl*
  int enumTag = -1;               // EnumConst: variant tag
  int builtin = 0;                // Builtin enum
  MemberKind memberKind = MemberKind::Unresolved;
  int memberIndex = -1;           // field index / vtable-free method idx
  void *viaModule = nullptr;      // ModuleSema* when member crosses modules
  void *resolvedFunc = nullptr;   // DFunc* chosen overload
  void *genInstance = nullptr;    // GenericInstance* for generic calls
  int ifaceMethodIndex = -1;
  int castKind = 0;               // CastKind
  int binKind = 0;                // BinKind
  int unKind = 0;                 // UnKind
  bool checkBounds = true;        // EIndex bounds check
  bool baseSelfCall = false;      // ECall: BaseName.method(...) takes current self
  void *scopeId = nullptr;        // Scope* where an EIdent local resolved
  explicit Expr(Kind k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Expr() = default;
};

struct EInt : Expr {
  unsigned long long value = 0; // magnitude when neg is set
  bool neg = false;             // unary minus applied to the literal
  std::string digits;           // full digit string (for big literals)
  bool big128 = false;
  EInt(SourceLoc l) : Expr(IntLit, l) {}
};
struct EFloat : Expr {
  double value = 0;
  EFloat(SourceLoc l) : Expr(FloatLit, l) {}
};
struct EBool : Expr {
  bool value = false;
  EBool(SourceLoc l) : Expr(BoolLit, l) {}
};
struct EChar : Expr {
  unsigned value = 0;
  EChar(SourceLoc l) : Expr(CharLit, l) {}
};
struct EString : Expr {
  std::string value;
  EString(SourceLoc l) : Expr(StringLit, l) {}
};
struct ENull : Expr { ENull(SourceLoc l) : Expr(NullLit, l) {} };
struct EIdent : Expr {
  std::string name;
  TypeExpr *typeArgs = nullptr; // `Option<i32>`: parsed generic args on a type name
  EIdent(SourceLoc l, std::string n) : Expr(Ident, l), name(std::move(n)) {}
};
struct ESelf : Expr { ESelf(SourceLoc l) : Expr(Self, l) {} };
struct EUnary : Expr {
  std::string op; // "-" "!" "~" "*" "&"
  Expr *operand = nullptr;
  EUnary(SourceLoc l, std::string o, Expr *e) : Expr(Unary, l), op(std::move(o)), operand(e) {}
};
struct EBinary : Expr {
  std::string op;
  Expr *lhs = nullptr, *rhs = nullptr;
  EBinary(SourceLoc l, std::string o, Expr *a, Expr *b)
      : Expr(Binary, l), op(std::move(o)), lhs(a), rhs(b) {}
};
struct EAssign : Expr {
  std::string op; // "" or "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>="
  Expr *target = nullptr, *value = nullptr;
  EAssign(SourceLoc l, std::string o, Expr *t, Expr *v)
      : Expr(Assign, l), op(std::move(o)), target(t), value(v) {}
};
struct ECast : Expr {
  Expr *e = nullptr;
  TypeExpr *ty = nullptr;
  ECast(SourceLoc l, Expr *e, TypeExpr *t) : Expr(Cast, l), e(e), ty(t) {}
};
struct ECall : Expr {
  Expr *callee = nullptr;
  std::vector<Expr *> args;
  Type *expectedType = nullptr; // context hint for generic inference (sema)
  ECall(SourceLoc l, Expr *c, std::vector<Expr *> a)
      : Expr(Call, l), callee(c), args(std::move(a)) {}
};
struct EMember : Expr {
  Expr *obj = nullptr;
  std::string name;
  std::vector<TypeExpr *> callTypeArgs; // module.Func<T>(...): explicit generic args
  EMember(SourceLoc l, Expr *o, std::string n)
      : Expr(Member, l), obj(o), name(std::move(n)) {}
};
struct EIndex : Expr {
  Expr *base = nullptr, *index = nullptr;
  EIndex(SourceLoc l, Expr *b, Expr *i) : Expr(Index, l), base(b), index(i) {}
};
struct EStructLit : Expr {
  TypeExpr *ty = nullptr;
  std::vector<std::pair<std::string, Expr *>> fields;
  EStructLit(SourceLoc l) : Expr(StructLit, l) {}
};
struct EArrayLit : Expr {
  std::vector<Expr *> elems;
  Expr *repeat = nullptr; // [x; n]
  EArrayLit(SourceLoc l) : Expr(ArrayLit, l) {}
};
struct ELambda : Expr {
  // same param structure as functions
  struct Param { std::string name; TypeExpr *type = nullptr; Expr *defVal = nullptr; SourceLoc loc; };
  std::vector<Param> params;
  TypeExpr *retType = nullptr;
  Stmt *body = nullptr;
  ELambda(SourceLoc l) : Expr(Lambda, l) {}
  // sema: captured outer locals (by value)
  struct Capture { std::string name; Type *type = nullptr; void *scopeId = nullptr; };
  std::vector<Capture> captures;
  Type *closureType = nullptr;
};
struct MatchArm {
  Pattern *pattern = nullptr;
  Stmt *body = nullptr; // SBlock
};
struct EUnsafeExpr : Expr {
  std::vector<Stmt *> stmts;
  EUnsafeExpr(SourceLoc l) : Expr(UnsafeExpr, l) {}
};
struct EMatch : Expr {
  Expr *scrutinee = nullptr;
  std::vector<MatchArm> arms;
  EMatch(SourceLoc l) : Expr(Match, l) {}
};
struct ERange : Expr {
  Expr *lo = nullptr, *hi = nullptr;
  bool inclusive = false;
  ERange(SourceLoc l) : Expr(Range, l) {}
};
struct ESizeof : Expr {
  TypeExpr *ty = nullptr;
  ESizeof(SourceLoc l) : Expr(Sizeof, l) {}
};
struct EAlignof : Expr {
  TypeExpr *ty = nullptr;
  EAlignof(SourceLoc l) : Expr(Alignof, l) {}
};

// ----------------------------------------------------------------- patterns --
struct Pattern2 {}; // (reserved; Pattern struct above is used)

// ---------------------------------------------------------------- statements --
struct Stmt {
  enum Kind {
    KExpr, KLet, KReturn, KIf, KWhile, KFor, KForIn, KBreak, KContinue, KSwitch, KBlock, KUnsafe,
  };
  Kind kind;
  SourceLoc loc;
  explicit Stmt(Kind k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Stmt() = default;
};
struct SExpr : Stmt {
  Expr *e = nullptr;
  SExpr(SourceLoc l, Expr *e) : Stmt(KExpr, l), e(e) {}
};
struct SLet : Stmt {
  bool isMut = false;
  bool isConst = false;
  bool isDeclOrAssign = false; // `x = expr`: new variable if x not in scope
  std::string name;
  TypeExpr *type = nullptr; // may be null (inferred)
  Expr *init = nullptr;
  SLet(SourceLoc l) : Stmt(KLet, l) {}
  Type *resolvedType = nullptr;    // declared/assigned type (sema)
  bool isAssignExisting = false;   // `x = e` resolved as assignment to existing var
  void *assignGlobal = nullptr;    // DGlobal* when assigning a global
};
struct SReturn : Stmt {
  Expr *e = nullptr;
  SReturn(SourceLoc l, Expr *e) : Stmt(KReturn, l), e(e) {}
};
struct SIf : Stmt {
  Expr *cond = nullptr;
  Stmt *thenBlock = nullptr; // SBlock
  Stmt *elseBlock = nullptr; // SBlock or SIf (else-if chain)
  SIf(SourceLoc l) : Stmt(KIf, l) {}
};
struct SWhile : Stmt {
  Expr *cond = nullptr;
  Stmt *body = nullptr;
  SWhile(SourceLoc l) : Stmt(KWhile, l) {}
};
struct SFor : Stmt {
  Stmt *init = nullptr;   // SLet or SExpr or null
  Expr *cond = nullptr;
  Stmt *step = nullptr;   // SExpr or null
  Stmt *body = nullptr;
  SFor(SourceLoc l) : Stmt(KFor, l) {}
};
struct SForIn : Stmt {
  bool isMut = false;
  std::string varName;
  Expr *iterable = nullptr; // ERange or array/string expr
  Stmt *body = nullptr;
  SForIn(SourceLoc l) : Stmt(KForIn, l) {}
};
struct SBreak : Stmt { SBreak(SourceLoc l) : Stmt(KBreak, l) {} };
struct SContinue : Stmt { SContinue(SourceLoc l) : Stmt(KContinue, l) {} };
struct SSwitch : Stmt {
  Expr *scrutinee = nullptr;
  struct Case { std::vector<Expr *> values; Stmt *body; };
  std::vector<Case> cases;
  Stmt *defaultBody = nullptr;
  SSwitch(SourceLoc l) : Stmt(KSwitch, l) {}
};
struct SBlock : Stmt {
  std::vector<Stmt *> stmts;
  SBlock(SourceLoc l) : Stmt(KBlock, l) {}
};
struct SUnsafe : Stmt {
  std::vector<Stmt *> stmts;
  SUnsafe(SourceLoc l) : Stmt(KUnsafe, l) {}
};

// ---------------------------------------------------------------- declarations --
// Attribute: @name or @name("value")
struct Attr {
  std::string name;
  std::string value;
  SourceLoc loc;
};

struct Decl {
  enum Kind {
    Func, Struct, Class, Interface, Trait, Enum, Global, Const, Import, Extern,
  };
  Kind kind;
  SourceLoc loc;
  explicit Decl(Kind k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Decl() = default;
};

struct Param {
  std::string name;
  TypeExpr *type = nullptr;
  Expr *defVal = nullptr;
  SourceLoc loc;
};

// declaration modifier bits used by the parser
enum DeclMod {
  MOD_PUB = 1, MOD_STATIC = 2, MOD_VIRTUAL = 4, MOD_OVERRIDE = 8, MOD_ABSTRACT = 16,
  MOD_UNSAFE = 32, MOD_TLS = 64, MOD_MUT = 128,
};

struct DFunc : Decl {
  std::string name;
  std::vector<std::string> genericParams;
  std::vector<Param> params;
  TypeExpr *retType = nullptr; // null => void
  Stmt *body = nullptr;        // SBlock; null for extern/interface-prototypes/abstract
  bool isPub = false, isExtern = false, isStatic = false, isVirtual = false,
       isOverride = false, isAbstract = false, isUnsafe = false, isVariadic = false;
  std::string linkName;     // @link_name
  bool checked = false;             // sema already checked this body
  Decl *parent = nullptr;   // enclosing struct/class/enum when method
  // Sema-resolved: interface default methods etc.
  DFunc(SourceLoc l) : Decl(Func, l) {}
};
struct Field {
  std::string name;
  TypeExpr *type = nullptr;
  Expr *defVal = nullptr; // field default value (optional)
  SourceLoc loc;
};
struct DStruct : Decl {
  std::string name;
  std::vector<std::string> genericParams;
  std::vector<Field> fields;
  std::vector<DFunc *> methods;
  bool isPacked = false;
  bool isPub = false;
  DStruct(SourceLoc l) : Decl(Struct, l) {}
};
struct DClass : Decl {
  std::string name;
  TypeExpr *base = nullptr;                    // super class
  std::vector<TypeExpr *> interfaces;          // implemented interfaces/traits
  std::vector<std::string> genericParams;
  std::vector<Field> fields;
  std::vector<DFunc *> methods;
  bool isPub = false;
  bool isAbstract = false;
  DClass(SourceLoc l) : Decl(Class, l) {}
};
struct DInterface : Decl {
  std::string name;
  std::vector<std::string> genericParams;
  std::vector<DFunc *> methods; // may have default bodies (traits)
  bool isPub = false;
  bool isTrait = false;
  DInterface(SourceLoc l, bool trait) : Decl(trait ? Trait : Interface, l), isTrait(trait) {}
};
struct EnumVariant {
  std::string name;
  std::vector<TypeExpr *> payloadTypes;
  SourceLoc loc;
};
struct DEnum : Decl {
  std::string name;
  std::vector<std::string> genericParams;
  std::vector<EnumVariant> variants;
  bool isPub = false;
  DEnum(SourceLoc l) : Decl(Enum, l) {}
};
struct DGlobal : Decl {
  std::string name;
  TypeExpr *type = nullptr;
  Expr *init = nullptr; // may be null => zero init
  bool isMut = false, isTLS = false, isPub = false;
  void *semaType = nullptr; // inferred type for the typeless `g = init` form
  DGlobal(SourceLoc l) : Decl(Global, l) {}
};
struct DConst : Decl {
  std::string name;
  TypeExpr *type = nullptr;
  Expr *init = nullptr;
  bool isPub = false;
  void *folded = nullptr; // codegen: cached constant
  DConst(SourceLoc l) : Decl(Const, l) {}
};
struct DImport : Decl {
  std::vector<std::string> parts;
  std::string alias; // may be empty
  DImport(SourceLoc l) : Decl(Import, l) {}
};
struct DExtern : Decl {
  DFunc *proto = nullptr;
  DExtern(SourceLoc l) : Decl(Extern, l) {}
};

// one parsed .cr file
struct SourceUnit {
  unsigned fileID = 0;
  std::string path;
  std::vector<Decl *> decls;
};

// ------------------------------------------------------------------- arena --
class ASTContext {
public:
  template <typename T, typename... Args> T *make(SourceLoc loc, Args &&...args) {
    void *mem = alloc(sizeof(T));
    T *n = new (mem) T(loc, std::forward<Args>(args)...);
    return n;
  }
  template <typename T> T *makeNoLoc() {
    void *mem = alloc(sizeof(T));
    return new (mem) T();
  }

private:
  std::deque<std::unique_ptr<char[]>> pool;
  static constexpr size_t CHUNK = 1 << 16;
  size_t used = 0, cap = 0;
  char *cur = nullptr;
  void *alloc(size_t n) {
    n = (n + 15) & ~size_t(15);
    if (cur == nullptr || used + n > cap) {
      cap = std::max(CHUNK, n * 2);
      pool.emplace_back(new char[cap]);
      cur = pool.back().get();
      used = 0;
    }
    void *p = cur + used;
    used += n;
    return p;
  }
};

} // namespace core
#endif
