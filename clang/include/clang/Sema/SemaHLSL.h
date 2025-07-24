//===----- SemaHLSL.h ----- Semantic Analysis for HLSL constructs ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for HLSL constructs.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAHLSL_H
#define LLVM_CLANG_SEMA_SEMAHLSL_H

#include "clang/Support/Compiler.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/TargetParser/Triple.h"
#include <initializer_list>

namespace clang {
class AttributeCommonInfo;
class IdentifierInfo;
class InitializedEntity;
class InitializationKind;
class ParsedAttr;
class Scope;
class VarDecl;

namespace hlsl {

// Introduce a wrapper struct around the underlying RootElement. This structure
// will retain extra clang diagnostic information that is not available in llvm.
struct RootSignatureElement {
  RootSignatureElement(SourceLocation Loc,
                       llvm::hlsl::rootsig::RootElement Element)
      : Loc(Loc), Element(Element) {}

  const llvm::hlsl::rootsig::RootElement &getElement() const { return Element; }
  const SourceLocation &getLocation() const { return Loc; }

private:
  SourceLocation Loc;
  llvm::hlsl::rootsig::RootElement Element;
};

} // namespace hlsl

using llvm::dxil::ResourceClass;

// FIXME: This can be hidden (as static function in SemaHLSL.cpp) once we no
// longer need to create builtin buffer types in HLSLExternalSemaSource.
CLANG_ABI bool CreateHLSLAttributedResourceType(
    Sema &S, QualType Wrapped, ArrayRef<const Attr *> AttrList,
    QualType &ResType, HLSLAttributedResourceLocInfo *LocInfo = nullptr);

enum class BindingType : uint8_t { NotAssigned, Explicit, Implicit };

// DeclBindingInfo struct stores information about required/assigned resource
// binding onon a declaration for specific resource class.
struct DeclBindingInfo {
  const VarDecl *Decl;
  ResourceClass ResClass;
  const HLSLResourceBindingAttr *Attr;
  BindingType BindType;

  DeclBindingInfo(const VarDecl *Decl, ResourceClass ResClass,
                  BindingType BindType = BindingType::NotAssigned,
                  const HLSLResourceBindingAttr *Attr = nullptr)
      : Decl(Decl), ResClass(ResClass), Attr(Attr), BindType(BindType) {}

  void setBindingAttribute(HLSLResourceBindingAttr *A, BindingType BT) {
    assert(Attr == nullptr && BindType == BindingType::NotAssigned &&
           "binding attribute already assigned");
    Attr = A;
    BindType = BT;
  }
};

// ResourceBindings class stores information about all resource bindings
// in a shader. It is used for binding diagnostics and implicit binding
// assigments.
class ResourceBindings {
public:
  CLANG_ABI DeclBindingInfo *addDeclBindingInfo(const VarDecl *VD,
                                      ResourceClass ResClass);
  CLANG_ABI DeclBindingInfo *getDeclBindingInfo(const VarDecl *VD,
                                      ResourceClass ResClass);
  CLANG_ABI bool hasBindingInfoForDecl(const VarDecl *VD) const;

private:
  // List of all resource bindings required by the shader.
  // A global declaration can have multiple bindings for different
  // resource classes. They are all stored sequentially in this list.
  // The DeclToBindingListIndex hashtable maps a declaration to the
  // index of the first binding info in the list.
  llvm::SmallVector<DeclBindingInfo> BindingsList;
  llvm::DenseMap<const VarDecl *, unsigned> DeclToBindingListIndex;
};

class SemaHLSL : public SemaBase {
public:
  CLANG_ABI SemaHLSL(Sema &S);

  CLANG_ABI Decl *ActOnStartBuffer(Scope *BufferScope, bool CBuffer, SourceLocation KwLoc,
                         IdentifierInfo *Ident, SourceLocation IdentLoc,
                         SourceLocation LBrace);
  CLANG_ABI void ActOnFinishBuffer(Decl *Dcl, SourceLocation RBrace);
  CLANG_ABI HLSLNumThreadsAttr *mergeNumThreadsAttr(Decl *D,
                                          const AttributeCommonInfo &AL, int X,
                                          int Y, int Z);
  CLANG_ABI HLSLWaveSizeAttr *mergeWaveSizeAttr(Decl *D, const AttributeCommonInfo &AL,
                                      int Min, int Max, int Preferred,
                                      int SpelledArgsCount);
  CLANG_ABI HLSLVkConstantIdAttr *
  mergeVkConstantIdAttr(Decl *D, const AttributeCommonInfo &AL, int Id);
  CLANG_ABI HLSLShaderAttr *mergeShaderAttr(Decl *D, const AttributeCommonInfo &AL,
                                  llvm::Triple::EnvironmentType ShaderType);
  CLANG_ABI HLSLParamModifierAttr *
  mergeParamModifierAttr(Decl *D, const AttributeCommonInfo &AL,
                         HLSLParamModifierAttr::Spelling Spelling);
  CLANG_ABI void ActOnTopLevelFunction(FunctionDecl *FD);
  CLANG_ABI void ActOnVariableDeclarator(VarDecl *VD);
  CLANG_ABI bool ActOnUninitializedVarDecl(VarDecl *D);
  CLANG_ABI void ActOnEndOfTranslationUnit(TranslationUnitDecl *TU);
  CLANG_ABI void CheckEntryPoint(FunctionDecl *FD);
  CLANG_ABI void CheckSemanticAnnotation(FunctionDecl *EntryPoint, const Decl *Param,
                               const HLSLAnnotationAttr *AnnotationAttr);
  CLANG_ABI void DiagnoseAttrStageMismatch(
      const Attr *A, llvm::Triple::EnvironmentType Stage,
      std::initializer_list<llvm::Triple::EnvironmentType> AllowedStages);

