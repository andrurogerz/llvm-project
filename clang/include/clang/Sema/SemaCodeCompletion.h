//===----- SemaCodeCompletion.h ------ Code completion support ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares facilities that support code completion.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMACODECOMPLETION_H
#define LLVM_CLANG_SEMA_SEMACODECOMPLETION_H

#include "clang/Support/Compiler.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/HeuristicResolver.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

namespace clang {
class DeclGroupRef;
class MacroInfo;
class Scope;
class TemplateName;

class SemaCodeCompletion : public SemaBase {
public:
  CLANG_ABI SemaCodeCompletion(Sema &S, CodeCompleteConsumer *CompletionConsumer);

  using TemplateTy = OpaquePtr<TemplateName>;
  using DeclGroupPtrTy = OpaquePtr<DeclGroupRef>;

  /// Code-completion consumer.
  CodeCompleteConsumer *CodeCompleter;
  HeuristicResolver Resolver;

  /// Describes the context in which code completion occurs.
  enum ParserCompletionContext {
    /// Code completion occurs at top-level or namespace context.
    PCC_Namespace,
    /// Code completion occurs within a class, struct, or union.
    PCC_Class,
    /// Code completion occurs within an Objective-C interface, protocol,
    /// or category.
    PCC_ObjCInterface,
    /// Code completion occurs within an Objective-C implementation or
    /// category implementation
    PCC_ObjCImplementation,
    /// Code completion occurs within the list of instance variables
    /// in an Objective-C interface, protocol, category, or implementation.
    PCC_ObjCInstanceVariableList,
    /// Code completion occurs following one or more template
    /// headers.
    PCC_Template,
    /// Code completion occurs following one or more template
    /// headers within a class.
    PCC_MemberTemplate,
    /// Code completion occurs within an expression.
    PCC_Expression,
    /// Code completion occurs within a statement, which may
    /// also be an expression or a declaration.
    PCC_Statement,
    /// Code completion occurs at the beginning of the
    /// initialization statement (or expression) in a for loop.
    PCC_ForInit,
    /// Code completion occurs within the condition of an if,
    /// while, switch, or for statement.
    PCC_Condition,
    /// Code completion occurs within the body of a function on a
    /// recovery path, where we do not have a specific handle on our position
    /// in the grammar.
    PCC_RecoveryInFunction,
    /// Code completion occurs where only a type is permitted.
    PCC_Type,
    /// Code completion occurs in a parenthesized expression, which
    /// might also be a type cast.
    PCC_ParenthesizedExpression,
    /// Code completion occurs within a sequence of declaration
    /// specifiers within a function, method, or block.
    PCC_LocalDeclarationSpecifiers,
    /// Code completion occurs at top-level in a REPL session
    PCC_TopLevelOrExpression,
  };

  CLANG_ABI void CodeCompleteModuleImport(SourceLocation ImportLoc, ModuleIdPath Path);
  CLANG_ABI void CodeCompleteOrdinaryName(Scope *S,
                                ParserCompletionContext CompletionContext);
  CLANG_ABI void CodeCompleteDeclSpec(Scope *S, DeclSpec &DS, bool AllowNonIdentifiers,
                            bool AllowNestedNameSpecifiers);

