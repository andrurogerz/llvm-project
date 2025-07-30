//==- BlockCounter.h - ADT for counting block visits ---------------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines BlockCounter, an abstract data type used to count
//  the number of times a given block has been visited along a path
//  analyzed by CoreEngine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BLOCKCOUNTER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BLOCKCOUNTER_H

#include "clang/Support/Compiler.h"
#include "llvm/Support/Allocator.h"

namespace clang {

class StackFrameContext;

namespace ento {

/// \class BlockCounter
/// An abstract data type used to count the number of times a given
/// block has been visited along a path analyzed by CoreEngine.
class BlockCounter {
  void *Data;

  BlockCounter(void *D) : Data(D) {}

public:
  BlockCounter() : Data(nullptr) {}

  CLANG_ABI unsigned getNumVisited(const StackFrameContext *CallSite,
                         unsigned BlockID) const;

  class Factory {
    void *F;
  public:
    CLANG_ABI Factory(llvm::BumpPtrAllocator& Alloc);
    CLANG_ABI ~Factory();

    CLANG_ABI BlockCounter GetEmptyCounter();
    CLANG_ABI BlockCounter IncrementCount(BlockCounter BC,
                                  const StackFrameContext *CallSite,
                                  unsigned BlockID);
  };

  friend class Factory;
};

} // end GR namespace

} // end clang namespace

#endif
