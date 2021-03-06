// Copyright (c) 2012, Rasmus Andersson. All rights reserved. Use of this source
// code is governed by a MIT-style license that can be found in the LICENSE file.
#include "_VisitorImplHeader.h"

void Visitor::dumpBlockSymbols() {
  if (blockStack_.empty()) return;
  const char *indent = "                                                                        ";
  BlockStack::const_iterator it = blockStack_.begin();
  int L = 1;
  std::cerr << "{" << std::endl;
  
  for (; it != blockStack_.end(); ++it, ++L) {
    BlockScope *bs = *it;
    BasicBlock *bb = bs->block();
    std::string ind(indent, std::min<int>(L*2, strlen(indent)));
    llvm::ValueSymbolTable* symbols = bb->getValueSymbolTable();
    llvm::ValueSymbolTable::const_iterator it2 = symbols->begin();
    
    for (; it2 != symbols->end(); ++it2) {
      std::cerr << ind << it2->getKey().str() << ": ";
    
      llvm::Value *V = it2->getValue();
      llvm::Type *T = V->getType();
      if (T->isLabelTy()) {
        std::cerr << "label" << std::endl;
      } else {
        if (T->isPointerTy()) std::cerr << "pointer ";
        V->dump();
      }
    }
    
    if (L-1 != blockStack_.size()-1) {
      std::cerr << ind << "{" << std::endl;
    }
  }
  
  while (--L) {
    std::string ind(indent, std::min<int>((L-1)*2, strlen(indent)));
    std::cerr << ind << "}" << std::endl;
  }
}


// Returns a module-global uniqe mangled name rooted in *name*
std::string Visitor::uniqueMangledName(const Text& name) const {
  std::string mname;
  int i = 0;
  while (1) {
    // Take the LHS var name and use it for the function name
    if (i > 0) {
      char buf[12]; snprintf(buf, 12, "$$%d", i);
      mname = mangledName(name + buf);
    } else {
      mname = mangledName(name);
    }
    if (module_->getNamedValue(mname) == 0 && module_->getNamedGlobal(mname) == 0) {
      // We got a unique name
      break;
    }
    ++i;
  }
  return mname;
}


bool Visitor::IRTypesForASTVariables(std::vector<Type*>& argSpec, ast::VariableList *argVars) {
  if (argVars != 0 && argVars->size() != 0) {
    argSpec.reserve(argVars->size());
    ast::VariableList::const_iterator it = argVars->begin();
    
    for (; it != argVars->end(); ++it) {
      ast::Variable* var = *it;
      
      // Type should be inferred?
      if (var->hasUnknownType()) {
        // TODO: runtime type inference support.
        return !!error("NOT IMPLEMENTED: runtime type inference of func arguments");
      }
      
      // Lookup IR type for AST type
      Type* T = IRTypeForASTType(var->type());
      if (T == 0)
        return !!error(R_FMT("No conversion for AST type " << var->toString() << " to IR type"));
      
      // Use T
      argSpec.push_back(T);
    }
  }
  
  return true;
}


const char* Visitor::typeName(const Type* T) const {
  if (T == 0) return "<null>";
  // Type::TypeID from Type.h:
  switch(T->getTypeID()) {
    case Type::VoidTyID: return "void";    ///<  0: type with no size
    case Type::FloatTyID: return "float";       ///<  1: 32-bit floating point type
    case Type::DoubleTyID: return "double";      ///<  2: 64-bit floating point type
    case Type::X86_FP80TyID: return "fp80";    ///<  3: 80-bit floating point type (X87)
    case Type::FP128TyID: return "fp128-m112";       ///<  4: 128-bit floating point type (112-bit mantissa)
    case Type::PPC_FP128TyID: return "fp64x2";   ///<  5: 128-bit floating point type (two 64-bits, PowerPC)
    case Type::LabelTyID: return "label";       ///<  6: Labels
    case Type::MetadataTyID: return "metadata";    ///<  7: Metadata
    case Type::X86_MMXTyID: return "mmxvec";     ///<  8: MMX vectors (64 bits, X86 specific)

    // Derived types... see DerivedTypes.h file.
    case Type::IntegerTyID: return "integer";     ///<  9: Arbitrary bit width integers
    case Type::FunctionTyID: return "function";    ///< 10: Functions
    case Type::StructTyID: return "struct";      ///< 11: Structures
    case Type::ArrayTyID: return "array";       ///< 12: Arrays
    case Type::PointerTyID: return "pointer";     ///< 13: Pointers
    case Type::VectorTyID: return "vector";      ///< 14: SIMD 'packed' format, or other vector type
    default: return "?";
  }
}


