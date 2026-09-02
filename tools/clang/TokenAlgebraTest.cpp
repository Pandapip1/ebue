// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TokenAlgebra.h"

#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace ntlibc::algebra;

static bool require(bool Condition, const char *Message) {
  if (!Condition)
    std::fprintf(stderr, "token-algebra-test: %s\n", Message);
  return Condition;
}

struct ExpectedTransition {
  TokenState After;
  TokenEvent Events;
};

static bool testTransitionTable() {
  constexpr TokenState States[] = {
      TokenState::Unknown, TokenState::Absent, TokenState::Linear,
      TokenState::Duplicable};
  constexpr TokenOperation Operations[] = {
      TokenOperation::Require,         TokenOperation::RequireAbsent,
      TokenOperation::Consume,         TokenOperation::ConsumeIfPresent,
      TokenOperation::Drop,            TokenOperation::GrantLinear,
      TokenOperation::GrantDuplicable,
  };
  constexpr ExpectedTransition Expected[][4] = {
      {{TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenEvent::MissingRequired},
       {TokenState::Linear, TokenEvent::None},
       {TokenState::Duplicable, TokenEvent::None}},
      {{TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Unknown, TokenEvent::PresentWhenAbsentRequired},
       {TokenState::Unknown, TokenEvent::PresentWhenAbsentRequired}},
      {{TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenEvent::MissingRequired},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None}},
      {{TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None}},
      {{TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None},
       {TokenState::Absent, TokenEvent::None}},
      {{TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Linear, TokenEvent::None},
       {TokenState::Unknown, TokenEvent::LinearDuplication},
       {TokenState::Unknown, TokenEvent::LinearDuplication}},
      {{TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Duplicable, TokenEvent::None},
       {TokenState::Unknown, TokenEvent::DuplicationClassMismatch},
       {TokenState::Duplicable, TokenEvent::None}},
  };
  bool Passed = true;
  for (unsigned Operation = 0; Operation < 7; ++Operation)
    for (unsigned State = 0; State < 4; ++State) {
      TokenTransition Result =
          applyTokenOperation(States[State], Operations[Operation]);
      bool Cell = Result.Before == States[State] &&
                  Result.After == Expected[Operation][State].After &&
                  Result.Events == Expected[Operation][State].Events &&
                  Result.Effects == TokenEffect::None &&
                  Result.permitted() ==
                      (Expected[Operation][State].Events == TokenEvent::None);
      if (!Cell)
        std::fprintf(stderr,
                     "token-algebra-test: transition cell op=%u state=%u\n",
                     Operation, State);
      Passed &= Cell;
    }
  return Passed;
}

struct ExpectedTransfer {
  TokenState SourceAfter;
  TokenState DestinationAfter;
  TokenEvent Events;
};

static bool testTransferTable() {
  constexpr TokenState States[] = {
      TokenState::Unknown, TokenState::Absent, TokenState::Linear,
      TokenState::Duplicable};
  constexpr TokenEvent MissingOccupied =
      TokenEvent::MissingRequired | TokenEvent::DestinationOccupied;
  constexpr TokenEvent OccupiedMismatch =
      TokenEvent::DestinationOccupied |
      TokenEvent::DuplicationClassMismatch;
  constexpr ExpectedTransfer Expected[4][4] = {
      {{TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven}},
      {{TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Unknown, TokenState::Unknown,
        TokenEvent::MissingRequired},
       {TokenState::Unknown, TokenState::Unknown, MissingOccupied},
       {TokenState::Unknown, TokenState::Unknown, MissingOccupied}},
      {{TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Absent, TokenState::Linear, TokenEvent::None},
       {TokenState::Unknown, TokenState::Unknown,
        TokenEvent::DestinationOccupied},
       {TokenState::Unknown, TokenState::Unknown, OccupiedMismatch}},
      {{TokenState::Unknown, TokenState::Unknown, TokenEvent::StateUnproven},
       {TokenState::Duplicable, TokenState::Duplicable, TokenEvent::None},
       {TokenState::Unknown, TokenState::Unknown, OccupiedMismatch},
       {TokenState::Unknown, TokenState::Unknown,
        TokenEvent::DestinationOccupied}},
  };
  constexpr LinearLoanClass Loans[] = {LinearLoanClass::Permissive,
                                       LinearLoanClass::Strict};
  bool Passed = true;
  for (unsigned Loan = 0; Loan < 2; ++Loan)
    for (unsigned Source = 0; Source < 4; ++Source)
      for (unsigned Destination = 0; Destination < 4; ++Destination) {
        TokenTransfer Result = transferToken(
            States[Source], States[Destination], Loans[Loan]);
        TokenEffect Effect = Loan == 1 && Source == 2 && Destination == 1
                                 ? TokenEffect::InvalidateStrictLoans
                                 : TokenEffect::None;
        const ExpectedTransfer &Cell = Expected[Source][Destination];
        bool Matches = Result.SourceBefore == States[Source] &&
                       Result.DestinationBefore == States[Destination] &&
                       Result.SourceAfter == Cell.SourceAfter &&
                       Result.DestinationAfter == Cell.DestinationAfter &&
                       Result.Events == Cell.Events &&
                       Result.Effects == Effect &&
                       Result.permitted() ==
                           (Cell.Events == TokenEvent::None);
        if (!Matches)
          std::fprintf(stderr,
                       "token-algebra-test: transfer cell loan=%u source=%u "
                       "destination=%u\n",
                       Loan, Source, Destination);
        Passed &= Matches;
      }
  return Passed;
}