  struct CodeCompleteExpressionData;
  CLANG_ABI void CodeCompleteExpression(Scope *S, const CodeCompleteExpressionData &Data);
  CLANG_ABI void CodeCompleteExpression(Scope *S, QualType PreferredType,
                              bool IsParenthesized = false);
  CLANG_ABI void CodeCompleteMemberReferenceExpr(Scope *S, Expr *Base, Expr *OtherOpBase,
                                       SourceLocation OpLoc, bool IsArrow,
                                       bool IsBaseExprStatement,
                                       QualType PreferredType);
  CLANG_ABI void CodeCompletePostfixExpression(Scope *S, ExprResult LHS,
                                     QualType PreferredType);
  CLANG_ABI void CodeCompleteTag(Scope *S, unsigned TagSpec);
  CLANG_ABI void CodeCompleteTypeQualifiers(DeclSpec &DS);
  CLANG_ABI void CodeCompleteFunctionQualifiers(DeclSpec &DS, Declarator &D,
                                      const VirtSpecifiers *VS = nullptr);
  CLANG_ABI void CodeCompleteBracketDeclarator(Scope *S);
  CLANG_ABI void CodeCompleteCase(Scope *S);
  enum class AttributeCompletion {
    Attribute,
    Scope,
    None,
  };
  CLANG_ABI void CodeCompleteAttribute(
      AttributeCommonInfo::Syntax Syntax,
      AttributeCompletion Completion = AttributeCompletion::Attribute,
      const IdentifierInfo *Scope = nullptr);
  /// Determines the preferred type of the current function argument, by
  /// examining the signatures of all possible overloads.
  /// Returns null if unknown or ambiguous, or if code completion is off.
  ///
  /// If the code completion point has been reached, also reports the function
  /// signatures that were considered.
  ///
  /// FIXME: rename to GuessCallArgumentType to reduce confusion.
  CLANG_ABI QualType ProduceCallSignatureHelp(Expr *Fn, ArrayRef<Expr *> Args,
                                    SourceLocation OpenParLoc);
  CLANG_ABI QualType ProduceConstructorSignatureHelp(QualType Type, SourceLocation Loc,
                                           ArrayRef<Expr *> Args,
                                           SourceLocation OpenParLoc,
                                           bool Braced);
  CLANG_ABI QualType ProduceCtorInitMemberSignatureHelp(
      Decl *ConstructorDecl, CXXScopeSpec SS, ParsedType TemplateTypeTy,
      ArrayRef<Expr *> ArgExprs, IdentifierInfo *II, SourceLocation OpenParLoc,
      bool Braced);
  CLANG_ABI QualType ProduceTemplateArgumentSignatureHelp(
      TemplateTy, ArrayRef<ParsedTemplateArgument>, SourceLocation LAngleLoc);
  CLANG_ABI void CodeCompleteInitializer(Scope *S, Decl *D);
  /// Trigger code completion for a record of \p BaseType. \p InitExprs are
  /// expressions in the initializer list seen so far and \p D is the current
  /// Designation being parsed.
  CLANG_ABI void CodeCompleteDesignator(const QualType BaseType,
                              llvm::ArrayRef<Expr *> InitExprs,
                              const Designation &D);
  CLANG_ABI void CodeCompleteKeywordAfterIf(bool AfterExclaim) const;
  CLANG_ABI void CodeCompleteAfterIf(Scope *S, bool IsBracedThen);

  CLANG_ABI void CodeCompleteQualifiedId(Scope *S, CXXScopeSpec &SS, bool EnteringContext,
                               bool IsUsingDeclaration, QualType BaseType,
                               QualType PreferredType);
  CLANG_ABI void CodeCompleteUsing(Scope *S);
  CLANG_ABI void CodeCompleteUsingDirective(Scope *S);
  CLANG_ABI void CodeCompleteNamespaceDecl(Scope *S);
  CLANG_ABI void CodeCompleteNamespaceAliasDecl(Scope *S);
  CLANG_ABI void CodeCompleteOperatorName(Scope *S);
  CLANG_ABI void CodeCompleteConstructorInitializer(
      Decl *Constructor, ArrayRef<CXXCtorInitializer *> Initializers);

  CLANG_ABI void CodeCompleteLambdaIntroducer(Scope *S, LambdaIntroducer &Intro,
                                    bool AfterAmpersand);
  CLANG_ABI void CodeCompleteAfterFunctionEquals(Declarator &D);

