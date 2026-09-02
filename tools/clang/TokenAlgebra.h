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

/* Unknown is abstract knowledge, not a fourth runtime multiplicity.  It is
 * the havoc state used after a policy-violating edge, which remains a real C
 * edge even though its token contract can no longer justify facts. */
enum class TokenState : uint8_t { Unknown, Absent, Linear, Duplicable };

enum class TokenOperation : uint8_t {
  Require,
  RequireAbsent,
  Consume,
  ConsumeIfPresent,
  Drop,
  GrantLinear,
  GrantDuplicable,
};

/* Policy violations are diagnostics.  They are deliberately distinct from
 * state effects such as expiring outstanding strict loans. */
enum class TokenEvent : uint8_t {
  None = 0,
  MissingRequired = 1U << 0,
  PresentWhenAbsentRequired = 1U << 1,
  LinearDuplication = 1U << 2,
  DuplicationClassMismatch = 1U << 3,
  DestinationOccupied = 1U << 4,
  StateUnproven = 1U << 5,
};

enum class TokenEffect : uint8_t {
  None = 0,
  InvalidateStrictLoans = 1U << 0,
};

constexpr TokenEvent operator|(TokenEvent Left, TokenEvent Right) {
  return static_cast<TokenEvent>(static_cast<uint8_t>(Left) |
                                 static_cast<uint8_t>(Right));
}

constexpr bool contains(TokenEvent Events, TokenEvent Event) {
  return (static_cast<uint8_t>(Events) & static_cast<uint8_t>(Event)) != 0;
}

struct TokenTransition {
  TokenState Before;
  TokenState After;
  TokenEvent Events;
  TokenEffect Effects;

  constexpr bool permitted() const { return Events == TokenEvent::None; }
};

/* A policy event does not erase the underlying C edge.  Unless an operation
 * has a precise result independently of its input (Drop and
 * ConsumeIfPresent), an unproved input or a violated precondition havocs the
 * token fact so later operations cannot reuse it. */
constexpr TokenTransition applyTokenOperation(TokenState Before,
                                              TokenOperation Operation) {
  switch (Operation) {
  case TokenOperation::Require:
    if (Before == TokenState::Unknown)
      return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
              TokenEffect::None};
    return Before == TokenState::Absent
               ? TokenTransition{Before, TokenState::Unknown,
                                 TokenEvent::MissingRequired,
                                 TokenEffect::None}
               : TokenTransition{Before, Before, TokenEvent::None,
                                 TokenEffect::None};
  case TokenOperation::RequireAbsent:
    if (Before == TokenState::Unknown)
      return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
              TokenEffect::None};
    return Before == TokenState::Absent
               ? TokenTransition{Before, Before, TokenEvent::None,
                                 TokenEffect::None}
               : TokenTransition{Before, TokenState::Unknown,
                                 TokenEvent::PresentWhenAbsentRequired,
                                 TokenEffect::None};
  case TokenOperation::Consume:
    if (Before == TokenState::Unknown)
      return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
              TokenEffect::None};
    return Before == TokenState::Absent
               ? TokenTransition{Before, TokenState::Unknown,
                                 TokenEvent::MissingRequired,
                                 TokenEffect::None}
               : TokenTransition{Before, TokenState::Absent,
                                 TokenEvent::None, TokenEffect::None};
  case TokenOperation::ConsumeIfPresent:
  case TokenOperation::Drop:
    return {Before, TokenState::Absent, TokenEvent::None, TokenEffect::None};
  case TokenOperation::GrantLinear:
    if (Before == TokenState::Unknown)
      return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
              TokenEffect::None};
    return Before == TokenState::Absent
               ? TokenTransition{Before, TokenState::Linear, TokenEvent::None,
                                 TokenEffect::None}
               : TokenTransition{Before, TokenState::Unknown,
                                 TokenEvent::LinearDuplication,
                                 TokenEffect::None};
  case TokenOperation::GrantDuplicable:
    if (Before == TokenState::Unknown)
      return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
              TokenEffect::None};
    if (Before == TokenState::Absent)
      return {Before, TokenState::Duplicable, TokenEvent::None,
              TokenEffect::None};
    if (Before == TokenState::Duplicable)
      return {Before, Before, TokenEvent::None, TokenEffect::None};
    return {Before, TokenState::Unknown,
            TokenEvent::DuplicationClassMismatch,
            TokenEffect::None};
  }
  return {Before, TokenState::Unknown, TokenEvent::StateUnproven,
          TokenEffect::None};
}

enum class LinearLoanClass : uint8_t { Permissive, Strict };

struct TokenTransfer {
  TokenState SourceBefore;
  TokenState DestinationBefore;
  TokenState SourceAfter;
  TokenState DestinationAfter;
  TokenEvent Events;
  TokenEffect Effects;

  constexpr bool permitted() const { return Events == TokenEvent::None; }
};

/* Assignment copies duplicable authority and moves linear authority.  A
 * strict linear move also asks the path-state adapter to expire every loan
 * rooted in the old carrier.  An occupied destination is never silently
 * overwritten: equal classes report occupation, while unequal classes also
 * report the contradictory multiplicity. */
constexpr TokenTransfer transferToken(TokenState Source,
                                      TokenState Destination,
                                      LinearLoanClass Loans) {
  if (Source == TokenState::Unknown || Destination == TokenState::Unknown)
    return {Source, Destination, TokenState::Unknown, TokenState::Unknown,
            TokenEvent::StateUnproven, TokenEffect::None};
  TokenEvent Events = TokenEvent::None;
  if (Source == TokenState::Absent)
    Events = Events | TokenEvent::MissingRequired;
  if (Destination != TokenState::Absent)
    Events = Events | TokenEvent::DestinationOccupied;
  if (Source != TokenState::Absent && Destination != TokenState::Absent &&
      Source != Destination)
    Events = Events | TokenEvent::DuplicationClassMismatch;
  if (Events != TokenEvent::None)
    return {Source, Destination, TokenState::Unknown, TokenState::Unknown,
            Events, TokenEffect::None};
  if (Source == TokenState::Duplicable)
    return {Source, Destination, Source, TokenState::Duplicable,
            TokenEvent::None, TokenEffect::None};
  TokenEffect Effects = Loans == LinearLoanClass::Strict
                            ? TokenEffect::InvalidateStrictLoans
                            : TokenEffect::None;
  return {Source, Destination, TokenState::Absent, TokenState::Linear,
          TokenEvent::None, Effects};
}

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