int main() {
  constexpr const char *Source = R"(
typedef struct { char byte; } dynamic_token
  __attribute__((annotate("qual:dynamic_storage")));
typedef struct { char byte; } wrong_qualifier
  __attribute__((annotate("qual:dynamic_storage_extra")));
int value_only __attribute__((annotate("qual:dynamic_storage")));
typedef struct { char byte; } malformed_word
  __attribute__((annotate("qual:sentinel_exclude=not-a-number")));
typedef struct { char byte; } malformed_overflow
  __attribute__((annotate("qual:sentinel_exclude=9223372036854775808")));
typedef struct { char byte; } null_sentinel
  __attribute__((annotate("qual:sentinel_exclude=NULL")));
typedef struct { char byte; } minimum_sentinel
  __attribute__((annotate("qual:sentinel_exclude=-9223372036854775808")));
typedef struct { char byte; } maximum_sentinel
  __attribute__((annotate("qual:sentinel_exclude=9223372036854775807")));
)";
  std::unique_ptr<clang::ASTUnit> AST =
      clang::tooling::buildASTFromCodeWithArgs(
          Source, std::vector<std::string>{"-xc", "-std=c11"},
          "token-algebra-fixture.c");
  if (!require(AST != nullptr, "failed to parse fixture"))
    return 1;
  clang::ASTContext &Context = AST->getASTContext();
  const TokenSort *Dynamic = findTokenSort(Context, "dynamic_token");
  bool Passed = testTransitionTable() && testTransferTable();
  Passed &= require(Dynamic != nullptr, "nominal token typedef not found");
  Passed &= require(hasQualifier(Dynamic, "qual:dynamic_storage"),
                    "exact qualifier not found");
  Passed &= require(!hasQualifier(findTokenSort(Context, "wrong_qualifier"),
                                  "qual:dynamic_storage"),
                    "prefix lookalike accepted as qualifier");
  Passed &= require(findTokenSort(Context, "value_only") == nullptr,
                    "annotation on non-typedef created a token sort");
  Passed &= require(!excludedSentinel(
                         findTokenSort(Context, "malformed_word")),
                    "nonnumeric sentinel accepted");
  Passed &= require(!excludedSentinel(
                         findTokenSort(Context, "malformed_overflow")),
                    "out-of-range sentinel accepted");
  Passed &= require(excludedSentinel(
                         findTokenSort(Context, "null_sentinel")) == 0,
                    "NULL sentinel did not denote zero");
  Passed &= require(
      excludedSentinel(findTokenSort(Context, "minimum_sentinel")) ==
          std::numeric_limits<int64_t>::min(),
      "minimum signed sentinel was not preserved");
  Passed &= require(
      excludedSentinel(findTokenSort(Context, "maximum_sentinel")) ==
          std::numeric_limits<int64_t>::max(),
      "maximum signed sentinel was not preserved");
  return Passed ? 0 : 1;
}
