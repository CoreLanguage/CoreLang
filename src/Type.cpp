#include "Type.h"
#include <cassert>

namespace core {

const char *primName(int kind) {
  switch (kind) {
#define PRIM(name, suffix, bits) case PRIM_##name: return #name;
#include "Prims.def"
  default: return "?";
  }
}
bool primIsInt(int k) {
  return (k >= PRIM_i8 && k <= PRIM_u128) || k == PRIM_usize || k == PRIM_isize ||
         k == PRIM_char;
}
bool primIsSigned(int k) {
  switch (k) {
  case PRIM_i8: case PRIM_i16: case PRIM_i32: case PRIM_i64: case PRIM_i128:
  case PRIM_isize:
  case PRIM_f32: case PRIM_f64:
  case PRIM_i8x16: case PRIM_i16x8: case PRIM_i32x4: case PRIM_i64x2:
    return true;
  default:
    return false;
  }
}
bool primIsFloat(int k) { return k == PRIM_f32 || k == PRIM_f64 || k == PRIM_f32x4 || k == PRIM_f64x2; }
unsigned primBits(int k) {
  switch (k) {
  case PRIM_bool: return 1;
  case PRIM_char: case PRIM_i8: case PRIM_u8: return 8;
  case PRIM_i16: case PRIM_u16: return 16;
  case PRIM_i32: case PRIM_u32: case PRIM_f32: return 32;
  case PRIM_i64: case PRIM_u64: case PRIM_f64: case PRIM_usize: case PRIM_isize: return 64;
  case PRIM_i128: case PRIM_u128: return 128;
  case PRIM_f32x4: case PRIM_f64x2: case PRIM_i32x4: case PRIM_i64x2:
  case PRIM_i8x16: case PRIM_i16x8: case PRIM_u8x16: case PRIM_u16x8:
  case PRIM_u32x4: case PRIM_u64x2:
    return 128;
  default: return 0;
  }
}
int primKindByName(const std::string &s) {
#define PRIM(name, suffix, bits) if (s == #name) return PRIM_##name;
#include "Prims.def"
  return -1;
}

std::string typeToString(Type *t) {
  if (!t) return "<null>";
  switch (t->kind) {
  case TypeKind::Prim: return primName(t->prim);
  case TypeKind::Never: return "never";
  case TypeKind::Invalid: return "<error>";
  case TypeKind::Ptr: return "ptr<" + typeToString(t->pointee) + ">";
  case TypeKind::Array:
    return "[" + typeToString(t->elem) + "; " + std::to_string(t->arrayLen) + "]";
  case TypeKind::Func: {
    std::string s = "func(";
    for (size_t i = 0; i < t->params.size(); i++) {
      if (i) s += ", ";
      s += typeToString(t->params[i]);
    }
    s += ")";
    if (t->ret && !t->ret->isVoid()) s += " -> " + typeToString(t->ret);
    return s;
  }
  case TypeKind::Struct: {
    std::string s = t->genericVarName.empty() ? "struct" : t->genericVarName;
    return s;
  }
  default: return "<type>";
  }
}

TypeContext::TypeContext() { invalidType.kind = TypeKind::Invalid; neverType.kind = TypeKind::Never; }

Type &TypeContext::intern(const std::string &key, Type t) {
  auto it = interned.find(key);
  if (it != interned.end()) return *it->second;
  Type *stored = new Type(t);
  interned[key] = stored;
  return *stored;
}

Type *TypeContext::prim(int k) {
  return &intern(strfmt("prim:%d", k), [&] { Type t; t.kind = TypeKind::Prim; t.prim = k; return t; }());
}
Type *TypeContext::ptr(Type *pointee) {
  return &intern(strfmt("ptr:%p", (void *)pointee), [&] {
    Type t; t.kind = TypeKind::Ptr; t.pointee = pointee; return t;
  }());
}
Type *TypeContext::array(Type *elem, long long len) {
  return &intern(strfmt("arr:%p:%lld", (void *)elem, len), [&] {
    Type t; t.kind = TypeKind::Array; t.elem = elem; t.arrayLen = len; return t;
  }());
}
Type *TypeContext::getStruct(void *decl, std::vector<Type *> args) {
  std::string key = strfmt("struct:%p", decl);
  for (auto *a : args) key += strfmt(":%p", (void *)a);
  return &intern(key, [&] {
    Type t; t.kind = TypeKind::Struct; t.decl = decl; t.genericArgs = args; return t;
  }());
}
Type *TypeContext::getClass(void *decl, std::vector<Type *> args) {
  std::string key = strfmt("class:%p", decl);
  for (auto *a : args) key += strfmt(":%p", (void *)a);
  return &intern(key, [&] {
    Type t; t.kind = TypeKind::Class; t.decl = decl; t.genericArgs = args; return t;
  }());
}
Type *TypeContext::getEnum(void *decl, std::vector<Type *> args) {
  std::string key = strfmt("enum:%p", decl);
  for (auto *a : args) key += strfmt(":%p", (void *)a);
  return &intern(key, [&] {
    Type t; t.kind = TypeKind::Enum; t.decl = decl; t.genericArgs = args; return t;
  }());
}
Type *TypeContext::getInterface(void *decl) {
  return &intern(strfmt("iface:%p", decl), [&] {
    Type t; t.kind = TypeKind::Interface; t.ifaceDecl = decl; return t;
  }());
}
Type *TypeContext::func(Type *ret, std::vector<Type *> params) {
  std::string key = strfmt("fn:%p", (void *)ret);
  for (auto *p : params) key += strfmt(":%p", (void *)p);
  return &intern(key, [&] {
    Type t; t.kind = TypeKind::Func; t.ret = ret; t.params = params; return t;
  }());
}
Type *TypeContext::genericVar(const std::string &name) {
  Type *t = &intern("genvar:" + name, [&] {
    Type t; t.kind = TypeKind::Invalid; t.isNamedGeneric = true; t.genericVarName = name; return t;
  }());
  return t;
}
bool TypeContext::same(Type *a, Type *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a->isNamedGeneric || b->isNamedGeneric) {
    return a->isNamedGeneric && b->isNamedGeneric && a->genericVarName == b->genericVarName;
  }
  return false;
}

} // namespace core
