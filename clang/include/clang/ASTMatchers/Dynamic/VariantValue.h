//===--- VariantValue.h - Polymorphic value type ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Polymorphic value type.
///
/// Supports all the types required for dynamic Matcher construction.
///  Used by the registry to construct matchers in a generic way.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_DYNAMIC_VARIANTVALUE_H
#define LLVM_CLANG_ASTMATCHERS_DYNAMIC_VARIANTVALUE_H

#include "clang/Support/Compiler.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include <memory>
#include <optional>
#include <vector>

namespace clang {
namespace ast_matchers {
namespace dynamic {

/// Kind identifier.
///
/// It supports all types that VariantValue can contain.
class ArgKind {
 public:
  enum Kind {
    AK_Matcher,
    AK_Node,
    AK_Boolean,
    AK_Double,
    AK_Unsigned,
    AK_String
  };
  /// Constructor for non-matcher types.
  ArgKind(Kind K) : K(K) { assert(K != AK_Matcher); }

  /// Constructor for matcher types.
  static ArgKind MakeMatcherArg(ASTNodeKind MatcherKind) {
    return ArgKind{AK_Matcher, MatcherKind};
  }

  static ArgKind MakeNodeArg(ASTNodeKind MatcherKind) {
    return ArgKind{AK_Node, MatcherKind};
  }

  Kind getArgKind() const { return K; }
  ASTNodeKind getMatcherKind() const {
    assert(K == AK_Matcher);
    return NodeKind;
  }
  ASTNodeKind getNodeKind() const {
    assert(K == AK_Node);
    return NodeKind;
  }

  /// Determines if this type can be converted to \p To.
  ///
  /// \param To the requested destination type.
  ///
  /// \param Specificity value corresponding to the "specificity" of the
  ///   conversion.
  CLANG_ABI bool isConvertibleTo(ArgKind To, unsigned *Specificity) const;

  bool operator<(const ArgKind &Other) const {
    if ((K == AK_Matcher && Other.K == AK_Matcher) ||
        (K == AK_Node && Other.K == AK_Node))
      return NodeKind < Other.NodeKind;
    return K < Other.K;
  }

  /// String representation of the type.
  CLANG_ABI std::string asString() const;

private:
  ArgKind(Kind K, ASTNodeKind NK) : K(K), NodeKind(NK) {}
  Kind K;
  ASTNodeKind NodeKind;
};

using ast_matchers::internal::DynTypedMatcher;

/// A variant matcher object.
///
/// The purpose of this object is to abstract simple and polymorphic matchers
/// into a single object type.
/// Polymorphic matchers might be implemented as a list of all the possible
/// overloads of the matcher. \c VariantMatcher knows how to select the
/// appropriate overload when needed.
/// To get a real matcher object out of a \c VariantMatcher you can do:
///  - getSingleMatcher() which returns a matcher, only if it is not ambiguous
///    to decide which matcher to return. Eg. it contains only a single
///    matcher, or a polymorphic one with only one overload.
///  - hasTypedMatcher<T>()/getTypedMatcher<T>(): These calls will determine if
///    the underlying matcher(s) can unambiguously return a Matcher<T>.
class VariantMatcher {
  /// Methods that depend on T from hasTypedMatcher/getTypedMatcher.
  class MatcherOps {
  public:
    MatcherOps(ASTNodeKind NodeKind) : NodeKind(NodeKind) {}

    CLANG_ABI bool canConstructFrom(const DynTypedMatcher &Matcher,
                          bool &IsExactMatch) const;

    /// Convert \p Matcher the destination type and return it as a new
    /// DynTypedMatcher.
    CLANG_ABI DynTypedMatcher convertMatcher(const DynTypedMatcher &Matcher) const;

    /// Constructs a variadic typed matcher from \p InnerMatchers.
    /// Will try to convert each inner matcher to the destination type and
    /// return std::nullopt if it fails to do so.
    CLANG_ABI std::optional<DynTypedMatcher>
    constructVariadicOperator(DynTypedMatcher::VariadicOperator Op,
                              ArrayRef<VariantMatcher> InnerMatchers) const;

  private:
    ASTNodeKind NodeKind;
  };

