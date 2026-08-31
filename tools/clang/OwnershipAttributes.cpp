// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/Twine.h"

using namespace clang;

namespace {

static ParsedAttrInfo::AttrHandling
attachArgumentAnnotation(Sema &S, Decl *D, const ParsedAttr &Attr,
                         StringRef AnnotationName) {
  const auto *Function = dyn_cast<FunctionDecl>(D);
  const Expr *Argument = Attr.getNumArgs() == 1 && Attr.isArgExpr(0)
                             ? Attr.getArgAsExpr(0)
                             : nullptr;
  std::optional<llvm::APSInt> Value =
      Argument ? Argument->getIntegerConstantExpr(S.Context) : std::nullopt;
  if (!Function || !Value || Value->isSigned() && Value->isNegative() ||
      Value->isZero() || Value->ugt(Function->getNumParams())) {
    unsigned ID = S.getDiagnostics().getCustomDiagID(
        DiagnosticsEngine::Error,
        "%0 requires one integer argument naming a one-based function "
        "parameter index");
    S.Diag(Attr.getLoc(), ID) << Attr;
    return ParsedAttrInfo::AttributeNotApplied;
  }
  std::string Annotation =
      (AnnotationName + ":" + llvm::Twine(Value->getZExtValue())).str();
  D->addAttr(
      AnnotateAttr::Create(S.Context, Annotation, nullptr, 0, Attr.getRange()));
  return ParsedAttrInfo::AttributeApplied;
}

static ParsedAttrInfo::AttrHandling
attachFamilyArgumentAnnotation(Sema &S, Decl *D, const ParsedAttr &Attr,
                               StringRef AnnotationName) {
  if (isa<ParmVarDecl>(D) && Attr.getNumArgs() == 1 && Attr.isArgIdent(0)) {
    IdentifierInfo *Family = Attr.getArgAsIdent(0)->Ident;
    std::string Annotation = (AnnotationName + ":" + Family->getName()).str();
    D->addAttr(AnnotateAttr::Create(S.Context, Annotation, nullptr, 0,
                                    Attr.getRange()));
    return ParsedAttrInfo::AttributeApplied;
  }
  const auto *Function = dyn_cast<FunctionDecl>(D);
  IdentifierInfo *Family = Attr.getNumArgs() == 2 && Attr.isArgIdent(0)
                               ? Attr.getArgAsIdent(0)->Ident
                               : nullptr;
  const Expr *Argument = Attr.getNumArgs() == 2 && Attr.isArgExpr(1)
                             ? Attr.getArgAsExpr(1)
                             : nullptr;
  std::optional<llvm::APSInt> Value =
      Argument ? Argument->getIntegerConstantExpr(S.Context) : std::nullopt;
  if (!Function || !Family || !Value ||
      (Value->isSigned() && Value->isNegative()) || Value->isZero() ||
      Value->ugt(Function->getNumParams())) {
    unsigned ID = S.getDiagnostics().getCustomDiagID(
        DiagnosticsEngine::Error,
        "%0 requires an ownership-family identifier on a parameter, or an "
        "ownership-family identifier followed by a one-based parameter index "
        "on a function");
    S.Diag(Attr.getLoc(), ID) << Attr;
    return ParsedAttrInfo::AttributeNotApplied;
  }
  std::string Annotation = (AnnotationName + ":" + Family->getName() + ":" +
                            llvm::Twine(Value->getZExtValue()))
                               .str();
  D->addAttr(
      AnnotateAttr::Create(S.Context, Annotation, nullptr, 0, Attr.getRange()));
  return ParsedAttrInfo::AttributeApplied;
}

static ParsedAttrInfo::AttrHandling
attachValueFamilyAnnotation(Sema &S, Decl *D, const ParsedAttr &Attr,
                            StringRef AnnotationName) {
  IdentifierInfo *Family = Attr.getNumArgs() == 1 && Attr.isArgIdent(0)
                               ? Attr.getArgAsIdent(0)->Ident
                               : nullptr;
  if (!isa<ValueDecl>(D) || !Family) {
    unsigned ID = S.getDiagnostics().getCustomDiagID(
        DiagnosticsEngine::Error,
        "%0 requires one ownership-family identifier on a value, storage, "
        "parameter, field, or function return declaration");
    S.Diag(Attr.getLoc(), ID) << Attr;
    return ParsedAttrInfo::AttributeNotApplied;
  }
  std::string Annotation = (AnnotationName + ":" + Family->getName()).str();
  D->addAttr(
      AnnotateAttr::Create(S.Context, Annotation, nullptr, 0, Attr.getRange()));
  return ParsedAttrInfo::AttributeApplied;
}

struct OwnershipReallocatesAttrInfo final : ParsedAttrInfo {
  OwnershipReallocatesAttrInfo() {
    OptArgs = 1;
    static constexpr Spelling SpellingsList[] = {
        {ParsedAttr::AS_GNU, "ownership_reallocates"},
        {ParsedAttr::AS_C23, "ownership_reallocates"},
        {ParsedAttr::AS_CXX11, "ownership_reallocates"}};
    Spellings = SpellingsList;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    return attachArgumentAnnotation(S, D, Attr, "ownership_reallocates");
  }
};

struct OwnershipReturnsArgumentAttrInfo final : ParsedAttrInfo {
  OwnershipReturnsArgumentAttrInfo() {
    OptArgs = 1;
    static constexpr Spelling SpellingsList[] = {
        {ParsedAttr::AS_GNU, "ownership_returns_argument"},
        {ParsedAttr::AS_C23, "ownership_returns_argument"},
        {ParsedAttr::AS_CXX11, "ownership_returns_argument"}};
    Spellings = SpellingsList;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    return attachArgumentAnnotation(S, D, Attr, "ownership_returns_argument");
  }
};

#define DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(ClassName, SpellingName,             \
                                          AnnotationName)                      \
  struct ClassName final : ParsedAttrInfo {                                    \
    ClassName() {                                                              \
      OptArgs = 2;                                                             \
      static constexpr Spelling SpellingsList[] = {                            \
          {ParsedAttr::AS_GNU, SpellingName},                                  \
          {ParsedAttr::AS_C23, SpellingName},                                  \
          {ParsedAttr::AS_CXX11, SpellingName}};                               \
      Spellings = SpellingsList;                                               \
    }                                                                          \
                                                                               \
    AttrHandling handleDeclAttribute(Sema &S, Decl *D,                         \
                                     const ParsedAttr &Attr) const override {  \
      return attachFamilyArgumentAnnotation(S, D, Attr, AnnotationName);       \
    }                                                                          \
  }

DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipConstructsAttrInfo,
                                  "ownership_constructs",
                                  "ownership_constructs");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipDestroysAttrInfo,
                                  "ownership_destroys", "ownership_destroys");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipRequiresHandleAttrInfo,
                                  "ownership_requires_handle",
                                  "ownership_requires_handle");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipStaticAttrInfo, "ownership_static",
                                  "ownership_static");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipRequiresTokenAttrInfo,
                                  "ownership_requires_token",
                                  "ownership_requires_token");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipConsumesTokenAttrInfo,
                                  "ownership_consumes_token",
                                  "ownership_consumes_token");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipGrantsTokenAttrInfo,
                                  "ownership_grants_token",
                                  "ownership_grants_token");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipGrantsDuplicableTokenAttrInfo,
                                  "ownership_grants_duplicable_token",
                                  "ownership_grants_duplicable_token");
DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE(OwnershipConsumesAnyTokenAttrInfo,
                                  "ownership_consumes_any_token",
                                  "ownership_consumes_any_token");

#undef DEFINE_FAMILY_OWNERSHIP_ATTRIBUTE

#define DEFINE_VALUE_OWNERSHIP_ATTRIBUTE(ClassName, SpellingName,              \
                                         AnnotationName)                       \
  struct ClassName final : ParsedAttrInfo {                                    \
    ClassName() {                                                              \
      OptArgs = 1;                                                             \
      static constexpr Spelling SpellingsList[] = {                            \
          {ParsedAttr::AS_GNU, SpellingName},                                  \
          {ParsedAttr::AS_C23, SpellingName},                                  \
          {ParsedAttr::AS_CXX11, SpellingName}};                               \
      Spellings = SpellingsList;                                               \
    }                                                                          \
                                                                               \
    AttrHandling handleDeclAttribute(Sema &S, Decl *D,                         \
                                     const ParsedAttr &Attr) const override {  \
      return attachValueFamilyAnnotation(S, D, Attr, AnnotationName);          \
    }                                                                          \
  }