StructType* Visitor::getArrayStructType(Type* elementType) {
  // { length i64, data elementType* }
  StructType *T = arrayStructTypes_[elementType];
  if (T == 0) {
    Type *tv[2];
    tv[0] = Type::getInt64Ty(getGlobalContext()); // i64
    //tv[1] = PointerType::get(Type::getInt8Ty(getGlobalContext()), 0); // i8*
    tv[1] = PointerType::get(elementType, 0); // i8*
    ArrayRef<Type*> elementTypes = makeArrayRef(tv, 2);
    T = StructType::get(getGlobalContext(), elementTypes, /*isPacked = */true);
    arrayStructTypes_[elementType] = T;
  }
  return T;
}


GlobalVariable* Visitor::createPrivateConstantGlobal(Constant* constantV, const Twine &name) {
  GlobalVariable *gV = new GlobalVariable(
    *module_, // Module M
    constantV->getType(), // Type* Ty
    true, // bool isConstant
    GlobalValue::PrivateLinkage, // LinkageTypes Linkage
    constantV, // Constant *Initializer
    name // const Twine &Name
    // GlobalVariable *InsertBefore = 0
    // bool ThreadLocal = false
    // unsigned AddressSpace = 0
    );

  gV->setName(name);
  gV->setUnnamedAddr(true);
  gV->setAlignment(1);

  return gV;
}


GlobalVariable* Visitor::createStruct(Constant** constants, size_t count, const Twine &name) {
  // Get or create an anonymous struct for constants
  Constant* arrayStV = ConstantStruct::getAnon(makeArrayRef(constants, count), true);
  //rlog("arrayStV: "); arrayStV->dump();
  
  // Put the struct into global variable so we can pass it around
  GlobalVariable* gArrayStV = createPrivateConstantGlobal(arrayStV, name);
  //rlog("gArrayStV: "); gArrayStV->dump();

  return gArrayStV;
}


GlobalVariable* Visitor::createArray(Constant* constantArray, const Twine &name) {
  assert(ConstantAggregateZero::classof(constantArray) || ConstantArray::classof(constantArray));
  
  // Create our struct: <{ i64 N, [i8 x N] }>
  Constant* stV[2];
  uint64_t length = static_cast<ArrayType*>(constantArray->getType())->getNumElements();
  stV[0] = ConstantInt::get(getGlobalContext(), APInt(64, length, false));
  stV[1] = constantArray;
  //stV[1] = createPrivateConstantGlobal(constantArray, "gv");
  
  return createStruct(stV, 2, name);
}


// ----------- trivial generators ------------


llvm::Module *Visitor::genModule(llvm::LLVMContext& context, const Text moduleName, const ast::Function *root) {
  DEBUG_TRACE_LLVM_VISITOR;
  llvm::Module *module = new Module(moduleName.UTF8String(), context);
  
  module_ = module;
  llvm::Value *returnValue = llvm::ConstantInt::get(llvm::getGlobalContext(), APInt(64, 0, true));
  //llvm::Value *moduleFunc = codegenFunction(root, std::string("minit__") + moduleName, builder_.getInt64Ty());
  llvm::Value *moduleFunc = codegenFunction(root, "main", returnValue->getType(), returnValue);
  module_ = 0;
  
  // Failure?
  if (moduleFunc == 0) {
    delete module;
    module = 0;
  }
  
  return module;
}

