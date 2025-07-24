//===----- SemaOpenMP.h -- Semantic Analysis for OpenMP constructs -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for OpenMP constructs and
/// clauses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAOPENMP_H
#define LLVM_CLANG_SEMA_SEMAOPENMP_H

#include "clang/Support/Compiler.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Frontend/OpenMP/OMP.h.inc"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include <optional>
#include <string>
#include <utility>

namespace clang {
namespace sema {
class FunctionScopeInfo;
} // namespace sema

class DeclContext;
class DeclGroupRef;
class ParsedAttr;
class Scope;

class SemaOpenMP : public SemaBase {
public:
  CLANG_ABI SemaOpenMP(Sema &S);

  friend class Parser;
  friend class Sema;

  using DeclGroupPtrTy = OpaquePtr<DeclGroupRef>;
  using CapturedParamNameType = std::pair<StringRef, QualType>;

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as device code".
  ///
  /// - If CurContext is a `declare target` function or it is known that the
  /// function is emitted for the device, emits the diagnostics immediately.
  /// - If CurContext is a non-`declare target` function and we are compiling
  ///   for the device, creates a diagnostic which is emitted if and when we
  ///   realize that the function will be codegen'ed.
  ///
  /// Example usage:
  ///
  ///  // Variable-length arrays are not allowed in NVPTX device code.
  ///  if (diagIfOpenMPDeviceCode(Loc, diag::err_vla_unsupported))
  ///    return ExprError();
  ///  // Otherwise, continue parsing as normal.
  CLANG_ABI SemaDiagnosticBuilder diagIfOpenMPDeviceCode(SourceLocation Loc,
                                               unsigned DiagID,
                                               const FunctionDecl *FD);

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as host code".
  ///
  /// - If CurContext is a `declare target` function or it is known that the
  /// function is emitted for the host, emits the diagnostics immediately.
  /// - If CurContext is a non-host function, just ignore it.
  ///
  /// Example usage:
  ///
  ///  // Variable-length arrays are not allowed in NVPTX device code.
  ///  if (diagIfOpenMPHostode(Loc, diag::err_vla_unsupported))
  ///    return ExprError();
  ///  // Otherwise, continue parsing as normal.
  CLANG_ABI SemaDiagnosticBuilder diagIfOpenMPHostCode(SourceLocation Loc,
                                             unsigned DiagID,
                                             const FunctionDecl *FD);

  /// The declarator \p D defines a function in the scope \p S which is nested
  /// in an `omp begin/end declare variant` scope. In this method we create a
  /// declaration for \p D and rename \p D according to the OpenMP context
  /// selector of the surrounding scope. Return all base functions in \p Bases.
  CLANG_ABI void ActOnStartOfFunctionDefinitionInOpenMPDeclareVariantScope(
      Scope *S, Declarator &D, MultiTemplateParamsArg TemplateParameterLists,
      SmallVectorImpl<FunctionDecl *> &Bases);

  /// Register \p D as specialization of all base functions in \p Bases in the
  /// current `omp begin/end declare variant` scope.
  CLANG_ABI void ActOnFinishedFunctionDefinitionInOpenMPDeclareVariantScope(
      Decl *D, SmallVectorImpl<FunctionDecl *> &Bases);

  /// Act on \p D, a function definition inside of an `omp [begin/end] assumes`.
  CLANG_ABI void ActOnFinishedFunctionDefinitionInOpenMPAssumeScope(Decl *D);

  /// Can we exit an OpenMP declare variant scope at the moment.
  bool isInOpenMPDeclareVariantScope() const {
    return !OMPDeclareVariantScopes.empty();
  }

  CLANG_ABI ExprResult
  VerifyPositiveIntegerConstantInClause(Expr *Op, OpenMPClauseKind CKind,
                                        bool StrictlyPositive = true,
                                        bool SuppressExprDiags = false);

  /// Given the potential call expression \p Call, determine if there is a
  /// specialization via the OpenMP declare variant mechanism available. If
  /// there is, return the specialized call expression, otherwise return the
  /// original \p Call.
  CLANG_ABI ExprResult ActOnOpenMPCall(ExprResult Call, Scope *Scope,
                             SourceLocation LParenLoc, MultiExprArg ArgExprs,
                             SourceLocation RParenLoc, Expr *ExecConfig);

  /// Handle a `omp begin declare variant`.
  CLANG_ABI void ActOnOpenMPBeginDeclareVariant(SourceLocation Loc, OMPTraitInfo &TI);

  /// Handle a `omp end declare variant`.
  CLANG_ABI void ActOnOpenMPEndDeclareVariant();

  /// Function tries to capture lambda's captured variables in the OpenMP region
  /// before the original lambda is captured.
  CLANG_ABI void tryCaptureOpenMPLambdas(ValueDecl *V);

  /// Return true if the provided declaration \a VD should be captured by
  /// reference.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  /// \param OpenMPCaptureLevel Capture level within an OpenMP construct.
  CLANG_ABI bool isOpenMPCapturedByRef(const ValueDecl *D, unsigned Level,
                             unsigned OpenMPCaptureLevel) const;

  /// Check if the specified variable is used in one of the private
  /// clauses (private, firstprivate, lastprivate, reduction etc.) in OpenMP
  /// constructs.
  CLANG_ABI VarDecl *isOpenMPCapturedDecl(ValueDecl *D, bool CheckScopeInfo = false,
                                unsigned StopAt = 0);

  /// The member expression(this->fd) needs to be rebuilt in the template
  /// instantiation to generate private copy for OpenMP when default
  /// clause is used. The function will return true if default
  /// cluse is used.
  CLANG_ABI bool isOpenMPRebuildMemberExpr(ValueDecl *D);

  CLANG_ABI ExprResult getOpenMPCapturedExpr(VarDecl *Capture, ExprValueKind VK,
                                   ExprObjectKind OK, SourceLocation Loc);

  /// If the current region is a loop-based region, mark the start of the loop
  /// construct.
  CLANG_ABI void startOpenMPLoop();

  /// If the current region is a range loop-based region, mark the start of the
  /// loop construct.
  CLANG_ABI void startOpenMPCXXRangeFor();

  /// Check if the specified variable is used in 'private' clause.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  CLANG_ABI OpenMPClauseKind isOpenMPPrivateDecl(ValueDecl *D, unsigned Level,
                                       unsigned CapLevel) const;

  /// Sets OpenMP capture kind (OMPC_private, OMPC_firstprivate, OMPC_map etc.)
  /// for \p FD based on DSA for the provided corresponding captured declaration
  /// \p D.
  CLANG_ABI void setOpenMPCaptureKind(FieldDecl *FD, const ValueDecl *D, unsigned Level);