DEFINE_VALUE_OWNERSHIP_ATTRIBUTE(OwnershipHoldsHandleAttrInfo,
                                 "ownership_holds_handle",
                                 "ownership_holds_handle");
DEFINE_VALUE_OWNERSHIP_ATTRIBUTE(OwnershipHoldsTokenAttrInfo,
                                 "ownership_holds_token",
                                 "ownership_holds_token");
DEFINE_VALUE_OWNERSHIP_ATTRIBUTE(OwnershipHoldsDuplicableTokenAttrInfo,
                                 "ownership_holds_duplicable_token",
                                 "ownership_holds_duplicable_token");

#undef DEFINE_VALUE_OWNERSHIP_ATTRIBUTE

} // namespace

static ParsedAttrInfoRegistry::Add<OwnershipReallocatesAttrInfo>
    Reallocates("ownership_reallocates", "conditional ownership transfer");
static ParsedAttrInfoRegistry::Add<OwnershipReturnsArgumentAttrInfo>
    ReturnsArgument("ownership_returns_argument",
                    "argument-preserving returned ownership");
static ParsedAttrInfoRegistry::Add<OwnershipConstructsAttrInfo>
    Constructs("ownership_constructs", "constructs an owned object");
static ParsedAttrInfoRegistry::Add<OwnershipDestroysAttrInfo>
    Destroys("ownership_destroys", "destroys an owned object");
static ParsedAttrInfoRegistry::Add<OwnershipRequiresHandleAttrInfo>
    RequiresHandle("ownership_requires_handle",
                   "requires an explicit live-object handle");
static ParsedAttrInfoRegistry::Add<OwnershipStaticAttrInfo>
    Static("ownership_static", "accepts static object initialization");
static ParsedAttrInfoRegistry::Add<OwnershipRequiresTokenAttrInfo>
    RequiresToken("ownership_requires_token", "requires a capability token");
static ParsedAttrInfoRegistry::Add<OwnershipConsumesTokenAttrInfo>
    ConsumesToken("ownership_consumes_token", "consumes a capability token");
static ParsedAttrInfoRegistry::Add<OwnershipGrantsTokenAttrInfo>
    GrantsToken("ownership_grants_token", "grants a linear capability token");
static ParsedAttrInfoRegistry::Add<OwnershipGrantsDuplicableTokenAttrInfo>
    GrantsDuplicableToken("ownership_grants_duplicable_token",
                          "grants a duplicable capability token");
static ParsedAttrInfoRegistry::Add<OwnershipConsumesAnyTokenAttrInfo>
    ConsumesAnyToken("ownership_consumes_any_token",
                     "consumes one member of an alternative token set");
static ParsedAttrInfoRegistry::Add<OwnershipHoldsHandleAttrInfo>
    HoldsHandle("ownership_holds_handle",
                "adds a handle class to a value's ownership type");
static ParsedAttrInfoRegistry::Add<OwnershipHoldsTokenAttrInfo>
    HoldsToken("ownership_holds_token",
               "adds a linear token to a value's ownership type");
static ParsedAttrInfoRegistry::Add<OwnershipHoldsDuplicableTokenAttrInfo>
    HoldsDuplicableToken("ownership_holds_duplicable_token",
                         "adds a duplicable token to a value's ownership type");
