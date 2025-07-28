//===--- TextNodeDumper.h - Printing of AST nodes -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements AST dumping of components of individual AST nodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TEXTNODEDUMPER_H
#define LLVM_CLANG_AST_TEXTNODEDUMPER_H

#include "clang/Support/Compiler.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDumperUtils.h"
#include "clang/AST/AttrVisitor.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentVisitor.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateArgumentVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/AST/TypeVisitor.h"

namespace clang {

class APValue;

class TextTreeStructure {
  raw_ostream &OS;
  const bool ShowColors;

  /// Pending[i] is an action to dump an entity at level i.
  llvm::SmallVector<std::function<void(bool IsLastChild)>, 32> Pending;

  /// Indicates whether we're at the top level.
  bool TopLevel = true;

  /// Indicates if we're handling the first child after entering a new depth.
  bool FirstChild = true;

  /// Prefix for currently-being-dumped entity.
  std::string Prefix;

public:
  /// Add a child of the current node.  Calls DoAddChild without arguments
  template <typename Fn> void AddChild(Fn DoAddChild) {
    return AddChild("", DoAddChild);
  }

  /// Add a child of the current node with an optional label.
  /// Calls DoAddChild without arguments.
  template <typename Fn> void AddChild(StringRef Label, Fn DoAddChild) {
    // If we're at the top level, there's nothing interesting to do; just
    // run the dumper.
    if (TopLevel) {
      TopLevel = false;
      DoAddChild();
      while (!Pending.empty()) {
        Pending.back()(true);
        Pending.pop_back();
      }
      Prefix.clear();
      OS << "\n";
      TopLevel = true;
      return;
    }

    auto DumpWithIndent = [this, DoAddChild,
                           Label(Label.str())](bool IsLastChild) {
      // Print out the appropriate tree structure and work out the prefix for
      // children of this node. For instance:
      //
      //   A        Prefix = ""
      //   |-B      Prefix = "| "
      //   | `-C    Prefix = "|   "
      //   `-D      Prefix = "  "
      //     |-E    Prefix = "  | "
      //     `-F    Prefix = "    "
      //   G        Prefix = ""
      //
      // Note that the first level gets no prefix.
      {
        OS << '\n';
        ColorScope Color(OS, ShowColors, IndentColor);
        OS << Prefix << (IsLastChild ? '`' : '|') << '-';
        if (!Label.empty())
          OS << Label << ": ";

        this->Prefix.push_back(IsLastChild ? ' ' : '|');
        this->Prefix.push_back(' ');
      }

      FirstChild = true;
      unsigned Depth = Pending.size();

      DoAddChild();

      // If any children are left, they're the last at their nesting level.
      // Dump those ones out now.
      while (Depth < Pending.size()) {
        Pending.back()(true);
        this->Pending.pop_back();
      }

      // Restore the old prefix.
      this->Prefix.resize(Prefix.size() - 2);
    };

    if (FirstChild) {
      Pending.push_back(std::move(DumpWithIndent));
    } else {
      Pending.back()(false);
      Pending.back() = std::move(DumpWithIndent);
    }
    FirstChild = false;
  }