  /// Payload interface to be specialized by each matcher type.
  ///
  /// It follows a similar interface as VariantMatcher itself.
  class CLANG_ABI Payload {
  public:
    virtual ~Payload();
    virtual std::optional<DynTypedMatcher> getSingleMatcher() const = 0;
    virtual std::string getTypeAsString() const = 0;
    virtual std::optional<DynTypedMatcher>
    getTypedMatcher(const MatcherOps &Ops) const = 0;
    virtual bool isConvertibleTo(ASTNodeKind Kind,
                                 unsigned *Specificity) const = 0;
  };

public:
  /// A null matcher.
  CLANG_ABI VariantMatcher();

  /// Clones the provided matcher.
  CLANG_ABI static VariantMatcher SingleMatcher(const DynTypedMatcher &Matcher);

  /// Clones the provided matchers.
  ///
  /// They should be the result of a polymorphic matcher.
  CLANG_ABI static VariantMatcher
  PolymorphicMatcher(std::vector<DynTypedMatcher> Matchers);

  /// Creates a 'variadic' operator matcher.
  ///
  /// It will bind to the appropriate type on getTypedMatcher<T>().
  CLANG_ABI static VariantMatcher
  VariadicOperatorMatcher(DynTypedMatcher::VariadicOperator Op,
                          std::vector<VariantMatcher> Args);

  /// Makes the matcher the "null" matcher.
  CLANG_ABI void reset();

  /// Whether the matcher is null.
  bool isNull() const { return !Value; }

  /// Return a single matcher, if there is no ambiguity.
  ///
  /// \returns the matcher, if there is only one matcher. An empty Optional, if
  /// the underlying matcher is a polymorphic matcher with more than one
  /// representation.
  CLANG_ABI std::optional<DynTypedMatcher> getSingleMatcher() const;

  /// Determines if the contained matcher can be converted to
  ///   \c Matcher<T>.
  ///
  /// For the Single case, it returns true if it can be converted to
  /// \c Matcher<T>.
  /// For the Polymorphic case, it returns true if one, and only one, of the
  /// overloads can be converted to \c Matcher<T>. If there are more than one
  /// that can, the result would be ambiguous and false is returned.
  template <class T>
  bool hasTypedMatcher() const {
    return hasTypedMatcher(ASTNodeKind::getFromNodeKind<T>());
  }

  bool hasTypedMatcher(ASTNodeKind NK) const {
    if (!Value) return false;
    return Value->getTypedMatcher(MatcherOps(NK)).has_value();
  }

  /// Determines if the contained matcher can be converted to \p Kind.
  ///
  /// \param Kind the requested destination type.
  ///
  /// \param Specificity value corresponding to the "specificity" of the
  ///   conversion.
  bool isConvertibleTo(ASTNodeKind Kind, unsigned *Specificity) const {
    if (Value)
      return Value->isConvertibleTo(Kind, Specificity);
    return false;
  }

  /// Return this matcher as a \c Matcher<T>.
  ///
  /// Handles the different types (Single, Polymorphic) accordingly.
  /// Asserts that \c hasTypedMatcher<T>() is true.
  template <class T>
  ast_matchers::internal::Matcher<T> getTypedMatcher() const {
    assert(hasTypedMatcher<T>() && "hasTypedMatcher<T>() == false");
    return Value->getTypedMatcher(MatcherOps(ASTNodeKind::getFromNodeKind<T>()))
        ->template convertTo<T>();
  }

  DynTypedMatcher getTypedMatcher(ASTNodeKind NK) const {
    assert(hasTypedMatcher(NK) && "hasTypedMatcher(NK) == false");
    return *Value->getTypedMatcher(MatcherOps(NK));
  }

  /// String representation of the type of the value.
  ///
  /// If the underlying matcher is a polymorphic one, the string will show all
  /// the types.
  CLANG_ABI std::string getTypeAsString() const;

private:
  explicit VariantMatcher(std::shared_ptr<Payload> Value)
      : Value(std::move(Value)) {}


  class SinglePayload;
  class PolymorphicPayload;
  class VariadicOpPayload;

