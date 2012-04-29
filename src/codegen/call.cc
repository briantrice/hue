#include "_VisitorImplHeader.h"

// Returns true if V can be used as the target in a call instruction
static FunctionType* functionTypeForValue(Value* V) {
  if (V == 0) return 0;
  
  Type* T = V->getType();
  
  if (T->isFunctionTy()) {
    return static_cast<FunctionType*>(T);
  } else if (   T->isPointerTy()
             && T->getNumContainedTypes() == 1
             && T->getContainedType(0)->isFunctionTy() ) {
    return static_cast<FunctionType*>(T->getContainedType(0));
  } else {
    return 0;
  }
}

// Returns true if V can be used as the target in a call instruction
inline static bool valueIsCallable(Value* V) {
  return !!functionTypeForValue(V);
  //if (V == 0) return 0;
  //
  //Type* T = V->getType();
  //
  //if (T->isFunctionTy()) {
  //  return true;
  //} else if (   T->isPointerTy()
  //           && T->getNumContainedTypes() == 1
  //           && T->getContainedType(0)->isFunctionTy() ) {
  //  return true;
  //} else {
  //  return false;
  //}
}



static std::ostringstream* _r_fmt_begin() {
  return new std::ostringstream;
}
static std::string _r_fmt_end(std::ostringstream* s) {
  std::string str = s->str();
  delete s;
  return str;
}

#define R_FMT(A) _r_fmt_end( static_cast<std::ostringstream*>(&((*_r_fmt_begin()) << A )) )


Value *Visitor::codegenCallExpression(const ast::CallExpression* node) {
  DEBUG_TRACE_LLVM_VISITOR;
  
  // Find value that the symbol references.
  Value* targetV = resolveSymbol(node->calleeName());
  if (targetV == 0) return 0;
  
  // Check type (must be a function) and get FunctionType in one call.
  FunctionType* FT = functionTypeForValue(targetV);
  if (!FT) return error("Trying to call something that is not a function");

  // Local ref to input arguments, for our convenience.
  const ast::CallExpression::ArgumentList& inputArgs = node->arguments();
  
  // Check arguments.
  if (static_cast<size_t>(FT->getNumParams()) != inputArgs.size()) {
    return error("Incorrect number of arguments passed to function call");
  }

  // Build argument list by codegen'ing all input variables
  std::vector<Value*> argValues;
  ast::CallExpression::ArgumentList::const_iterator inIt = inputArgs.begin();
  FunctionType::param_iterator ftIt = FT->param_begin();
  unsigned i = 0;
  for (; inIt < inputArgs.end(); ++inIt, ++ftIt, ++i) {
    // First, codegen the input value
    Value* inputV = codegen(*inIt);
    if (inputV == 0) return 0;
    
    // Verify that the input type matches the expected type
    Type* expectedT = *ftIt;
    if (inputV->getType()->getTypeID() != expectedT->getTypeID()) {
      if (inputV->getType()->canLosslesslyBitCastTo(expectedT))
        rlogw("TODO: canLosslesslyBitCastTo == true");
      // TODO: Potentially cast simple values like numbers
      return error(R_FMT("Invalid type for argument " << i << " in call to " << node->calleeName()));
    }
    
    argValues.push_back(inputV);
  }
  
  // Create call instruction
  //rlog("targetV ->"); targetV->dump();
  return builder_.CreateCall(targetV, argValues, node->calleeName()+"_res");
}

#include "_VisitorImplFooter.h"