  TextTreeStructure(raw_ostream &OS, bool ShowColors)
      : OS(OS), ShowColors(ShowColors) {}
};

class TextNodeDumper
    : public TextTreeStructure,
      public comments::ConstCommentVisitor<TextNodeDumper, void,
                                           const comments::FullComment *>,
      public ConstAttrVisitor<TextNodeDumper>,
      public ConstTemplateArgumentVisitor<TextNodeDumper>,
      public ConstStmtVisitor<TextNodeDumper>,
      public TypeVisitor<TextNodeDumper>,
      public TypeLocVisitor<TextNodeDumper>,
      public ConstDeclVisitor<TextNodeDumper> {
  raw_ostream &OS;
  const bool ShowColors;

  /// Keep track of the last location we print out so that we can
  /// print out deltas from then on out.
  const char *LastLocFilename = "";
  unsigned LastLocLine = ~0U;

  /// \p Context, \p SM, and \p Traits can be null. This is because we want
  /// to be able to call \p dump() in a debugger without having to pass the
  /// \p ASTContext to \p dump. Not all parts of the AST dump output will be
  /// available without the \p ASTContext.
  const ASTContext *Context = nullptr;
  const SourceManager *SM = nullptr;

  /// The policy to use for printing; can be defaulted.
  PrintingPolicy PrintPolicy = LangOptions();

  const comments::CommandTraits *Traits = nullptr;

  const char *getCommandName(unsigned CommandID);
  void printFPOptions(FPOptionsOverride FPO);

  void dumpAPValueChildren(const APValue &Value, QualType Ty,
                           const APValue &(*IdxToChildFun)(const APValue &,
                                                           unsigned),
                           unsigned NumChildren, StringRef LabelSingular,
                           StringRef LabelPlurial);

public:
  CLANG_ABI TextNodeDumper(raw_ostream &OS, const ASTContext &Context, bool ShowColors);
  CLANG_ABI TextNodeDumper(raw_ostream &OS, bool ShowColors);

  CLANG_ABI void Visit(const comments::Comment *C, const comments::FullComment *FC);

  CLANG_ABI void Visit(const Attr *A);

  CLANG_ABI void Visit(const TemplateArgument &TA, SourceRange R,
             const Decl *From = nullptr, StringRef Label = {});

  CLANG_ABI void Visit(const Stmt *Node);

  CLANG_ABI void Visit(const Type *T);

  CLANG_ABI void Visit(QualType T);

  CLANG_ABI void Visit(TypeLoc);

  CLANG_ABI void Visit(const Decl *D);

  CLANG_ABI void Visit(const CXXCtorInitializer *Init);

  CLANG_ABI void Visit(const OMPClause *C);

  CLANG_ABI void Visit(const OpenACCClause *C);

  CLANG_ABI void Visit(const BlockDecl::Capture &C);

  CLANG_ABI void Visit(const GenericSelectionExpr::ConstAssociation &A);

  CLANG_ABI void Visit(const ConceptReference *);

  CLANG_ABI void Visit(const concepts::Requirement *R);

  CLANG_ABI void Visit(const APValue &Value, QualType Ty);

  CLANG_ABI void dumpPointer(const void *Ptr);
  CLANG_ABI void dumpLocation(SourceLocation Loc);
  CLANG_ABI void dumpSourceRange(SourceRange R);
  CLANG_ABI void dumpBareType(QualType T, bool Desugar = true);
  CLANG_ABI void dumpType(QualType T);
  CLANG_ABI void dumpBareDeclRef(const Decl *D);
  CLANG_ABI void dumpName(const NamedDecl *ND);
  CLANG_ABI void dumpAccessSpecifier(AccessSpecifier AS);
  CLANG_ABI void dumpCleanupObject(const ExprWithCleanups::CleanupObject &C);
  CLANG_ABI void dumpTemplateSpecializationKind(TemplateSpecializationKind TSK);
  CLANG_ABI void dumpNestedNameSpecifier(const NestedNameSpecifier *NNS);
  CLANG_ABI void dumpConceptReference(const ConceptReference *R);
  CLANG_ABI void dumpTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void dumpBareTemplateName(TemplateName TN);
  CLANG_ABI void dumpTemplateName(TemplateName TN, StringRef Label = {});

  CLANG_ABI void dumpDeclRef(const Decl *D, StringRef Label = {});

  CLANG_ABI void visitTextComment(const comments::TextComment *C,
                        const comments::FullComment *);
  CLANG_ABI void visitInlineCommandComment(const comments::InlineCommandComment *C,
                                 const comments::FullComment *);
  CLANG_ABI void visitHTMLStartTagComment(const comments::HTMLStartTagComment *C,
                                const comments::FullComment *);
  CLANG_ABI void visitHTMLEndTagComment(const comments::HTMLEndTagComment *C,
                              const comments::FullComment *);
  CLANG_ABI void visitBlockCommandComment(const comments::BlockCommandComment *C,
                                const comments::FullComment *);
  CLANG_ABI void visitParamCommandComment(const comments::ParamCommandComment *C,
                                const comments::FullComment *FC);
  CLANG_ABI void visitTParamCommandComment(const comments::TParamCommandComment *C,
                                 const comments::FullComment *FC);
  CLANG_ABI void visitVerbatimBlockComment(const comments::VerbatimBlockComment *C,
                                 const comments::FullComment *);
  CLANG_ABI void
  visitVerbatimBlockLineComment(const comments::VerbatimBlockLineComment *C,
                                const comments::FullComment *);
  CLANG_ABI void visitVerbatimLineComment(const comments::VerbatimLineComment *C,
                                const comments::FullComment *);

// Implements Visit methods for Attrs.
#include "clang/AST/AttrTextNodeDump.inc"

  CLANG_ABI void VisitNullTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitTypeTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitDeclarationTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitNullPtrTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitIntegralTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitStructuralValueTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitTemplateTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitTemplateExpansionTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitExpressionTemplateArgument(const TemplateArgument &TA);
  CLANG_ABI void VisitPackTemplateArgument(const TemplateArgument &TA);

  CLANG_ABI void VisitIfStmt(const IfStmt *Node);
  CLANG_ABI void VisitSwitchStmt(const SwitchStmt *Node);
  CLANG_ABI void VisitWhileStmt(const WhileStmt *Node);
  CLANG_ABI void VisitLabelStmt(const LabelStmt *Node);
  CLANG_ABI void VisitGotoStmt(const GotoStmt *Node);
  CLANG_ABI void VisitCaseStmt(const CaseStmt *Node);
  CLANG_ABI void VisitReturnStmt(const ReturnStmt *Node);
  CLANG_ABI void VisitCoawaitExpr(const CoawaitExpr *Node);
  CLANG_ABI void VisitCoreturnStmt(const CoreturnStmt *Node);
  CLANG_ABI void VisitCompoundStmt(const CompoundStmt *Node);
  CLANG_ABI void VisitConstantExpr(const ConstantExpr *Node);
  CLANG_ABI void VisitCallExpr(const CallExpr *Node);
  CLANG_ABI void VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *Node);
  CLANG_ABI void VisitCastExpr(const CastExpr *Node);
  CLANG_ABI void VisitImplicitCastExpr(const ImplicitCastExpr *Node);
  CLANG_ABI void VisitDeclRefExpr(const DeclRefExpr *Node);
  CLANG_ABI void VisitDependentScopeDeclRefExpr(const DependentScopeDeclRefExpr *Node);
  CLANG_ABI void VisitSYCLUniqueStableNameExpr(const SYCLUniqueStableNameExpr *Node);
  CLANG_ABI void VisitPredefinedExpr(const PredefinedExpr *Node);
  CLANG_ABI void VisitCharacterLiteral(const CharacterLiteral *Node);
  CLANG_ABI void VisitIntegerLiteral(const IntegerLiteral *Node);
  CLANG_ABI void VisitFixedPointLiteral(const FixedPointLiteral *Node);
  CLANG_ABI void VisitFloatingLiteral(const FloatingLiteral *Node);
  CLANG_ABI void VisitStringLiteral(const StringLiteral *Str);
  CLANG_ABI void VisitInitListExpr(const InitListExpr *ILE);
  CLANG_ABI void VisitGenericSelectionExpr(const GenericSelectionExpr *E);
  CLANG_ABI void VisitUnaryOperator(const UnaryOperator *Node);
  CLANG_ABI void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Node);
  CLANG_ABI void VisitMemberExpr(const MemberExpr *Node);
  CLANG_ABI void VisitExtVectorElementExpr(const ExtVectorElementExpr *Node);
  CLANG_ABI void VisitBinaryOperator(const BinaryOperator *Node);
  CLANG_ABI void VisitCompoundAssignOperator(const CompoundAssignOperator *Node);
  CLANG_ABI void VisitAddrLabelExpr(const AddrLabelExpr *Node);
  CLANG_ABI void VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node);
  CLANG_ABI void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node);
  CLANG_ABI void VisitCXXThisExpr(const CXXThisExpr *Node);
  CLANG_ABI void VisitCXXFunctionalCastExpr(const CXXFunctionalCastExpr *Node);
  CLANG_ABI void VisitCXXStaticCastExpr(const CXXStaticCastExpr *Node);
  CLANG_ABI void VisitCXXUnresolvedConstructExpr(const CXXUnresolvedConstructExpr *Node);
  CLANG_ABI void VisitCXXConstructExpr(const CXXConstructExpr *Node);
  CLANG_ABI void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *Node);
  CLANG_ABI void VisitCXXNewExpr(const CXXNewExpr *Node);
  CLANG_ABI void VisitCXXDeleteExpr(const CXXDeleteExpr *Node);
  CLANG_ABI void VisitTypeTraitExpr(const TypeTraitExpr *Node);
  CLANG_ABI void VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *Node);
  CLANG_ABI void VisitExpressionTraitExpr(const ExpressionTraitExpr *Node);
  CLANG_ABI void VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *Node);
  CLANG_ABI void VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *Node);
  CLANG_ABI void VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *Node);
  CLANG_ABI void VisitExprWithCleanups(const ExprWithCleanups *Node);
  CLANG_ABI void VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *Node);
  CLANG_ABI void VisitSizeOfPackExpr(const SizeOfPackExpr *Node);
  CLANG_ABI void
  VisitCXXDependentScopeMemberExpr(const CXXDependentScopeMemberExpr *Node);
  CLANG_ABI void VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node);
  CLANG_ABI void VisitObjCEncodeExpr(const ObjCEncodeExpr *Node);
  CLANG_ABI void VisitObjCMessageExpr(const ObjCMessageExpr *Node);
  CLANG_ABI void VisitObjCBoxedExpr(const ObjCBoxedExpr *Node);
  CLANG_ABI void VisitObjCSelectorExpr(const ObjCSelectorExpr *Node);
  CLANG_ABI void VisitObjCProtocolExpr(const ObjCProtocolExpr *Node);
  CLANG_ABI void VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node);
  CLANG_ABI void VisitObjCSubscriptRefExpr(const ObjCSubscriptRefExpr *Node);
  CLANG_ABI void VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node);
  CLANG_ABI void VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node);
  CLANG_ABI void VisitOMPIteratorExpr(const OMPIteratorExpr *Node);
  CLANG_ABI void VisitConceptSpecializationExpr(const ConceptSpecializationExpr *Node);
  CLANG_ABI void VisitRequiresExpr(const RequiresExpr *Node);

  CLANG_ABI void VisitRValueReferenceType(const ReferenceType *T);
  CLANG_ABI void VisitArrayType(const ArrayType *T);
  CLANG_ABI void VisitConstantArrayType(const ConstantArrayType *T);
  CLANG_ABI void VisitVariableArrayType(const VariableArrayType *T);
  CLANG_ABI void VisitDependentSizedArrayType(const DependentSizedArrayType *T);
  CLANG_ABI void VisitDependentSizedExtVectorType(const DependentSizedExtVectorType *T);
  CLANG_ABI void VisitVectorType(const VectorType *T);
  CLANG_ABI void VisitFunctionType(const FunctionType *T);
  CLANG_ABI void VisitFunctionProtoType(const FunctionProtoType *T);
  CLANG_ABI void VisitUnresolvedUsingType(const UnresolvedUsingType *T);
  CLANG_ABI void VisitUsingType(const UsingType *T);
  CLANG_ABI void VisitTypedefType(const TypedefType *T);
  CLANG_ABI void VisitUnaryTransformType(const UnaryTransformType *T);
  CLANG_ABI void VisitTagType(const TagType *T);
  CLANG_ABI void VisitTemplateTypeParmType(const TemplateTypeParmType *T);
  CLANG_ABI void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T);
  CLANG_ABI void
  VisitSubstTemplateTypeParmPackType(const SubstTemplateTypeParmPackType *T);
  CLANG_ABI void VisitAutoType(const AutoType *T);
  CLANG_ABI void VisitDeducedTemplateSpecializationType(
      const DeducedTemplateSpecializationType *T);
  CLANG_ABI void VisitTemplateSpecializationType(const TemplateSpecializationType *T);
  CLANG_ABI void VisitInjectedClassNameType(const InjectedClassNameType *T);
  CLANG_ABI void VisitObjCInterfaceType(const ObjCInterfaceType *T);
  CLANG_ABI void VisitPackExpansionType(const PackExpansionType *T);

  CLANG_ABI void VisitTypeLoc(TypeLoc TL);

  CLANG_ABI void VisitLabelDecl(const LabelDecl *D);
  CLANG_ABI void VisitTypedefDecl(const TypedefDecl *D);
  CLANG_ABI void VisitEnumDecl(const EnumDecl *D);
  CLANG_ABI void VisitRecordDecl(const RecordDecl *D);
  CLANG_ABI void VisitEnumConstantDecl(const EnumConstantDecl *D);
  CLANG_ABI void VisitIndirectFieldDecl(const IndirectFieldDecl *D);
  CLANG_ABI void VisitFunctionDecl(const FunctionDecl *D);
  CLANG_ABI void VisitCXXDeductionGuideDecl(const CXXDeductionGuideDecl *D);
  CLANG_ABI void VisitFieldDecl(const FieldDecl *D);
  CLANG_ABI void VisitVarDecl(const VarDecl *D);
  CLANG_ABI void VisitBindingDecl(const BindingDecl *D);
  CLANG_ABI void VisitCapturedDecl(const CapturedDecl *D);
  CLANG_ABI void VisitImportDecl(const ImportDecl *D);
  CLANG_ABI void VisitPragmaCommentDecl(const PragmaCommentDecl *D);
  CLANG_ABI void VisitPragmaDetectMismatchDecl(const PragmaDetectMismatchDecl *D);
  CLANG_ABI void VisitOMPExecutableDirective(const OMPExecutableDirective *D);
  CLANG_ABI void VisitOMPDeclareReductionDecl(const OMPDeclareReductionDecl *D);
  CLANG_ABI void VisitOMPRequiresDecl(const OMPRequiresDecl *D);
  CLANG_ABI void VisitOMPCapturedExprDecl(const OMPCapturedExprDecl *D);
  CLANG_ABI void VisitNamespaceDecl(const NamespaceDecl *D);
  CLANG_ABI void VisitUsingDirectiveDecl(const UsingDirectiveDecl *D);
  CLANG_ABI void VisitNamespaceAliasDecl(const NamespaceAliasDecl *D);
  CLANG_ABI void VisitTypeAliasDecl(const TypeAliasDecl *D);
  CLANG_ABI void VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D);
  CLANG_ABI void VisitCXXRecordDecl(const CXXRecordDecl *D);
  CLANG_ABI void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D);
  CLANG_ABI void VisitClassTemplateDecl(const ClassTemplateDecl *D);
  CLANG_ABI void VisitBuiltinTemplateDecl(const BuiltinTemplateDecl *D);
  CLANG_ABI void VisitVarTemplateDecl(const VarTemplateDecl *D);
  CLANG_ABI void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D);
  CLANG_ABI void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D);
  CLANG_ABI void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D);
  CLANG_ABI void VisitUsingDecl(const UsingDecl *D);
  CLANG_ABI void VisitUnresolvedUsingTypenameDecl(const UnresolvedUsingTypenameDecl *D);
  CLANG_ABI void VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D);
  CLANG_ABI void VisitUsingEnumDecl(const UsingEnumDecl *D);
  CLANG_ABI void VisitUsingShadowDecl(const UsingShadowDecl *D);
  CLANG_ABI void VisitConstructorUsingShadowDecl(const ConstructorUsingShadowDecl *D);
  CLANG_ABI void VisitLinkageSpecDecl(const LinkageSpecDecl *D);
  CLANG_ABI void VisitAccessSpecDecl(const AccessSpecDecl *D);
  CLANG_ABI void VisitFriendDecl(const FriendDecl *D);
  CLANG_ABI void VisitObjCIvarDecl(const ObjCIvarDecl *D);
  CLANG_ABI void VisitObjCMethodDecl(const ObjCMethodDecl *D);
  CLANG_ABI void VisitObjCTypeParamDecl(const ObjCTypeParamDecl *D);
  CLANG_ABI void VisitObjCCategoryDecl(const ObjCCategoryDecl *D);
  CLANG_ABI void VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D);
  CLANG_ABI void VisitObjCProtocolDecl(const ObjCProtocolDecl *D);
  CLANG_ABI void VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D);
  CLANG_ABI void VisitObjCImplementationDecl(const ObjCImplementationDecl *D);
  CLANG_ABI void VisitObjCCompatibleAliasDecl(const ObjCCompatibleAliasDecl *D);
  CLANG_ABI void VisitObjCPropertyDecl(const ObjCPropertyDecl *D);
  CLANG_ABI void VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D);
  CLANG_ABI void VisitBlockDecl(const BlockDecl *D);
  CLANG_ABI void VisitConceptDecl(const ConceptDecl *D);
  CLANG_ABI void
  VisitLifetimeExtendedTemporaryDecl(const LifetimeExtendedTemporaryDecl *D);
  CLANG_ABI void VisitHLSLBufferDecl(const HLSLBufferDecl *D);
  CLANG_ABI void VisitHLSLRootSignatureDecl(const HLSLRootSignatureDecl *D);
  CLANG_ABI void VisitHLSLOutArgExpr(const HLSLOutArgExpr *E);
  CLANG_ABI void VisitOpenACCConstructStmt(const OpenACCConstructStmt *S);
  CLANG_ABI void VisitOpenACCLoopConstruct(const OpenACCLoopConstruct *S);
  CLANG_ABI void VisitOpenACCCombinedConstruct(const OpenACCCombinedConstruct *S);
  CLANG_ABI void VisitOpenACCDataConstruct(const OpenACCDataConstruct *S);
  CLANG_ABI void VisitOpenACCEnterDataConstruct(const OpenACCEnterDataConstruct *S);
  CLANG_ABI void VisitOpenACCExitDataConstruct(const OpenACCExitDataConstruct *S);
  CLANG_ABI void VisitOpenACCHostDataConstruct(const OpenACCHostDataConstruct *S);
  CLANG_ABI void VisitOpenACCWaitConstruct(const OpenACCWaitConstruct *S);
  CLANG_ABI void VisitOpenACCInitConstruct(const OpenACCInitConstruct *S);
  CLANG_ABI void VisitOpenACCSetConstruct(const OpenACCSetConstruct *S);
  CLANG_ABI void VisitOpenACCShutdownConstruct(const OpenACCShutdownConstruct *S);
  CLANG_ABI void VisitOpenACCUpdateConstruct(const OpenACCUpdateConstruct *S);
  CLANG_ABI void VisitOpenACCAtomicConstruct(const OpenACCAtomicConstruct *S);
  CLANG_ABI void VisitOpenACCCacheConstruct(const OpenACCCacheConstruct *S);
  CLANG_ABI void VisitOpenACCAsteriskSizeExpr(const OpenACCAsteriskSizeExpr *S);
  CLANG_ABI void VisitOpenACCDeclareDecl(const OpenACCDeclareDecl *D);
  CLANG_ABI void VisitOpenACCRoutineDecl(const OpenACCRoutineDecl *D);
  CLANG_ABI void VisitOpenACCRoutineDeclAttr(const OpenACCRoutineDeclAttr *A);
  CLANG_ABI void VisitEmbedExpr(const EmbedExpr *S);
  CLANG_ABI void VisitAtomicExpr(const AtomicExpr *AE);
  CLANG_ABI void VisitConvertVectorExpr(const ConvertVectorExpr *S);
};

} // namespace clang

#endif // LLVM_CLANG_AST_TEXTNODEDUMPER_H
