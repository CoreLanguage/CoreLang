// Core compiler - the type system.
//
// Types are interned in a TypeContext: two expressions have the same type
// exactly when their Type* pointers are equal.
#ifndef CORE_TYPE_H
#define CORE_TYPE_H

#include "AST.h"
#include "Common.h"
#include <map>
#include <string>
#include <vector>

namespace core {

// All primitive types, canonical order. The parser and lexer refer to these
// by name; `primKindByName` maps names to indices.
enum PrimKind {
#define PRIM(name, suffix, bits) PRIM_##name,
#include "Prims.def"
  PRIM_COUNT
};
const char *primName(int kind);          // e.g. "i32"
bool primIsInt(int k);
bool primIsSigned(int k);
bool primIsFloat(int k);
unsigned primBits(int k);                // 8/16/32/64/128/...
int primKindByName(const std::string &s);

struct StructDeclInfo; // resolved struct-like decl (sema)

enum class TypeKind {
  Prim,       // primitive (incl. bool/char/string/void/never/vector types)
  Ptr,        // ptr<T>
  Array,      // [T; N]
  Struct,     // struct type (DStruct)
  Class,      // class type (DClass)
  Interface,  // interface/trait value type (fat pointer) (DInterface)
  Enum,       // enum type (DEnum)
  Func,       // function value type: {fnptr, env} closure pair
  Never,
  Invalid,    // error recovery
};

struct Type {
  TypeKind kind = TypeKind::Invalid;
  // Prim
  int prim = -1;
  // Ptr / Array
  Type *pointee = nullptr;   // Ptr
  Type *elem = nullptr;      // Array
  long long arrayLen = -1;   // Array
  // Aggregate (Struct/Class/Enum)
  void *decl = nullptr;      // DStruct*/DClass*/DEnum*  (sema-resolved decls)
  std::vector<Type *> genericArgs; // instantiated args for generics
  // Interface
  void *ifaceDecl = nullptr; // DInterface*
  // Func
  Type *ret = nullptr;
  std::vector<Type *> params;

  bool isPrim() const { return kind == TypeKind::Prim; }
  bool isPtr() const { return kind == TypeKind::Ptr; }
  bool isArray() const { return kind == TypeKind::Array; }
  bool isStruct() const { return kind == TypeKind::Struct; }
  bool isClass() const { return kind == TypeKind::Class; }
  bool isInterface() const { return kind == TypeKind::Interface; }
  bool isEnum() const { return kind == TypeKind::Enum; }
  bool isFunc() const { return kind == TypeKind::Func; }
  bool isVoid() const { return kind == TypeKind::Prim && prim == PRIM_void; }
  bool isNever() const { return kind == TypeKind::Never; }
  bool isString() const { return kind == TypeKind::Prim && prim == PRIM_string; }
  bool isBool() const { return kind == TypeKind::Prim && prim == PRIM_bool; }
  bool isChar() const { return kind == TypeKind::Prim && prim == PRIM_char; }
  bool isInt() const { return isPrim() && primIsInt(prim); }
  bool isFloat() const { return isPrim() && primIsFloat(prim); }
  bool isVector() const {
    if (!isPrim()) return false;
    int p = prim;
    return p >= PRIM_f32x4 && p <= PRIM_u64x2;
  }
  bool isIntegerLike() const { return isInt() || isChar(); }
  bool isNumeric() const { return isInt() || isFloat() || isChar(); }
  bool isBoolLike() const { return isBool() || isEnum(); }
  // aggregate value type assigned by copying bytes
  bool isAggregate() const {
    return isStruct() || isClass() || isArray() || isInterface() || isEnum();
  }
  bool isGenericVar() const { return isNamedGeneric; }
  bool isNamedGeneric = false; // a standing generic parameter (during template checks)
  std::string genericVarName;
};

// Pretty name for diagnostics, e.g. `ptr<i32>`, `[i32; 5]`.
std::string typeToString(Type *t);

class TypeContext {
public:
  TypeContext();
  Type *prim(int k);
  Type *ptr(Type *pointee);
  Type *array(Type *elem, long long len);
  Type *getStruct(void *decl, std::vector<Type *> args);
  Type *getClass(void *decl, std::vector<Type *> args);
  Type *getEnum(void *decl, std::vector<Type *> args);
  Type *getInterface(void *decl);
  Type *func(Type *ret, std::vector<Type *> params);
  Type *invalid() { return &invalidType; }
  Type *never() { return &neverType; }
  Type *genericVar(const std::string &name);

  // structural equality (pointer equality works for interned types, but
  // generic variables compare by name)
  bool same(Type *a, Type *b);

private:
  std::map<std::string, Type *> interned;
  Type invalidType, neverType;
  Type &intern(const std::string &key, Type t);
};

} // namespace core
#endif
