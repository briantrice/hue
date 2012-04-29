#ifndef RSMS_CODEGEN_LLVM_VISITOR_H
#define RSMS_CODEGEN_LLVM_VISITOR_H

#define DEBUG_LLVM_VISITOR 1
#if DEBUG_LLVM_VISITOR
  #include "../DebugTrace.h"
  #define DEBUG_TRACE_LLVM_VISITOR DEBUG_TRACE
#else
  #define DEBUG_TRACE_LLVM_VISITOR do{}while(0)
#endif

#include "../ast/Node.h"
#include "../ast/Expression.h"
#include "../ast/Function.h"
#include "../ast/Block.h"
#include "../ast/TypeDeclaration.h"
#include "../ast/Variable.h"

#include <stdlib.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <tr1/unordered_map> // <map>
#include <deque>

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace rsms { namespace codegen {

class Visitor {
  class BlockScope;

  // Represents a named symbol (or "alias")
  class Symbol {
  public:
    static Symbol Empty;
    llvm::Value *value;
    bool isMutable;
    BlockScope *owningScope;
    inline bool isAlloca() const { return value ? llvm::AllocaInst::classof(value) : false; };
    inline bool empty() const { return value == 0; }
    Symbol() : value(0), isMutable(true), owningScope(0) {}
    Symbol(llvm::Value *V, bool M = true, BlockScope* S = 0) : value(V), isMutable(M), owningScope() {}
  };
  
  // Iterable stack of block scopes
  typedef std::deque<BlockScope*> BlockStack;
  
  // Represents a scope of named symbols.
  class BlockScope {
  public:
    //typedef std::map<std::string,Symbol> SymbolMap;
    typedef std::tr1::unordered_map<std::string,Symbol> SymbolMap;
    
    BlockScope(Visitor& visitor, llvm::BasicBlock *block) : visitor_(visitor), block_(block) {
      visitor_.blockStack_.push_back(this);
      visitor_.builder_.SetInsertPoint(block);
    }
    ~BlockScope() {
      visitor_.blockStack_.pop_back();
      if (visitor_.blockStack_.empty()) {
        visitor_.builder_.ClearInsertionPoint();
      } else {
        visitor_.builder_.SetInsertPoint(visitor_.blockStack_.back()->block());
      }
    }
    
    inline llvm::BasicBlock *block() const { return block_; }
    inline const SymbolMap& symbols() const { return symbols_; }
    
    void setSymbol(const std::string& name, llvm::Value *V, bool isMutable = true) {
      Symbol& symbol = symbols_[name];
      symbol.value = V;
      symbol.isMutable = isMutable;
      symbol.owningScope = this;
    }
    
    // Look up a symbol only in this scope.
    // Use Visitor::lookupSymbol to lookup stuff in any scope
    const Symbol& lookupSymbol(const std::string& name, bool deep = true) const {
      SymbolMap::const_iterator it = symbols_.find(name);
      if (it != symbols_.end()) return it->second;
      return Symbol::Empty;
    }
    
  private:
    Visitor& visitor_;
    llvm::BasicBlock *block_;
    SymbolMap symbols_;
  };
  
public:
  Visitor() : module_(NULL), builder_(llvm::getGlobalContext()) {}
  
  // Register an error
  llvm::Value *error(const std::string& str) {
    std::ostringstream ss;
    ss << str;
    errors_.push_back(ss.str());
    std::cerr << "\e[31;1m" << ss.str() << "\e[0m" << std::endl;
    return 0;
  }
  
  // Register a warning
  llvm::Value *warning(const std::string& str) {
    std::ostringstream ss;
    ss << str;
    warnings_.push_back(ss.str());
    std::cerr << "\e[35;1mWarning:\e[0m " << ss.str() << std::endl;
    return 0;
  }
  
  inline const std::vector<std::string>& errors() const { return errors_; };
  inline const std::vector<std::string>& warnings() const { return warnings_; };
  
  // Generate code for a module rooting at *root*
  llvm::Module *genModule(llvm::LLVMContext& context, std::string moduleName, const ast::Function *root);
  
  
protected:
  // Current block scope, or 0 if none
  inline BlockScope* blockScope() const { return blockStack_.empty() ? 0 : blockStack_.back(); }
  
  // Current block, or 0 if none
  inline llvm::BasicBlock* block() const { return builder_.GetInsertBlock(); }
  
  const Symbol& lookupSymbol(const std::string& name, bool deep = true) const {
    // Scan symbol maps starting at top of stack moving down
    BlockStack::const_reverse_iterator bsit = blockStack_.rbegin();
    for (; bsit != blockStack_.rend(); ++bsit) {
      BlockScope* bs = *bsit;
      BlockScope::SymbolMap::const_iterator it = bs->symbols().find(name);
      if (it != bs->symbols().end()) {
        return it->second;
      }
    }
    return Symbol::Empty;
  }
  
  llvm::Value *resolveSymbol(const std::string& name);
  
  // Dump all symbols in the current block stack to stderr
  void dumpBlockSymbols();
  
  
  llvm::Type* returnTypeForFunctionInterface(const ast::FunctionInterface *node) {
    // Find out the return type of the function
    ast::TypeDeclarationList *returnTypes = node->returnTypes();
    if (returnTypes == 0 || returnTypes->size() == 0 /*TODO: || size==1 && type==TNull */) {
      return builder_.getVoidTy();
    } else {
      assert(returnTypes->size() == 1); // TODO: Support multiple return values
      ast::TypeDeclaration *astType = (*returnTypes)[0];
      switch (astType->type) {
        case ast::TypeDeclaration::Int: return builder_.getInt64Ty();
        case ast::TypeDeclaration::Float: return builder_.getDoubleTy();
        //case ast::TypeDeclaration::Func: return builder_.getDoubleTy();
        //case ast::TypeDeclaration::Named: return 1;
        default: return 0;
      }
    }
  }
  
  llvm::Type *IRTypeForASTTypeDecl(const ast::TypeDeclaration& typeDecl) {
    switch (typeDecl.type) {
      case ast::TypeDeclaration::Int: return builder_.getInt64Ty();
      case ast::TypeDeclaration::Float: return builder_.getDoubleTy();
      // case ast::TypeDeclaration::Named: -- Custom type
      default: return 0;
    }
  }
  
  llvm::Value* createNewLocalSymbol(ast::Variable *variable, llvm::Value *rhsV);
  
  inline std::string mangledName(const std::string& localName) {
    return module_->getModuleIdentifier() + "$" + localName;
  }
  // Returns a module-global uniqe mangled name rooted in *name*
  std::string uniqueMangledName(const std::string& name);
  
  // Create an alloca of type T
  llvm::AllocaInst *createAlloca(llvm::Type* T, const std::string& name) {
    llvm::IRBuilder<> TmpB(block(), block()->begin());
    return TmpB.CreateAlloca(T, 0, name);
  }
  
  // Create an alloca of type V->getType and store V into that alloca
  llvm::AllocaInst *createAllocaAndStoreValue(llvm::Value* V, const std::string& name, llvm::StoreInst** storeInst = 0) {
    llvm::AllocaInst *allocaInst = createAlloca(V->getType(), name);
    if (allocaInst) {
      llvm::StoreInst* si = builder_.CreateStore(V, allocaInst);
      if (storeInst != 0) *storeInst = si;
    }
    return allocaInst;
  }
  
  // ------------------------------------------------
  
  // Emit LLVM IR for this AST node along with all the things it depends on.
  // "Value" is the class used to represent a
  // "Static Single Assignment (SSA) register" or "SSA value" in LLVM.
  llvm::Value *codegen(const ast::Node *node) {
    DEBUG_TRACE_LLVM_VISITOR;
    //std::cout << "Node [" << node->type << "]" << std::endl;
    switch (node->type) {
      case ast::Node::TSymbolExpression:        return codegenSymbolExpression((ast::SymbolExpression*)node);
      case ast::Node::TBinaryExpression:        return codegenBinaryExpression((ast::BinaryExpression*)node);
      case ast::Node::TFunction:                return codegenFunction        ((ast::Function*)node);
      case ast::Node::TIntLiteralExpression:    return codegenIntLiteral      ((ast::IntLiteralExpression*)node);
      case ast::Node::TFloatLiteralExpression:  return codegenFloatLiteral    ((ast::FloatLiteralExpression*)node);
      case ast::Node::TAssignmentExpression:    return codegenAssignment      ((ast::AssignmentExpression*)node);
      case ast::Node::TExternalFunction:        return codegenExternalFunction((ast::ExternalFunction*)node);
      case ast::Node::TCallExpression:          return codegenCallExpression  ((ast::CallExpression*)node);
      default: return error("Unable to generate code for node");
    }
  }
  
  llvm::Function *codegenFunctionInterface(const ast::FunctionInterface *node,
                                           std::string name = "",
                                           llvm::Type *returnType = 0);
  
  llvm::Value *codegenExternalFunction(const ast::ExternalFunction* node);
  
  llvm::Value *codegenFunction(const ast::Function *node,
                               std::string name = "",
                               llvm::Type* returnType = 0,
                               llvm::Value* returnValue = 0);
  
  llvm::Value *codegenBlock(const ast::Block *block, llvm::BasicBlock *BB);
  
  llvm::Value *codegenAssignment(const ast::AssignmentExpression* node);
  
  llvm::Value *codegenCallExpression(const ast::CallExpression* node);
  
  llvm::Value *codegenIntLiteral(const ast::IntLiteralExpression *intLiteral, bool fixedSize = true);
  llvm::Value *codegenFloatLiteral(const ast::FloatLiteralExpression *floatLiteral, bool fixedSize = true);
  
  llvm::Value *codegenBinaryExpression(ast::BinaryExpression *binExpr);
  
  llvm::Value *codegenSymbolExpression(ast::SymbolExpression *symbolExpr);


private:
  std::vector<std::string> errors_;
  std::vector<std::string> warnings_;
  llvm::Module* module_;
  llvm::IRBuilder<> builder_;
  BlockStack blockStack_;
};

}} // namespace rsms::codegen
#endif  // RSMS_CODEGEN_LLVM_VISITOR_H