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
      (AnnotationName + ":" + llvm::Twine(Value->getZExtValue()))
          .str();
  D->addAttr(AnnotateAttr::Create(S.Context, Annotation, nullptr, 0,
                                  Attr.getRange()));
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
    return attachArgumentAnnotation(S, D, Attr,
                                    "ownership_returns_argument");
  }
};

} // namespace

static ParsedAttrInfoRegistry::Add<OwnershipReallocatesAttrInfo>
    Reallocates("ownership_reallocates", "conditional ownership transfer");
static ParsedAttrInfoRegistry::Add<OwnershipReturnsArgumentAttrInfo>
    ReturnsArgument("ownership_returns_argument",
                    "argument-preserving returned ownership");