  CLANG_ABI QualType handleVectorBinOpConversion(ExprResult &LHS, ExprResult &RHS,
                                       QualType LHSType, QualType RHSType,
                                       bool IsCompAssign);
  CLANG_ABI void emitLogicalOperatorFixIt(Expr *LHS, Expr *RHS, BinaryOperatorKind Opc);

  /// Computes the unique Root Signature identifier from the given signature,
  /// then lookup if there is a previousy created Root Signature decl.
  ///
  /// Returns the identifier and if it was found
  CLANG_ABI std::pair<IdentifierInfo *, bool>
  ActOnStartRootSignatureDecl(StringRef Signature);

  /// Creates the Root Signature decl of the parsed Root Signature elements
  /// onto the AST and push it onto current Scope
  CLANG_ABI void
  ActOnFinishRootSignatureDecl(SourceLocation Loc, IdentifierInfo *DeclIdent,
                               ArrayRef<hlsl::RootSignatureElement> Elements);

  // Returns true if any RootSignatureElement is invalid and a diagnostic was
  // produced
  CLANG_ABI bool
  handleRootSignatureElements(ArrayRef<hlsl::RootSignatureElement> Elements);
  CLANG_ABI void handleRootSignatureAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleNumThreadsAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleWaveSizeAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleVkConstantIdAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleSV_DispatchThreadIDAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleSV_GroupThreadIDAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleSV_GroupIDAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleSV_PositionAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handlePackOffsetAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleShaderAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleResourceBindingAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI void handleParamModifierAttr(Decl *D, const ParsedAttr &AL);
  CLANG_ABI bool handleResourceTypeAttr(QualType T, const ParsedAttr &AL);

  CLANG_ABI void handleVkExtBuiltinInputAttr(Decl *D, const ParsedAttr &AL);

  CLANG_ABI bool CheckBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);
  CLANG_ABI QualType ProcessResourceTypeAttributes(QualType Wrapped);
  CLANG_ABI HLSLAttributedResourceLocInfo
  TakeLocForHLSLAttribute(const HLSLAttributedResourceType *RT);

  // HLSL Type trait implementations
  CLANG_ABI bool IsScalarizedLayoutCompatible(QualType T1, QualType T2) const;
  CLANG_ABI bool IsTypedResourceElementCompatible(QualType T1);

  CLANG_ABI bool CheckCompatibleParameterABI(FunctionDecl *New, FunctionDecl *Old);

  // Diagnose whether the input ID is uint/unit2/uint3 type.
  CLANG_ABI bool diagnoseInputIDType(QualType T, const ParsedAttr &AL);
  CLANG_ABI bool diagnosePositionType(QualType T, const ParsedAttr &AL);

  CLANG_ABI bool CanPerformScalarCast(QualType SrcTy, QualType DestTy);
  CLANG_ABI bool ContainsBitField(QualType BaseTy);
  CLANG_ABI bool CanPerformElementwiseCast(Expr *Src, QualType DestType);
  CLANG_ABI bool CanPerformAggregateSplatCast(Expr *Src, QualType DestType);
  CLANG_ABI ExprResult ActOnOutParamExpr(ParmVarDecl *Param, Expr *Arg);

  CLANG_ABI QualType getInoutParameterType(QualType Ty);

  CLANG_ABI bool transformInitList(const InitializedEntity &Entity, InitListExpr *Init);
  CLANG_ABI bool handleInitialization(VarDecl *VDecl, Expr *&Init);
  CLANG_ABI void deduceAddressSpace(VarDecl *Decl);

private:
  // HLSL resource type attributes need to be processed all at once.
  // This is a list to collect them.
  llvm::SmallVector<const Attr *> HLSLResourcesTypeAttrs;

  /// TypeLoc data for HLSLAttributedResourceType instances that we
  /// have not yet populated.
  llvm::DenseMap<const HLSLAttributedResourceType *,
                 HLSLAttributedResourceLocInfo>
      LocsForHLSLAttributedResources;

  // List of all resource bindings
  ResourceBindings Bindings;

  // Global declaration collected for the $Globals default constant
  // buffer which will be created at the end of the translation unit.
  llvm::SmallVector<Decl *> DefaultCBufferDecls;

  uint32_t ImplicitBindingNextOrderID = 0;

private:
  void collectResourceBindingsOnVarDecl(VarDecl *D);
  void collectResourceBindingsOnUserRecordDecl(const VarDecl *VD,
                                               const RecordType *RT);
  void processExplicitBindingsOnDecl(VarDecl *D);

  void diagnoseAvailabilityViolations(TranslationUnitDecl *TU);

  bool initGlobalResourceDecl(VarDecl *VD);
  uint32_t getNextImplicitBindingOrderID() {
    return ImplicitBindingNextOrderID++;
  }
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAHLSL_H