  std::shared_ptr<const Payload> Value;
};

/// Variant value class.
///
/// Basically, a tagged union with value type semantics.
/// It is used by the registry as the return value and argument type for the
/// matcher factory methods.
/// It can be constructed from any of the supported types. It supports
/// copy/assignment.
///
/// Supported types:
///  - \c bool
//   - \c double
///  - \c unsigned
///  - \c llvm::StringRef
///  - \c VariantMatcher (\c DynTypedMatcher / \c Matcher<T>)
class VariantValue {
public:
  VariantValue() : Type(VT_Nothing) {}

  CLANG_ABI VariantValue(const VariantValue &Other);
  CLANG_ABI ~VariantValue();
  CLANG_ABI VariantValue &operator=(const VariantValue &Other);

  /// Specific constructors for each supported type.
  CLANG_ABI VariantValue(bool Boolean);
  CLANG_ABI VariantValue(double Double);
  CLANG_ABI VariantValue(unsigned Unsigned);
  CLANG_ABI VariantValue(StringRef String);
  CLANG_ABI VariantValue(ASTNodeKind NodeKind);
  CLANG_ABI VariantValue(const VariantMatcher &Matchers);

  /// Constructs an \c unsigned value (disambiguation from bool).
  VariantValue(int Signed) : VariantValue(static_cast<unsigned>(Signed)) {}

  /// Returns true iff this is not an empty value.
  explicit operator bool() const { return hasValue(); }
  bool hasValue() const { return Type != VT_Nothing; }

  /// Boolean value functions.
  CLANG_ABI bool isBoolean() const;
  CLANG_ABI bool getBoolean() const;
  CLANG_ABI void setBoolean(bool Boolean);

  /// Double value functions.
  CLANG_ABI bool isDouble() const;
  CLANG_ABI double getDouble() const;
  CLANG_ABI void setDouble(double Double);

  /// Unsigned value functions.
  CLANG_ABI bool isUnsigned() const;
  CLANG_ABI unsigned getUnsigned() const;
  CLANG_ABI void setUnsigned(unsigned Unsigned);

  /// String value functions.
  CLANG_ABI bool isString() const;
  CLANG_ABI const std::string &getString() const;
  CLANG_ABI void setString(StringRef String);

  CLANG_ABI bool isNodeKind() const;
  CLANG_ABI const ASTNodeKind &getNodeKind() const;
  CLANG_ABI void setNodeKind(ASTNodeKind NodeKind);

  /// Matcher value functions.
  CLANG_ABI bool isMatcher() const;
  CLANG_ABI const VariantMatcher &getMatcher() const;
  CLANG_ABI void setMatcher(const VariantMatcher &Matcher);

  /// Determines if the contained value can be converted to \p Kind.
  ///
  /// \param Kind the requested destination type.
  ///
  /// \param Specificity value corresponding to the "specificity" of the
  ///   conversion.
  CLANG_ABI bool isConvertibleTo(ArgKind Kind, unsigned* Specificity) const;

  /// Determines if the contained value can be converted to any kind
  /// in \p Kinds.
  ///
  /// \param Kinds the requested destination types.
  ///
  /// \param Specificity value corresponding to the "specificity" of the
  ///   conversion. It is the maximum specificity of all the possible
  ///   conversions.
  CLANG_ABI bool isConvertibleTo(ArrayRef<ArgKind> Kinds, unsigned *Specificity) const;

  /// String representation of the type of the value.
  CLANG_ABI std::string getTypeAsString() const;

private:
  void reset();

  /// All supported value types.
  enum ValueType {
    VT_Nothing,
    VT_Boolean,
    VT_Double,
    VT_Unsigned,
    VT_String,
    VT_Matcher,
    VT_NodeKind
  };

  /// All supported value types.
  union AllValues {
    unsigned Unsigned;
    double Double;
    bool Boolean;
    std::string *String;
    VariantMatcher *Matcher;
    ASTNodeKind *NodeKind;
  };

  ValueType Type;
  AllValues Value;
};

} // end namespace dynamic
} // end namespace ast_matchers
} // end namespace clang

#endif // LLVM_CLANG_ASTMATCHERS_DYNAMIC_VARIANTVALUE_H