  CLANG_ABI void CodeCompleteObjCAtDirective(Scope *S);
  CLANG_ABI void CodeCompleteObjCAtVisibility(Scope *S);
  CLANG_ABI void CodeCompleteObjCAtStatement(Scope *S);
  CLANG_ABI void CodeCompleteObjCAtExpression(Scope *S);
  CLANG_ABI void CodeCompleteObjCPropertyFlags(Scope *S, ObjCDeclSpec &ODS);
  CLANG_ABI void CodeCompleteObjCPropertyGetter(Scope *S);
  CLANG_ABI void CodeCompleteObjCPropertySetter(Scope *S);
  CLANG_ABI void CodeCompleteObjCPassingType(Scope *S, ObjCDeclSpec &DS,
                                   bool IsParameter);
  CLANG_ABI void CodeCompleteObjCMessageReceiver(Scope *S);
  CLANG_ABI void CodeCompleteObjCSuperMessage(Scope *S, SourceLocation SuperLoc,
                                    ArrayRef<const IdentifierInfo *> SelIdents,
                                    bool AtArgumentExpression);
  CLANG_ABI void CodeCompleteObjCClassMessage(Scope *S, ParsedType Receiver,
                                    ArrayRef<const IdentifierInfo *> SelIdents,
                                    bool AtArgumentExpression,
                                    bool IsSuper = false);
  CLANG_ABI void CodeCompleteObjCInstanceMessage(
      Scope *S, Expr *Receiver, ArrayRef<const IdentifierInfo *> SelIdents,
      bool AtArgumentExpression, ObjCInterfaceDecl *Super = nullptr);
  CLANG_ABI void CodeCompleteObjCForCollection(Scope *S, DeclGroupPtrTy IterationVar);
  CLANG_ABI void CodeCompleteObjCSelector(Scope *S,
                                ArrayRef<const IdentifierInfo *> SelIdents);
  CLANG_ABI void CodeCompleteObjCProtocolReferences(ArrayRef<IdentifierLoc> Protocols);
  CLANG_ABI void CodeCompleteObjCProtocolDecl(Scope *S);
  CLANG_ABI void CodeCompleteObjCInterfaceDecl(Scope *S);
  CLANG_ABI void CodeCompleteObjCClassForwardDecl(Scope *S);
  CLANG_ABI void CodeCompleteObjCSuperclass(Scope *S, IdentifierInfo *ClassName,
                                  SourceLocation ClassNameLoc);
  CLANG_ABI void CodeCompleteObjCImplementationDecl(Scope *S);
  CLANG_ABI void CodeCompleteObjCInterfaceCategory(Scope *S, IdentifierInfo *ClassName,
                                         SourceLocation ClassNameLoc);
  CLANG_ABI void CodeCompleteObjCImplementationCategory(Scope *S,
                                              IdentifierInfo *ClassName,
                                              SourceLocation ClassNameLoc);
  CLANG_ABI void CodeCompleteObjCPropertyDefinition(Scope *S);
  CLANG_ABI void CodeCompleteObjCPropertySynthesizeIvar(Scope *S,
                                              IdentifierInfo *PropertyName);
  CLANG_ABI void CodeCompleteObjCMethodDecl(Scope *S,
                                  std::optional<bool> IsInstanceMethod,
                                  ParsedType ReturnType);
  CLANG_ABI void CodeCompleteObjCMethodDeclSelector(
      Scope *S, bool IsInstanceMethod, bool AtParameterName,
      ParsedType ReturnType, ArrayRef<const IdentifierInfo *> SelIdents);
  CLANG_ABI void CodeCompleteObjCClassPropertyRefExpr(Scope *S,
                                            const IdentifierInfo &ClassName,
                                            SourceLocation ClassNameLoc,
                                            bool IsBaseExprStatement);
  CLANG_ABI void CodeCompletePreprocessorDirective(bool InConditional);
  CLANG_ABI void CodeCompleteInPreprocessorConditionalExclusion(Scope *S);
  CLANG_ABI void CodeCompletePreprocessorMacroName(bool IsDefinition);
  CLANG_ABI void CodeCompletePreprocessorExpression();
  CLANG_ABI void CodeCompletePreprocessorMacroArgument(Scope *S, IdentifierInfo *Macro,
                                             MacroInfo *MacroInfo,
                                             unsigned Argument);
  CLANG_ABI void CodeCompleteIncludedFile(llvm::StringRef Dir, bool IsAngled);
  CLANG_ABI void CodeCompleteNaturalLanguage();
  CLANG_ABI void CodeCompleteAvailabilityPlatformName();
  CLANG_ABI void
  GatherGlobalCodeCompletions(CodeCompletionAllocator &Allocator,
                              CodeCompletionTUInfo &CCTUInfo,
                              SmallVectorImpl<CodeCompletionResult> &Results);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMACODECOMPLETION_H