// ExternalFunction
Value *Visitor::codegenExternalFunction(const ast::ExternalFunction* node) {
  DEBUG_TRACE_LLVM_VISITOR;
  
  // Figure out return type (unless it's been overridden by returnType) if
  // the interface declares the return type.
  Type* returnType = returnTypeForFunctionType(node->functionType());
  if (returnType == 0) return error("Unable to transcode return type from AST to IR");
  
  return codegenFunctionType(node->functionType(), node->name().UTF8String(), returnType);
}

// Block
Value *Visitor::codegenBlock(const ast::Block *block) {
  DEBUG_TRACE_LLVM_VISITOR;
  
  const ast::NodeList& nodes = block->nodes();
  ast::NodeList::const_iterator it1 = nodes.begin();
  Value *lastValue = 0;
  for (; it1 < nodes.end(); it1++) {
    lastValue = codegen(*it1);
    if (lastValue == 0) return 0;
  }
  
  BasicBlock* BB = builder_.GetInsertBlock();
  rlog("codegen'd block: "); BB->dump();
  
  rlog("block's last value: "); lastValue->dump();
  if (lastValue->getType()->isFunctionTy()) {
    rlog("ZOMG FUNC");
  }
  
  return (lastValue == 0) ? error("Empty block") : lastValue;
}

// Int
Value *Visitor::codegenIntLiteral(const ast::IntLiteral *literal, bool fixedSize) {
  DEBUG_TRACE_LLVM_VISITOR;
  // TODO: Infer the minimal size needed if fixedSize is false
  const unsigned numBits = 64;
  return ConstantInt::get(getGlobalContext(), APInt(numBits, literal->text().UTF8String(), literal->radix()));
}

// Float
Value *Visitor::codegenFloatLiteral(const ast::FloatLiteral *literal, bool fixedSize) {
  DEBUG_TRACE_LLVM_VISITOR;
  // TODO: Infer the minimal size needed if fixedSize is false
  const fltSemantics& size = APFloat::IEEEdouble;
  return ConstantFP::get(getGlobalContext(), APFloat(size, literal->text().UTF8String()));
}

Value *Visitor::codegenBoolLiteral(const ast::BoolLiteral *literal) {
  DEBUG_TRACE_LLVM_VISITOR;
  return literal->isTrue() ? ConstantInt::getTrue(getGlobalContext())
                           : ConstantInt::getFalse(getGlobalContext());
}

// Reference or load a symbol
Value *Visitor::resolveSymbol(const Text& name) {
  DEBUG_TRACE_LLVM_VISITOR;
  
  // Lookup symbol
  const Symbol& symbol = lookupSymbol(name);
  if (symbol.empty())
    return error((std::string("Unknown variable \"") + name.UTF8String() + "\"").c_str());
  
  // Value must be either a global or in the same scope
  assert(blockScope() != 0);
  if (symbol.owningScope != blockScope()) {
    if (!GlobalValue::classof(symbol.value)) {
      // TODO: Future: If we support funcs that capture its environment, that code should
      // likely live here.
      return error((std::string("Unknown variable \"") + name.UTF8String() + "\" (no reachable symbols in scope)").c_str());
    } else {
      rlog("Resolving \"" << name << "\" to a global constant");
    }
  } else {
    if (GlobalValue::classof(symbol.value)) {
      rlog("Resolving \"" << name << "\" to a global & local constant");
    } else {
      rlog("Resolving \"" << name << "\" to a local variable");
    }
  }

  // Load an alloca or return a reference
  if (symbol.isAlloca()) {
    return builder_.CreateLoad(symbol.value, name.c_str());
  } else {
    return symbol.value;
  }
}


Value *Visitor::codegenSymbol(const ast::Symbol *symbolExpr) {
  DEBUG_TRACE_LLVM_VISITOR;
  assert(symbolExpr != 0);
  return resolveSymbol(symbolExpr->name());
}



Visitor::Symbol Visitor::Symbol::Empty;

#include "_VisitorImplFooter.h"
