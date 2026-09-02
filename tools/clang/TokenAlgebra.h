// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NTLIBC_TOKEN_ALGEBRA_H
#define NTLIBC_TOKEN_ALGEBRA_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <optional>

namespace ntlibc::algebra {

/* A token sort is nominal: its typedef declaration, not the spelling of its
 * name or of an unrelated annotation, supplies the policy qualifiers. */
using TokenSort = clang::TypedefNameDecl;

inline const TokenSort *findTokenSort(clang::ASTContext &Context,
                                      llvm::StringRef Name) {
  clang::IdentifierInfo &Identifier = Context.Idents.get(Name);
  clang::DeclarationName Declaration(&Identifier);
  for (clang::NamedDecl *Candidate :
       Context.getTranslationUnitDecl()->lookup(Declaration))
    if (const auto *Token = llvm::dyn_cast<TokenSort>(Candidate))
      return Token;
  return nullptr;
}

inline bool hasQualifier(const TokenSort *Token, llvm::StringRef Qualifier) {
  if (!Token)
    return false;
  for (const clang::AnnotateAttr *Attribute :
       Token->specific_attrs<clang::AnnotateAttr>())
    if (Attribute->getAnnotation() == Qualifier)
      return true;
  return false;
}

inline std::optional<int64_t> excludedSentinel(const TokenSort *Token) {
  if (!Token)
    return std::nullopt;
  constexpr llvm::StringRef Prefix = "qual:sentinel_exclude=";
  for (const clang::AnnotateAttr *Attribute :
       Token->specific_attrs<clang::AnnotateAttr>()) {
    llvm::StringRef Text = Attribute->getAnnotation();
    if (!Text.consume_front(Prefix))
      continue;
    if (Text == "NULL")
      return 0;
    int64_t Value = 0;
    if (!Text.getAsInteger(10, Value))
      return Value;
  }
  return std::nullopt;
}

} // namespace ntlibc::algebra

#endif
