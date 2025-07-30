//===--- CommentParser.h - Doxygen comment parser ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Doxygen comment parser.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_COMMENTPARSER_H
#define LLVM_CLANG_AST_COMMENTPARSER_H

#include "clang/Support/Compiler.h"
#include "clang/AST/Comment.h"
#include "clang/AST/CommentLexer.h"
#include "clang/AST/CommentSema.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/Allocator.h"

namespace clang {
class SourceManager;

namespace comments {
class CommandTraits;

/// Doxygen comment parser.
class Parser {
  Parser(const Parser &) = delete;
  void operator=(const Parser &) = delete;

  friend class TextTokenRetokenizer;

  Lexer &L;

  Sema &S;

  /// Allocator for anything that goes into AST nodes.
  llvm::BumpPtrAllocator &Allocator;

  /// Source manager for the comment being parsed.
  const SourceManager &SourceMgr;

  DiagnosticsEngine &Diags;

  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) {
    return Diags.Report(Loc, DiagID);
  }

  const CommandTraits &Traits;

  /// Current lookahead token.  We can safely assume that all tokens are from
  /// a single source file.
  Token Tok;

  /// A stack of additional lookahead tokens.
  SmallVector<Token, 8> MoreLATokens;

  void consumeToken() {
    if (MoreLATokens.empty())
      L.lex(Tok);
    else
      Tok = MoreLATokens.pop_back_val();
  }

  void putBack(const Token &OldTok) {
    MoreLATokens.push_back(Tok);
    Tok = OldTok;
  }

  void putBack(ArrayRef<Token> Toks) {
    if (Toks.empty())
      return;

    MoreLATokens.push_back(Tok);
    MoreLATokens.append(Toks.rbegin(), std::prev(Toks.rend()));

    Tok = Toks[0];
  }

  bool isTokBlockCommand() {
    return (Tok.is(tok::backslash_command) || Tok.is(tok::at_command)) &&
           Traits.getCommandInfo(Tok.getCommandID())->IsBlockCommand;
  }

public:
  CLANG_ABI Parser(Lexer &L, Sema &S, llvm::BumpPtrAllocator &Allocator,
         const SourceManager &SourceMgr, DiagnosticsEngine &Diags,
         const CommandTraits &Traits);

  /// Parse arguments for \\param command.
  CLANG_ABI void parseParamCommandArgs(ParamCommandComment *PC,
                             TextTokenRetokenizer &Retokenizer);

  /// Parse arguments for \\tparam command.
  CLANG_ABI void parseTParamCommandArgs(TParamCommandComment *TPC,
                              TextTokenRetokenizer &Retokenizer);

  CLANG_ABI ArrayRef<Comment::Argument>
  parseCommandArgs(TextTokenRetokenizer &Retokenizer, unsigned NumArgs);

  /// Parse arguments for \throws command supported args are in form of class
  /// or template.
  CLANG_ABI ArrayRef<Comment::Argument>
  parseThrowCommandArgs(TextTokenRetokenizer &Retokenizer, unsigned NumArgs);

  CLANG_ABI ArrayRef<Comment::Argument>
  parseParCommandArgs(TextTokenRetokenizer &Retokenizer, unsigned NumArgs);

  CLANG_ABI BlockCommandComment *parseBlockCommand();
  CLANG_ABI InlineCommandComment *parseInlineCommand();

  CLANG_ABI HTMLStartTagComment *parseHTMLStartTag();
  CLANG_ABI HTMLEndTagComment *parseHTMLEndTag();

  CLANG_ABI BlockContentComment *parseParagraphOrBlockCommand();

  CLANG_ABI VerbatimBlockComment *parseVerbatimBlock();
  CLANG_ABI VerbatimLineComment *parseVerbatimLine();
  CLANG_ABI BlockContentComment *parseBlockContent();
  CLANG_ABI FullComment *parseFullComment();
};

} // end namespace comments
} // end namespace clang

#endif
