// Core compiler - LLVM IR generation.
// Consumes sema-annotated AST and produces LLVM IR for the whole program
// (one llvm::Module; every reachable Core file contributes its code).
#ifndef CORE_CODEGEN_H
#define CORE_CODEGEN_H

#include "Sema.h"
#include "Type.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

namespace core {

struct CodegenOptions {
  int optLevel = 0;            // 0..3
  bool sizeOpt = false;        // -Os
  bool debugInfo = false;      // --debug / -g
  std::string targetTriple;    // empty = host
  bool freestanding = false;
  std::string entryName = "main";
};

class Codegen {
public:
  Codegen(Sema &sema, TypeContext &tc, Diagnostics &diag, const CodegenOptions &opts);
  // Generate IR for all modules. Returns false on internal failure.
  bool generate(llvm::Module &module, ModuleSema *entryModule);
  llvm::LLVMContext &llvmContext() { return ctx; }

private:
  Sema &sema;
  TypeContext &tc;
  Diagnostics &diag;
  CodegenOptions opts;
  llvm::LLVMContext ctx;
  llvm::IRBuilder<> builder;
  llvm::Module *mod = nullptr;

  // per-function state
  llvm::Function *fn = nullptr;
  DFunc *curFuncDecl = nullptr;
  ModuleSema *curModule = nullptr;
  std::map<std::string, llvm::Value *> localSlots; // local var -> alloca
  std::vector<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>> breakStack;
  std::vector<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>> continueStack;
  llvm::DIBuilder *dbg = nullptr;
  llvm::DISubprogram *curSP = nullptr;
  bool dbgEnabled = false;
  std::map<unsigned, void *> cuMapRaw; // fileID -> llvm::DIFile*
  std::map<unsigned, void *> &debugCUs() { return cuMapRaw; }

  // ---- types ----
  llvm::Type *llvmType(Type *t);
  llvm::StructType *structTypeFor(Type *t);       // struct/class aggregate
  llvm::StructType *stringType();
  llvm::StructType *stringTy = nullptr;
  llvm::Type *enumStorageType(Type *t, unsigned *payloadOffset, unsigned *totalSize, unsigned *alignOut);
  unsigned fieldGEPIndex(Type *aggTy, int memberIndex); // incl. vptr offset

  // ---- decls ----
  llvm::Function *declareFunc(DFunc *f, const std::vector<Type *> &genericArgs);
  std::string funcSymbol(DFunc *f, const std::vector<Type *> &genericArgs);
  void emitFuncBody(DFunc *f, const std::vector<Type *> &genericArgs);
  llvm::GlobalVariable *globalFor(DGlobal *g);
  llvm::Value *vtableFor(DClass *c);
  llvm::Value *itableFor(DClass *c, DInterface *iface);
  llvm::Value *armResultSlot = nullptr; // match-arm value target
  llvm::Value *makeClosureForFunction(DFunc *f);
  llvm::Value *makeClosureValue(llvm::Function *fnPtr, llvm::Value *env);
  llvm::Constant *makeStringConst(const std::string &bytes);
  llvm::Value *emitBuiltinCall(ECall *c, Builtin b, const std::string &name);
  llvm::Value *emitVariantCtor(ECall *c);
  llvm::Value *emitSelfArg(Expr *obj);
  void emitVptrStoreIfInit(DFunc *f);
  bool enumHasPayloads(DEnum *e);
  llvm::Value *emitVariantValue(DEnum *en, unsigned tag, Type *enumTy);
public:
  llvm::Type *llvmTypeFor(Type *t) { return llvmType(t); }

private:
  llvm::CallInst *ccall(llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value *> args,
                        const std::string &name);
  llvm::CallInst *ccall(llvm::FunctionType *fty, llvm::Value *callee,
                        llvm::ArrayRef<llvm::Value *> args, const std::string &name);
  std::vector<llvm::Value *> emitCallArgs(DFunc *f, ECall *c);

  // ---- expressions ----
  llvm::Value *emitExpr(Expr *e);                 // rvalue
  llvm::Value *emitLValue(Expr *e);               // address
  llvm::Value *emitBinary(EBinary *b);
  llvm::Value *emitCall(ECall *c);
  llvm::Value *emitCast(ECast *c);
  llvm::Value *emitLambda(ELambda *lam);
  llvm::Value *emitStructLit(EStructLit *sl);
  llvm::Value *emitArrayLit(EArrayLit *al);
  llvm::Value *emitMatch(EMatch *m);
  llvm::Constant *evalConst(Expr *e);             // for global initializers
  llvm::Value *emitDefaultValue(Type *t);

  // ---- statements ----
  void emitStmt(Stmt *s);
  void emitBlock(Stmt *block);

  // ---- runtime helpers ----
  llvm::FunctionCallee rtFunc(const std::string &name, llvm::Type *ret,
                              std::vector<llvm::Type *> params, bool varargs = false);
  void emitBoundsCheck(llvm::Value *idx, long long len, SourceLoc loc);
  void emitPanic(const std::string &msg, SourceLoc loc);

  // debug info
  void emitDebugLoc(SourceLoc loc);
  llvm::DIScope *curDebugScope();
};

} // namespace core
#endif
