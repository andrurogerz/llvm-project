//=--- CommonBugCategories.h - Provides common issue categories -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H

#include "clang/Support/Compiler.h"

// Common strings used for the "category" of many static analyzer issues.
namespace clang {
namespace ento {
namespace categories {
extern const CLANG_ABI char *const AppleAPIMisuse;
extern const CLANG_ABI char *const CoreFoundationObjectiveC;
extern const CLANG_ABI char *const LogicError;
extern const CLANG_ABI char *const MemoryRefCount;
extern const CLANG_ABI char *const MemoryError;
extern const CLANG_ABI char *const UnixAPI;
extern const CLANG_ABI char *const CXXObjectLifecycle;
extern const CLANG_ABI char *const CXXMoveSemantics;
extern const CLANG_ABI char *const SecurityError;
extern const CLANG_ABI char *const UnusedCode;
extern const CLANG_ABI char *const TaintedData;
} // namespace categories
} // namespace ento
} // namespace clang
#endif