  /// Check if the specified variable is captured  by 'target' directive.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  CLANG_ABI bool isOpenMPTargetCapturedDecl(const ValueDecl *D, unsigned Level,
                                  unsigned CaptureLevel) const;

  /// Check if the specified global variable must be captured  by outer capture
  /// regions.
  /// \param Level Relative level of nested OpenMP construct for that
  /// the check is performed.
  CLANG_ABI bool isOpenMPGlobalCapturedDecl(ValueDecl *D, unsigned Level,
                                  unsigned CaptureLevel) const;

  CLANG_ABI ExprResult PerformOpenMPImplicitIntegerConversion(SourceLocation OpLoc,
                                                    Expr *Op);
  /// Called on start of new data sharing attribute block.
  CLANG_ABI void StartOpenMPDSABlock(OpenMPDirectiveKind K,
                           const DeclarationNameInfo &DirName, Scope *CurScope,
                           SourceLocation Loc);
  /// Start analysis of clauses.
  CLANG_ABI void StartOpenMPClause(OpenMPClauseKind K);
  /// End analysis of clauses.
  CLANG_ABI void EndOpenMPClause();
  /// Called on end of data sharing attribute block.
  CLANG_ABI void EndOpenMPDSABlock(Stmt *CurDirective);

  /// Check if the current region is an OpenMP loop region and if it is,
  /// mark loop control variable, used in \p Init for loop initialization, as
  /// private by default.
  /// \param Init First part of the for loop.
  CLANG_ABI void ActOnOpenMPLoopInitialization(SourceLocation ForLoc, Stmt *Init);

  /// Called on well-formed '\#pragma omp metadirective' after parsing
  /// of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMetaDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);

  // OpenMP directives and clauses.
  /// Called on correct id-expression from the '#pragma omp
  /// threadprivate'.
  CLANG_ABI ExprResult ActOnOpenMPIdExpression(Scope *CurScope, CXXScopeSpec &ScopeSpec,
                                     const DeclarationNameInfo &Id,
                                     OpenMPDirectiveKind Kind);
  /// Called on well-formed '#pragma omp threadprivate'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPThreadprivateDirective(SourceLocation Loc,
                                                   ArrayRef<Expr *> VarList);
  /// Builds a new OpenMPThreadPrivateDecl and checks its correctness.
  CLANG_ABI OMPThreadPrivateDecl *CheckOMPThreadPrivateDecl(SourceLocation Loc,
                                                  ArrayRef<Expr *> VarList);
  /// Called on well-formed '#pragma omp allocate'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPAllocateDirective(SourceLocation Loc,
                                              ArrayRef<Expr *> VarList,
                                              ArrayRef<OMPClause *> Clauses,
                                              DeclContext *Owner = nullptr);

  /// Called on well-formed '#pragma omp [begin] assume[s]'.
  CLANG_ABI void ActOnOpenMPAssumesDirective(SourceLocation Loc,
                                   OpenMPDirectiveKind DKind,
                                   ArrayRef<std::string> Assumptions,
                                   bool SkippedClauses);

  /// Check if there is an active global `omp begin assumes` directive.
  bool isInOpenMPAssumeScope() const { return !OMPAssumeScoped.empty(); }

  /// Check if there is an active global `omp assumes` directive.
  bool hasGlobalOpenMPAssumes() const { return !OMPAssumeGlobal.empty(); }

  /// Called on well-formed '#pragma omp end assumes'.
  CLANG_ABI void ActOnOpenMPEndAssumesDirective();

  /// Called on well-formed '#pragma omp requires'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPRequiresDirective(SourceLocation Loc,
                                              ArrayRef<OMPClause *> ClauseList);
  /// Check restrictions on Requires directive
  CLANG_ABI OMPRequiresDecl *CheckOMPRequiresDecl(SourceLocation Loc,
                                        ArrayRef<OMPClause *> Clauses);
  /// Check if the specified type is allowed to be used in 'omp declare
  /// reduction' construct.
  CLANG_ABI QualType ActOnOpenMPDeclareReductionType(SourceLocation TyLoc,
                                           TypeResult ParsedType);
  /// Called on start of '#pragma omp declare reduction'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPDeclareReductionDirectiveStart(
      Scope *S, DeclContext *DC, DeclarationName Name,
      ArrayRef<std::pair<QualType, SourceLocation>> ReductionTypes,
      AccessSpecifier AS, Decl *PrevDeclInScope = nullptr);
  /// Initialize declare reduction construct initializer.
  CLANG_ABI void ActOnOpenMPDeclareReductionCombinerStart(Scope *S, Decl *D);
  /// Finish current declare reduction construct initializer.
  CLANG_ABI void ActOnOpenMPDeclareReductionCombinerEnd(Decl *D, Expr *Combiner);
  /// Initialize declare reduction construct initializer.
  /// \return omp_priv variable.
  CLANG_ABI VarDecl *ActOnOpenMPDeclareReductionInitializerStart(Scope *S, Decl *D);
  /// Finish current declare reduction construct initializer.
  CLANG_ABI void ActOnOpenMPDeclareReductionInitializerEnd(Decl *D, Expr *Initializer,
                                                 VarDecl *OmpPrivParm);
  /// Called at the end of '#pragma omp declare reduction'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPDeclareReductionDirectiveEnd(
      Scope *S, DeclGroupPtrTy DeclReductions, bool IsValid);

  /// Check variable declaration in 'omp declare mapper' construct.
  CLANG_ABI TypeResult ActOnOpenMPDeclareMapperVarDecl(Scope *S, Declarator &D);
  /// Check if the specified type is allowed to be used in 'omp declare
  /// mapper' construct.
  CLANG_ABI QualType ActOnOpenMPDeclareMapperType(SourceLocation TyLoc,
                                        TypeResult ParsedType);
  /// Called for '#pragma omp declare mapper'.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPDeclareMapperDirective(
      Scope *S, DeclContext *DC, DeclarationName Name, QualType MapperType,
      SourceLocation StartLoc, DeclarationName VN, AccessSpecifier AS,
      Expr *MapperVarRef, ArrayRef<OMPClause *> Clauses,
      Decl *PrevDeclInScope = nullptr);
  /// Build the mapper variable of '#pragma omp declare mapper'.
  CLANG_ABI ExprResult ActOnOpenMPDeclareMapperDirectiveVarDecl(Scope *S,
                                                      QualType MapperType,
                                                      SourceLocation StartLoc,
                                                      DeclarationName VN);
  CLANG_ABI void ActOnOpenMPIteratorVarDecl(VarDecl *VD);
  CLANG_ABI bool isOpenMPDeclareMapperVarDeclAllowed(const VarDecl *VD) const;
  CLANG_ABI const ValueDecl *getOpenMPDeclareMapperVarName() const;

  struct DeclareTargetContextInfo {
    struct MapInfo {
      OMPDeclareTargetDeclAttr::MapTypeTy MT;
      SourceLocation Loc;
    };
    /// Explicitly listed variables and functions in a 'to' or 'link' clause.
    llvm::DenseMap<NamedDecl *, MapInfo> ExplicitlyMapped;

    /// The 'device_type' as parsed from the clause.
    OMPDeclareTargetDeclAttr::DevTypeTy DT = OMPDeclareTargetDeclAttr::DT_Any;

    /// The directive kind, `begin declare target` or `declare target`.
    OpenMPDirectiveKind Kind;

    /// The directive with indirect clause.
    std::optional<Expr *> Indirect;

    /// The directive location.
    SourceLocation Loc;

    DeclareTargetContextInfo(OpenMPDirectiveKind Kind, SourceLocation Loc)
        : Kind(Kind), Loc(Loc) {}
  };

  /// Called on the start of target region i.e. '#pragma omp declare target'.
  CLANG_ABI bool ActOnStartOpenMPDeclareTargetContext(DeclareTargetContextInfo &DTCI);

  /// Called at the end of target region i.e. '#pragma omp end declare target'.
  CLANG_ABI const DeclareTargetContextInfo ActOnOpenMPEndDeclareTargetDirective();

  /// Called once a target context is completed, that can be when a
  /// '#pragma omp end declare target' was encountered or when a
  /// '#pragma omp declare target' without declaration-definition-seq was
  /// encountered.
  CLANG_ABI void ActOnFinishedOpenMPDeclareTargetContext(DeclareTargetContextInfo &DTCI);

  /// Report unterminated 'omp declare target' or 'omp begin declare target' at
  /// the end of a compilation unit.
  CLANG_ABI void DiagnoseUnterminatedOpenMPDeclareTarget();

  /// Searches for the provided declaration name for OpenMP declare target
  /// directive.
  CLANG_ABI NamedDecl *lookupOpenMPDeclareTargetName(Scope *CurScope,
                                           CXXScopeSpec &ScopeSpec,
                                           const DeclarationNameInfo &Id);

  /// Called on correct id-expression from the '#pragma omp declare target'.
  CLANG_ABI void ActOnOpenMPDeclareTargetName(NamedDecl *ND, SourceLocation Loc,
                                    OMPDeclareTargetDeclAttr::MapTypeTy MT,
                                    DeclareTargetContextInfo &DTCI);

  /// Check declaration inside target region.
  CLANG_ABI void
  checkDeclIsAllowedInOpenMPTarget(Expr *E, Decl *D,
                                   SourceLocation IdLoc = SourceLocation());

  /// Adds OMPDeclareTargetDeclAttr to referenced variables in declare target
  /// directive.
  CLANG_ABI void ActOnOpenMPDeclareTargetInitializer(Decl *D);

  /// Finishes analysis of the deferred functions calls that may be declared as
  /// host/nohost during device/host compilation.
  CLANG_ABI void finalizeOpenMPDelayedAnalysis(const FunctionDecl *Caller,
                                     const FunctionDecl *Callee,
                                     SourceLocation Loc);

  /// Return true if currently in OpenMP task with untied clause context.
  CLANG_ABI bool isInOpenMPTaskUntiedContext() const;

  /// Return true inside OpenMP declare target region.
  bool isInOpenMPDeclareTargetContext() const {
    return !DeclareTargetNesting.empty();
  }
  /// Return true inside OpenMP target region.
  CLANG_ABI bool isInOpenMPTargetExecutionDirective() const;

  /// Return the number of captured regions created for an OpenMP directive.
  CLANG_ABI static int getOpenMPCaptureLevels(OpenMPDirectiveKind Kind);

  /// Initialization of captured region for OpenMP region.
  CLANG_ABI void ActOnOpenMPRegionStart(OpenMPDirectiveKind DKind, Scope *CurScope);

  /// Called for syntactical loops (ForStmt or CXXForRangeStmt) associated to
  /// an OpenMP loop directive.
  CLANG_ABI StmtResult ActOnOpenMPCanonicalLoop(Stmt *AStmt);

  /// Process a canonical OpenMP loop nest that can either be a canonical
  /// literal loop (ForStmt or CXXForRangeStmt), or the generated loop of an
  /// OpenMP loop transformation construct.
  CLANG_ABI StmtResult ActOnOpenMPLoopnest(Stmt *AStmt);

  /// End of OpenMP region.
  ///
  /// \param S Statement associated with the current OpenMP region.
  /// \param Clauses List of clauses for the current OpenMP region.
  ///
  /// \returns Statement for finished OpenMP region.
  CLANG_ABI StmtResult ActOnOpenMPRegionEnd(StmtResult S, ArrayRef<OMPClause *> Clauses);
  CLANG_ABI StmtResult ActOnOpenMPExecutableDirective(
      OpenMPDirectiveKind Kind, const DeclarationNameInfo &DirName,
      OpenMPDirectiveKind CancelRegion, ArrayRef<OMPClause *> Clauses,
      Stmt *AStmt, SourceLocation StartLoc, SourceLocation EndLoc);
  /// Process an OpenMP informational directive.
  ///
  /// \param Kind The directive kind.
  /// \param DirName Declaration name info.
  /// \param Clauses Array of clauses for directive.
  /// \param AStmt The associated statement.
  /// \param StartLoc The start location.
  /// \param EndLoc The end location.
  CLANG_ABI StmtResult ActOnOpenMPInformationalDirective(
      OpenMPDirectiveKind Kind, const DeclarationNameInfo &DirName,
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc);
  /// Process an OpenMP assume directive.
  ///
  /// \param Clauses Array of clauses for directive.
  /// \param AStmt The associated statement.
  /// \param StartLoc The start location.
  /// \param EndLoc The end location.
  CLANG_ABI StmtResult ActOnOpenMPAssumeDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);

  /// Called on well-formed '\#pragma omp parallel' after parsing
  /// of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  using VarsWithInheritedDSAType =
      llvm::SmallDenseMap<const ValueDecl *, const Expr *, 4>;
  /// Called on well-formed '\#pragma omp simd' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                           SourceLocation StartLoc, SourceLocation EndLoc,
                           VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '#pragma omp tile' after parsing of its clauses and
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTileDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  CLANG_ABI StmtResult ActOnOpenMPStripeDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp unroll' after parsing of its clauses
  /// and the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPUnrollDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp reverse'.
  CLANG_ABI StmtResult ActOnOpenMPReverseDirective(Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp interchange' after parsing of its
  /// clauses and the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPInterchangeDirective(ArrayRef<OMPClause *> Clauses,
                                             Stmt *AStmt,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp for' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPForDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                          SourceLocation StartLoc, SourceLocation EndLoc,
                          VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp for simd' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPForSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                              SourceLocation StartLoc, SourceLocation EndLoc,
                              VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp sections' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPSectionsDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp section' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPSectionDirective(Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp scope' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPScopeDirective(ArrayRef<OMPClause *> Clauses,
                                       Stmt *AStmt, SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp single' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPSingleDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp master' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMasterDirective(Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp critical' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPCriticalDirective(const DeclarationNameInfo &DirName,
                                          ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel for' after parsing
  /// of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel for simd' after
  /// parsing of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master' after
  /// parsing of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMasterDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel masked' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMaskedDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel sections' after
  /// parsing of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelSectionsDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp task' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTaskDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskyield'.
  CLANG_ABI StmtResult ActOnOpenMPTaskyieldDirective(SourceLocation StartLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp error'.
  /// Error direcitive is allowed in both declared and excutable contexts.
  /// Adding InExContext to identify which context is called from.
  CLANG_ABI StmtResult ActOnOpenMPErrorDirective(ArrayRef<OMPClause *> Clauses,
                                       SourceLocation StartLoc,
                                       SourceLocation EndLoc,
                                       bool InExContext = true);
  /// Called on well-formed '\#pragma omp barrier'.
  CLANG_ABI StmtResult ActOnOpenMPBarrierDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskwait'.
  CLANG_ABI StmtResult ActOnOpenMPTaskwaitDirective(ArrayRef<OMPClause *> Clauses,
                                          SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskgroup'.
  CLANG_ABI StmtResult ActOnOpenMPTaskgroupDirective(ArrayRef<OMPClause *> Clauses,
                                           Stmt *AStmt, SourceLocation StartLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp flush'.
  CLANG_ABI StmtResult ActOnOpenMPFlushDirective(ArrayRef<OMPClause *> Clauses,
                                       SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp depobj'.
  CLANG_ABI StmtResult ActOnOpenMPDepobjDirective(ArrayRef<OMPClause *> Clauses,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp scan'.
  CLANG_ABI StmtResult ActOnOpenMPScanDirective(ArrayRef<OMPClause *> Clauses,
                                      SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp ordered' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPOrderedDirective(ArrayRef<OMPClause *> Clauses,
                                         Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp atomic' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPAtomicDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target data' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetDataDirective(ArrayRef<OMPClause *> Clauses,
                                            Stmt *AStmt,
                                            SourceLocation StartLoc,
                                            SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target enter data' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetEnterDataDirective(ArrayRef<OMPClause *> Clauses,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc,
                                                 Stmt *AStmt);
  /// Called on well-formed '\#pragma omp target exit data' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetExitDataDirective(ArrayRef<OMPClause *> Clauses,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc,
                                                Stmt *AStmt);
  /// Called on well-formed '\#pragma omp target parallel' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetParallelDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target parallel for' after
  /// parsing of the  associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsDirective(ArrayRef<OMPClause *> Clauses,
                                       Stmt *AStmt, SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp teams loop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams loop' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel loop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target parallel loop' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetParallelGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp cancellation point'.
  CLANG_ABI StmtResult
  ActOnOpenMPCancellationPointDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        OpenMPDirectiveKind CancelRegion);
  /// Called on well-formed '\#pragma omp cancel'.
  CLANG_ABI StmtResult ActOnOpenMPCancelDirective(ArrayRef<OMPClause *> Clauses,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        OpenMPDirectiveKind CancelRegion);
  /// Called on well-formed '\#pragma omp taskloop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPTaskLoopDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                               SourceLocation StartLoc, SourceLocation EndLoc,
                               VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp taskloop simd' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp master taskloop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMasterTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp master taskloop simd' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMasterTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master taskloop' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMasterTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master taskloop simd' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMasterTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp masked taskloop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMaskedTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp masked taskloop simd' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMaskedTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel masked taskloop' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMaskedTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel masked taskloop simd' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPParallelMaskedTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPDistributeDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                                 SourceLocation StartLoc, SourceLocation EndLoc,
                                 VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target update'.
  CLANG_ABI StmtResult ActOnOpenMPTargetUpdateDirective(ArrayRef<OMPClause *> Clauses,
                                              SourceLocation StartLoc,
                                              SourceLocation EndLoc,
                                              Stmt *AStmt);
  /// Called on well-formed '\#pragma omp distribute parallel for' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute parallel for simd'
  /// after parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute simd' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target parallel for simd' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target simd' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult
  ActOnOpenMPTargetSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                                 SourceLocation StartLoc, SourceLocation EndLoc,
                                 VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute' after parsing of
  /// the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsDistributeDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute simd' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute parallel for simd'
  /// after parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute parallel for'
  /// after parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTeamsDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsDirective(ArrayRef<OMPClause *> Clauses,
                                             Stmt *AStmt,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target teams distribute' after parsing
  /// of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsDistributeDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute parallel for'
  /// after parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute parallel for
  /// simd' after parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute simd' after
  /// parsing of the associated statement.
  CLANG_ABI StmtResult ActOnOpenMPTargetTeamsDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp interop'.
  CLANG_ABI StmtResult ActOnOpenMPInteropDirective(ArrayRef<OMPClause *> Clauses,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp dispatch' after parsing of the
  // /associated statement.
  CLANG_ABI StmtResult ActOnOpenMPDispatchDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp masked' after parsing of the
  // /associated statement.
  CLANG_ABI StmtResult ActOnOpenMPMaskedDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);

  /// Called on well-formed '\#pragma omp loop' after parsing of the
  /// associated statement.
  CLANG_ABI StmtResult ActOnOpenMPGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);

  /// Checks correctness of linear modifiers.
  CLANG_ABI bool CheckOpenMPLinearModifier(OpenMPLinearClauseKind LinKind,
                                 SourceLocation LinLoc);
  /// Checks that the specified declaration matches requirements for the linear
  /// decls.
  CLANG_ABI bool CheckOpenMPLinearDecl(const ValueDecl *D, SourceLocation ELoc,
                             OpenMPLinearClauseKind LinKind, QualType Type,
                             bool IsDeclareSimd = false);

  /// Called on well-formed '\#pragma omp declare simd' after parsing of
  /// the associated method/function.
  CLANG_ABI DeclGroupPtrTy ActOnOpenMPDeclareSimdDirective(
      DeclGroupPtrTy DG, OMPDeclareSimdDeclAttr::BranchStateTy BS,
      Expr *Simdlen, ArrayRef<Expr *> Uniforms, ArrayRef<Expr *> Aligneds,
      ArrayRef<Expr *> Alignments, ArrayRef<Expr *> Linears,
      ArrayRef<unsigned> LinModifiers, ArrayRef<Expr *> Steps, SourceRange SR);

  /// Checks '\#pragma omp declare variant' variant function and original
  /// functions after parsing of the associated method/function.
  /// \param DG Function declaration to which declare variant directive is
  /// applied to.
  /// \param VariantRef Expression that references the variant function, which
  /// must be used instead of the original one, specified in \p DG.
  /// \param TI The trait info object representing the match clause.
  /// \param NumAppendArgs The number of omp_interop_t arguments to account for
  /// in checking.
  /// \returns std::nullopt, if the function/variant function are not compatible
  /// with the pragma, pair of original function/variant ref expression
  /// otherwise.
  CLANG_ABI std::optional<std::pair<FunctionDecl *, Expr *>>
  checkOpenMPDeclareVariantFunction(DeclGroupPtrTy DG, Expr *VariantRef,
                                    OMPTraitInfo &TI, unsigned NumAppendArgs,
                                    SourceRange SR);

  /// Called on well-formed '\#pragma omp declare variant' after parsing of
  /// the associated method/function.
  /// \param FD Function declaration to which declare variant directive is
  /// applied to.
  /// \param VariantRef Expression that references the variant function, which
  /// must be used instead of the original one, specified in \p DG.
  /// \param TI The context traits associated with the function variant.
  /// \param AdjustArgsNothing The list of 'nothing' arguments.
  /// \param AdjustArgsNeedDevicePtr The list of 'need_device_ptr' arguments.
  /// \param AppendArgs The list of 'append_args' arguments.
  /// \param AdjustArgsLoc The Location of an 'adjust_args' clause.
  /// \param AppendArgsLoc The Location of an 'append_args' clause.
  /// \param SR The SourceRange of the 'declare variant' directive.
  CLANG_ABI void ActOnOpenMPDeclareVariantDirective(
      FunctionDecl *FD, Expr *VariantRef, OMPTraitInfo &TI,
      ArrayRef<Expr *> AdjustArgsNothing,
      ArrayRef<Expr *> AdjustArgsNeedDevicePtr,
      ArrayRef<Expr *> AdjustArgsNeedDeviceAddr,
      ArrayRef<OMPInteropInfo> AppendArgs, SourceLocation AdjustArgsLoc,
      SourceLocation AppendArgsLoc, SourceRange SR);

  /// Called on device_num selector in context selectors.
  CLANG_ABI void ActOnOpenMPDeviceNum(Expr *DeviceNumExpr);

  CLANG_ABI OMPClause *ActOnOpenMPSingleExprClause(OpenMPClauseKind Kind, Expr *Expr,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed 'allocator' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAllocatorClause(Expr *Allocator,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'if' clause.
  CLANG_ABI OMPClause *ActOnOpenMPIfClause(OpenMPDirectiveKind NameModifier,
                                 Expr *Condition, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation NameModifierLoc,
                                 SourceLocation ColonLoc,
                                 SourceLocation EndLoc);
  /// Called on well-formed 'final' clause.
  CLANG_ABI OMPClause *ActOnOpenMPFinalClause(Expr *Condition, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'num_threads' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNumThreadsClause(
      OpenMPNumThreadsClauseModifier Modifier, Expr *NumThreads,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      SourceLocation ModifierLoc, SourceLocation EndLoc);
  /// Called on well-formed 'align' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAlignClause(Expr *Alignment, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'safelen' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSafelenClause(Expr *Length, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'simdlen' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSimdlenClause(Expr *Length, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-form 'sizes' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSizesClause(ArrayRef<Expr *> SizeExprs,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-form 'permutation' clause after parsing its arguments.
  CLANG_ABI OMPClause *ActOnOpenMPPermutationClause(ArrayRef<Expr *> PermExprs,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);
  /// Called on well-form 'full' clauses.
  CLANG_ABI OMPClause *ActOnOpenMPFullClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-form 'partial' clauses.
  CLANG_ABI OMPClause *ActOnOpenMPPartialClause(Expr *FactorExpr, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'collapse' clause.
  CLANG_ABI OMPClause *ActOnOpenMPCollapseClause(Expr *NumForLoops,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'ordered' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPOrderedClause(SourceLocation StartLoc, SourceLocation EndLoc,
                           SourceLocation LParenLoc = SourceLocation(),
                           Expr *NumForLoops = nullptr);
  /// Called on well-formed 'grainsize' clause.
  CLANG_ABI OMPClause *ActOnOpenMPGrainsizeClause(OpenMPGrainsizeClauseModifier Modifier,
                                        Expr *Size, SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation ModifierLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'num_tasks' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNumTasksClause(OpenMPNumTasksClauseModifier Modifier,
                                       Expr *NumTasks, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation ModifierLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'hint' clause.
  CLANG_ABI OMPClause *ActOnOpenMPHintClause(Expr *Hint, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'detach' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDetachClause(Expr *Evt, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);

  CLANG_ABI OMPClause *ActOnOpenMPSimpleClause(OpenMPClauseKind Kind, unsigned Argument,
                                     SourceLocation ArgumentLoc,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'when' clause.
  CLANG_ABI OMPClause *ActOnOpenMPWhenClause(OMPTraitInfo &TI, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'default' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDefaultClause(llvm::omp::DefaultKind Kind,
                                      SourceLocation KindLoc,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'proc_bind' clause.
  CLANG_ABI OMPClause *ActOnOpenMPProcBindClause(llvm::omp::ProcBindKind Kind,
                                       SourceLocation KindLoc,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'order' clause.
  CLANG_ABI OMPClause *ActOnOpenMPOrderClause(OpenMPOrderClauseModifier Modifier,
                                    OpenMPOrderClauseKind Kind,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation MLoc, SourceLocation KindLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'update' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUpdateClause(OpenMPDependClauseKind Kind,
                                     SourceLocation KindLoc,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'holds' clause.
  CLANG_ABI OMPClause *ActOnOpenMPHoldsClause(Expr *E, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'absent' or 'contains' clauses.
  CLANG_ABI OMPClause *ActOnOpenMPDirectivePresenceClause(
      OpenMPClauseKind CK, llvm::ArrayRef<OpenMPDirectiveKind> DKVec,
      SourceLocation Loc, SourceLocation LLoc, SourceLocation RLoc);
  CLANG_ABI OMPClause *ActOnOpenMPNullaryAssumptionClause(OpenMPClauseKind CK,
                                                SourceLocation Loc,
                                                SourceLocation RLoc);

  CLANG_ABI OMPClause *ActOnOpenMPSingleExprWithArgClause(
      OpenMPClauseKind Kind, ArrayRef<unsigned> Arguments, Expr *Expr,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      ArrayRef<SourceLocation> ArgumentsLoc, SourceLocation DelimLoc,
      SourceLocation EndLoc);
  /// Called on well-formed 'schedule' clause.
  CLANG_ABI OMPClause *ActOnOpenMPScheduleClause(
      OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
      OpenMPScheduleClauseKind Kind, Expr *ChunkSize, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation M1Loc, SourceLocation M2Loc,
      SourceLocation KindLoc, SourceLocation CommaLoc, SourceLocation EndLoc);

  CLANG_ABI OMPClause *ActOnOpenMPClause(OpenMPClauseKind Kind, SourceLocation StartLoc,
                               SourceLocation EndLoc);
  /// Called on well-formed 'nowait' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNowaitClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'untied' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUntiedClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'mergeable' clause.
  CLANG_ABI OMPClause *ActOnOpenMPMergeableClause(SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'read' clause.
  CLANG_ABI OMPClause *ActOnOpenMPReadClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'write' clause.
  CLANG_ABI OMPClause *ActOnOpenMPWriteClause(SourceLocation StartLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'update' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUpdateClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'capture' clause.
  CLANG_ABI OMPClause *ActOnOpenMPCaptureClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'compare' clause.
  CLANG_ABI OMPClause *ActOnOpenMPCompareClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'fail' clause.
  CLANG_ABI OMPClause *ActOnOpenMPFailClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  CLANG_ABI OMPClause *ActOnOpenMPFailClause(OpenMPClauseKind Kind,
                                   SourceLocation KindLoc,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);

  /// Called on well-formed 'seq_cst' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSeqCstClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'acq_rel' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAcqRelClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'acquire' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAcquireClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'release' clause.
  CLANG_ABI OMPClause *ActOnOpenMPReleaseClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'relaxed' clause.
  CLANG_ABI OMPClause *ActOnOpenMPRelaxedClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'weak' clause.
  CLANG_ABI OMPClause *ActOnOpenMPWeakClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);

  /// Called on well-formed 'init' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPInitClause(Expr *InteropVar, OMPInteropInfo &InteropInfo,
                        SourceLocation StartLoc, SourceLocation LParenLoc,
                        SourceLocation VarLoc, SourceLocation EndLoc);

  /// Called on well-formed 'use' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUseClause(Expr *InteropVar, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation VarLoc, SourceLocation EndLoc);

  /// Called on well-formed 'destroy' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDestroyClause(Expr *InteropVar, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation VarLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'novariants' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNovariantsClause(Expr *Condition,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed 'nocontext' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNocontextClause(Expr *Condition,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'filter' clause.
  CLANG_ABI OMPClause *ActOnOpenMPFilterClause(Expr *ThreadID, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'threads' clause.
  CLANG_ABI OMPClause *ActOnOpenMPThreadsClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'simd' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSIMDClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'nogroup' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNogroupClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'unified_address' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUnifiedAddressClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc);

  /// Called on well-formed 'unified_address' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUnifiedSharedMemoryClause(SourceLocation StartLoc,
                                                  SourceLocation EndLoc);

  /// Called on well-formed 'reverse_offload' clause.
  CLANG_ABI OMPClause *ActOnOpenMPReverseOffloadClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc);

  /// Called on well-formed 'dynamic_allocators' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDynamicAllocatorsClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc);

  /// Called on well-formed 'atomic_default_mem_order' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAtomicDefaultMemOrderClause(
      OpenMPAtomicDefaultMemOrderClauseKind Kind, SourceLocation KindLoc,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc);

  /// Called on well-formed 'self_maps' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSelfMapsClause(SourceLocation StartLoc,
                                       SourceLocation EndLoc);

  /// Called on well-formed 'at' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAtClause(OpenMPAtClauseKind Kind,
                                 SourceLocation KindLoc,
                                 SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc);

  /// Called on well-formed 'severity' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSeverityClause(OpenMPSeverityClauseKind Kind,
                                       SourceLocation KindLoc,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);

  /// Called on well-formed 'message' clause.
  /// passing string for message.
  CLANG_ABI OMPClause *ActOnOpenMPMessageClause(Expr *MS, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);

  /// Data used for processing a list of variables in OpenMP clauses.
  struct OpenMPVarListDataTy final {
    Expr *DepModOrTailExpr = nullptr;
    Expr *IteratorExpr = nullptr;
    SourceLocation ColonLoc;
    SourceLocation RLoc;
    CXXScopeSpec ReductionOrMapperIdScopeSpec;
    DeclarationNameInfo ReductionOrMapperId;
    int ExtraModifier = -1; ///< Additional modifier for linear, map, depend or
                            ///< lastprivate clause.
    int OriginalSharingModifier = 0; // Default is shared
    SmallVector<OpenMPMapModifierKind, NumberOfOMPMapClauseModifiers>
        MapTypeModifiers;
    SmallVector<SourceLocation, NumberOfOMPMapClauseModifiers>
        MapTypeModifiersLoc;
    SmallVector<OpenMPMotionModifierKind, NumberOfOMPMotionModifiers>
        MotionModifiers;
    SmallVector<SourceLocation, NumberOfOMPMotionModifiers> MotionModifiersLoc;
    bool IsMapTypeImplicit = false;
    SourceLocation ExtraModifierLoc;
    SourceLocation OriginalSharingModifierLoc;
    SourceLocation OmpAllMemoryLoc;
    SourceLocation
        StepModifierLoc; /// 'step' modifier location for linear clause
    SmallVector<OpenMPAllocateClauseModifier,
                NumberOfOMPAllocateClauseModifiers>
        AllocClauseModifiers;
    SmallVector<SourceLocation, NumberOfOMPAllocateClauseModifiers>
        AllocClauseModifiersLoc;
    Expr *AllocateAlignment = nullptr;
    struct OpenMPReductionClauseModifiers {
      int ExtraModifier;
      int OriginalSharingModifier;
      OpenMPReductionClauseModifiers(int Extra, int Original)
          : ExtraModifier(Extra), OriginalSharingModifier(Original) {}
    };
  };

  CLANG_ABI OMPClause *ActOnOpenMPVarListClause(OpenMPClauseKind Kind,
                                      ArrayRef<Expr *> Vars,
                                      const OMPVarListLocTy &Locs,
                                      OpenMPVarListDataTy &Data);
  /// Called on well-formed 'inclusive' clause.
  CLANG_ABI OMPClause *ActOnOpenMPInclusiveClause(ArrayRef<Expr *> VarList,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'exclusive' clause.
  CLANG_ABI OMPClause *ActOnOpenMPExclusiveClause(ArrayRef<Expr *> VarList,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'allocate' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPAllocateClause(Expr *Allocator, Expr *Alignment,
                            OpenMPAllocateClauseModifier FirstModifier,
                            SourceLocation FirstModifierLoc,
                            OpenMPAllocateClauseModifier SecondModifier,
                            SourceLocation SecondModifierLoc,
                            ArrayRef<Expr *> VarList, SourceLocation StartLoc,
                            SourceLocation ColonLoc, SourceLocation LParenLoc,
                            SourceLocation EndLoc);
  /// Called on well-formed 'private' clause.
  CLANG_ABI OMPClause *ActOnOpenMPPrivateClause(ArrayRef<Expr *> VarList,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'firstprivate' clause.
  CLANG_ABI OMPClause *ActOnOpenMPFirstprivateClause(ArrayRef<Expr *> VarList,
                                           SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed 'lastprivate' clause.
  CLANG_ABI OMPClause *ActOnOpenMPLastprivateClause(
      ArrayRef<Expr *> VarList, OpenMPLastprivateModifier LPKind,
      SourceLocation LPKindLoc, SourceLocation ColonLoc,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc);
  /// Called on well-formed 'shared' clause.
  CLANG_ABI OMPClause *ActOnOpenMPSharedClause(ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'reduction' clause.
  CLANG_ABI OMPClause *ActOnOpenMPReductionClause(
      ArrayRef<Expr *> VarList,
      OpenMPVarListDataTy::OpenMPReductionClauseModifiers Modifiers,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      SourceLocation ModifierLoc, SourceLocation ColonLoc,
      SourceLocation EndLoc, CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = {});
  /// Called on well-formed 'task_reduction' clause.
  CLANG_ABI OMPClause *ActOnOpenMPTaskReductionClause(
      ArrayRef<Expr *> VarList, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
      CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = {});
  /// Called on well-formed 'in_reduction' clause.
  CLANG_ABI OMPClause *ActOnOpenMPInReductionClause(
      ArrayRef<Expr *> VarList, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
      CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = {});
  /// Called on well-formed 'linear' clause.
  CLANG_ABI OMPClause *ActOnOpenMPLinearClause(
      ArrayRef<Expr *> VarList, Expr *Step, SourceLocation StartLoc,
      SourceLocation LParenLoc, OpenMPLinearClauseKind LinKind,
      SourceLocation LinLoc, SourceLocation ColonLoc,
      SourceLocation StepModifierLoc, SourceLocation EndLoc);
  /// Called on well-formed 'aligned' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAlignedClause(ArrayRef<Expr *> VarList, Expr *Alignment,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation ColonLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'copyin' clause.
  CLANG_ABI OMPClause *ActOnOpenMPCopyinClause(ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'copyprivate' clause.
  CLANG_ABI OMPClause *ActOnOpenMPCopyprivateClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed 'flush' pseudo clause.
  CLANG_ABI OMPClause *ActOnOpenMPFlushClause(ArrayRef<Expr *> VarList,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'depobj' pseudo clause.
  CLANG_ABI OMPClause *ActOnOpenMPDepobjClause(Expr *Depobj, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'depend' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDependClause(const OMPDependClause::DependDataTy &Data,
                                     Expr *DepModifier,
                                     ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'device' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDeviceClause(OpenMPDeviceClauseModifier Modifier,
                                     Expr *Device, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation ModifierLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'map' clause.
  CLANG_ABI OMPClause *ActOnOpenMPMapClause(
      Expr *IteratorModifier, ArrayRef<OpenMPMapModifierKind> MapTypeModifiers,
      ArrayRef<SourceLocation> MapTypeModifiersLoc,
      CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo &MapperId,
      OpenMPMapClauseKind MapType, bool IsMapTypeImplicit,
      SourceLocation MapLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
      const OMPVarListLocTy &Locs, bool NoDiagnose = false,
      ArrayRef<Expr *> UnresolvedMappers = {});
  /// Called on well-formed 'num_teams' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNumTeamsClause(ArrayRef<Expr *> VarList,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'thread_limit' clause.
  CLANG_ABI OMPClause *ActOnOpenMPThreadLimitClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed 'priority' clause.
  CLANG_ABI OMPClause *ActOnOpenMPPriorityClause(Expr *Priority, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'dist_schedule' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDistScheduleClause(
      OpenMPDistScheduleClauseKind Kind, Expr *ChunkSize,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation KindLoc,
      SourceLocation CommaLoc, SourceLocation EndLoc);
  /// Called on well-formed 'defaultmap' clause.
  CLANG_ABI OMPClause *ActOnOpenMPDefaultmapClause(
      OpenMPDefaultmapClauseModifier M, OpenMPDefaultmapClauseKind Kind,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation MLoc,
      SourceLocation KindLoc, SourceLocation EndLoc);
  /// Called on well-formed 'to' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPToClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                      ArrayRef<SourceLocation> MotionModifiersLoc,
                      CXXScopeSpec &MapperIdScopeSpec,
                      DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                      ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                      ArrayRef<Expr *> UnresolvedMappers = {});
  /// Called on well-formed 'from' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPFromClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                        ArrayRef<SourceLocation> MotionModifiersLoc,
                        CXXScopeSpec &MapperIdScopeSpec,
                        DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                        ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                        ArrayRef<Expr *> UnresolvedMappers = {});
  /// Called on well-formed 'use_device_ptr' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUseDevicePtrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs);
  /// Called on well-formed 'use_device_addr' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUseDeviceAddrClause(ArrayRef<Expr *> VarList,
                                            const OMPVarListLocTy &Locs);
  /// Called on well-formed 'is_device_ptr' clause.
  CLANG_ABI OMPClause *ActOnOpenMPIsDevicePtrClause(ArrayRef<Expr *> VarList,
                                          const OMPVarListLocTy &Locs);
  /// Called on well-formed 'has_device_addr' clause.
  CLANG_ABI OMPClause *ActOnOpenMPHasDeviceAddrClause(ArrayRef<Expr *> VarList,
                                            const OMPVarListLocTy &Locs);
  /// Called on well-formed 'nontemporal' clause.
  CLANG_ABI OMPClause *ActOnOpenMPNontemporalClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);

  /// Data for list of allocators.
  struct UsesAllocatorsData {
    /// Allocator.
    Expr *Allocator = nullptr;
    /// Allocator traits.
    Expr *AllocatorTraits = nullptr;
    /// Locations of '(' and ')' symbols.
    SourceLocation LParenLoc, RParenLoc;
  };
  /// Called on well-formed 'uses_allocators' clause.
  CLANG_ABI OMPClause *ActOnOpenMPUsesAllocatorClause(SourceLocation StartLoc,
                                            SourceLocation LParenLoc,
                                            SourceLocation EndLoc,
                                            ArrayRef<UsesAllocatorsData> Data);
  /// Called on well-formed 'affinity' clause.
  CLANG_ABI OMPClause *ActOnOpenMPAffinityClause(SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation ColonLoc,
                                       SourceLocation EndLoc, Expr *Modifier,
                                       ArrayRef<Expr *> Locators);
  /// Called on a well-formed 'bind' clause.
  CLANG_ABI OMPClause *ActOnOpenMPBindClause(OpenMPBindClauseKind Kind,
                                   SourceLocation KindLoc,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_dyn_cgroup_mem' clause.
  CLANG_ABI OMPClause *ActOnOpenMPXDynCGroupMemClause(Expr *Size, SourceLocation StartLoc,
                                            SourceLocation LParenLoc,
                                            SourceLocation EndLoc);

  /// Called on well-formed 'doacross' clause.
  CLANG_ABI OMPClause *
  ActOnOpenMPDoacrossClause(OpenMPDoacrossClauseModifier DepType,
                            SourceLocation DepLoc, SourceLocation ColonLoc,
                            ArrayRef<Expr *> VarList, SourceLocation StartLoc,
                            SourceLocation LParenLoc, SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_attribute' clause.
  CLANG_ABI OMPClause *ActOnOpenMPXAttributeClause(ArrayRef<const Attr *> Attrs,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_bare' clause.
  CLANG_ABI OMPClause *ActOnOpenMPXBareClause(SourceLocation StartLoc,
                                    SourceLocation EndLoc);

  CLANG_ABI ExprResult ActOnOMPArraySectionExpr(Expr *Base, SourceLocation LBLoc,
                                      Expr *LowerBound,
                                      SourceLocation ColonLocFirst,
                                      SourceLocation ColonLocSecond,
                                      Expr *Length, Expr *Stride,
                                      SourceLocation RBLoc);
  CLANG_ABI ExprResult ActOnOMPArrayShapingExpr(Expr *Base, SourceLocation LParenLoc,
                                      SourceLocation RParenLoc,
                                      ArrayRef<Expr *> Dims,
                                      ArrayRef<SourceRange> Brackets);

  /// Data structure for iterator expression.
  struct OMPIteratorData {
    IdentifierInfo *DeclIdent = nullptr;
    SourceLocation DeclIdentLoc;
    ParsedType Type;
    OMPIteratorExpr::IteratorRange Range;
    SourceLocation AssignLoc;
    SourceLocation ColonLoc;
    SourceLocation SecColonLoc;
  };

  CLANG_ABI ExprResult ActOnOMPIteratorExpr(Scope *S, SourceLocation IteratorKwLoc,
                                  SourceLocation LLoc, SourceLocation RLoc,
                                  ArrayRef<OMPIteratorData> Data);

  CLANG_ABI void handleOMPAssumeAttr(Decl *D, const ParsedAttr &AL);

  /// Setter and getter functions for device_num.
  CLANG_ABI void setOpenMPDeviceNum(int Num);

  CLANG_ABI int getOpenMPDeviceNum() const;

  CLANG_ABI void setOpenMPDeviceNumID(StringRef ID);

private:
  void *VarDataSharingAttributesStack;

  /// Number of nested '#pragma omp declare target' directives.
  SmallVector<DeclareTargetContextInfo, 4> DeclareTargetNesting;

  /// Initialization of data-sharing attributes stack.
  void InitDataSharingAttributesStack();
  void DestroyDataSharingAttributesStack();

  /// Returns OpenMP nesting level for current directive.
  unsigned getOpenMPNestingLevel() const;

  /// Adjusts the function scopes index for the target-based regions.
  void adjustOpenMPTargetScopeIndex(unsigned &FunctionScopesIndex,
                                    unsigned Level) const;

  /// Returns the number of scopes associated with the construct on the given
  /// OpenMP level.
  int getNumberOfConstructScopes(unsigned Level) const;

  /// Push new OpenMP function region for non-capturing function.
  void pushOpenMPFunctionRegion();

  /// Pop OpenMP function region for non-capturing function.
  void popOpenMPFunctionRegion(const sema::FunctionScopeInfo *OldFSI);

  /// Analyzes and checks a loop nest for use by a loop transformation.
  ///
  /// \param Kind          The loop transformation directive kind.
  /// \param NumLoops      How many nested loops the directive is expecting.
  /// \param AStmt         Associated statement of the transformation directive.
  /// \param LoopHelpers   [out] The loop analysis result.
  /// \param Body          [out] The body code nested in \p NumLoops loop.
  /// \param OriginalInits [out] Collection of statements and declarations that
  ///                      must have been executed/declared before entering the
  ///                      loop.
  ///
  /// \return Whether there was any error.
  bool checkTransformableLoopNest(
      OpenMPDirectiveKind Kind, Stmt *AStmt, int NumLoops,
      SmallVectorImpl<OMPLoopBasedDirective::HelperExprs> &LoopHelpers,
      Stmt *&Body, SmallVectorImpl<SmallVector<Stmt *, 0>> &OriginalInits);

  /// Helper to keep information about the current `omp begin/end declare
  /// variant` nesting.
  struct OMPDeclareVariantScope {
    /// The associated OpenMP context selector.
    OMPTraitInfo *TI;

    /// The associated OpenMP context selector mangling.
    std::string NameSuffix;

    CLANG_ABI OMPDeclareVariantScope(OMPTraitInfo &TI);
  };

  /// Return the OMPTraitInfo for the surrounding scope, if any.
  OMPTraitInfo *getOMPTraitInfoForSurroundingScope() {
    return OMPDeclareVariantScopes.empty() ? nullptr
                                           : OMPDeclareVariantScopes.back().TI;
  }

  /// The current `omp begin/end declare variant` scopes.
  SmallVector<OMPDeclareVariantScope, 4> OMPDeclareVariantScopes;

  /// The current `omp begin/end assumes` scopes.
  SmallVector<OMPAssumeAttr *, 4> OMPAssumeScoped;

  /// All `omp assumes` we encountered so far.
  SmallVector<OMPAssumeAttr *, 4> OMPAssumeGlobal;

  /// Device number specified by the context selector.
  int DeviceNum = -1;

  /// Device number identifier specified by the context selector.
  StringRef DeviceNumID;
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAOPENMP_H
